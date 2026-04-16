#include "platform/youtube/YouTubeLiveDiscovery.h"
#include "core/Constants.h"
#include "platform/youtube/YouTubeEndpoints.h"
#include "platform/youtube/YouTubeUrlUtils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>

namespace {

int discoveryBackoffDelayMs(int attempt)
{
    if (attempt <= 0) return 5000;
    if (attempt < 3) return 15000;
    if (attempt < 6) return 30000;
    if (attempt < 10) return 60000;
    return 120000;
}

bool isQuotaExceededMessage(const QString& message)
{
    const QString n = message.trimmed().toLower();
    return n.contains(QStringLiteral("quota")) || n.contains(QStringLiteral("rate limit"))
        || n.contains(QStringLiteral("userrate")) || n.contains(QStringLiteral("daily limit"));
}

bool isQuotaOrRateReason(const QString& reason)
{
    const QString r = reason.trimmed().toLower();
    return r == QStringLiteral("quotaexceeded") || r == QStringLiteral("ratelimitexceeded")
        || r == QStringLiteral("userratelimitexceeded") || r == QStringLiteral("dailylimitexceeded")
        || r == QStringLiteral("dailylimitexceededunreg");
}

bool isPreferredLiveCycleForChatId(const QString& lifeCycle)
{
    const QString s = lifeCycle.trimmed().toLower();
    return s == QStringLiteral("live") || s == QStringLiteral("live_starting")
        || s == QStringLiteral("testing") || s == QStringLiteral("test_starting");
}

QString apiErrorMessage(const QJsonObject& response)
{
    return response.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString().trimmed();
}

QString apiErrorReason(const QJsonObject& response)
{
    const QJsonObject error = response.value(QStringLiteral("error")).toObject();
    const QJsonArray errors = error.value(QStringLiteral("errors")).toArray();
    if (!errors.isEmpty() && errors.first().isObject()) {
        return errors.first().toObject().value(QStringLiteral("reason")).toString().trimmed();
    }
    return QString();
}

} // namespace

// ─── Constructor / Lifecycle ───────────────────────────────────────────

YouTubeLiveDiscovery::YouTubeLiveDiscovery(QNetworkAccessManager* network,
                                            int* requestInFlight,
                                            int* generation,
                                            QObject* parent)
    : QObject(parent)
    , m_network(network)
    , m_requestInFlight(requestInFlight)
    , m_generation(generation)
{
}

void YouTubeLiveDiscovery::start(const QString& channelId, const QString& channelHandle,
                                  const QString& configuredChannelId, const QString& configuredChannelHandle,
                                  const QString& manualVideoIdOverride, const QString& accessToken)
{
    m_channelId = channelId;
    m_channelHandle = channelHandle;
    m_configuredChannelId = configuredChannelId;
    m_configuredChannelHandle = configuredChannelHandle;
    m_manualVideoIdOverride = manualVideoIdOverride;
    m_accessToken = accessToken;
    m_running = true;
    m_bootstrapDiscoverAttempts = 0;
    m_bootstrapSearchFallbackTried = false;
    m_nextSearchFallbackAllowedAtUtc = QDateTime();
    m_nextWebFallbackAllowedAtUtc = QDateTime();
    m_announcedLiveChatPending = false;
    m_lastLiveStateCode.clear();
    m_lastLiveStateDetail.clear();
}

void YouTubeLiveDiscovery::stop()
{
    m_running = false;
}

void YouTubeLiveDiscovery::reset()
{
    m_bootstrapDiscoverAttempts = qMin(m_bootstrapDiscoverAttempts, 3);
    m_bootstrapSearchFallbackTried = false;
    m_nextWebFallbackAllowedAtUtc = QDateTime();
    m_announcedLiveChatPending = false;
}

void YouTubeLiveDiscovery::updateAccessToken(const QString& accessToken)
{
    m_accessToken = accessToken;
}

// ─── Infrastructure Helpers ────────────────────────────────────────────

QNetworkRequest YouTubeLiveDiscovery::createBearerRequest(const QUrl& url) const
{
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_accessToken).toUtf8());
    return req;
}

QNetworkRequest YouTubeLiveDiscovery::createWebScrapingRequest(const QUrl& url) const
{
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("Mozilla/5.0 BotManagerQt5/1.0 (+YouTubeLiveResolver)"));
    return req;
}

int YouTubeLiveDiscovery::setupRequestGuard(QNetworkReply* reply)
{
    const int gen = *m_generation;
    QPointer<QNetworkReply> guard(reply);
    QTimer::singleShot(BotManager::Timings::kHttpRequestTimeoutMs, this, [this, gen, guard]() {
        if (gen != *m_generation || !guard || guard->isFinished()) {
            return;
        }
        guard->abort();
    });
    *m_requestInFlight = 1;
    return gen;
}

int YouTubeLiveDiscovery::connectDiscoveryDelayMs() const
{
    return discoveryBackoffDelayMs(m_bootstrapDiscoverAttempts);
}

bool YouTubeLiveDiscovery::isBootstrapDiscoveryPhase() const
{
    return m_bootstrapDiscoverAttempts < 10;
}

bool YouTubeLiveDiscovery::isThirdPartyChannel() const
{
    return !m_configuredChannelId.isEmpty() || !m_configuredChannelHandle.isEmpty();
}

void YouTubeLiveDiscovery::emitLiveStateInfo(const QString& code, const QString& detail)
{
    if (code == m_lastLiveStateCode && detail == m_lastLiveStateDetail) {
        return;
    }
    m_lastLiveStateCode = code;
    m_lastLiveStateDetail = detail;
    emit progress(code, detail);
}

void YouTubeLiveDiscovery::emitLiveChatPendingInfoOnce(const QString& detail)
{
    if (m_announcedLiveChatPending) {
        return;
    }
    m_announcedLiveChatPending = true;
    emit progress(QStringLiteral("INFO_LIVECHAT_ID_PENDING"), detail);
}

// ─── Tick Entry Point ──────────────────────────────────────────────────

void YouTubeLiveDiscovery::tick()
{
    if (!m_running || (m_requestInFlight && *m_requestInFlight)) {
        return;
    }

    if (!m_manualVideoIdOverride.isEmpty()) {
        emitLiveStateInfo(QStringLiteral("INFO_LIVE_STATE_CHECKING"),
            QStringLiteral("Checking YouTube live state via manual video override."));
        requestVideoDetailsForLiveChat(m_manualVideoIdOverride);
        return;
    }
    if (m_channelHandle.isEmpty() && m_channelId.isEmpty()) {
        emitLiveStateInfo(QStringLiteral("INFO_LIVE_STATE_CHECKING"),
            QStringLiteral("Resolving YouTube channel handle."));
        requestOwnChannelProfile();
        return;
    }
    if (!m_channelHandle.isEmpty()) {
        const bool webCoolingDown = m_nextWebFallbackAllowedAtUtc.isValid()
            && QDateTime::currentDateTimeUtc() < m_nextWebFallbackAllowedAtUtc;
        if (!webCoolingDown) {
            m_nextWebFallbackAllowedAtUtc = QDateTime::currentDateTimeUtc().addSecs(180);
            emitLiveStateInfo(QStringLiteral("INFO_LIVE_STATE_CHECKING"),
                QStringLiteral("Checking YouTube live state via handle URL."));
            requestLiveByHandleWeb();
            return;
        }
    }
    emitLiveStateInfo(QStringLiteral("INFO_LIVE_STATE_CHECKING"),
        QStringLiteral("Checking YouTube live state."));
    requestActiveBroadcast();
}

// ─── Request Methods (will be populated in subsequent steps) ───────────
// Placeholder stubs — these will be replaced with actual implementations
// copied and adapted from YouTubeAdapter.cpp

void YouTubeLiveDiscovery::requestOwnChannelProfile() { /* TODO */ }
void YouTubeLiveDiscovery::requestActiveBroadcast() { /* TODO */ }
void YouTubeLiveDiscovery::requestLiveByHandleWeb() { /* TODO */ }
void YouTubeLiveDiscovery::requestRecentStreamByHandleWeb() { /* TODO */ }
void YouTubeLiveDiscovery::requestLiveByChannelPageWeb() { /* TODO */ }
void YouTubeLiveDiscovery::requestLiveByChannelEmbedWeb() { /* TODO */ }
void YouTubeLiveDiscovery::requestPublicFeedForLiveChat() { /* TODO */ }
void YouTubeLiveDiscovery::requestLiveByChannelSearch() { /* TODO */ }
void YouTubeLiveDiscovery::requestMineUploadsPlaylistForLiveChat() { /* TODO */ }
void YouTubeLiveDiscovery::requestPlaylistItemsForLiveChat(const QString&) { /* TODO */ }
void YouTubeLiveDiscovery::requestRecentVideoDetailsForLiveChat(const QStringList&) { /* TODO */ }
void YouTubeLiveDiscovery::requestVideoDetailsForLiveChat(const QString&) { /* TODO */ }
