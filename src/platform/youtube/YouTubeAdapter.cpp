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
int clampPollInterval(int value)
{
    if (value < 1000) {
        return 1000;
    }
    if (value > 10000) {
        return 10000;
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
            const QString apiMessage = obj.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString().trimmed();
            const QString message = apiMessage.isEmpty() ? reply->errorString() : apiMessage;
            const QString normalized = message.toLower();
            if (normalized.contains(QStringLiteral("not enabled for live streaming")) && !m_channelId.isEmpty()) {
                requestLiveByChannelSearch();
            } else {
                handleRequestFailure(QStringLiteral("LIVE_DISCOVERY_FAILED"), message);
            }
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
            scheduleNextTick(3000);
            reply->deleteLater();
            return;
        }

        QString liveChatId;
        for (const QJsonValue& v : items) {
            const QJsonObject item = v.toObject();
            const QJsonObject status = item.value(QStringLiteral("status")).toObject();
            const QString lifeCycle = status.value(QStringLiteral("lifeCycleStatus")).toString().trimmed().toLower();
            if (lifeCycle != QStringLiteral("live") && lifeCycle != QStringLiteral("live_starting")) {
                continue;
            }
            const QJsonObject snippet = item.value(QStringLiteral("snippet")).toObject();
            liveChatId = snippet.value(QStringLiteral("liveChatId")).toString().trimmed();
            if (!liveChatId.isEmpty()) {
                break;
            }
        }
        if (liveChatId.isEmpty()) {
            m_liveChatId.clear();
            m_nextPageToken.clear();
            if (!m_channelId.isEmpty()) {
                requestLiveByChannelSearch();
                reply->deleteLater();
                return;
            }
            if (m_pendingConnectResult) {
                m_pendingConnectResult = false;
                m_connected = true;
                emit connected(platformId());
            }
            scheduleNextTick(3000);
            reply->deleteLater();
            return;
        }

        m_liveChatId = liveChatId;
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
        scheduleNextTick(3000);
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
        scheduleNextTick(3000);
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
            scheduleNextTick(3000);
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
            scheduleNextTick(3000);
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
            scheduleNextTick(3000);
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
        scheduleNextTick(3000);
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
        scheduleNextTick(3000);
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
            scheduleNextTick(3000);
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
            m_liveChatId = liveChatId;
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
        scheduleNextTick(3000);
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
            const QString apiMessage = obj.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString().trimmed();
            const QString message = apiMessage.isEmpty() ? reply->errorString() : apiMessage;
            if (httpStatus == 404 || message.contains(QStringLiteral("liveChatNotFound"), Qt::CaseInsensitive)) {
                m_liveChatId.clear();
                m_nextPageToken.clear();
                scheduleNextTick(3000);
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
        int pollingInterval = obj.value(QStringLiteral("pollingIntervalMillis")).toInt(2500);
        if (pollingInterval <= 0) {
            pollingInterval = 2500;
        }

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
            msg.text = snippet.value(QStringLiteral("displayMessage")).toString();
            if (msg.text.trimmed().isEmpty()) {
                const QJsonObject textDetails = snippet.value(QStringLiteral("textMessageDetails")).toObject();
                msg.text = textDetails.value(QStringLiteral("messageText")).toString();
            }
            msg.timestamp = QDateTime::fromString(snippet.value(QStringLiteral("publishedAt")).toString(), Qt::ISODate);
            if (!msg.timestamp.isValid()) {
                msg.timestamp = QDateTime::currentDateTime();
            }
            emit chatReceived(msg);
        }

        scheduleNextTick(clampPollInterval(pollingInterval));
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
        scheduleNextTick(300000);
    } else {
        scheduleNextTick(3000);
    }
    emit error(platformId(), code, message);
}
