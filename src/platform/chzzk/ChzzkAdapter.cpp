#include "platform/chzzk/ChzzkAdapter.h"
#include "core/Constants.h"
#include "platform/chzzk/ChzzkEndpoints.h"
#include "platform/chzzk/ChzzkEmojiResolver.h"
#include "utils/JsonHelper.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QUrlQuery>
#include <QWebSocket>

namespace {

using JsonHelper::readStringByKeys;
using JsonHelper::parseJsonObjectString;
using JsonHelper::jsonObjectFromValue;
using JsonHelper::parseEventTime;

bool looksLikeChatPayload(const QJsonObject& obj)
{
    return obj.contains(QStringLiteral("content"))
        || obj.contains(QStringLiteral("message"))
        || obj.contains(QStringLiteral("profile"))
        || obj.contains(QStringLiteral("senderChannelId"));
}
} // namespace

ChzzkAdapter::ChzzkAdapter(QObject* parent)
    : IChatPlatformAdapter(parent)
{
    m_network = new QNetworkAccessManager(this);
    m_emojiResolver = new ChzzkEmojiResolver(m_network, this);
    m_connectWatchdog = new QTimer(this);
    m_connectWatchdog->setSingleShot(true);
    connect(m_connectWatchdog, &QTimer::timeout, this, [this]() {
        if (m_stopping || !m_pendingConnectResult) {
            return;
        }
        handleConnectFailure(QStringLiteral("CONNECT_TIMEOUT"), QStringLiteral("CHZZK connect timeout"));
    });

    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, [this]() {
        if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
            m_socket->sendTextMessage(QStringLiteral("2"));
        }
    });

    m_socket = new QWebSocket;
    m_socket->setParent(this);
    connect(m_socket, &QWebSocket::connected, this, &ChzzkAdapter::onSocketConnected);
    connect(m_socket, &QWebSocket::disconnected, this, &ChzzkAdapter::onSocketDisconnected);
    connect(m_socket, &QWebSocket::textMessageReceived, this, &ChzzkAdapter::onSocketTextMessageReceived);
    connect(m_socket,
        static_cast<void (QWebSocket::*)(QAbstractSocket::SocketError)>(&QWebSocket::error),
        this,
        [this](QAbstractSocket::SocketError) {
            if (m_stopping) {
                return;
            }
            if (m_pendingConnectResult) {
                handleConnectFailure(QStringLiteral("SOCKET_ERROR"), m_socket->errorString());
            } else {
                emit error(platformId(), QStringLiteral("SOCKET_ERROR"), m_socket->errorString());
            }
        });
}

PlatformId ChzzkAdapter::platformId() const
{
    return PlatformId::Chzzk;
}

void ChzzkAdapter::applyRuntimeAccessToken(const QString& accessToken)
{
    const QString trimmed = accessToken.trimmed();
    if (m_accessToken == trimmed) {
        return;
    }

    m_accessToken = trimmed;
    emit error(platformId(), QStringLiteral("INFO_RUNTIME_TOKEN_UPDATED"),
        trimmed.isEmpty()
            ? QStringLiteral("CHZZK runtime access token cleared.")
            : QStringLiteral("CHZZK runtime access token updated."));
}

void ChzzkAdapter::resetProgressAnnouncements()
{
    m_announcedSessionPending = false;
    m_announcedSubscribePending = false;
    m_announcedChatReady = false;
}

void ChzzkAdapter::start(const PlatformSettings& settings)
{
    if (m_connected) {
        emit connected(platformId());
        return;
    }

    if (settings.clientId.trimmed().isEmpty() || settings.clientSecret.trimmed().isEmpty() || settings.redirectUri.trimmed().isEmpty() || settings.scope.trimmed().isEmpty()) {
        emit error(platformId(), QStringLiteral("INVALID_CONFIG"), QStringLiteral("CHZZK config is incomplete."));
        return;
    }
    if (settings.runtimeAccessToken.trimmed().isEmpty()) {
        emit error(platformId(), QStringLiteral("TOKEN_MISSING"), QStringLiteral("CHZZK access token is missing. Refresh/Re-auth required."));
        return;
    }

    m_stopping = false;
    m_connectSignalEmitted = false;
    m_pendingConnectResult = true;
    ++m_socketGeneration;
    m_accessToken = settings.runtimeAccessToken.trimmed();
    m_clientId = settings.clientId.trimmed();
    m_clientSecret = settings.clientSecret.trimmed();
    m_channelId = settings.channelId.trimmed();
    m_channelName = settings.channelName.trimmed();
    if (m_channelName.isEmpty()) {
        m_channelName = settings.accountLabel.trimmed();
    }
    m_sessionKey.clear();
    m_useClientSessionAuth = false;
    m_clientSessionFallbackTried = false;
    m_chatSubscribed = false;
    m_subscribeInFlight = false;
    m_subscribeInFlightSessionKey.clear();
    m_subscribeRetryCount = 0;
    m_subscribeSessionKey.clear();
    m_subscribeRecoverCount = 0;
    resetProgressAnnouncements();
    if (!m_channelId.isEmpty()) {
        m_emojiResolver->loadEmojiPacks(m_channelId);
    }
    if (m_connectWatchdog) {
        m_connectWatchdog->start(BotManager::Timings::kChzzkConnectWatchdogMs);
    }

    requestSessionAuth();
}

void ChzzkAdapter::stop()
{
    if (!m_connected && !m_connectSignalEmitted && m_socket->state() == QAbstractSocket::UnconnectedState) {
        emit disconnected(platformId());
        return;
    }

    m_stopping = true;
    m_pendingConnectResult = false;
    ++m_socketGeneration;
    if (m_connectWatchdog) {
        m_connectWatchdog->stop();
    }
    m_sessionKey.clear();
    m_useClientSessionAuth = false;
    m_clientSessionFallbackTried = false;
    m_chatSubscribed = false;
    m_subscribeInFlight = false;
    m_subscribeInFlightSessionKey.clear();
    m_subscribeRetryCount = 0;
    m_subscribeSessionKey.clear();
    m_subscribeRecoverCount = 0;
    resetProgressAnnouncements();
    m_seenMessageIds.clear();
    if (m_heartbeatTimer) {
        m_heartbeatTimer->stop();
    }
    m_socket->abort();
    m_accessToken.clear();
    m_connected = false;
    if (m_connectSignalEmitted) {
        m_connectSignalEmitted = false;
        emit disconnected(platformId());
    } else {
        emit disconnected(platformId());
    }
}

bool ChzzkAdapter::isConnected() const
{
    return m_connected;
}

void ChzzkAdapter::requestSessionAuth()
{
    if (m_stopping) {
        return;
    }
    if (m_accessToken.isEmpty()) {
        handleConnectFailure(QStringLiteral("TOKEN_MISSING"), QStringLiteral("CHZZK access token missing"));
        return;
    }

    const QUrl authUrl(m_useClientSessionAuth
            ? Chzzk::OpenApi::sessionAuthClient()
            : Chzzk::OpenApi::sessionAuth());
    QNetworkRequest req(authUrl);
    if (m_useClientSessionAuth) {
        if (!m_clientId.isEmpty()) {
            req.setRawHeader("Client-Id", m_clientId.toUtf8());
        }
        if (!m_clientSecret.isEmpty()) {
            req.setRawHeader("Client-Secret", m_clientSecret.toUtf8());
        }
    } else {
        req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_accessToken).toUtf8());
    }

    QNetworkReply* reply = m_network->get(req);
    if (!reply) {
        handleConnectFailure(QStringLiteral("SESSION_AUTH_FAILED"), QStringLiteral("Failed to create session auth request"));
        return;
    }

    const int gen = m_socketGeneration;
    QPointer<QNetworkReply> guard(reply);
    QTimer::singleShot(BotManager::Timings::kChzzkSessionAuthTimeoutMs, this, [this, gen, guard]() {
        if (gen != m_socketGeneration || m_stopping || !guard || guard->isFinished()) {
            return;
        }
        guard->abort();
        if (m_pendingConnectResult) {
            handleConnectFailure(QStringLiteral("SESSION_AUTH_TIMEOUT"), QStringLiteral("sessions/auth timeout"));
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        if (gen != m_socketGeneration || m_stopping) {
            reply->deleteLater();
            return;
        }
        if (!m_pendingConnectResult) {
            reply->deleteLater();
            return;
        }

        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        QJsonObject obj;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            obj = doc.object();
        }

        const QJsonObject content = obj.value(QStringLiteral("content")).toObject();
        const QJsonObject payload = content.isEmpty() ? obj : content;
        const QString sessionUrl = readStringByKeys(payload, obj, { QStringLiteral("url"), QStringLiteral("sessionUrl"), QStringLiteral("connectUrl") });
        const QString sessionKey = readStringByKeys(payload, obj, { QStringLiteral("sessionKey"), QStringLiteral("session_key") });
        const QString apiMessage = readStringByKeys(payload, obj, { QStringLiteral("message"), QStringLiteral("error") });

        int apiCode = 0;
        bool hasApiCode = false;
        if (obj.value(QStringLiteral("code")).isDouble()) {
            apiCode = obj.value(QStringLiteral("code")).toInt();
            hasApiCode = true;
        } else if (obj.value(QStringLiteral("code")).isString()) {
            bool ok = false;
            const int parsed = obj.value(QStringLiteral("code")).toString().toInt(&ok);
            if (ok) {
                apiCode = parsed;
                hasApiCode = true;
            }
        }

        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        const bool apiOk = !hasApiCode || apiCode == 200;
        if (reply->error() != QNetworkReply::NoError || !httpOk || !apiOk || sessionUrl.isEmpty()) {
            const QString detail = QStringLiteral("http=%1 apiCode=%2 msg=%3")
                                       .arg(httpStatus)
                                       .arg(hasApiCode ? QString::number(apiCode) : QStringLiteral("-"))
                                       .arg(apiMessage.isEmpty() ? reply->errorString() : apiMessage);
            handleConnectFailure(QStringLiteral("SESSION_AUTH_FAILED"), detail);
            reply->deleteLater();
            return;
        }

        if (!sessionKey.isEmpty()) {
            m_sessionKey = sessionKey;
        }
        const QString modeText = m_useClientSessionAuth ? QStringLiteral("client") : QStringLiteral("user");
        const QString sessionPresence = m_sessionKey.isEmpty() ? QStringLiteral("-") : QStringLiteral("present");
        emit error(platformId(), QStringLiteral("TRACE_CHZZK_SESSION_AUTH_OK"),
            QStringLiteral("mode=%1 sessionUrl=%2 sessionKey=%3")
                .arg(modeText, sessionUrl.left(120), sessionPresence));
        connectSocket(sessionUrl);
        reply->deleteLater();
    });
}

void ChzzkAdapter::connectSocket(const QString& sessionUrl)
{
    const QUrl socketUrl = buildSocketUrl(sessionUrl);
    if (!socketUrl.isValid()) {
        handleConnectFailure(QStringLiteral("SOCKET_URL_INVALID"), QStringLiteral("Invalid CHZZK session socket URL"));
        return;
    }
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
    m_socket->open(socketUrl);
}

QUrl ChzzkAdapter::buildSocketUrl(const QString& sessionUrl) const
{
    QUrl url(sessionUrl);
    if (!url.isValid()) {
        return QUrl();
    }

    if (url.scheme() == QStringLiteral("https")) {
        url.setScheme(QStringLiteral("wss"));
    } else if (url.scheme() == QStringLiteral("http")) {
        url.setScheme(QStringLiteral("ws"));
    }

    if (url.path().trimmed().isEmpty() || url.path() == QStringLiteral("/")) {
        url.setPath(QStringLiteral("/socket.io/"));
    }

    QUrlQuery query(url);
    if (!query.hasQueryItem(QStringLiteral("transport"))) {
        query.addQueryItem(QStringLiteral("transport"), QStringLiteral("websocket"));
    }
    if (!query.hasQueryItem(QStringLiteral("EIO"))) {
        query.addQueryItem(QStringLiteral("EIO"), QStringLiteral("3"));
    }
    url.setQuery(query);
    return url;
}

void ChzzkAdapter::subscribeChatEvent(const QString& sessionKey)
{
    if (sessionKey.trimmed().isEmpty()) {
        return;
    }
    if (m_subscribeSessionKey != sessionKey) {
        m_subscribeSessionKey = sessionKey;
        m_subscribeRetryCount = 0;
    }
    if (m_chatSubscribed) {
        return;
    }
    if (m_subscribeInFlight && m_subscribeInFlightSessionKey == sessionKey) {
        return;
    }

    QUrl url(Chzzk::OpenApi::subscribeChat());
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("sessionKey"), sessionKey);
    if (m_useClientSessionAuth && !m_channelId.trimmed().isEmpty()) {
        query.addQueryItem(QStringLiteral("channelId"), m_channelId.trimmed());
    }
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    req.setHeader(QNetworkRequest::ContentLengthHeader, 0);
    if (m_useClientSessionAuth) {
        if (!m_clientId.isEmpty()) {
            req.setRawHeader("Client-Id", m_clientId.toUtf8());
        }
        if (!m_clientSecret.isEmpty()) {
            req.setRawHeader("Client-Secret", m_clientSecret.toUtf8());
        }
    } else {
        req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_accessToken).toUtf8());
    }

    QNetworkReply* reply = m_network->post(req, QByteArray());
    if (!reply) {
        emit error(platformId(), QStringLiteral("SUBSCRIBE_FAILED"), QStringLiteral("Failed to create chat subscribe request"));
        return;
    }
    m_subscribeInFlight = true;
    m_subscribeInFlightSessionKey = sessionKey;
    if (!m_announcedSubscribePending) {
        m_announcedSubscribePending = true;
        emit error(platformId(), QStringLiteral("INFO_CHZZK_SUBSCRIBE_PENDING"),
            QStringLiteral("Connected but waiting for CHZZK chat subscription."));
    }

    const int gen = m_socketGeneration;
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, sessionKey]() {
        if (gen != m_socketGeneration || m_stopping) {
            reply->deleteLater();
            return;
        }
        m_subscribeInFlight = false;
        m_subscribeInFlightSessionKey.clear();

        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        if (httpStatus == 409) {
            m_chatSubscribed = true;
            m_subscribeRetryCount = 0;
            m_subscribeRecoverCount = 0;
            m_announcedSubscribePending = false;
            emit error(platformId(), QStringLiteral("TRACE_CHZZK_SUBSCRIBE_OK"),
                QStringLiteral("sessionKey=%1 (already subscribed)").arg(sessionKey.left(12)));
            if (!m_announcedChatReady) {
                m_announcedChatReady = true;
                emit error(platformId(), QStringLiteral("INFO_CHZZK_CHAT_READY"),
                    QStringLiteral("CHZZK chat subscription is ready."));
            }
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError || !httpOk) {
            const QByteArray raw = reply->readAll();
            const QString detail = QStringLiteral("http=%1 err=%2 body=%3")
                                       .arg(httpStatus)
                                       .arg(reply->errorString(), QString::fromUtf8(raw.left(300)));
            if (httpStatus == 404 && m_subscribeRetryCount < 6 && sessionKey == m_sessionKey) {
                ++m_subscribeRetryCount;
                const int backoffMs = qMin(7000, 500 * (1 << (m_subscribeRetryCount - 1)));
                emit error(platformId(), QStringLiteral("TRACE_CHZZK_SUBSCRIBE_RETRY"),
                    QStringLiteral("retry=%1 backoffMs=%2 %3")
                        .arg(m_subscribeRetryCount)
                        .arg(backoffMs)
                        .arg(detail));
                const int gen2 = m_socketGeneration;
                QTimer::singleShot(backoffMs, this, [this, gen2, sessionKey]() {
                    if (gen2 != m_socketGeneration || m_stopping) {
                        return;
                    }
                    if (m_chatSubscribed || m_sessionKey != sessionKey) {
                        return;
                    }
                    subscribeChatEvent(sessionKey);
                });
            } else if (httpStatus == 404 && sessionKey == m_sessionKey && m_subscribeRecoverCount < 2) {
                ++m_subscribeRecoverCount;
                emit error(platformId(), QStringLiteral("TRACE_CHZZK_SESSION_RECOVER"),
                    QStringLiteral("recover=%1, refreshing session").arg(m_subscribeRecoverCount));
                m_subscribeRetryCount = 0;
                m_subscribeSessionKey.clear();
                m_chatSubscribed = false;
                m_sessionKey.clear();
                m_announcedSubscribePending = false;
                m_announcedChatReady = false;
                requestSessionAuth();
            } else if (httpStatus == 404
                && !m_useClientSessionAuth
                && !m_clientSessionFallbackTried
                && !m_clientId.trimmed().isEmpty()
                && !m_clientSecret.trimmed().isEmpty()) {
                m_clientSessionFallbackTried = true;
                m_useClientSessionAuth = true;
                m_subscribeRetryCount = 0;
                m_subscribeRecoverCount = 0;
                m_subscribeSessionKey.clear();
                m_chatSubscribed = false;
                m_subscribeInFlight = false;
                m_subscribeInFlightSessionKey.clear();
                m_sessionKey.clear();
                m_announcedSubscribePending = false;
                m_announcedChatReady = false;
                emit error(platformId(), QStringLiteral("TRACE_CHZZK_AUTH_MODE_SWITCH"),
                    QStringLiteral("switching session auth mode: user -> client"));
                requestSessionAuth();
            } else {
                emit error(platformId(), QStringLiteral("SUBSCRIBE_FAILED"), detail);
            }
        } else {
            m_chatSubscribed = true;
            m_subscribeRetryCount = 0;
            m_subscribeRecoverCount = 0;
            m_announcedSubscribePending = false;
            emit error(platformId(), QStringLiteral("TRACE_CHZZK_SUBSCRIBE_OK"),
                QStringLiteral("sessionKey=%1").arg(sessionKey.left(12)));
            if (!m_announcedChatReady) {
                m_announcedChatReady = true;
                emit error(platformId(), QStringLiteral("INFO_CHZZK_CHAT_READY"),
                    QStringLiteral("CHZZK chat subscription is ready."));
            }
        }
        reply->deleteLater();
    });
}

void ChzzkAdapter::onSocketConnected()
{
    if (m_stopping) {
        return;
    }
    m_connected = true;
    m_pendingConnectResult = false;
    if (m_connectWatchdog) {
        m_connectWatchdog->stop();
    }
    if (!m_connectSignalEmitted) {
        m_connectSignalEmitted = true;
        emit connected(platformId());
    }
    emit error(platformId(), QStringLiteral("TRACE_CHZZK_SOCKET_CONNECTED"), QStringLiteral("socket connected"));
    if (m_sessionKey.isEmpty() && !m_announcedSessionPending) {
        m_announcedSessionPending = true;
        emit error(platformId(), QStringLiteral("INFO_CHZZK_SESSION_PENDING"),
            QStringLiteral("Connected but waiting for CHZZK sessionKey."));
    }
    if (!m_sessionKey.isEmpty()) {
        if (m_announcedSessionPending) {
            m_announcedSessionPending = false;
        }
        const int gen = m_socketGeneration;
        QTimer::singleShot(300, this, [this, gen]() {
            if (gen != m_socketGeneration || m_stopping || m_sessionKey.isEmpty()) {
                return;
            }
            subscribeChatEvent(m_sessionKey);
        });
    }
}

void ChzzkAdapter::onSocketDisconnected()
{
    m_seenMessageIds.clear();
    if (m_heartbeatTimer) {
        m_heartbeatTimer->stop();
    }

    if (m_stopping) {
        return;
    }

    const bool wasConnected = m_connected || m_connectSignalEmitted;
    m_connected = false;
    resetProgressAnnouncements();
    if (m_pendingConnectResult) {
        handleConnectFailure(QStringLiteral("SOCKET_DISCONNECTED"), QStringLiteral("Socket disconnected before connect completed"));
        return;
    }
    emit error(platformId(), QStringLiteral("TRACE_CHZZK_SOCKET_DISCONNECTED"), QStringLiteral("socket disconnected"));
    if (wasConnected) {
        m_connectSignalEmitted = false;
        m_chatSubscribed = false;
        m_subscribeInFlight = false;
        m_subscribeRetryCount = 0;
        m_sessionKey.clear();
        emit error(platformId(), QStringLiteral("INFO_CHZZK_RECONNECTING"),
            QStringLiteral("Socket disconnected, attempting reconnection in 3 seconds."));
        QTimer::singleShot(BotManager::Timings::kChzzkReconnectDelayMs, this, [this]() {
            if (m_stopping) {
                return;
            }
            emit error(platformId(), QStringLiteral("INFO_CHZZK_RECONNECT_START"),
                QStringLiteral("Reconnecting to CHZZK session."));
            requestSessionAuth();
        });
    }
}

void ChzzkAdapter::onSocketTextMessageReceived(const QString& payload)
{
    emit error(platformId(), QStringLiteral("TRACE_CHZZK_SOCKET_TEXT"), payload.left(160));
    const QString trimmed = payload.trimmed();
    if (trimmed.startsWith(QLatin1Char('{'))) {
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8());
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            const QString type = obj.value(QStringLiteral("type")).toString().trimmed().toUpper();
            const QString eventType = obj.value(QStringLiteral("eventType")).toString().trimmed().toUpper();
            if (type == QStringLiteral("CHAT") || eventType == QStringLiteral("CHAT")) {
                const QJsonObject data = obj.value(QStringLiteral("data")).toObject();
                processSocketIoEvent(QStringLiteral("CHAT"), data.isEmpty() ? QJsonValue(obj) : QJsonValue(data));
                return;
            }
            if (type == QStringLiteral("CONNECTED")
                || type == QStringLiteral("SUBSCRIBED")
                || type == QStringLiteral("UNSUBSCRIBED")
                || type == QStringLiteral("REVOKED")
                || obj.contains(QStringLiteral("sessionKey"))
                || obj.value(QStringLiteral("data")).toObject().contains(QStringLiteral("sessionKey"))) {
                processSocketIoEvent(QStringLiteral("SYSTEM"), QJsonValue(obj));
                return;
            }
        }
    }
    processSocketIoPacket(payload);
}

void ChzzkAdapter::processSocketIoPacket(const QString& packet)
{
    if (packet.isEmpty() || m_stopping) {
        return;
    }

    // Engine.IO open packet — parse pingInterval and start heartbeat
    if (packet.startsWith(QLatin1Char('0'))) {
        const QJsonDocument openDoc = QJsonDocument::fromJson(packet.mid(1).toUtf8());
        if (openDoc.isObject()) {
            const int pingInterval = openDoc.object().value(QStringLiteral("pingInterval")).toInt(25000);
            if (m_heartbeatTimer) {
                m_heartbeatTimer->start(qMax(pingInterval, 5000));
            }
        }
        return;
    }

    // Engine.IO ping -> pong
    if (packet.startsWith(QLatin1Char('2'))) {
        const QString suffix = packet.mid(1);
        m_socket->sendTextMessage(QStringLiteral("3") + suffix);
        return;
    }

    // Socket.IO connect ack
    if (packet == QStringLiteral("40")) {
        return;
    }

    // Socket.IO event packet:
    // - 42["EVENT", {...}]
    // - 42/namespace,["EVENT", {...}]
    if (packet.startsWith(QStringLiteral("42"))) {
        int jsonPos = packet.indexOf(QLatin1Char('['));
        if (jsonPos < 0) {
            return;
        }
        const QString payload = packet.mid(jsonPos);
        const QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8());
        if (!doc.isArray()) {
            return;
        }
        const QJsonArray arr = doc.array();
        if (arr.size() < 2 || !arr.at(0).isString()) {
            return;
        }
        processSocketIoEvent(arr.at(0).toString(), arr.at(1));
    }
}

void ChzzkAdapter::processSocketIoEvent(const QString& eventName, const QJsonValue& payloadValue)
{
    const QString upperEvent = eventName.trimmed().toUpper();
    if (upperEvent == QStringLiteral("SYSTEM")) {
        const QJsonObject payload = jsonObjectFromValue(payloadValue);
        if (payload.isEmpty()) {
            emit error(platformId(), QStringLiteral("TRACE_CHZZK_SYSTEM_PARSE_EMPTY"), QStringLiteral("SYSTEM payload parse failed"));
            return;
        }
        const QJsonObject data = payload.value(QStringLiteral("data")).toObject();
        const QString eventType = readStringByKeys(payload, data, { QStringLiteral("eventType"), QStringLiteral("type"), QStringLiteral("systemType") }).toUpper();
        const QString sessionKey = readStringByKeys(payload, data, { QStringLiteral("sessionKey"), QStringLiteral("session_key") });
        if (!sessionKey.isEmpty() && sessionKey != m_sessionKey) {
            m_sessionKey = sessionKey;
            m_chatSubscribed = false;
            m_subscribeInFlight = false;
            m_subscribeInFlightSessionKey.clear();
            m_subscribeRetryCount = 0;
            m_subscribeSessionKey.clear();
            m_announcedSessionPending = false;
            m_announcedSubscribePending = false;
            m_announcedChatReady = false;
            emit error(platformId(), QStringLiteral("TRACE_CHZZK_SESSION_KEY"), QStringLiteral("sessionKey updated"));
            const int gen = m_socketGeneration;
            QTimer::singleShot(300, this, [this, gen]() {
                if (gen != m_socketGeneration || m_stopping || m_sessionKey.isEmpty()) {
                    return;
                }
                subscribeChatEvent(m_sessionKey);
            });
        }
        if (eventType.contains(QStringLiteral("CONNECTED")) && !m_sessionKey.isEmpty()) {
            m_chatSubscribed = false;
            m_subscribeInFlight = false;
            m_subscribeInFlightSessionKey.clear();
            m_subscribeRetryCount = 0;
            m_subscribeSessionKey.clear();
            m_announcedSessionPending = false;
            m_announcedSubscribePending = false;
            m_announcedChatReady = false;
            const int gen = m_socketGeneration;
            QTimer::singleShot(300, this, [this, gen]() {
                if (gen != m_socketGeneration || m_stopping || m_sessionKey.isEmpty()) {
                    return;
                }
                subscribeChatEvent(m_sessionKey);
            });
        }
        if (eventType.contains(QStringLiteral("SUBSCRIBED"))) {
            m_chatSubscribed = true;
            m_subscribeInFlight = false;
            m_subscribeInFlightSessionKey.clear();
            m_announcedSubscribePending = false;
            if (!m_announcedChatReady) {
                m_announcedChatReady = true;
                emit error(platformId(), QStringLiteral("INFO_CHZZK_CHAT_READY"),
                    QStringLiteral("CHZZK chat subscription is ready."));
            }
        }
        return;
    }

    const bool isChatEventName = upperEvent.contains(QStringLiteral("CHAT"));
    const QJsonObject payloadRoot = jsonObjectFromValue(payloadValue);
    const QJsonObject payloadData = payloadRoot.value(QStringLiteral("data")).toObject();
    const QString payloadType = readStringByKeys(payloadRoot, payloadData, { QStringLiteral("type"), QStringLiteral("eventType") }).toUpper();
    const bool isChatByType = payloadType.contains(QStringLiteral("CHAT"));
    if (!isChatEventName && !isChatByType && !looksLikeChatPayload(payloadRoot) && !looksLikeChatPayload(payloadData)) {
        return;
    }

    auto emitFromPayloadObject = [this](const QJsonObject& input) {
        QJsonObject payload = input;
        if (payload.contains(QStringLiteral("data")) && payload.value(QStringLiteral("data")).isObject()) {
            payload = payload.value(QStringLiteral("data")).toObject();
        }

        QJsonObject profile = payload.value(QStringLiteral("profile")).toObject();
        if (profile.isEmpty() && payload.value(QStringLiteral("profile")).isString()) {
            profile = parseJsonObjectString(payload.value(QStringLiteral("profile")).toString());
        }

        UnifiedChatMessage msg;
        msg.platform = platformId();
        msg.channelId = readStringByKeys(payload, QJsonObject(), { QStringLiteral("channelId"), QStringLiteral("channel_id"), QStringLiteral("streamerChannelId") });
        if (msg.channelId.isEmpty()) {
            msg.channelId = m_channelId;
        }
        msg.channelName = m_channelName.isEmpty() ? QStringLiteral("CHZZK") : m_channelName;
        msg.messageId = readStringByKeys(payload, QJsonObject(), { QStringLiteral("messageId"), QStringLiteral("chatId") });
        msg.authorId = readStringByKeys(payload, profile, { QStringLiteral("senderChannelId"), QStringLiteral("userId"), QStringLiteral("uid"), QStringLiteral("memberNo") });
        msg.authorName = readStringByKeys(profile, payload, { QStringLiteral("nickname"), QStringLiteral("name"), QStringLiteral("senderNickname"), QStringLiteral("profileName") });

        msg.text = readStringByKeys(payload, QJsonObject(), { QStringLiteral("content"), QStringLiteral("message"), QStringLiteral("text") });
        if (msg.text.trimmed().isEmpty() && payload.value(QStringLiteral("message")).isObject()) {
            const QJsonObject messageObj = payload.value(QStringLiteral("message")).toObject();
            msg.text = readStringByKeys(messageObj, payload, { QStringLiteral("content"), QStringLiteral("message"), QStringLiteral("text") });
        }
        if (msg.text.trimmed().isEmpty() && payload.value(QStringLiteral("content")).isObject()) {
            const QJsonObject contentObj = payload.value(QStringLiteral("content")).toObject();
            msg.text = readStringByKeys(contentObj, payload, { QStringLiteral("content"), QStringLiteral("message"), QStringLiteral("text") });
        }

        const QString messageTime = readStringByKeys(payload, QJsonObject(),
            { QStringLiteral("messageTime"), QStringLiteral("timestamp"), QStringLiteral("time"), QStringLiteral("createdTime") });
        msg.timestamp = parseEventTime(messageTime);
        if (!msg.timestamp.isValid()) {
            msg.timestamp = QDateTime::currentDateTime();
        }

        if (msg.text.trimmed().isEmpty()) {
            return;
        }

        // Parse {:emojiId:} patterns and build richText + emojis
        {
            static const QRegularExpression reEmoji(QStringLiteral("\\{:([A-Za-z0-9_-]+):\\}"));
            const QString& text = msg.text;
            QString richText;
            QVector<ChatEmojiInfo> emojiList;
            int offset = 0;
            auto it = reEmoji.globalMatch(text);
            while (it.hasNext()) {
                const auto match = it.next();
                richText += text.mid(offset, match.capturedStart() - offset).toHtmlEscaped();
                offset = match.capturedEnd();

                const QString emojiId = match.captured(1);
                const QString imageUrl = m_emojiResolver->imageUrlForId(emojiId);

                if (!imageUrl.isEmpty()) {
                    richText += QStringLiteral("<img src='emoji://%1' width='24' height='24' alt='{:%1:}'/>")
                                    .arg(emojiId);
                    ChatEmojiInfo info;
                    info.emojiId = emojiId;
                    info.imageUrl = imageUrl;
                    info.fallbackText = QStringLiteral("{:%1:}").arg(emojiId);
                    emojiList.append(info);
                } else {
                    richText += match.captured(0).toHtmlEscaped();
                }
            }
            richText += text.mid(offset).toHtmlEscaped();

            if (!emojiList.isEmpty()) {
                msg.richText = richText;
                msg.emojis = emojiList;
            }
        }

        const QString msgId = msg.messageId.trimmed();
        if (!msgId.isEmpty()) {
            if (m_seenMessageIds.contains(msgId)) {
                return;
            }
            m_seenMessageIds.insert(msgId);
            if (m_seenMessageIds.size() > BotManager::Limits::kChzzkSeenMessageIdsMax) {
                m_seenMessageIds.clear();
                m_seenMessageIds.insert(msgId);
            }
        }
        emit chatReceived(msg);
    };

    if (payloadValue.isArray()) {
        const QJsonArray arr = payloadValue.toArray();
        for (const QJsonValue& v : arr) {
            if (v.isObject()) {
                emitFromPayloadObject(v.toObject());
            }
        }
        return;
    }
    if (payloadValue.isObject()) {
        emitFromPayloadObject(payloadValue.toObject());
        return;
    }
    if (!payloadRoot.isEmpty()) {
        emitFromPayloadObject(payloadRoot);
    }
}

bool ChzzkAdapter::sendMessage(const QString& text)
{
    if (!m_connected || m_accessToken.trimmed().isEmpty()) {
        emit messageSent(platformId(), false, QStringLiteral("Not connected or token missing"));
        return false;
    }

    QNetworkRequest req(QUrl(Chzzk::OpenApi::chatsSend()));
    req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_accessToken).toUtf8());
    if (!m_clientId.isEmpty()) {
        req.setRawHeader("Client-Id", m_clientId.toUtf8());
    }
    if (!m_clientSecret.isEmpty()) {
        req.setRawHeader("Client-Secret", m_clientSecret.toUtf8());
    }
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject payload;
    payload.insert(QStringLiteral("message"), text);
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
        const int apiCode = obj.value(QStringLiteral("code")).toInt(200);
        if (reply->error() != QNetworkReply::NoError || !httpOk || apiCode != 200) {
            const QString apiMessage = obj.value(QStringLiteral("message")).toString().trimmed();
            const QString message = apiMessage.isEmpty() ? reply->errorString() : apiMessage;
            emit messageSent(platformId(), false, QStringLiteral("HTTP_%1 %2").arg(httpStatus).arg(message));
        } else {
            emit messageSent(platformId(), true, QString());
        }
        reply->deleteLater();
    });
    return true;
}

void ChzzkAdapter::handleConnectFailure(const QString& code, const QString& message)
{
    m_connected = false;
    m_connectSignalEmitted = false;
    m_pendingConnectResult = false;
    m_useClientSessionAuth = false;
    m_clientSessionFallbackTried = false;
    m_chatSubscribed = false;
    m_subscribeInFlight = false;
    m_subscribeInFlightSessionKey.clear();
    m_subscribeRetryCount = 0;
    m_subscribeSessionKey.clear();
    m_subscribeRecoverCount = 0;
    resetProgressAnnouncements();
    if (m_connectWatchdog) {
        m_connectWatchdog->stop();
    }
    if (m_socket) {
        m_socket->abort();
    }
    emit error(platformId(), code, message);
}
