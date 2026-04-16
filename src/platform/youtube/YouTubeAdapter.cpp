#include "platform/youtube/YouTubeAdapter.h"
#include "core/Constants.h"
#include "platform/youtube/YouTubeChatMessageParser.h"
#include "platform/youtube/YouTubeEndpoints.h"
#include "platform/youtube/YouTubeLiveChatWebClient.h"
#include "platform/youtube/YouTubeLiveDiscovery.h"
#include "platform/youtube/YouTubeUrlUtils.h"
#include "platform/youtube/YouTubeStreamListClient.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpressionMatchIterator>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <QtGlobal>

namespace {
int normalizePollIntervalMillis(int value)
{
    if (value <= 0) {
        return 2500;
    }
    if (value < 1000) {
        return 1000;
    }
    return value;
}

int discoveryBackoffDelayMs(int attempt)
{
    if (attempt <= 0) {
        return 5000;
    }
    if (attempt < 3) {
        return 15000;
    }
    if (attempt < 6) {
        return 30000;
    }
    if (attempt < 10) {
        return 60000;
    }
    return 120000;
}

bool isQuotaExceededMessage(const QString& message)
{
    const QString normalized = message.trimmed().toLower();
    return normalized.contains(QStringLiteral("quota"))
        || normalized.contains(QStringLiteral("rate limit"))
        || normalized.contains(QStringLiteral("userrate"))
        || normalized.contains(QStringLiteral("daily limit"));
}

bool isQuotaOrRateReason(const QString& reason)
{
    const QString r = reason.trimmed().toLower();
    return r == QStringLiteral("quotaexceeded")
        || r == QStringLiteral("ratelimitexceeded")
        || r == QStringLiteral("userratelimitexceeded")
        || r == QStringLiteral("dailylimitexceeded")
        || r == QStringLiteral("dailylimitexceededunreg");
}

bool isPreferredLiveCycleForChatId(const QString& lifeCycle)
{
    const QString s = lifeCycle.trimmed().toLower();
    return s == QStringLiteral("live")
        || s == QStringLiteral("live_starting")
        || s == QStringLiteral("testing")
        || s == QStringLiteral("test_starting");
}

QString apiErrorMessage(const QJsonObject& response)
{
    const QJsonObject error = response.value(QStringLiteral("error")).toObject();
    return error.value(QStringLiteral("message")).toString().trimmed();
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

YouTubeAdapter::YouTubeAdapter(QObject* parent)
    : IChatPlatformAdapter(parent)
{
    m_network = new QNetworkAccessManager(this);
    m_loopTimer = new QTimer(this);
    m_discovery = new YouTubeLiveDiscovery(m_network, &m_requestInFlight, &m_generation, this);
    connect(m_discovery, &YouTubeLiveDiscovery::discoveryCompleted, this, [this](const QString& liveChatId, const QString& videoId) {
        m_liveChatId = liveChatId;
        if (!videoId.isEmpty()) m_discoveredVideoId = videoId;
        m_nextPageToken.clear();
        m_announcedLiveChatPending = false;
    });
    connect(m_discovery, &YouTubeLiveDiscovery::connectionReady, this, [this]() {
        if (m_pendingConnectResult) {
            m_pendingConnectResult = false;
            m_connected = true;
            emit connected(platformId());
        }
    });
    connect(m_discovery, &YouTubeLiveDiscovery::progress, this, [this](const QString& code, const QString& detail) {
        emit error(platformId(), code, detail);
    });
    connect(m_discovery, &YouTubeLiveDiscovery::requestTick, this, &YouTubeAdapter::scheduleNextTick);
    m_webChatClient = new YouTubeLiveChatWebClient(this);
    m_streamListClient = new YouTubeStreamListClient(this);
    m_loopTimer->setSingleShot(true);
    connect(m_loopTimer, &QTimer::timeout, this, &YouTubeAdapter::onLoopTick);
    connect(m_webChatClient, &YouTubeLiveChatWebClient::started,
        this, &YouTubeAdapter::onWebChatStarted);
    connect(m_webChatClient, &YouTubeLiveChatWebClient::messagesReceived,
        this, &YouTubeAdapter::onWebChatMessagesReceived);
    connect(m_webChatClient, &YouTubeLiveChatWebClient::failed,
        this, &YouTubeAdapter::onWebChatFailed);
    connect(m_webChatClient, &YouTubeLiveChatWebClient::ended,
        this, &YouTubeAdapter::onWebChatEnded);
    connect(m_streamListClient, &YouTubeStreamListClient::started,
        this, &YouTubeAdapter::onStreamListStarted, Qt::QueuedConnection);
    connect(m_streamListClient, &YouTubeStreamListClient::responseObserved,
        this, &YouTubeAdapter::onStreamListResponseObserved, Qt::QueuedConnection);
    connect(m_streamListClient, &YouTubeStreamListClient::messagesReceived,
        this, &YouTubeAdapter::onStreamListMessagesReceived, Qt::QueuedConnection);
    connect(m_streamListClient, &YouTubeStreamListClient::streamCheckpoint,
        this, &YouTubeAdapter::onStreamListCheckpoint, Qt::QueuedConnection);
    connect(m_streamListClient, &YouTubeStreamListClient::streamEnded,
        this, &YouTubeAdapter::onStreamListEnded, Qt::QueuedConnection);
    connect(m_streamListClient, &YouTubeStreamListClient::streamFailed,
        this, &YouTubeAdapter::onStreamListFailed, Qt::QueuedConnection);
}

PlatformId YouTubeAdapter::platformId() const
{
    return PlatformId::YouTube;
}

QString YouTubeAdapter::parseManualVideoIdOverride(const QString& raw) const
{
    return YouTubeUrlUtils::parseManualVideoIdOverride(raw);
}

QString YouTubeAdapter::normalizeHandleForUrl(const QString& raw) const
{
    return YouTubeUrlUtils::normalizeHandleForUrl(raw);
}

bool YouTubeAdapter::isLikelyYouTubeVideoIdCandidate(const QString& value) const
{
    return YouTubeUrlUtils::isLikelyVideoIdCandidate(value);
}

QString YouTubeAdapter::extractYouTubeVideoIdFromUrl(const QUrl& url) const
{
    return YouTubeUrlUtils::extractVideoIdFromUrl(url);
}

QString YouTubeAdapter::extractYouTubeVideoIdFromHtml(const QString& html) const
{
    return YouTubeUrlUtils::extractVideoIdFromHtml(html);
}

void YouTubeAdapter::applyRuntimeAccessToken(const QString& accessToken)
{
    const QString trimmed = accessToken.trimmed();
    if (m_accessToken == trimmed) {
        return;
    }

    m_accessToken = trimmed;
    m_discovery->updateAccessToken(trimmed);
    emit error(platformId(), QStringLiteral("INFO_RUNTIME_TOKEN_UPDATED"),
        trimmed.isEmpty()
            ? QStringLiteral("YouTube runtime access token cleared.")
            : QStringLiteral("YouTube runtime access token updated."));

    if (!m_running) {
        return;
    }

    if (trimmed.isEmpty()) {
        clearStreamRuntimeState();
        m_requestInFlight = false;
        m_streamTransportReady = false;
        setLiveStateUnknown(QStringLiteral("YouTube access token is unavailable."));
        return;
    }

    if (m_streamListClient && m_streamListClient->isRunning()) {
        m_streamTransportReady = false;
        m_streamFailureCount = 0;
        m_streamFallbackUntilUtc = QDateTime();
        m_streamListClient->stop();
        scheduleNextTick(BotManager::Timings::kImmediateRetickMs);
        return;
    }

    if (m_webChatClient && m_webChatClient->isRunning()) {
        m_webChatClient->stop();
        m_webChatFailureCount = 0;
        m_webChatFallbackUntilUtc = QDateTime();
        scheduleNextTick(BotManager::Timings::kImmediateRetickMs);
        return;
    }

    if (m_liveChatId.isEmpty()) {
        setLiveStateChecking(QStringLiteral("Token updated, re-checking live state."));
        scheduleNextTick(BotManager::Timings::kImmediateRetickMs);
        return;
    }

    scheduleNextTick(BotManager::Timings::kImmediateRetickMs);
}

bool YouTubeAdapter::shouldUseStreamListTransport() const
{
    if (!m_streamListClient || !m_streamListClient->isSupported()) {
        return false;
    }
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    return !m_streamFallbackUntilUtc.isValid() || nowUtc >= m_streamFallbackUntilUtc;
}

int YouTubeAdapter::streamReconnectDelayMs() const
{
    if (m_streamFailureCount <= 1) {
        return 1000;
    }
    if (m_streamFailureCount == 2) {
        return 2000;
    }
    if (m_streamFailureCount == 3) {
        return 4000;
    }
    if (m_streamFailureCount == 4) {
        return 8000;
    }
    return 15000;
}

void YouTubeAdapter::startStreamListTransport()
{
    if (!m_streamListClient || !shouldUseStreamListTransport() || m_liveChatId.trimmed().isEmpty()) {
        return;
    }
    m_streamTransportReady = false;
    const quint64 watchdogNonce = ++m_streamConnectWatchdogNonce;
    emit error(platformId(), QStringLiteral("YT_STREAM_CONNECTING"),
        QStringLiteral("Starting YouTube streamList transport."));
    m_streamListClient->start(m_accessToken, m_liveChatId, m_streamResumeToken);
    QTimer::singleShot(15000, this, [this, watchdogNonce]() {
        if (!m_running || !m_streamListClient || !m_streamListClient->isRunning()) {
            return;
        }
        if (watchdogNonce != m_streamConnectWatchdogNonce || m_streamTransportReady) {
            return;
        }
        emit error(platformId(), QStringLiteral("YT_STREAM_START_TIMEOUT"),
            QStringLiteral("streamList produced no response within 15000ms; switching to polling fallback."));
        m_streamFallbackUntilUtc = QDateTime::currentDateTimeUtc().addSecs(300);
        m_streamTransportReady = false;
        m_streamFailureCount = 3;
        m_streamListClient->stop();
        emit error(platformId(), QStringLiteral("YT_STREAM_FALLBACK_POLLING"),
            QStringLiteral("streamList startup timeout; switching to polling fallback for 300s."));
        scheduleNextTick(BotManager::Timings::kImmediateRetickMs);
    });
}

void YouTubeAdapter::clearStreamRuntimeState()
{
    if (m_streamListClient) {
        m_streamListClient->stop();
    }
    m_streamResumeToken.clear();
    m_streamFailureCount = 0;
    m_streamFallbackUntilUtc = QDateTime();
}

void YouTubeAdapter::publishReceivedMessage(UnifiedChatMessage message)
{
    const QString messageId = message.messageId.trimmed();
    if (messageId.isEmpty() || m_seenMessageIds.contains(messageId)) {
        return;
    }

    if (m_lastPublishedTimestampUtc.isValid() && message.timestamp.isValid()) {
        const qint64 diffSec = message.timestamp.toUTC().secsTo(m_lastPublishedTimestampUtc);
        if (diffSec > 5) {
            return;
        }
    }

    m_seenMessageIds.insert(messageId);
    if (m_seenMessageIds.size() > BotManager::Limits::kYouTubeSeenMessageIdsMax) {
        m_seenMessageIds.clear();
        m_seenMessageIds.insert(messageId);
    }

    if (message.timestamp.isValid()) {
        const QDateTime utc = message.timestamp.toUTC();
        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        const QDateTime cap = nowUtc.addSecs(3);
        const QDateTime clamped = (utc > cap) ? cap : utc;
        if (!m_lastPublishedTimestampUtc.isValid() || clamped > m_lastPublishedTimestampUtc) {
            m_lastPublishedTimestampUtc = clamped;
        }
    }

    message.platform = platformId();
    if (message.channelId.trimmed().isEmpty()) {
        message.channelId = m_channelId;
    }
    if (message.channelName.trimmed().isEmpty()) {
        message.channelName = m_channelName.isEmpty() ? QStringLiteral("YouTube") : m_channelName;
    }
    if (!message.timestamp.isValid()) {
        message.timestamp = QDateTime::currentDateTime();
    }
    emit chatReceived(message);
}

void YouTubeAdapter::markStreamTransportReady()
{
    if (!m_running || m_streamTransportReady) {
        return;
    }
    m_streamTransportReady = true;
    emit error(platformId(), QStringLiteral("YT_STREAM_CONNECTED"),
        QStringLiteral("YouTube streamList transport connected."));
}

void YouTubeAdapter::onStreamListStarted()
{
    if (!m_running) {
        return;
    }
    if (m_pendingConnectResult) {
        m_pendingConnectResult = false;
        m_connected = true;
        emit connected(platformId());
    }
}

void YouTubeAdapter::onStreamListResponseObserved(int itemCount, bool hasNextPageToken, bool hasOfflineAt)
{
    if (!m_running) {
        return;
    }
    markStreamTransportReady();
    emit error(platformId(), QStringLiteral("TRACE_YT_STREAM_RESPONSE"),
        QStringLiteral("items=%1 nextPageToken=%2 offlineAt=%3")
            .arg(itemCount)
            .arg(hasNextPageToken ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(hasOfflineAt ? QStringLiteral("yes") : QStringLiteral("no")));
}

void YouTubeAdapter::onStreamListMessagesReceived(const QVector<UnifiedChatMessage>& messages)
{
    if (!m_running) {
        return;
    }
    markStreamTransportReady();
    m_streamFailureCount = 0;
    m_streamFallbackUntilUtc = QDateTime();
    setLiveStateOnline(QStringLiteral("Receiving live chat"));
    for (const UnifiedChatMessage& msg : messages) {
        publishReceivedMessage(msg);
    }
}

void YouTubeAdapter::onStreamListCheckpoint(const QString& nextPageToken)
{
    if (!m_running) {
        return;
    }
    markStreamTransportReady();
    m_streamResumeToken = nextPageToken.trimmed();
    m_nextPageToken = m_streamResumeToken;
    emit error(platformId(), QStringLiteral("TRACE_YT_STREAM_CHECKPOINT"),
        QStringLiteral("nextPageToken updated"));
}

void YouTubeAdapter::onStreamListEnded(const QString& reason)
{
    if (!m_running) {
        return;
    }
    const QString detail = reason.trimmed().isEmpty()
        ? QStringLiteral("streamList ended")
        : reason.trimmed();
    emit error(platformId(), QStringLiteral("YT_STREAM_DISCONNECTED"), detail);
    m_streamResumeToken.clear();
    m_liveChatId.clear();
    m_nextPageToken.clear();
    m_bootstrapSearchFallbackTried = false;
    m_nextWebFallbackAllowedAtUtc = QDateTime();
    m_announcedLiveChatPending = false;
    m_streamFailureCount = 0;
    m_streamTransportReady = false;
    setLiveStateOffline(detail);
    scheduleNextTick(BotManager::Timings::kDefaultPollIntervalMs);
}

void YouTubeAdapter::onStreamListFailed(const QString& code, const QString& detail)
{
    if (!m_running) {
        return;
    }

    const QString normalizedCode = code.trimmed().toUpper();
    emit error(platformId(), normalizedCode, detail);

    if (normalizedCode == QStringLiteral("YT_STREAM_INVALID_ARGUMENT")) {
        if (!m_streamResumeToken.isEmpty()) {
            m_streamResumeToken.clear();
            emit error(platformId(), QStringLiteral("YT_STREAM_RESUME_TOKEN_RESET"),
                QStringLiteral("streamList resume token reset after invalid argument."));
        } else {
            m_liveChatId.clear();
            m_nextPageToken.clear();
            m_bootstrapSearchFallbackTried = false;
            m_nextWebFallbackAllowedAtUtc = QDateTime();
            m_announcedLiveChatPending = false;
        }
        scheduleNextTick(1000);
        return;
    }

    if (normalizedCode == QStringLiteral("YT_STREAM_NOT_FOUND")
        || normalizedCode == QStringLiteral("YT_STREAM_FAILED_PRECONDITION")) {
        m_streamResumeToken.clear();
        m_liveChatId.clear();
        m_nextPageToken.clear();
        m_bootstrapSearchFallbackTried = false;
        m_nextWebFallbackAllowedAtUtc = QDateTime();
        m_announcedLiveChatPending = false;
        setLiveStateOffline(detail);
        scheduleNextTick(BotManager::Timings::kDefaultPollIntervalMs);
        return;
    }

    if (normalizedCode == QStringLiteral("YT_STREAM_PERMISSION_DENIED")
        || normalizedCode == QStringLiteral("YT_STREAM_UNAUTHENTICATED")) {
        m_streamResumeToken.clear();
        m_streamFailureCount = 0;
        m_streamTransportReady = false;
        scheduleNextTick(BotManager::Timings::kQuotaBackoffMs);
        return;
    }

    if (normalizedCode == QStringLiteral("YT_STREAM_RESOURCE_EXHAUSTED")) {
        m_streamFailureCount = 0;
        m_streamTransportReady = false;
        m_streamFallbackUntilUtc = QDateTime();
        emit error(platformId(), QStringLiteral("YT_STREAM_QUOTA_BACKOFF"),
            QStringLiteral("streamList quota/rate limit detected; keeping stream transport in backoff for 300s."));
        scheduleNextTick(BotManager::Timings::kQuotaBackoffMs);
        return;
    }

    ++m_streamFailureCount;
    if (m_streamFailureCount >= 3) {
        m_streamFallbackUntilUtc = QDateTime::currentDateTimeUtc().addSecs(300);
        m_nextPageToken = m_streamResumeToken.trimmed();
        m_streamResumeToken.clear();
        m_streamTransportReady = false;
        emit error(platformId(), QStringLiteral("YT_STREAM_FALLBACK_POLLING"),
            QStringLiteral("streamList failed repeatedly; switching to polling fallback for 300s."));
        scheduleNextTick(BotManager::Timings::kImmediateRetickMs);
        return;
    }

    emit error(platformId(), QStringLiteral("YT_STREAM_BACKOFF"),
        QStringLiteral("retry=%1 backoffMs=%2").arg(m_streamFailureCount).arg(streamReconnectDelayMs()));
    scheduleNextTick(streamReconnectDelayMs());
}


bool YouTubeAdapter::shouldUseWebChatTransport() const
{
    if (!m_webChatClient || m_discoveredVideoId.trimmed().isEmpty()) {
        return false;
    }
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    return !m_webChatFallbackUntilUtc.isValid() || nowUtc >= m_webChatFallbackUntilUtc;
}

void YouTubeAdapter::startWebChatTransport()
{
    if (!m_webChatClient || !shouldUseWebChatTransport() || m_discoveredVideoId.trimmed().isEmpty()) {
        return;
    }
    emit error(platformId(), QStringLiteral("INFO_YT_WEBCHAT_CONNECTING"),
        QStringLiteral("Starting YouTube InnerTube web chat transport for videoId=%1").arg(m_discoveredVideoId));
    m_webChatClient->start(m_discoveredVideoId);
}

void YouTubeAdapter::onWebChatStarted()
{
    if (!m_running) {
        return;
    }
    m_webChatFailureCount = 0;
    emit error(platformId(), QStringLiteral("INFO_YT_WEBCHAT_CONNECTED"),
        QStringLiteral("YouTube InnerTube web chat transport connected."));
    if (m_pendingConnectResult) {
        m_pendingConnectResult = false;
        m_connected = true;
        emit connected(platformId());
    }
}

void YouTubeAdapter::onWebChatMessagesReceived(const QVector<UnifiedChatMessage>& messages)
{
    if (!m_running) {
        return;
    }
    m_webChatFailureCount = 0;
    m_webChatFallbackUntilUtc = QDateTime();
    setLiveStateOnline(QStringLiteral("Receiving live chat (InnerTube)"));
    for (const UnifiedChatMessage& msg : messages) {
        publishReceivedMessage(msg);
    }
}

void YouTubeAdapter::onWebChatFailed(const QString& code, const QString& detail)
{
    if (!m_running) {
        return;
    }
    emit error(platformId(), code, detail);
    ++m_webChatFailureCount;
    if (m_webChatFailureCount >= 3) {
        m_bootstrapDiscoverAttempts = qMin(m_bootstrapDiscoverAttempts, 3);
        m_webChatFallbackUntilUtc = QDateTime::currentDateTimeUtc().addSecs(300);
        emit error(platformId(), QStringLiteral("INFO_YT_WEBCHAT_FALLBACK"),
            QStringLiteral("InnerTube web chat failed %1 times; falling back to other transports for 300s.")
                .arg(m_webChatFailureCount));
    }
    setLiveStateChecking(QStringLiteral("Retrying chat connection..."));
    const int backoffMs = qMin(BotManager::Timings::kDefaultPollIntervalMs * m_webChatFailureCount,
                              BotManager::Timings::kDiscoveryBackoffMaxMs);
    scheduleNextTick(qMax(backoffMs, BotManager::Timings::kDefaultPollIntervalMs));
}

void YouTubeAdapter::onWebChatEnded(const QString& reason)
{
    if (!m_running) {
        return;
    }
    emit error(platformId(), QStringLiteral("INFO_YT_WEBCHAT_ENDED"), reason);
    m_liveChatId.clear();
    m_discoveredVideoId.clear();
    m_nextPageToken.clear();
    m_bootstrapSearchFallbackTried = false;
    m_bootstrapDiscoverAttempts = qMin(m_bootstrapDiscoverAttempts, 3);
    m_announcedLiveChatPending = false;
    m_nextWebFallbackAllowedAtUtc = QDateTime();
    setLiveStateOffline(reason);
    scheduleNextTick(BotManager::Timings::kDiscoveryRetryMs);
}

void YouTubeAdapter::emitLiveStateInfo(const QString& code, const QString& detail)
{
    const QString normalizedCode = code.trimmed().toUpper();
    const QString normalizedDetail = detail.trimmed();
    if (normalizedCode.isEmpty()) {
        return;
    }
    if (m_lastLiveStateCode == normalizedCode && m_lastLiveStateDetail == normalizedDetail) {
        return;
    }
    m_lastLiveStateCode = normalizedCode;
    m_lastLiveStateDetail = normalizedDetail;
    emit error(platformId(), normalizedCode, normalizedDetail);
}

void YouTubeAdapter::setLiveStateChecking(const QString& detail)
{
    emitLiveStateInfo(QStringLiteral("INFO_LIVE_STATE_CHECKING"), detail);
}

void YouTubeAdapter::setLiveStateOnline(const QString& detail)
{
    emitLiveStateInfo(QStringLiteral("INFO_LIVE_STATE_ONLINE"), detail);
}

void YouTubeAdapter::setLiveStateOffline(const QString& detail)
{
    emitLiveStateInfo(QStringLiteral("INFO_LIVE_STATE_OFFLINE"), detail);
}

void YouTubeAdapter::setLiveStateUnknown(const QString& detail)
{
    emitLiveStateInfo(QStringLiteral("INFO_LIVE_STATE_UNKNOWN"), detail);
}

void YouTubeAdapter::start(const PlatformSettings& settings)
{
    if (m_running) {
        if (m_connected) {
            emit connected(platformId());
        }
        return;
    }
    if (m_connected) {
        emit connected(platformId());
        return;
    }

    if (settings.clientId.trimmed().isEmpty() || settings.redirectUri.trimmed().isEmpty() || settings.scope.trimmed().isEmpty()) {
        setLiveStateUnknown(QStringLiteral("YouTube configuration is incomplete."));
        emit error(platformId(), QStringLiteral("INVALID_CONFIG"), QStringLiteral("YouTube config is incomplete."));
        return;
    }

    m_accessToken = settings.runtimeAccessToken.trimmed();
    if (m_accessToken.isEmpty()) {
        setLiveStateUnknown(QStringLiteral("YouTube access token is unavailable."));
        emit error(platformId(), QStringLiteral("TOKEN_MISSING"), QStringLiteral("YouTube access token is missing. Refresh/Re-auth required."));
        return;
    }

    m_channelId = settings.channelId.trimmed();
    m_channelName = settings.channelName.trimmed();
    m_channelHandle = normalizeHandleForUrl(settings.accountLabel);
    m_manualVideoIdOverride = parseManualVideoIdOverride(settings.liveVideoIdOverride);
    m_configuredChannelId = m_channelId;
    m_configuredChannelHandle = m_channelHandle;
    if (m_channelName.isEmpty()) {
        m_channelName = settings.accountLabel.trimmed();
    }

    m_liveChatId.clear();
    m_nextPageToken.clear();
    m_seenMessageIds.clear();
    m_requestInFlight = false;
    clearStreamRuntimeState();
    m_bootstrapDiscoverAttempts = 0;
    m_bootstrapSearchFallbackTried = false;
    m_announcedLiveChatPending = false;
    m_nextWebFallbackAllowedAtUtc = QDateTime();
    m_nextSearchFallbackAllowedAtUtc = QDateTime();
    m_lastLiveStateCode.clear();
    m_lastLiveStateDetail.clear();
    m_discoveredVideoId.clear();
    m_discovery->start(m_channelId, m_channelHandle, m_configuredChannelId, m_configuredChannelHandle,
                       m_manualVideoIdOverride, m_accessToken);
    m_lastPublishedTimestampUtc = QDateTime();
    m_webChatFailureCount = 0;
    m_webChatFallbackUntilUtc = QDateTime();
    m_streamTransportReady = false;
    ++m_generation;
    m_running = true;
    m_pendingConnectResult = true;
    m_connected = false;
    if (m_streamListClient) {
        const QString transportCode = m_streamListClient->isSupported()
            ? QStringLiteral("INFO_YT_TRANSPORT_STREAMLIST_BUILD")
            : QStringLiteral("INFO_YT_TRANSPORT_POLLING_ONLY_BUILD");
        emit error(platformId(), transportCode, m_streamListClient->supportDetail());
    }
    setLiveStateChecking(QStringLiteral("Checking YouTube live state."));
    scheduleNextTick(250);
}

void YouTubeAdapter::stop()
{
    if (!m_running && !m_connected) {
        emit disconnected(platformId());
        return;
    }

    ++m_generation;
    m_discovery->stop();
    m_running = false;
    m_pendingConnectResult = false;
    m_requestInFlight = false;
    if (m_loopTimer) {
        m_loopTimer->stop();
    }
    if (m_webChatClient) {
        m_webChatClient->stop();
    }
    clearStreamRuntimeState();
    m_liveChatId.clear();
    m_discoveredVideoId.clear();
    m_nextPageToken.clear();
    m_seenMessageIds.clear();
    m_accessToken.clear();
    m_manualVideoIdOverride.clear();
    m_channelHandle.clear();
    m_configuredChannelId.clear();
    m_configuredChannelHandle.clear();
    m_bootstrapDiscoverAttempts = 0;
    m_bootstrapSearchFallbackTried = false;
    m_announcedLiveChatPending = false;
    m_nextWebFallbackAllowedAtUtc = QDateTime();
    m_nextSearchFallbackAllowedAtUtc = QDateTime();
    m_lastLiveStateCode.clear();
    m_lastLiveStateDetail.clear();
    m_webChatFailureCount = 0;
    m_webChatFallbackUntilUtc = QDateTime();
    m_streamTransportReady = false;
    setLiveStateUnknown(QStringLiteral("Disconnected"));

    QTimer::singleShot(100, this, [this]() {
        m_running = false;
        m_connected = false;
        emit disconnected(platformId());
    });
}

bool YouTubeAdapter::isConnected() const
{
    return m_connected;
}

QString YouTubeAdapter::currentLiveChatId() const
{
    return m_liveChatId;
}

void YouTubeAdapter::scheduleNextTick(int delayMs)
{
    if (!m_running || !m_loopTimer) {
        return;
    }
    m_loopTimer->start(qMax(delayMs, 0));
}

void YouTubeAdapter::onLoopTick()
{
    if (!m_running || m_requestInFlight) {
        return;
    }

    if (m_liveChatId.isEmpty()) {
        m_discovery->tick();
        return;
    }
    if (shouldUseWebChatTransport()) {
        if (!m_webChatClient->isRunning()) {
            startWebChatTransport();
        }
        return;
    }
    if (shouldUseStreamListTransport()) {
        if (!m_streamListClient->isRunning()) {
            startStreamListTransport();
        }
        return;
    }
    requestLiveChatMessages();
}


void YouTubeAdapter::requestLiveChatMessages()
{
    if (m_accessToken.isEmpty() || m_liveChatId.isEmpty()) {
        scheduleNextTick(BotManager::Timings::kDefaultPollIntervalMs);
        return;
    }

    QUrl url(YouTube::Api::liveChatMessages());
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("id,snippet,authorDetails"));
    query.addQueryItem(QStringLiteral("liveChatId"), m_liveChatId);
    query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("200"));
    if (!m_nextPageToken.isEmpty()) {
        query.addQueryItem(QStringLiteral("pageToken"), m_nextPageToken);
    }
    url.setQuery(query);

    QNetworkReply* reply = m_network->get(createBearerRequest(url));
    if (!reply) {
        handleRequestFailure(QStringLiteral("REQUEST_FAILED"), QStringLiteral("Failed to create liveChat/messages request"));
        return;
    }

    const int gen = setupRequestGuard(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        if (gen != m_generation) {
            reply->deleteLater();
            return;
        }

        m_requestInFlight = false;
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();

        QJsonObject obj;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            obj = doc.object();
        }

        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        if (reply->error() != QNetworkReply::NoError || !httpOk) {
            const QString apiMessage = apiErrorMessage(obj);
            const QString reason = apiErrorReason(obj).toLower();
            const QString message = apiMessage.isEmpty() ? reply->errorString() : apiMessage;
            if (httpStatus == 404 || reason == QStringLiteral("livechatnotfound")
                || message.contains(QStringLiteral("liveChatNotFound"), Qt::CaseInsensitive)) {
                m_liveChatId.clear();
                m_nextPageToken.clear();
                m_bootstrapSearchFallbackTried = false;
                m_nextWebFallbackAllowedAtUtc = QDateTime();
                m_announcedLiveChatPending = false;
                setLiveStateChecking(QStringLiteral("liveChatId expired; re-checking live state."));
                scheduleNextTick(BotManager::Timings::kDefaultPollIntervalMs);
                reply->deleteLater();
                return;
            }
            if (reason == QStringLiteral("livechatended") || reason == QStringLiteral("livechatdisabled")) {
                m_liveChatId.clear();
                m_nextPageToken.clear();
                m_bootstrapSearchFallbackTried = false;
                m_nextWebFallbackAllowedAtUtc = QDateTime();
                m_announcedLiveChatPending = false;
                setLiveStateOffline(QStringLiteral("Live chat unavailable"));
                scheduleNextTick(BotManager::Timings::kDiscoveryRetryMs);
                emit error(platformId(), QStringLiteral("LIVE_CHAT_UNAVAILABLE"), message);
                reply->deleteLater();
                return;
            }
            if (reason == QStringLiteral("ratelimitexceeded")) {
                m_lastLiveStateCode.clear();
                scheduleNextTick(60000);
                emit error(platformId(), QStringLiteral("CHAT_RATE_LIMIT"), message);
                reply->deleteLater();
                return;
            }
            if (isQuotaExceededMessage(message) || isQuotaOrRateReason(reason)) {
                m_lastLiveStateCode.clear();
                scheduleNextTick(BotManager::Timings::kQuotaBackoffMs);
                emit error(platformId(), QStringLiteral("CHAT_RATE_LIMIT"), message);
                reply->deleteLater();
                return;
            }
            handleRequestFailure(QStringLiteral("CHAT_POLL_FAILED"), message);
            reply->deleteLater();
            return;
        }

        if (m_pendingConnectResult) {
            m_pendingConnectResult = false;
            m_connected = true;
            emit connected(platformId());
        }

        setLiveStateOnline(QStringLiteral("Receiving live chat"));
        m_nextPageToken = obj.value(QStringLiteral("nextPageToken")).toString().trimmed();
        const int pollingInterval = normalizePollIntervalMillis(obj.value(QStringLiteral("pollingIntervalMillis")).toInt(2500));

        const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
        for (const QJsonValue& value : items) {
            const UnifiedChatMessage msg = parseYouTubeChatMessageJson(value.toObject());
            publishReceivedMessage(msg);
        }

        scheduleNextTick(pollingInterval);
        reply->deleteLater();
    });
}

bool YouTubeAdapter::sendMessage(const QString& text)
{
    if (!m_connected || m_accessToken.trimmed().isEmpty()) {
        emit messageSent(platformId(), false, QStringLiteral("Not connected or token missing"));
        return false;
    }
    if (m_liveChatId.trimmed().isEmpty()) {
        emit messageSent(platformId(), false, QStringLiteral("liveChatId unavailable"));
        return false;
    }

    QUrl url(YouTube::Api::liveChatMessages());
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("snippet"));
    url.setQuery(query);

    QNetworkRequest req = createBearerRequest(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject payload;
    QJsonObject snippet;
    QJsonObject details;
    details.insert(QStringLiteral("messageText"), text);
    snippet.insert(QStringLiteral("liveChatId"), m_liveChatId);
    snippet.insert(QStringLiteral("type"), QStringLiteral("textMessageEvent"));
    snippet.insert(QStringLiteral("textMessageDetails"), details);
    payload.insert(QStringLiteral("snippet"), snippet);

    QNetworkReply* reply = m_network->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    if (!reply) {
        emit messageSent(platformId(), false, QStringLiteral("Failed to create send request"));
        return false;
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        QJsonObject obj;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            obj = doc.object();
        }

        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        if (reply->error() != QNetworkReply::NoError || !httpOk) {
            const QString apiMessage = apiErrorMessage(obj);
            const QString message = apiMessage.isEmpty() ? reply->errorString() : apiMessage;
            emit messageSent(platformId(), false, QStringLiteral("HTTP_%1 %2").arg(httpStatus).arg(message));
        } else {
            const QString messageId = obj.value(QStringLiteral("id")).toString().trimmed();
            emit messageSent(platformId(), true, QStringLiteral("messageId=%1").arg(messageId.isEmpty() ? QStringLiteral("-") : messageId));
        }
        reply->deleteLater();
    });
    return true;
}

QNetworkRequest YouTubeAdapter::createBearerRequest(const QUrl& url) const
{
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_accessToken).toUtf8());
    return req;
}

QNetworkRequest YouTubeAdapter::createWebScrapingRequest(const QUrl& url) const
{
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("Mozilla/5.0 BotManagerQt5/1.0 (+YouTubeLiveResolver)"));
    return req;
}

int YouTubeAdapter::setupRequestGuard(QNetworkReply* reply)
{
    const int gen = setupRequestGuard(reply);
    return gen;
}

void YouTubeAdapter::handleRequestFailure(const QString& code, const QString& message)
{
    if (m_pendingConnectResult) {
        m_pendingConnectResult = false;
        m_running = false;
        m_connected = false;
        setLiveStateUnknown(message);
        emit error(platformId(), code, message);
        return;
    }
    if (isQuotaExceededMessage(message)) {
        m_lastLiveStateCode.clear();
        scheduleNextTick(BotManager::Timings::kQuotaBackoffMs);
    } else {
        m_lastLiveStateCode.clear();
        scheduleNextTick(discoveryBackoffDelayMs(m_bootstrapDiscoverAttempts));
    }
    emit error(platformId(), code, message);
}
