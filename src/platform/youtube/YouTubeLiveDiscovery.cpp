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
                                            bool* requestInFlight,
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
        QStringLiteral("Mozilla/5.0 OnionmixerChatManagerQt5/1.0 (+YouTubeLiveResolver)"));
    return req;
}

int YouTubeLiveDiscovery::setupRequestGuard(QNetworkReply* reply)
{
    const int gen = *m_generation;
    QPointer<QNetworkReply> guard(reply);
    QTimer::singleShot(OnionmixerChatManager::Timings::kHttpRequestTimeoutMs, this, [this, gen, guard]() {
        if (gen != *m_generation || !guard || guard->isFinished()) {
            return;
        }
        guard->abort();
    });
    *m_requestInFlight = true;
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


// ─── Request Methods ───────────────────────────────────────────────────

void YouTubeLiveDiscovery::requestOwnChannelProfile()
{
    if (m_accessToken.isEmpty()) {
        emit progress(QStringLiteral("TOKEN_MISSING"), QStringLiteral("YouTube access token missing"));
        emit connectionReady();
        return;
    }

    QUrl url(YouTube::Api::channels());
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("id,snippet"));
    query.addQueryItem(QStringLiteral("mine"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("1"));
    url.setQuery(query);

    QNetworkReply* reply = m_network->get(createBearerRequest(url));
    if (!reply) {
        emit progress(QStringLiteral("YOUTUBE_PROFILE_LOOKUP_FAILED"), QStringLiteral("Failed to create channels.mine request"));
        emit connectionReady();
        return;
    }

    const int gen = setupRequestGuard(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        if (gen != *m_generation) { reply->deleteLater(); return; }
        *m_requestInFlight = false;
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        QJsonObject obj;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) obj = doc.object();

        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        if (reply->error() != QNetworkReply::NoError || !httpOk) {
            const QString message = apiErrorMessage(obj).isEmpty() ? reply->errorString() : apiErrorMessage(obj);
            emit progress(QStringLiteral("YOUTUBE_PROFILE_LOOKUP_FAILED"), message);
            emit requestTick(connectDiscoveryDelayMs());
            reply->deleteLater();
            return;
        }

        const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
        const QJsonObject first = items.isEmpty() ? QJsonObject() : items.first().toObject();
        const QString channelId = first.value(QStringLiteral("id")).toString().trimmed();
        const QJsonObject snippet = first.value(QStringLiteral("snippet")).toObject();
        const QString channelTitle = snippet.value(QStringLiteral("title")).toString().trimmed();
        const QString customUrl = snippet.value(QStringLiteral("customUrl")).toString().trimmed();

        if (!channelId.isEmpty() && m_configuredChannelId.isEmpty()) m_channelId = channelId;
        if (!channelTitle.isEmpty() && m_channelName.isEmpty()) m_channelName = channelTitle;
        if (m_configuredChannelHandle.isEmpty()) m_channelHandle = YouTubeUrlUtils::normalizeHandleForUrl(customUrl);

        emit progress(QStringLiteral("INFO_YOUTUBE_PROFILE_RESOLVED"),
            QStringLiteral("channelId=%1 handle=%2 title=%3")
                .arg(m_channelId.isEmpty() ? QStringLiteral("-") : m_channelId,
                    m_channelHandle.isEmpty() ? QStringLiteral("-") : m_channelHandle,
                    m_channelName.isEmpty() ? QStringLiteral("-") : m_channelName));
        emit requestTick(50);
        reply->deleteLater();
    });
}

void YouTubeLiveDiscovery::requestLiveByHandleWeb()
{
    if (m_channelHandle.isEmpty()) { requestActiveBroadcast(); return; }
    QUrl url(YouTube::Web::handleLive(m_channelHandle));
    QNetworkReply* reply = m_network->get(createWebScrapingRequest(url));
    if (!reply) {
        emit progress(QStringLiteral("INFO_LIVE_HANDLE_URL_FAILED"), QStringLiteral("Failed to create handle /live lookup request."));
        requestActiveBroadcast();
        return;
    }
    const int gen = setupRequestGuard(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        if (gen != *m_generation) { reply->deleteLater(); return; }
        *m_requestInFlight = false;
        const QUrl finalUrl = reply->url();
        const QString html = QString::fromUtf8(reply->readAll());
        const QString videoId = !YouTubeUrlUtils::extractVideoIdFromUrl(finalUrl).isEmpty()
            ? YouTubeUrlUtils::extractVideoIdFromUrl(finalUrl) : YouTubeUrlUtils::extractVideoIdFromHtml(html);
        if (!videoId.isEmpty()) {
            emit progress(QStringLiteral("INFO_LIVE_DISCOVERY_HANDLE_URL"), QStringLiteral("Resolved candidate video via handle live URL: %1").arg(videoId));
            requestVideoDetailsForLiveChat(videoId);
        } else {
            emit progress(QStringLiteral("INFO_LIVE_HANDLE_URL_FALLBACK_STREAMS"), QStringLiteral("Handle /live did not resolve a video; falling back to /streams page."));
            requestRecentStreamByHandleWeb();
        }
        reply->deleteLater();
    });
}

void YouTubeLiveDiscovery::requestRecentStreamByHandleWeb()
{
    if (m_channelHandle.isEmpty()) { requestActiveBroadcast(); return; }
    QUrl url(YouTube::Web::handleStreams(m_channelHandle));
    QNetworkReply* reply = m_network->get(createWebScrapingRequest(url));
    if (!reply) { requestLiveByChannelEmbedWeb(); return; }
    const int gen = setupRequestGuard(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        if (gen != *m_generation) { reply->deleteLater(); return; }
        *m_requestInFlight = false;
        const QString html = QString::fromUtf8(reply->readAll());
        const QString videoId = YouTubeUrlUtils::extractVideoIdFromHtml(html);
        if (!videoId.isEmpty()) {
            emit progress(QStringLiteral("INFO_LIVE_DISCOVERY_HANDLE_STREAMS"), QStringLiteral("Resolved candidate recent stream: %1").arg(videoId));
            requestVideoDetailsForLiveChat(videoId);
        } else {
            requestLiveByChannelPageWeb();
        }
        reply->deleteLater();
    });
}

void YouTubeLiveDiscovery::requestLiveByChannelPageWeb()
{
    if (m_channelId.isEmpty()) { requestLiveByChannelEmbedWeb(); return; }
    QUrl url(YouTube::Web::channelLive(m_channelId));
    QNetworkReply* reply = m_network->get(createWebScrapingRequest(url));
    if (!reply) { requestLiveByChannelEmbedWeb(); return; }
    const int gen = setupRequestGuard(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        if (gen != *m_generation) { reply->deleteLater(); return; }
        *m_requestInFlight = false;
        const QUrl finalUrl = reply->url();
        const QString html = QString::fromUtf8(reply->readAll());
        const QString videoId = !YouTubeUrlUtils::extractVideoIdFromUrl(finalUrl).isEmpty()
            ? YouTubeUrlUtils::extractVideoIdFromUrl(finalUrl) : YouTubeUrlUtils::extractVideoIdFromHtml(html);
        if (!videoId.isEmpty()) {
            emit progress(QStringLiteral("INFO_LIVE_DISCOVERY_CHANNEL_PAGE"), QStringLiteral("Resolved candidate video via channel live URL: %1").arg(videoId));
            requestVideoDetailsForLiveChat(videoId);
        } else {
            requestLiveByChannelEmbedWeb();
        }
        reply->deleteLater();
    });
}

void YouTubeLiveDiscovery::requestLiveByChannelEmbedWeb()
{
    if (m_channelId.isEmpty()) { requestActiveBroadcast(); return; }
    QUrl url(YouTube::Web::embedLiveStream());
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("channel"), m_channelId);
    url.setQuery(query);
    QNetworkReply* reply = m_network->get(createWebScrapingRequest(url));
    if (!reply) { requestPublicFeedForLiveChat(); return; }
    const int gen = setupRequestGuard(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        if (gen != *m_generation) { reply->deleteLater(); return; }
        *m_requestInFlight = false;
        const QUrl finalUrl = reply->url();
        const QString html = QString::fromUtf8(reply->readAll());
        const QString videoId = !YouTubeUrlUtils::extractVideoIdFromUrl(finalUrl).isEmpty()
            ? YouTubeUrlUtils::extractVideoIdFromUrl(finalUrl) : YouTubeUrlUtils::extractVideoIdFromHtml(html);
        if (!videoId.isEmpty()) {
            emit progress(QStringLiteral("INFO_LIVE_DISCOVERY_CHANNEL_EMBED"), QStringLiteral("Resolved candidate video via channel embed: %1").arg(videoId));
            requestVideoDetailsForLiveChat(videoId);
        } else {
            requestPublicFeedForLiveChat();
        }
        reply->deleteLater();
    });
}

void YouTubeLiveDiscovery::requestPublicFeedForLiveChat()
{
    if (m_channelId.isEmpty()) { requestActiveBroadcast(); return; }
    QUrl url(YouTube::Web::feedsVideosXml());
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("channel_id"), m_channelId);
    url.setQuery(query);
    QNetworkReply* reply = m_network->get(createWebScrapingRequest(url));
    if (!reply) { requestActiveBroadcast(); return; }
    const int gen = setupRequestGuard(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        if (gen != *m_generation) { reply->deleteLater(); return; }
        *m_requestInFlight = false;
        const QByteArray body = reply->readAll();
        QStringList videoIds;
        QXmlStreamReader xml(body);
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QStringLiteral("videoId")) {
                const QString candidate = xml.readElementText().trimmed();
                if (YouTubeUrlUtils::isLikelyVideoIdCandidate(candidate) && !videoIds.contains(candidate))
                    videoIds.append(candidate);
            }
        }
        if (videoIds.isEmpty()) { requestActiveBroadcast(); }
        else { requestRecentVideoDetailsForLiveChat(videoIds.mid(0, 10)); }
        reply->deleteLater();
    });
}

void YouTubeLiveDiscovery::requestActiveBroadcast()
{
    if (m_accessToken.isEmpty()) {
        emit progress(QStringLiteral("TOKEN_MISSING"), QStringLiteral("YouTube access token missing"));
        emit connectionReady();
        return;
    }
    QUrl url(YouTube::Api::liveBroadcasts());
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("id,snippet,status"));
    query.addQueryItem(QStringLiteral("mine"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("broadcastStatus"), QStringLiteral("active"));
    query.addQueryItem(QStringLiteral("broadcastType"), QStringLiteral("all"));
    query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("50"));
    url.setQuery(query);
    QNetworkReply* reply = m_network->get(createBearerRequest(url));
    if (!reply) {
        emit progress(QStringLiteral("REQUEST_FAILED"), QStringLiteral("Failed to create liveBroadcasts request"));
        emit connectionReady();
        return;
    }
    const int gen = setupRequestGuard(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        if (gen != *m_generation) { reply->deleteLater(); return; }
        *m_requestInFlight = false;
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        QJsonObject obj;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) obj = doc.object();
        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        if (reply->error() != QNetworkReply::NoError || !httpOk) {
            const QString apiMsg = apiErrorMessage(obj);
            const QString reason = apiErrorReason(obj).toLower();
            const QString message = apiMsg.isEmpty() ? reply->errorString() : apiMsg;
            const bool quotaOrRate = isQuotaExceededMessage(message) || isQuotaOrRateReason(reason);
            if (quotaOrRate) {
                emit connectionReady();
                m_lastLiveStateCode.clear();
                emit requestTick(OnionmixerChatManager::Timings::kQuotaBackoffMs);
                emit progress(QStringLiteral("LIVE_DISCOVERY_FAILED"), message);
                reply->deleteLater(); return;
            }
            const bool liveNotEnabled = message.toLower().contains(QStringLiteral("not enabled for live streaming"))
                || reason.contains(QStringLiteral("insufficientlivepermissions")) || reason.contains(QStringLiteral("livestreamingnotenabled"));
            const bool fallbackCoolingDown = m_nextSearchFallbackAllowedAtUtc.isValid() && QDateTime::currentDateTimeUtc() < m_nextSearchFallbackAllowedAtUtc;
            if (liveNotEnabled && !m_channelId.isEmpty() && !m_bootstrapSearchFallbackTried && !fallbackCoolingDown) {
                m_bootstrapSearchFallbackTried = true;
                m_nextSearchFallbackAllowedAtUtc = QDateTime::currentDateTimeUtc().addSecs(300);
                requestLiveByChannelSearch();
                reply->deleteLater(); return;
            }
            emit connectionReady();
            m_lastLiveStateCode.clear();
            emit requestTick(liveNotEnabled ? OnionmixerChatManager::Timings::kQuotaBackoffMs : connectDiscoveryDelayMs());
            emit progress(QStringLiteral("LIVE_DISCOVERY_FAILED"), message);
            reply->deleteLater(); return;
        }
        emit connectionReady();
        const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
        if (items.isEmpty()) {
            ++m_bootstrapDiscoverAttempts;
            const bool fallbackCoolingDown = m_nextSearchFallbackAllowedAtUtc.isValid() && QDateTime::currentDateTimeUtc() < m_nextSearchFallbackAllowedAtUtc;
            if (isThirdPartyChannel() && !m_channelId.isEmpty() && !m_bootstrapSearchFallbackTried && !fallbackCoolingDown) {
                m_bootstrapSearchFallbackTried = true;
                m_nextSearchFallbackAllowedAtUtc = QDateTime::currentDateTimeUtc().addSecs(300);
                requestLiveByChannelSearch();
                reply->deleteLater(); return;
            }
            emitLiveStateInfo(QStringLiteral("INFO_LIVE_STATE_OFFLINE"), QStringLiteral("No active broadcast"));
            emitLiveChatPendingInfoOnce(QStringLiteral("Connected but liveChatId is not available yet."));
            emit requestTick(connectDiscoveryDelayMs());
            reply->deleteLater(); return;
        }
        QString liveChatId, broadcastVideoId, liveTitle;
        bool hasLiveBroadcast = false;
        for (const QJsonValue& v : items) {
            const QJsonObject item = v.toObject();
            const QString lifeCycle = item.value(QStringLiteral("status")).toObject().value(QStringLiteral("lifeCycleStatus")).toString();
            if (isPreferredLiveCycleForChatId(lifeCycle)) { hasLiveBroadcast = true; liveTitle = item.value(QStringLiteral("snippet")).toObject().value(QStringLiteral("title")).toString().trimmed(); break; }
        }
        for (const QJsonValue& v : items) {
            const QJsonObject item = v.toObject();
            const QString candidate = item.value(QStringLiteral("snippet")).toObject().value(QStringLiteral("liveChatId")).toString().trimmed();
            if (!candidate.isEmpty() && isPreferredLiveCycleForChatId(item.value(QStringLiteral("status")).toObject().value(QStringLiteral("lifeCycleStatus")).toString())) {
                liveChatId = candidate;
                broadcastVideoId = item.value(QStringLiteral("id")).toString().trimmed();
                break;
            }
        }
        if (liveChatId.isEmpty()) {
            for (const QJsonValue& v : items) {
                const QString candidate = v.toObject().value(QStringLiteral("snippet")).toObject().value(QStringLiteral("liveChatId")).toString().trimmed();
                if (!candidate.isEmpty()) { liveChatId = candidate; break; }
            }
        }
        if (liveChatId.isEmpty()) {
            ++m_bootstrapDiscoverAttempts;
            if (hasLiveBroadcast) emitLiveStateInfo(QStringLiteral("INFO_LIVE_STATE_ONLINE"), liveTitle.isEmpty() ? QStringLiteral("Active broadcast detected") : liveTitle);
            else emitLiveStateInfo(QStringLiteral("INFO_LIVE_STATE_OFFLINE"), QStringLiteral("No active broadcast"));
            const bool fc = m_nextSearchFallbackAllowedAtUtc.isValid() && QDateTime::currentDateTimeUtc() < m_nextSearchFallbackAllowedAtUtc;
            if (isBootstrapDiscoveryPhase() && !m_channelId.isEmpty() && !m_bootstrapSearchFallbackTried && !fc) {
                m_bootstrapSearchFallbackTried = true;
                m_nextSearchFallbackAllowedAtUtc = QDateTime::currentDateTimeUtc().addSecs(300);
                requestLiveByChannelSearch();
                reply->deleteLater(); return;
            }
            emitLiveChatPendingInfoOnce(QStringLiteral("Connected but liveChatId discovery is still pending."));
            emit requestTick(connectDiscoveryDelayMs());
            reply->deleteLater(); return;
        }
        m_announcedLiveChatPending = false;
        emitLiveStateInfo(QStringLiteral("INFO_LIVE_STATE_ONLINE"), liveTitle.isEmpty() ? QStringLiteral("Active broadcast detected") : liveTitle);
        emit progress(QStringLiteral("INFO_LIVECHAT_ID_READY"), QStringLiteral("YouTube liveChatId resolved via liveBroadcasts."));
        m_bootstrapDiscoverAttempts = 10;
        emit discoveryCompleted(liveChatId, broadcastVideoId);
        emit requestTick(OnionmixerChatManager::Timings::kImmediateRetickMs);
        reply->deleteLater();
    });
}

void YouTubeLiveDiscovery::requestLiveByChannelSearch()
{
    if (m_accessToken.isEmpty()) {
        emit connectionReady(); ++m_bootstrapDiscoverAttempts;
        emitLiveStateInfo(QStringLiteral("INFO_LIVE_STATE_CHECKING"), QStringLiteral("Waiting for channel/live lookup."));
        emitLiveChatPendingInfoOnce(QStringLiteral("Connected but waiting for channel/live lookup."));
        emit requestTick(connectDiscoveryDelayMs()); return;
    }
    QUrl url(YouTube::Api::search());
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("snippet"));
    query.addQueryItem(QStringLiteral("eventType"), QStringLiteral("live"));
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("video"));
    query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("5"));
    query.addQueryItem(QStringLiteral("order"), QStringLiteral("date"));
    if (isThirdPartyChannel() && !m_channelId.isEmpty()) query.addQueryItem(QStringLiteral("channelId"), m_channelId);
    else query.addQueryItem(QStringLiteral("forMine"), QStringLiteral("true"));
    url.setQuery(query);
    QNetworkReply* reply = m_network->get(createBearerRequest(url));
    if (!reply) { emit connectionReady(); ++m_bootstrapDiscoverAttempts; emit requestTick(connectDiscoveryDelayMs()); return; }
    const int gen = setupRequestGuard(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        if (gen != *m_generation) { reply->deleteLater(); return; }
        *m_requestInFlight = false;
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        QJsonObject obj; const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) obj = doc.object();
        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        if (reply->error() != QNetworkReply::NoError || !httpOk) {
            const QString reason = apiErrorReason(obj).toLower();
            const QString message = apiErrorMessage(obj).isEmpty() ? reply->errorString() : apiErrorMessage(obj);
            emit connectionReady();
            if (reason == QStringLiteral("invalidargument") || message.contains(QStringLiteral("invalid argument"), Qt::CaseInsensitive)) {
                emit progress(QStringLiteral("LIVE_SEARCH_FALLBACK_RECENT"), QStringLiteral("owned live search returned invalid argument; falling back to recent owned videos."));
                requestMineUploadsPlaylistForLiveChat(); reply->deleteLater(); return;
            }
            ++m_bootstrapDiscoverAttempts;
            emit progress(QStringLiteral("LIVE_SEARCH_FAILED"), message);
            emitLiveChatPendingInfoOnce(QStringLiteral("Connected but live search did not return liveChatId."));
            emit requestTick(connectDiscoveryDelayMs()); reply->deleteLater(); return;
        }
        const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
        if (items.isEmpty()) {
            emit connectionReady(); ++m_bootstrapDiscoverAttempts;
            emitLiveStateInfo(QStringLiteral("INFO_LIVE_STATE_OFFLINE"), QStringLiteral("No owned live search result"));
            emitLiveChatPendingInfoOnce(QStringLiteral("Connected but no owned live video was found for this account."));
            emit requestTick(connectDiscoveryDelayMs()); reply->deleteLater(); return;
        }
        const QString videoId = items.first().toObject().value(QStringLiteral("id")).toObject().value(QStringLiteral("videoId")).toString().trimmed();
        if (videoId.isEmpty()) {
            emit connectionReady(); ++m_bootstrapDiscoverAttempts;
            emit requestTick(connectDiscoveryDelayMs()); reply->deleteLater(); return;
        }
        requestVideoDetailsForLiveChat(videoId); reply->deleteLater();
    });
}

void YouTubeLiveDiscovery::requestMineUploadsPlaylistForLiveChat()
{
    if (m_accessToken.isEmpty()) { emit connectionReady(); ++m_bootstrapDiscoverAttempts; emit requestTick(connectDiscoveryDelayMs()); return; }
    QUrl url(YouTube::Api::channels());
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("contentDetails"));
    query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("1"));
    if (isThirdPartyChannel() && !m_channelId.isEmpty()) query.addQueryItem(QStringLiteral("id"), m_channelId);
    else query.addQueryItem(QStringLiteral("mine"), QStringLiteral("true"));
    url.setQuery(query);
    QNetworkReply* reply = m_network->get(createBearerRequest(url));
    if (!reply) { emit connectionReady(); ++m_bootstrapDiscoverAttempts; emit requestTick(connectDiscoveryDelayMs()); return; }
    const int gen = setupRequestGuard(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        if (gen != *m_generation) { reply->deleteLater(); return; }
        *m_requestInFlight = false;
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        QJsonObject obj; const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) obj = doc.object();
        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        if (reply->error() != QNetworkReply::NoError || !httpOk) {
            emit connectionReady(); ++m_bootstrapDiscoverAttempts;
            emit progress(QStringLiteral("UPLOADS_PLAYLIST_LOOKUP_FAILED"), apiErrorMessage(obj).isEmpty() ? reply->errorString() : apiErrorMessage(obj));
            emit requestTick(connectDiscoveryDelayMs()); reply->deleteLater(); return;
        }
        const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
        const QString playlistId = items.isEmpty() ? QString() : items.first().toObject().value(QStringLiteral("contentDetails")).toObject().value(QStringLiteral("relatedPlaylists")).toObject().value(QStringLiteral("uploads")).toString().trimmed();
        if (playlistId.isEmpty()) { emit connectionReady(); ++m_bootstrapDiscoverAttempts; emit requestTick(connectDiscoveryDelayMs()); reply->deleteLater(); return; }
        requestPlaylistItemsForLiveChat(playlistId); reply->deleteLater();
    });
}

void YouTubeLiveDiscovery::requestPlaylistItemsForLiveChat(const QString& playlistId)
{
    if (m_accessToken.isEmpty() || playlistId.trimmed().isEmpty()) { emit connectionReady(); ++m_bootstrapDiscoverAttempts; emit requestTick(connectDiscoveryDelayMs()); return; }
    QUrl url(YouTube::Api::playlistItems());
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("snippet"));
    query.addQueryItem(QStringLiteral("playlistId"), playlistId.trimmed());
    query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("50"));
    url.setQuery(query);
    QNetworkReply* reply = m_network->get(createBearerRequest(url));
    if (!reply) { emit connectionReady(); ++m_bootstrapDiscoverAttempts; emit requestTick(connectDiscoveryDelayMs()); return; }
    const int gen = setupRequestGuard(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        if (gen != *m_generation) { reply->deleteLater(); return; }
        *m_requestInFlight = false;
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        QJsonObject obj; const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) obj = doc.object();
        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        if (reply->error() != QNetworkReply::NoError || !httpOk) {
            emit connectionReady(); ++m_bootstrapDiscoverAttempts;
            emit progress(QStringLiteral("PLAYLIST_ITEMS_LOOKUP_FAILED"), apiErrorMessage(obj).isEmpty() ? reply->errorString() : apiErrorMessage(obj));
            emit requestTick(connectDiscoveryDelayMs()); reply->deleteLater(); return;
        }
        QStringList videoIds;
        const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
        for (const QJsonValue& v : items) {
            const QString vid = v.toObject().value(QStringLiteral("snippet")).toObject().value(QStringLiteral("resourceId")).toObject().value(QStringLiteral("videoId")).toString().trimmed();
            if (!vid.isEmpty() && !videoIds.contains(vid)) videoIds.append(vid);
        }
        if (videoIds.isEmpty()) { emit connectionReady(); ++m_bootstrapDiscoverAttempts; emitLiveStateInfo(QStringLiteral("INFO_LIVE_STATE_OFFLINE"), QStringLiteral("No owned recent video result")); emit requestTick(connectDiscoveryDelayMs()); reply->deleteLater(); return; }
        requestRecentVideoDetailsForLiveChat(videoIds); reply->deleteLater();
    });
}

void YouTubeLiveDiscovery::requestRecentVideoDetailsForLiveChat(const QStringList& videoIds)
{
    if (m_accessToken.isEmpty() || videoIds.isEmpty()) { emit connectionReady(); ++m_bootstrapDiscoverAttempts; emit requestTick(connectDiscoveryDelayMs()); return; }
    QUrl url(YouTube::Api::videos());
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("liveStreamingDetails,snippet,status"));
    query.addQueryItem(QStringLiteral("id"), videoIds.join(QLatin1Char(',')));
    query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("10"));
    url.setQuery(query);
    QNetworkReply* reply = m_network->get(createBearerRequest(url));
    if (!reply) { emit connectionReady(); ++m_bootstrapDiscoverAttempts; emit requestTick(connectDiscoveryDelayMs()); return; }
    const int gen = setupRequestGuard(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        if (gen != *m_generation) { reply->deleteLater(); return; }
        *m_requestInFlight = false;
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        QJsonObject obj; const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) obj = doc.object();
        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        if (reply->error() != QNetworkReply::NoError || !httpOk) {
            emit connectionReady(); ++m_bootstrapDiscoverAttempts;
            emit progress(QStringLiteral("RECENT_VIDEO_DETAILS_FAILED"), apiErrorMessage(obj).isEmpty() ? reply->errorString() : apiErrorMessage(obj));
            emit requestTick(connectDiscoveryDelayMs()); reply->deleteLater(); return;
        }
        const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
        for (const QJsonValue& v : items) {
            const QJsonObject item = v.toObject();
            const QString liveChatId = item.value(QStringLiteral("liveStreamingDetails")).toObject().value(QStringLiteral("activeLiveChatId")).toString().trimmed();
            if (liveChatId.isEmpty()) continue;
            const QString candidateVideoId = item.value(QStringLiteral("id")).toString().trimmed();
            m_announcedLiveChatPending = false;
            const QString title = item.value(QStringLiteral("snippet")).toObject().value(QStringLiteral("title")).toString().trimmed();
            emitLiveStateInfo(QStringLiteral("INFO_LIVE_STATE_ONLINE"), title.isEmpty() ? QStringLiteral("Active broadcast detected") : title);
            emit progress(QStringLiteral("INFO_LIVECHAT_ID_READY"), QStringLiteral("YouTube liveChatId resolved via uploads playlist recent videos."));
            m_bootstrapDiscoverAttempts = 10;
            emit connectionReady();
            emit discoveryCompleted(liveChatId, candidateVideoId);
            emit requestTick(OnionmixerChatManager::Timings::kImmediateRetickMs);
            reply->deleteLater(); return;
        }
        emit connectionReady(); ++m_bootstrapDiscoverAttempts;
        emitLiveStateInfo(QStringLiteral("INFO_LIVE_STATE_OFFLINE"), QStringLiteral("No activeLiveChatId in recent owned videos"));
        emit requestTick(connectDiscoveryDelayMs()); reply->deleteLater();
    });
}

void YouTubeLiveDiscovery::requestVideoDetailsForLiveChat(const QString& videoId)
{
    if (m_accessToken.isEmpty() || videoId.trimmed().isEmpty()) { emit connectionReady(); ++m_bootstrapDiscoverAttempts; emit requestTick(connectDiscoveryDelayMs()); return; }
    QUrl url(YouTube::Api::videos());
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("liveStreamingDetails,status"));
    query.addQueryItem(QStringLiteral("id"), videoId.trimmed());
    query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("1"));
    url.setQuery(query);
    QNetworkReply* reply = m_network->get(createBearerRequest(url));
    if (!reply) { emit connectionReady(); ++m_bootstrapDiscoverAttempts; emit requestTick(connectDiscoveryDelayMs()); return; }
    const int gen = setupRequestGuard(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, videoId]() {
        if (gen != *m_generation) { reply->deleteLater(); return; }
        *m_requestInFlight = false;
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        QJsonObject obj; const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) obj = doc.object();
        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        if (reply->error() != QNetworkReply::NoError || !httpOk) {
            emit connectionReady(); ++m_bootstrapDiscoverAttempts;
            emit requestTick(connectDiscoveryDelayMs()); reply->deleteLater(); return;
        }
        const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
        if (items.isEmpty()) {
            emit connectionReady(); ++m_bootstrapDiscoverAttempts;
            emitLiveStateInfo(QStringLiteral("INFO_LIVE_STATE_CHECKING"), QStringLiteral("Candidate video details unavailable."));
            emit requestTick(connectDiscoveryDelayMs()); reply->deleteLater(); return;
        }
        const QJsonObject first = items.first().toObject();
        const QString liveChatId = first.value(QStringLiteral("liveStreamingDetails")).toObject().value(QStringLiteral("activeLiveChatId")).toString().trimmed();
        if (!liveChatId.isEmpty()) {
            m_announcedLiveChatPending = false;
            emitLiveStateInfo(QStringLiteral("INFO_LIVE_STATE_ONLINE"), QStringLiteral("Active broadcast detected"));
            emit progress(QStringLiteral("INFO_LIVECHAT_ID_READY"), QStringLiteral("YouTube liveChatId resolved via videos.liveStreamingDetails."));
            m_bootstrapDiscoverAttempts = 10;
            emit connectionReady();
            emit discoveryCompleted(liveChatId, videoId.trimmed());
            emit requestTick(OnionmixerChatManager::Timings::kImmediateRetickMs);
            reply->deleteLater(); return;
        }
        emit connectionReady(); ++m_bootstrapDiscoverAttempts;
        emitLiveStateInfo(QStringLiteral("INFO_LIVE_STATE_ONLINE"), QStringLiteral("Active broadcast detected (chat pending)"));
        emitLiveChatPendingInfoOnce(QStringLiteral("Connected but activeLiveChatId is still unavailable."));
        emit requestTick(connectDiscoveryDelayMs()); reply->deleteLater();
    });
}
