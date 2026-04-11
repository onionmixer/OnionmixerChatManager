#include "platform/youtube/YouTubeAdapter.h"

#include <QDateTime>
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
#include <QtGlobal>

namespace {
QString firstNonEmpty(const QStringList& values)
{
    for (const QString& v : values) {
        const QString t = v.trimmed();
        if (!t.isEmpty()) {
            return t;
        }
    }
    return QString();
}

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

QString summarizeBySnippetType(const QString& type, const QJsonObject& snippet)
{
    const QString t = type.trimmed();
    if (t == QStringLiteral("textMessageEvent")) {
        return firstNonEmpty({
            snippet.value(QStringLiteral("displayMessage")).toString(),
            snippet.value(QStringLiteral("textMessageDetails")).toObject().value(QStringLiteral("messageText")).toString(),
        });
    }
    if (t == QStringLiteral("messageDeletedEvent")) {
        const QString deletedId = snippet.value(QStringLiteral("messageDeletedDetails")).toObject().value(QStringLiteral("deletedMessageId")).toString().trimmed();
        return deletedId.isEmpty()
            ? QStringLiteral("[Message deleted]")
            : QStringLiteral("[Message deleted] id=%1").arg(deletedId);
    }
    if (t == QStringLiteral("userBannedEvent")) {
        const QJsonObject banned = snippet.value(QStringLiteral("userBannedDetails")).toObject();
        const QString name = banned.value(QStringLiteral("bannedUserDetails")).toObject().value(QStringLiteral("displayName")).toString().trimmed();
        const QString banType = banned.value(QStringLiteral("banType")).toString().trimmed();
        const QString duration = QString::number(banned.value(QStringLiteral("banDurationSeconds")).toInt());
        if (banType.compare(QStringLiteral("temporary"), Qt::CaseInsensitive) == 0) {
            return QStringLiteral("[User banned] %1 (%2s)").arg(name.isEmpty() ? QStringLiteral("-") : name, duration);
        }
        return QStringLiteral("[User banned] %1").arg(name.isEmpty() ? QStringLiteral("-") : name);
    }
    if (t == QStringLiteral("superChatEvent")) {
        const QJsonObject d = snippet.value(QStringLiteral("superChatDetails")).toObject();
        const QString amount = d.value(QStringLiteral("amountDisplayString")).toString().trimmed();
        const QString comment = d.value(QStringLiteral("userComment")).toString().trimmed();
        return comment.isEmpty()
            ? QStringLiteral("[Super Chat] %1").arg(amount.isEmpty() ? QStringLiteral("-") : amount)
            : QStringLiteral("[Super Chat] %1 %2").arg(amount.isEmpty() ? QStringLiteral("-") : amount, comment);
    }
    if (t == QStringLiteral("superStickerEvent")) {
        const QJsonObject d = snippet.value(QStringLiteral("superStickerDetails")).toObject();
        const QString amount = d.value(QStringLiteral("amountDisplayString")).toString().trimmed();
        const QString alt = d.value(QStringLiteral("superStickerMetadata")).toObject().value(QStringLiteral("altText")).toString().trimmed();
        return alt.isEmpty()
            ? QStringLiteral("[Super Sticker] %1").arg(amount.isEmpty() ? QStringLiteral("-") : amount)
            : QStringLiteral("[Super Sticker] %1 %2").arg(amount.isEmpty() ? QStringLiteral("-") : amount, alt);
    }
    if (t == QStringLiteral("memberMilestoneChatEvent")) {
        const QJsonObject d = snippet.value(QStringLiteral("memberMilestoneChatDetails")).toObject();
        const QString month = QString::number(d.value(QStringLiteral("memberMonth")).toInt());
        const QString comment = d.value(QStringLiteral("userComment")).toString().trimmed();
        return comment.isEmpty()
            ? QStringLiteral("[Member Milestone] %1 months").arg(month)
            : QStringLiteral("[Member Milestone] %1 months %2").arg(month, comment);
    }
    if (t == QStringLiteral("newSponsorEvent")) {
        const QJsonObject d = snippet.value(QStringLiteral("newSponsorDetails")).toObject();
        const QString level = d.value(QStringLiteral("memberLevelName")).toString().trimmed();
        const bool isUpgrade = d.value(QStringLiteral("isUpgrade")).toBool();
        return isUpgrade
            ? QStringLiteral("[Membership Upgrade] %1").arg(level.isEmpty() ? QStringLiteral("-") : level)
            : QStringLiteral("[New Member] %1").arg(level.isEmpty() ? QStringLiteral("-") : level);
    }
    if (t == QStringLiteral("membershipGiftingEvent")) {
        const QJsonObject d = snippet.value(QStringLiteral("membershipGiftingDetails")).toObject();
        const int count = d.value(QStringLiteral("giftMembershipsCount")).toInt();
        const QString level = d.value(QStringLiteral("giftMembershipsLevelName")).toString().trimmed();
        return QStringLiteral("[Membership Gifting] count=%1 level=%2").arg(count).arg(level.isEmpty() ? QStringLiteral("-") : level);
    }
    if (t == QStringLiteral("giftMembershipReceivedEvent")) {
        const QJsonObject d = snippet.value(QStringLiteral("giftMembershipReceivedDetails")).toObject();
        const QString level = d.value(QStringLiteral("memberLevelName")).toString().trimmed();
        return QStringLiteral("[Gift Membership Received] %1").arg(level.isEmpty() ? QStringLiteral("-") : level);
    }
    if (t == QStringLiteral("pollEvent") || t == QStringLiteral("pollDetails")) {
        const QJsonObject pollDetails = snippet.value(QStringLiteral("pollDetails")).toObject();
        const QString question = pollDetails.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("questionText")).toString().trimmed();
        return question.isEmpty()
            ? QStringLiteral("[Poll]")
            : QStringLiteral("[Poll] %1").arg(question);
    }
    if (t == QStringLiteral("giftEvent")) {
        const QJsonObject gift = snippet.value(QStringLiteral("giftEventDetails")).toObject().value(QStringLiteral("giftMetadata")).toObject();
        const QString giftName = gift.value(QStringLiteral("giftName")).toString().trimmed();
        const int jewels = gift.value(QStringLiteral("jewelsAmount")).toInt();
        return QStringLiteral("[Gift] %1 jewels=%2").arg(giftName.isEmpty() ? QStringLiteral("-") : giftName).arg(jewels);
    }
    if (t == QStringLiteral("chatEndedEvent")) {
        return QStringLiteral("[Chat ended]");
    }
    if (t == QStringLiteral("sponsorOnlyModeStartedEvent")) {
        return QStringLiteral("[Sponsors-only mode started]");
    }
    if (t == QStringLiteral("sponsorOnlyModeEndedEvent")) {
        return QStringLiteral("[Sponsors-only mode ended]");
    }
    if (t == QStringLiteral("tombstone")) {
        return QStringLiteral("[Deleted message placeholder]");
    }
    return t.isEmpty() ? QString() : QStringLiteral("[%1]").arg(t);
}
} // namespace

YouTubeAdapter::YouTubeAdapter(QObject* parent)
    : IChatPlatformAdapter(parent)
{
    m_network = new QNetworkAccessManager(this);
    m_loopTimer = new QTimer(this);
    m_loopTimer->setSingleShot(true);
    connect(m_loopTimer, &QTimer::timeout, this, &YouTubeAdapter::onLoopTick);
}

PlatformId YouTubeAdapter::platformId() const
{
    return PlatformId::YouTube;
}

int YouTubeAdapter::connectDiscoveryDelayMs() const
{
    return isBootstrapDiscoveryPhase() ? 1000 : 3000;
}

bool YouTubeAdapter::isBootstrapDiscoveryPhase() const
{
    return m_bootstrapDiscoverAttempts < 10;
}

void YouTubeAdapter::emitLiveChatPendingInfoOnce(const QString& detail)
{
    if (!m_liveChatId.isEmpty() || m_announcedLiveChatPending) {
        return;
    }
    m_announcedLiveChatPending = true;
    emit error(platformId(), QStringLiteral("INFO_LIVECHAT_ID_PENDING"), detail);
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
        emit error(platformId(), QStringLiteral("INVALID_CONFIG"), QStringLiteral("YouTube config is incomplete."));
        return;
    }

    m_accessToken = settings.runtimeAccessToken.trimmed();
    if (m_accessToken.isEmpty()) {
        emit error(platformId(), QStringLiteral("TOKEN_MISSING"), QStringLiteral("YouTube access token is missing. Refresh/Re-auth required."));
        return;
    }

    m_channelId = settings.channelId.trimmed();
    m_channelName = settings.channelName.trimmed();
    if (m_channelName.isEmpty()) {
        m_channelName = settings.accountLabel.trimmed();
    }

    m_liveChatId.clear();
    m_nextPageToken.clear();
    m_seenMessageIds.clear();
    m_requestInFlight = false;
    m_bootstrapDiscoverAttempts = 0;
    m_bootstrapSearchFallbackTried = false;
    m_announcedLiveChatPending = false;
    m_nextSearchFallbackAllowedAtUtc = QDateTime();
    ++m_generation;
    m_running = true;
    m_pendingConnectResult = true;
    m_connected = false;
    scheduleNextTick(100);
}

void YouTubeAdapter::stop()
{
    if (!m_running && !m_connected) {
        emit disconnected(platformId());
        return;
    }

    ++m_generation;
    m_running = false;
    m_pendingConnectResult = false;
    m_requestInFlight = false;
    if (m_loopTimer) {
        m_loopTimer->stop();
    }
    m_liveChatId.clear();
    m_nextPageToken.clear();
    m_seenMessageIds.clear();
    m_accessToken.clear();
    m_bootstrapDiscoverAttempts = 0;
    m_bootstrapSearchFallbackTried = false;
    m_announcedLiveChatPending = false;
    m_nextSearchFallbackAllowedAtUtc = QDateTime();

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
        requestActiveBroadcast();
        return;
    }
    requestLiveChatMessages();
}

void YouTubeAdapter::requestActiveBroadcast()
{
    if (m_accessToken.isEmpty()) {
        handleRequestFailure(QStringLiteral("TOKEN_MISSING"), QStringLiteral("YouTube access token missing"));
        return;
    }

    QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/liveBroadcasts"));
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("id,snippet,status"));
    query.addQueryItem(QStringLiteral("mine"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("50"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_accessToken).toUtf8());

    QNetworkReply* reply = m_network->get(req);
    if (!reply) {
        handleRequestFailure(QStringLiteral("REQUEST_FAILED"), QStringLiteral("Failed to create liveBroadcasts request"));
        return;
    }

    const int gen = m_generation;
    QPointer<QNetworkReply> guard(reply);
    QTimer::singleShot(8000, this, [this, gen, guard]() {
        if (gen != m_generation || !guard || guard->isFinished()) {
            return;
        }
        guard->abort();
    });
    m_requestInFlight = true;
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
            const QString normalized = message.toLower();
            const bool quotaOrRate = isQuotaExceededMessage(message) || isQuotaOrRateReason(reason);
            const bool liveNotEnabled = normalized.contains(QStringLiteral("not enabled for live streaming"))
                || reason.contains(QStringLiteral("insufficientlivepermissions"))
                || reason.contains(QStringLiteral("livestreamingnotenabled"));

            if (quotaOrRate) {
                if (m_pendingConnectResult) {
                    m_pendingConnectResult = false;
                    m_connected = true;
                    emit connected(platformId());
                }
                scheduleNextTick(10000);
                emit error(platformId(), QStringLiteral("LIVE_DISCOVERY_FAILED"), message);
                reply->deleteLater();
                return;
            }

            if (liveNotEnabled) {
                if (m_pendingConnectResult) {
                    m_pendingConnectResult = false;
                    m_connected = true;
                    emit connected(platformId());
                }
                scheduleNextTick(connectDiscoveryDelayMs());
                emit error(platformId(), QStringLiteral("LIVE_DISCOVERY_FAILED"), message);
                reply->deleteLater();
                return;
            }

            const bool fallbackCoolingDown = m_nextSearchFallbackAllowedAtUtc.isValid()
                && QDateTime::currentDateTimeUtc() < m_nextSearchFallbackAllowedAtUtc;
            if (isBootstrapDiscoveryPhase() && !m_channelId.isEmpty() && !m_bootstrapSearchFallbackTried && !fallbackCoolingDown) {
                m_bootstrapSearchFallbackTried = true;
                m_nextSearchFallbackAllowedAtUtc = QDateTime::currentDateTimeUtc().addSecs(120);
                requestLiveByChannelSearch();
                reply->deleteLater();
                return;
            }

            if (m_pendingConnectResult) {
                m_pendingConnectResult = false;
                m_connected = true;
                emit connected(platformId());
            }
            scheduleNextTick(connectDiscoveryDelayMs());
            emit error(platformId(), QStringLiteral("LIVE_DISCOVERY_FAILED"), message);
            reply->deleteLater();
            return;
        }

        if (m_pendingConnectResult) {
            m_pendingConnectResult = false;
            m_connected = true;
            emit connected(platformId());
        }

        const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
        if (items.isEmpty()) {
            m_liveChatId.clear();
            m_nextPageToken.clear();
            ++m_bootstrapDiscoverAttempts;
            emitLiveChatPendingInfoOnce(QStringLiteral("Connected but liveChatId is not available yet."));
            scheduleNextTick(connectDiscoveryDelayMs());
            reply->deleteLater();
            return;
        }

        QString liveChatId;
        for (const QJsonValue& v : items) {
            const QJsonObject item = v.toObject();
            const QJsonObject snippet = item.value(QStringLiteral("snippet")).toObject();
            const QString candidate = snippet.value(QStringLiteral("liveChatId")).toString().trimmed();
            if (candidate.isEmpty()) {
                continue;
            }
            const QJsonObject status = item.value(QStringLiteral("status")).toObject();
            const QString lifeCycle = status.value(QStringLiteral("lifeCycleStatus")).toString();
            if (isPreferredLiveCycleForChatId(lifeCycle)) {
                liveChatId = candidate;
                break;
            }
        }
        if (liveChatId.isEmpty()) {
            for (const QJsonValue& v : items) {
                const QJsonObject item = v.toObject();
                const QString candidate = item.value(QStringLiteral("snippet")).toObject().value(QStringLiteral("liveChatId")).toString().trimmed();
                if (!candidate.isEmpty()) {
                    liveChatId = candidate;
                    break;
                }
            }
        }
        if (liveChatId.isEmpty()) {
            m_liveChatId.clear();
            m_nextPageToken.clear();
            ++m_bootstrapDiscoverAttempts;
            const bool fallbackCoolingDown = m_nextSearchFallbackAllowedAtUtc.isValid()
                && QDateTime::currentDateTimeUtc() < m_nextSearchFallbackAllowedAtUtc;
            if (isBootstrapDiscoveryPhase() && !m_channelId.isEmpty() && !m_bootstrapSearchFallbackTried && !fallbackCoolingDown) {
                m_bootstrapSearchFallbackTried = true;
                m_nextSearchFallbackAllowedAtUtc = QDateTime::currentDateTimeUtc().addSecs(120);
                requestLiveByChannelSearch();
                reply->deleteLater();
                return;
            }
            if (m_pendingConnectResult) {
                m_pendingConnectResult = false;
                m_connected = true;
                emit connected(platformId());
            }
            emitLiveChatPendingInfoOnce(QStringLiteral("Connected but liveChatId discovery is still pending."));
            scheduleNextTick(connectDiscoveryDelayMs());
            reply->deleteLater();
            return;
        }

        const bool wasEmpty = m_liveChatId.isEmpty();
        m_liveChatId = liveChatId;
        m_announcedLiveChatPending = false;
        if (wasEmpty) {
            emit error(platformId(), QStringLiteral("INFO_LIVECHAT_ID_READY"),
                QStringLiteral("YouTube liveChatId resolved via liveBroadcasts."));
        }
        m_bootstrapDiscoverAttempts = 10;
        m_nextPageToken.clear();
        scheduleNextTick(100);
        reply->deleteLater();
    });
}

void YouTubeAdapter::requestLiveByChannelSearch()
{
    if (m_accessToken.isEmpty() || m_channelId.isEmpty()) {
        if (m_pendingConnectResult) {
            m_pendingConnectResult = false;
            m_connected = true;
            emit connected(platformId());
        }
        ++m_bootstrapDiscoverAttempts;
        emitLiveChatPendingInfoOnce(QStringLiteral("Connected but waiting for channel/live lookup."));
        scheduleNextTick(connectDiscoveryDelayMs());
        return;
    }

    QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/search"));
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("id"));
    query.addQueryItem(QStringLiteral("channelId"), m_channelId);
    query.addQueryItem(QStringLiteral("eventType"), QStringLiteral("live"));
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("video"));
    query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("1"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_accessToken).toUtf8());
    QNetworkReply* reply = m_network->get(req);
    if (!reply) {
        if (m_pendingConnectResult) {
            m_pendingConnectResult = false;
            m_connected = true;
            emit connected(platformId());
        }
        ++m_bootstrapDiscoverAttempts;
        emitLiveChatPendingInfoOnce(QStringLiteral("Connected but live search request could not be created."));
        scheduleNextTick(connectDiscoveryDelayMs());
        return;
    }

    const int gen = m_generation;
    QPointer<QNetworkReply> guard(reply);
    QTimer::singleShot(8000, this, [gen, guard]() {
        if (!guard || guard->isFinished()) {
            return;
        }
        Q_UNUSED(gen)
        guard->abort();
    });
    m_requestInFlight = true;
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
            if (m_pendingConnectResult) {
                m_pendingConnectResult = false;
                m_connected = true;
                emit connected(platformId());
            }
            ++m_bootstrapDiscoverAttempts;
            emitLiveChatPendingInfoOnce(QStringLiteral("Connected but live search did not return liveChatId."));
            scheduleNextTick(connectDiscoveryDelayMs());
            reply->deleteLater();
            return;
        }

        const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
        if (items.isEmpty()) {
            if (m_pendingConnectResult) {
                m_pendingConnectResult = false;
                m_connected = true;
                emit connected(platformId());
            }
            m_liveChatId.clear();
            m_nextPageToken.clear();
            ++m_bootstrapDiscoverAttempts;
            emitLiveChatPendingInfoOnce(QStringLiteral("Connected but no live video was found for this channel."));
            scheduleNextTick(connectDiscoveryDelayMs());
            reply->deleteLater();
            return;
        }

        const QJsonObject first = items.first().toObject();
        const QJsonObject idObj = first.value(QStringLiteral("id")).toObject();
        const QString videoId = idObj.value(QStringLiteral("videoId")).toString().trimmed();
        if (videoId.isEmpty()) {
            if (m_pendingConnectResult) {
                m_pendingConnectResult = false;
                m_connected = true;
                emit connected(platformId());
            }
            ++m_bootstrapDiscoverAttempts;
            emitLiveChatPendingInfoOnce(QStringLiteral("Connected but live video id is unavailable."));
            scheduleNextTick(connectDiscoveryDelayMs());
            reply->deleteLater();
            return;
        }

        requestVideoDetailsForLiveChat(videoId);
        reply->deleteLater();
    });
}

void YouTubeAdapter::requestVideoDetailsForLiveChat(const QString& videoId)
{
    if (m_accessToken.isEmpty() || videoId.trimmed().isEmpty()) {
        if (m_pendingConnectResult) {
            m_pendingConnectResult = false;
            m_connected = true;
            emit connected(platformId());
        }
        ++m_bootstrapDiscoverAttempts;
        emitLiveChatPendingInfoOnce(QStringLiteral("Connected but live video details are not ready."));
        scheduleNextTick(connectDiscoveryDelayMs());
        return;
    }

    QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/videos"));
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("liveStreamingDetails"));
    query.addQueryItem(QStringLiteral("id"), videoId.trimmed());
    query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("1"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_accessToken).toUtf8());
    QNetworkReply* reply = m_network->get(req);
    if (!reply) {
        if (m_pendingConnectResult) {
            m_pendingConnectResult = false;
            m_connected = true;
            emit connected(platformId());
        }
        ++m_bootstrapDiscoverAttempts;
        emitLiveChatPendingInfoOnce(QStringLiteral("Connected but video details request could not be created."));
        scheduleNextTick(connectDiscoveryDelayMs());
        return;
    }

    const int gen = m_generation;
    QPointer<QNetworkReply> guard(reply);
    QTimer::singleShot(8000, this, [gen, guard]() {
        if (!guard || guard->isFinished()) {
            return;
        }
        Q_UNUSED(gen)
        guard->abort();
    });
    m_requestInFlight = true;
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
            if (m_pendingConnectResult) {
                m_pendingConnectResult = false;
                m_connected = true;
                emit connected(platformId());
            }
            ++m_bootstrapDiscoverAttempts;
            emitLiveChatPendingInfoOnce(QStringLiteral("Connected but live video details did not return liveChatId."));
            scheduleNextTick(connectDiscoveryDelayMs());
            reply->deleteLater();
            return;
        }

        const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
        QString liveChatId;
        if (!items.isEmpty()) {
            const QJsonObject first = items.first().toObject();
            const QJsonObject details = first.value(QStringLiteral("liveStreamingDetails")).toObject();
            liveChatId = details.value(QStringLiteral("activeLiveChatId")).toString().trimmed();
        }

        if (!liveChatId.isEmpty()) {
            const bool wasEmpty = m_liveChatId.isEmpty();
            m_liveChatId = liveChatId;
            m_announcedLiveChatPending = false;
            if (wasEmpty) {
                emit error(platformId(), QStringLiteral("INFO_LIVECHAT_ID_READY"),
                    QStringLiteral("YouTube liveChatId resolved via videos.liveStreamingDetails."));
            }
            m_bootstrapDiscoverAttempts = 10;
            m_nextPageToken.clear();
            if (m_pendingConnectResult) {
                m_pendingConnectResult = false;
                m_connected = true;
                emit connected(platformId());
            }
            scheduleNextTick(100);
            reply->deleteLater();
            return;
        }

        if (m_pendingConnectResult) {
            m_pendingConnectResult = false;
            m_connected = true;
            emit connected(platformId());
        }
        m_liveChatId.clear();
        m_nextPageToken.clear();
        ++m_bootstrapDiscoverAttempts;
        emitLiveChatPendingInfoOnce(QStringLiteral("Connected but activeLiveChatId is still unavailable."));
        scheduleNextTick(connectDiscoveryDelayMs());
        reply->deleteLater();
    });
}

void YouTubeAdapter::requestLiveChatMessages()
{
    if (m_accessToken.isEmpty() || m_liveChatId.isEmpty()) {
        scheduleNextTick(3000);
        return;
    }

    QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/liveChat/messages"));
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("id,snippet,authorDetails"));
    query.addQueryItem(QStringLiteral("liveChatId"), m_liveChatId);
    query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("200"));
    if (!m_nextPageToken.isEmpty()) {
        query.addQueryItem(QStringLiteral("pageToken"), m_nextPageToken);
    }
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_accessToken).toUtf8());

    QNetworkReply* reply = m_network->get(req);
    if (!reply) {
        handleRequestFailure(QStringLiteral("REQUEST_FAILED"), QStringLiteral("Failed to create liveChat/messages request"));
        return;
    }

    const int gen = m_generation;
    QPointer<QNetworkReply> guard(reply);
    QTimer::singleShot(8000, this, [this, gen, guard]() {
        if (gen != m_generation || !guard || guard->isFinished()) {
            return;
        }
        guard->abort();
    });
    m_requestInFlight = true;
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
                m_announcedLiveChatPending = false;
                scheduleNextTick(3000);
                reply->deleteLater();
                return;
            }
            if (reason == QStringLiteral("livechatended") || reason == QStringLiteral("livechatdisabled")) {
                m_liveChatId.clear();
                m_nextPageToken.clear();
                m_bootstrapSearchFallbackTried = false;
                m_announcedLiveChatPending = false;
                scheduleNextTick(5000);
                emit error(platformId(), QStringLiteral("LIVE_CHAT_UNAVAILABLE"), message);
                reply->deleteLater();
                return;
            }
            if (reason == QStringLiteral("ratelimitexceeded")) {
                scheduleNextTick(10000);
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

        m_nextPageToken = obj.value(QStringLiteral("nextPageToken")).toString().trimmed();
        const int pollingInterval = normalizePollIntervalMillis(obj.value(QStringLiteral("pollingIntervalMillis")).toInt(2500));

        const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
        for (const QJsonValue& value : items) {
            const QJsonObject item = value.toObject();
            const QString messageId = item.value(QStringLiteral("id")).toString().trimmed();
            if (messageId.isEmpty() || m_seenMessageIds.contains(messageId)) {
                continue;
            }
            m_seenMessageIds.insert(messageId);
            if (m_seenMessageIds.size() > 4000) {
                m_seenMessageIds.clear();
            }

            const QJsonObject snippet = item.value(QStringLiteral("snippet")).toObject();
            const QJsonObject author = item.value(QStringLiteral("authorDetails")).toObject();

            UnifiedChatMessage msg;
            msg.platform = platformId();
            msg.messageId = messageId;
            msg.channelId = m_channelId;
            msg.channelName = m_channelName.isEmpty() ? QStringLiteral("YouTube") : m_channelName;
            msg.authorId = author.value(QStringLiteral("channelId")).toString().trimmed();
            if (msg.authorId.isEmpty()) {
                msg.authorId = snippet.value(QStringLiteral("authorChannelId")).toString().trimmed();
            }
            msg.authorName = author.value(QStringLiteral("displayName")).toString().trimmed();
            const QString type = snippet.value(QStringLiteral("type")).toString().trimmed();
            const bool hasDisplayContent = !snippet.contains(QStringLiteral("hasDisplayContent"))
                || snippet.value(QStringLiteral("hasDisplayContent")).toBool();
            msg.text = firstNonEmpty({
                snippet.value(QStringLiteral("displayMessage")).toString(),
                snippet.value(QStringLiteral("textMessageDetails")).toObject().value(QStringLiteral("messageText")).toString(),
            });
            if (msg.text.trimmed().isEmpty()) {
                msg.text = summarizeBySnippetType(type, snippet);
            }
            if (msg.authorName.isEmpty() && type == QStringLiteral("userBannedEvent")) {
                msg.authorName = snippet.value(QStringLiteral("userBannedDetails"))
                                     .toObject()
                                     .value(QStringLiteral("bannedUserDetails"))
                                     .toObject()
                                     .value(QStringLiteral("displayName"))
                                     .toString()
                                     .trimmed();
            }
            if (msg.text.trimmed().isEmpty() && !hasDisplayContent) {
                msg.text = QStringLiteral("[Event] %1").arg(type.isEmpty() ? QStringLiteral("unknown") : type);
            }
            msg.timestamp = QDateTime::fromString(snippet.value(QStringLiteral("publishedAt")).toString(), Qt::ISODate);
            if (!msg.timestamp.isValid()) {
                msg.timestamp = QDateTime::currentDateTime();
            }
            emit chatReceived(msg);
        }

        scheduleNextTick(pollingInterval);
        reply->deleteLater();
    });
}

void YouTubeAdapter::handleRequestFailure(const QString& code, const QString& message)
{
    if (m_pendingConnectResult) {
        m_pendingConnectResult = false;
        m_running = false;
        m_connected = false;
        emit error(platformId(), code, message);
        return;
    }
    if (isQuotaExceededMessage(message)) {
        scheduleNextTick(10000);
    } else {
        scheduleNextTick(3000);
    }
    emit error(platformId(), code, message);
}
