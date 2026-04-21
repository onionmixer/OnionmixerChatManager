#include "core/BroadChatServer.h"

#include "core/ClientSession.h"
#include "core/EmojiImageCache.h"
#include "shared/BroadChatLogging.h"
#include "shared/BroadChatProtocol.h"
#include "ui/ChatMessageModel.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>

BroadChatServer::BroadChatServer(const ChatMessageModel* model,
                                 EmojiImageCache* emojiCache,
                                 QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_emojiCache(emojiCache)
{
    if (m_emojiCache) {
        connect(m_emojiCache, &EmojiImageCache::imageReady,
                this, &BroadChatServer::onEmojiImageReady);
    }
    m_pingTimer = new QTimer(this);
    m_pingTimer->setInterval(kPingIntervalMs);
    connect(m_pingTimer, &QTimer::timeout, this, &BroadChatServer::onPingTick);
}

BroadChatServer::~BroadChatServer()
{
    stop();
}

bool BroadChatServer::start(const QHostAddress& bindAddress, quint16 port,
                            const QString& authToken)
{
    // v13-16·v24 D3: hook 설치 누락 감지.
    // Debug 빌드: Q_ASSERT_X로 즉시 중단 — 운영 누락 조기 발견.
    // Release 빌드: one-time WARN 로그 (서비스 continuity 우선).
    // 테스트 환경은 setUp에서 markLifecycleHookInstalled() 호출로 통과.
    Q_ASSERT_X(m_lifecycleHookInstalled, "BroadChatServer::start",
               "lifecycle hook not installed. MainWindow must connect "
               "ConnectionCoordinator::stateChanged and call "
               "markLifecycleHookInstalled() before start().");
    if (!m_lifecycleHookInstalled && !m_startWarnedOnce) {
        m_startWarnedOnce = true;
        qCWarning(lcBroadChatWarn)
            << "lifecycle hook not installed — start() called directly. "
               "MainWindow should connect ConnectionCoordinator::stateChanged "
               "and call markLifecycleHookInstalled().";
    }

    if (m_server && m_server->isListening()) {
        return true;
    }
    if (!m_server) {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection,
                this, &BroadChatServer::onNewConnection);
    }

    // v22-10 auth_token 보관 (stop+start 경유만 교체)
    m_authToken = authToken.trimmed(); // v21-γ-6 trim 정책

    // v19-8 QTcpServer는 SO_REUSEADDR를 기본 설정 (Qt 5.15+).
    // TIME_WAIT 완화 자동.
    if (!m_server->listen(bindAddress, port)) {
        const QString err = m_server->errorString();
        qCWarning(lcBroadChatWarn) << "listen(" << bindAddress << ":" << port
                                   << ") failed:" << err;
        emit listenFailed(err);
        return false;
    }

    // v22-3 lifecycle 로그에 auth 상태 포함
    qCInfo(lcBroadChat) << "server listening on" << bindAddress << ":" << port
                        << "(auth=" << (isAuthRequired() ? "enabled" : "disabled") << ")";
    emit listeningChanged(true, m_server->serverPort());
    return true;
}

void BroadChatServer::stop()
{
    if (!m_server || !m_server->isListening()) return;

    // §19.4 v15-16 순서:
    // 1. ping 타이머 선제 stop (v11-6)
    if (m_pingTimer && m_pingTimer->isActive()) {
        m_pingTimer->stop();
    }

    // 2. 각 세션에 bye 송신
    const auto snapshot = m_sessions;
    for (ClientSession* session : snapshot) {
        if (session) {
            session->sendByeAndClose(QStringLiteral("shutdown"));
        }
    }

    // 3. 서버 close → accept 중단. 기존 세션은 disconnected로 자연 정리.
    m_server->close();
    m_sessions.clear();
    emit clientCountChanged(0);
    qCInfo(lcBroadChat) << "server stopped (graceful)";
    emit listeningChanged(false, 0);
}

int BroadChatServer::activeClientCount() const
{
    return m_sessions.size();
}

bool BroadChatServer::isListening() const
{
    return m_server && m_server->isListening();
}

bool BroadChatServer::isAuthTokenValid(const QString& candidate) const
{
    // v21-γ-6 trim 정책: 양쪽 trim 후 비교
    // v21-13·v22-15: 평문 비교로 충분 (TLS 없는 환경)
    return m_authToken == candidate.trimmed();
}

void BroadChatServer::broadcastChat(const UnifiedChatMessage& message)
{
    if (!m_server || !m_server->isListening()) return;
    if (m_sessions.isEmpty()) return;

    // §11.2 v11-7: emoji URL pre-register
    if (m_emojiCache) {
        for (const ChatEmojiInfo& e : message.emojis) {
            m_emojiCache->registerUrl(e.emojiId, e.imageUrl);
        }
    }

    const QJsonObject data = BroadChatProtocol::buildChatData(message);
    const qint64 ts = QDateTime::currentMSecsSinceEpoch();

    const auto snapshot = m_sessions;
    for (ClientSession* session : snapshot) {
        if (!session || session->state() != ClientSession::State::Active) continue;
        session->sendEnvelope(QStringLiteral("chat"), data, QString(), ts);
        // v21-6 slow timeout 체크 — 10s 초과 시 close
        if (session->checkSlowTimeout()) {
            session->sendByeAndClose(QStringLiteral("timeout"),
                                    QStringLiteral("slow write buffer"));
        }
    }
}

void BroadChatServer::broadcastViewerCount(int youtube, int chzzk)
{
    // v79: 세션 존재 여부와 무관하게 값 캐싱 — 신규 세션의 sendInitialStateToSession 이 참조.
    m_lastYoutubeViewers = youtube;
    m_lastChzzkViewers = chzzk;
    m_hasCachedViewers = true;

    if (!m_server || !m_server->isListening()) return;
    if (m_sessions.isEmpty()) return;

    const QJsonObject data = BroadChatProtocol::buildViewersData(youtube, chzzk);
    const qint64 ts = QDateTime::currentMSecsSinceEpoch();
    const auto snapshot = m_sessions;
    for (ClientSession* session : snapshot) {
        if (!session || session->state() != ClientSession::State::Active) continue;
        session->sendEnvelope(QStringLiteral("viewers"), data, QString(), ts);
    }
}

void BroadChatServer::broadcastPlatformStatus(PlatformId platform, const QString& state,
                                              bool live, const QString& runtimePhase)
{
    const QJsonObject data =
        BroadChatProtocol::buildPlatformStatusData(platform, state, live, runtimePhase);
    // v79: 플랫폼별 최신 상태 캐싱 (세션 유무 무관) — 신규 세션 sendInitialStateToSession 에서 참조.
    m_lastPlatformStatus.insert(static_cast<int>(platform), data);

    if (!m_server || !m_server->isListening()) return;
    if (m_sessions.isEmpty()) return;

    const qint64 ts = QDateTime::currentMSecsSinceEpoch();
    const auto snapshot = m_sessions;
    for (ClientSession* session : snapshot) {
        if (!session || session->state() != ClientSession::State::Active) continue;
        session->sendEnvelope(QStringLiteral("platform_status"), data, QString(), ts);
    }
}

// v79: 세션이 Active 전환된 직후 호출 — 캐시된 viewer/platform_status 를 해당 세션에만 push.
// broadcastXxx 는 "값 변경 시에만" 호출되므로 값이 오래 유지되는 동안 접속한 신규 클라는
// 현재 상태를 절대 알 수 없는 이슈 해소. OnionmixerBroadChatClient 시청자 카운터 플레이스홀더
// "—" 영구 표시 버그의 근본 수정.
void BroadChatServer::sendInitialStateToSession(ClientSession* session)
{
    if (!session || session->state() != ClientSession::State::Active) return;
    const qint64 ts = QDateTime::currentMSecsSinceEpoch();

    // viewers — 캐시가 있을 때만 push (초기 -1/-1 은 의미 없음)
    if (m_hasCachedViewers) {
        const QJsonObject data = BroadChatProtocol::buildViewersData(
            m_lastYoutubeViewers, m_lastChzzkViewers);
        session->sendEnvelope(QStringLiteral("viewers"), data, QString(), ts);
    }

    // platform_status — 플랫폼별 캐시 전체 push (순서는 QHash 순회 — 의미상 무관)
    for (auto it = m_lastPlatformStatus.constBegin();
         it != m_lastPlatformStatus.constEnd(); ++it) {
        session->sendEnvelope(QStringLiteral("platform_status"), it.value(), QString(), ts);
    }
}

void BroadChatServer::onNewConnection()
{
    // §7.2 v13-20·v19-10 drain loop
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket* sock = m_server->nextPendingConnection();
        if (!sock) break;

        const QString peerInfo = QStringLiteral("%1:%2")
            .arg(sock->peerAddress().toString())
            .arg(sock->peerPort());

        if (m_sessions.size() >= kMaxClients) {
            // §7.2 v6-11·v16-7: hello 전 bye 송신 유효
            qCWarning(lcBroadChatWarn) << "rejecting connection from" << peerInfo
                                       << ": max clients reached";
            ClientSession* tmp = new ClientSession(sock, this);
            tmp->sendByeAndClose(QStringLiteral("too_many_clients"));
            connect(tmp, &ClientSession::closed, tmp, &QObject::deleteLater);
            continue;
        }

        auto* session = new ClientSession(sock, this);
        m_sessions.append(session);

        connect(session, &ClientSession::helloReceived,
                this, &BroadChatServer::onSessionHello);
        connect(session, &ClientSession::closed,
                this, &BroadChatServer::onSessionClosed);
        connect(session, &ClientSession::protocolError,
                this, &BroadChatServer::onSessionProtocolError);
        connect(session, &ClientSession::messageReceived,
                this, &BroadChatServer::onSessionMessage);

        qCInfo(lcBroadChat) << "client connection accepted from" << peerInfo
                            << "; total=" << m_sessions.size();
        emit clientCountChanged(m_sessions.size());

        // §7.2 v4: 첫 클라 연결 시 ping 타이머 start.
        if (m_pingTimer && !m_pingTimer->isActive()) {
            m_pingTimer->start();
        }

        // server_hello 송신 — 서버 주도 (§7.1 v16-8 · v22-1 authRequired 포함).
        session->sendServerHello(isAuthRequired());
    }
}

void BroadChatServer::onSessionHello(const QString& clientId, int protocolVersion)
{
    Q_UNUSED(protocolVersion)
    qCInfo(lcBroadChat) << "client hello completed clientId=" << clientId;
    // v79: Active 전환 직후 해당 세션에만 캐시된 viewer/platform_status push.
    auto* session = qobject_cast<ClientSession*>(sender());
    if (session) {
        sendInitialStateToSession(session);
    }
    emit clientConnected(clientId);
}

void BroadChatServer::onSessionClosed(const QString& reason)
{
    auto* session = qobject_cast<ClientSession*>(sender());
    if (!session) return;
    removeSession(session, reason);
}

void BroadChatServer::onSessionProtocolError(const QString& detail)
{
    auto* session = qobject_cast<ClientSession*>(sender());
    if (!session) return;
    emit protocolError(session->clientId(), detail);
}

void BroadChatServer::removeSession(ClientSession* session, const QString& reason)
{
    if (!session) return;
    const QString clientId = session->clientId();

    // §4.2 v13-2: pending emoji queue 정리
    auto it = m_emojiPending.begin();
    while (it != m_emojiPending.end()) {
        auto& vec = it.value();
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                 [session](const EmojiPending& p) { return p.session == session; }),
                  vec.end());
        if (vec.isEmpty()) {
            it = m_emojiPending.erase(it);
        } else {
            ++it;
        }
    }

    m_sessions.removeAll(session);
    emit clientCountChanged(m_sessions.size());
    emit clientDisconnected(clientId, reason);
    session->deleteLater();

    // §7.2 v4: 마지막 클라 해제 시 ping 타이머 stop.
    if (m_sessions.isEmpty() && m_pingTimer && m_pingTimer->isActive()) {
        m_pingTimer->stop();
    }
}

void BroadChatServer::onSessionMessage(const QString& type, const QJsonObject& data,
                                       const QString& id)
{
    auto* session = qobject_cast<ClientSession*>(sender());
    if (!session) return;

    if (type == QStringLiteral("request_history")) {
        handleRequestHistory(session, id, data);
        return;
    }
    if (type == QStringLiteral("request_emoji")) {
        handleRequestEmoji(session, id, data);
        return;
    }
    if (type == QStringLiteral("ping")) {
        QJsonObject pongData;
        session->sendEnvelope(QStringLiteral("pong"), pongData, id,
                              QDateTime::currentMSecsSinceEpoch());
        return;
    }
    if (type == QStringLiteral("pong")) {
        session->notifyPongReceived();
        return;
    }

    qCWarning(lcBroadChatWarn) << "unknown message type from client:" << type;
}

void BroadChatServer::handleRequestHistory(ClientSession* session, const QString& requestId,
                                           const QJsonObject& data)
{
    if (!m_model) {
        QJsonObject resp;
        resp.insert(QStringLiteral("messages"), QJsonArray{});
        resp.insert(QStringLiteral("hasMore"), false);
        resp.insert(QStringLiteral("total"), 0);
        session->sendEnvelope(QStringLiteral("history_chunk"), resp, requestId,
                              QDateTime::currentMSecsSinceEpoch());
        return;
    }

    int maxCount = data.value(QStringLiteral("maxCount")).toInt(kDefaultHistoryCount);
    if (maxCount <= 0) maxCount = kDefaultHistoryCount;
    if (maxCount > kMaxHistoryCount) {
        qCWarning(lcBroadChatWarn) << "request_history maxCount" << maxCount
                                   << "clamped to" << kMaxHistoryCount;
        maxCount = kMaxHistoryCount;
    }

    const int total = m_model->messageCount();
    const int startRow = qMax(0, total - maxCount);

    QJsonArray arr;
    for (int i = startRow; i < total; ++i) {
        const UnifiedChatMessage* msg = m_model->messageAt(i);
        if (!msg) continue;
        // v21-2: history 응답 시에도 emoji URL pre-register
        if (m_emojiCache) {
            for (const ChatEmojiInfo& e : msg->emojis) {
                m_emojiCache->registerUrl(e.emojiId, e.imageUrl);
            }
        }
        arr.append(BroadChatProtocol::buildChatData(*msg));
    }

    QJsonObject resp;
    resp.insert(QStringLiteral("messages"), arr);
    resp.insert(QStringLiteral("hasMore"), startRow > 0);
    resp.insert(QStringLiteral("total"), total);

    session->sendEnvelope(QStringLiteral("history_chunk"), resp, requestId,
                          QDateTime::currentMSecsSinceEpoch());
    qCDebug(lcBroadChatTrace) << "history_chunk sent count=" << (total - startRow)
                              << "total=" << total;
}

void BroadChatServer::handleRequestEmoji(ClientSession* session, const QString& requestId,
                                         const QJsonObject& data)
{
    const QString emojiId = data.value(QStringLiteral("emojiId")).toString();
    if (emojiId.isEmpty()) {
        sendEmojiResponse(session, requestId, QString(),
                          QStringLiteral("unknown_id"));
        return;
    }

    if (!m_emojiCache) {
        sendEmojiResponse(session, requestId, emojiId, QStringLiteral("network_error"));
        return;
    }

    if (m_emojiCache->contains(emojiId)) {
        sendEmojiResponse(session, requestId, emojiId);
        return;
    }

    const QString url = m_emojiCache->getUrl(emojiId);
    if (url.isEmpty()) {
        sendEmojiResponse(session, requestId, emojiId, QStringLiteral("unknown_id"));
        return;
    }

    const bool alreadyFetching = m_emojiPending.contains(emojiId) &&
                                 !m_emojiPending.value(emojiId).isEmpty();
    m_emojiPending[emojiId].append({session, requestId});
    if (!alreadyFetching) {
        m_emojiCache->ensureLoaded(emojiId, url);
    }
}

void BroadChatServer::sendEmojiResponse(ClientSession* session, const QString& requestId,
                                        const QString& emojiId, const QString& error)
{
    QJsonObject data;
    data.insert(QStringLiteral("emojiId"), emojiId);
    if (error.isEmpty() && m_emojiCache && m_emojiCache->contains(emojiId)) {
        const QByteArray raw = m_emojiCache->getRawBytes(emojiId);
        const QString mime = m_emojiCache->getMime(emojiId);
        data.insert(QStringLiteral("mime"), mime.isEmpty() ? QStringLiteral("image/png") : mime);
        data.insert(QStringLiteral("bytesBase64"), QString::fromLatin1(raw.toBase64()));
    } else {
        data.insert(QStringLiteral("mime"), QString());
        data.insert(QStringLiteral("bytesBase64"), QString());
        data.insert(QStringLiteral("error"),
                    error.isEmpty() ? QStringLiteral("http_timeout") : error);
    }
    session->sendEnvelope(QStringLiteral("emoji_image"), data, requestId,
                          QDateTime::currentMSecsSinceEpoch());
}

void BroadChatServer::onPingTick()
{
    const auto snapshot = m_sessions;
    for (ClientSession* session : snapshot) {
        if (!session) continue;
        if (session->sendPingAndCheckTimeout()) {
            qCWarning(lcBroadChatWarn) << "ping timeout clientId=" << session->clientId();
            session->sendByeAndClose(QStringLiteral("timeout"));
        }
    }
}

void BroadChatServer::onEmojiImageReady(const QString& emojiId)
{
    if (m_sessions.isEmpty()) return;
    if (!m_emojiPending.contains(emojiId)) return;

    const QVector<EmojiPending> pending = m_emojiPending.take(emojiId);
    for (const EmojiPending& p : pending) {
        if (!p.session || !m_sessions.contains(p.session)) continue;
        sendEmojiResponse(p.session, p.requestId, emojiId);
    }
}
