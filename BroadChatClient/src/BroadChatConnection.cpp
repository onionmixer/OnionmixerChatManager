#include "BroadChatConnection.h"

#include "shared/BroadChatLogging.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>

namespace {
// §6.4.2 v15-3: 영숫자·하이픈·underscore·dot. 기본 prefix + 8자 UUID suffix.
QString makeDefaultClientId()
{
    const QString uuid =
        QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-');
    return QStringLiteral("broadchat-instance-") + uuid.left(8);
}

// §6.4.3 chat.data → UnifiedChatMessage. Mirrors BroadChatProtocol::buildChatData.
// Inline here for now; if the migration session adds a parse counterpart to the
// shared lib, switch to that and drop this helper.
UnifiedChatMessage parseChatData(const QJsonObject& d)
{
    UnifiedChatMessage m;
    const QString platform = d.value(QStringLiteral("platform")).toString();
    m.platform = (platform == QStringLiteral("chzzk"))
                     ? PlatformId::Chzzk
                     : PlatformId::YouTube;
    m.messageId = d.value(QStringLiteral("messageId")).toString();
    m.channelId = d.value(QStringLiteral("channelId")).toString();
    m.channelName = d.value(QStringLiteral("channelName")).toString();
    m.authorId = d.value(QStringLiteral("authorId")).toString();
    m.authorName = d.value(QStringLiteral("authorName")).toString();
    m.rawAuthorDisplayName =
        d.value(QStringLiteral("rawAuthorDisplayName")).toString();
    m.rawAuthorChannelId =
        d.value(QStringLiteral("rawAuthorChannelId")).toString();
    m.authorIsChatOwner =
        d.value(QStringLiteral("authorIsChatOwner")).toBool();
    m.authorIsChatModerator =
        d.value(QStringLiteral("authorIsChatModerator")).toBool();
    m.authorIsChatSponsor =
        d.value(QStringLiteral("authorIsChatSponsor")).toBool();
    m.authorIsVerified =
        d.value(QStringLiteral("authorIsVerified")).toBool();
    m.text = d.value(QStringLiteral("text")).toString();
    m.richText = d.value(QStringLiteral("richText")).toString();

    const QJsonArray emojis = d.value(QStringLiteral("emojis")).toArray();
    m.emojis.reserve(emojis.size());
    for (const auto& v : emojis) {
        const QJsonObject ej = v.toObject();
        ChatEmojiInfo info;
        info.emojiId = ej.value(QStringLiteral("emojiId")).toString();
        info.imageUrl = ej.value(QStringLiteral("imageUrl")).toString();
        info.fallbackText = ej.value(QStringLiteral("fallbackText")).toString();
        m.emojis.append(info);
    }

    // §6.4.3 v7-8: ISO 8601 UTC, ms 여부 관대 처리.
    const QString ts = d.value(QStringLiteral("timestamp")).toString();
    if (!ts.isEmpty()) {
        m.timestamp = QDateTime::fromString(ts, Qt::ISODateWithMs);
        if (!m.timestamp.isValid()) {
            m.timestamp = QDateTime::fromString(ts, Qt::ISODate);
        }
    }
    return m;
}
} // namespace

BroadChatConnection::BroadChatConnection(QObject* parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_clientId(makeDefaultClientId())
    , m_emojiTimer(new QTimer(this))
{
    // §6.1 v2-8: 2x kMaxLineBytes 버퍼로 여유 확보. 서버 측과 대칭.
    m_socket->setReadBufferSize(2 * BroadChatProtocol::kMaxLineBytes);

    // §16.3 Critical (v68 #6): 시스템 프록시 환경에서도 loopback/LAN 접속은
    // 프록시 경유 없이 직결 — BroadChat 은 내부 프로토콜이라 HTTP CONNECT 호환성 없음.
    m_socket->setProxy(QNetworkProxy::NoProxy);

    connect(m_socket, &QTcpSocket::connected,
            this, &BroadChatConnection::onConnected);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &BroadChatConnection::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &BroadChatConnection::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred,
            this, &BroadChatConnection::onSocketError);

    // v12-15: 1초 간격으로 pending 타임아웃 검사 (5s 초과 → "timeout").
    m_emojiTimer->setInterval(1000);
    connect(m_emojiTimer, &QTimer::timeout,
            this, &BroadChatConnection::onEmojiTimerTick);

    // v2-9 재연결 타이머 (one-shot)
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout,
            this, &BroadChatConnection::onReconnectTick);
}

BroadChatConnection::~BroadChatConnection() = default;

void BroadChatConnection::setClientId(const QString& id)
{
    if (!id.isEmpty()) m_clientId = id;
}

bool BroadChatConnection::start(const QString& host, quint16 port,
                                const QString& authToken)
{
    if (host.isEmpty() || port == 0) {
        qCCritical(lcBroadChatErr) << "invalid host or port";
        return false;
    }
    m_host = host;
    m_port = port;
    m_authToken = authToken.trimmed(); // v40 trim 정책
    m_stopping = false;
    // 새 endpoint로 시작 — 재시도 카운터·disable 리셋
    m_retryCount = 0;
    m_reconnectDisabled = false;
    m_lastByeReason.clear();
    cancelReconnect();
    resetState();

    qCInfo(lcBroadChat) << "connecting to" << m_host << ":" << m_port
                        << "clientId=" << m_clientId;
    m_socket->connectToHost(m_host, m_port);
    return true;
}

void BroadChatConnection::stop()
{
    m_stopping = true;
    cancelReconnect();
    if (!m_socket) return;

    if (m_socket->state() == QTcpSocket::ConnectedState && !m_byeSent) {
        m_byeSent = true;
        QJsonObject data;
        data.insert(QStringLiteral("reason"), QStringLiteral("normal"));
        sendEnvelope(QStringLiteral("bye"), data, QString(),
                     QDateTime::currentMSecsSinceEpoch());
        // §6.5 v12-6 best-effort: 짧은 flush window 제공.
        m_socket->waitForBytesWritten(500);
    }
    m_socket->disconnectFromHost();
}

bool BroadChatConnection::isConnected() const
{
    return m_socket && m_socket->state() == QTcpSocket::ConnectedState;
}

void BroadChatConnection::onConnected()
{
    qCInfo(lcBroadChat) << "socket connected; awaiting server_hello"
                        << "peer=" << m_socket->peerAddress().toString()
                        << ":" << m_socket->peerPort();
    // §16.2 Critical (v68 #5): Nagle 알고리즘 off — chat envelope 는 작은 JSON 라인이라
    // 지연 누적 없이 즉시 flush 필요. loopback 에서는 체감 차이 작지만 LAN/WAN 원격 운영 시
    // 채팅 표시 지연 방지.
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    // v2-9: 연결 성공 시 재시도 카운터 리셋
    m_retryCount = 0;
    emit connected();
}

void BroadChatConnection::onDisconnected()
{
    // v13-18 미완성 라인 drop.
    if (!m_lineBuffer.isEmpty()) {
        qCDebug(lcBroadChatTrace)
            << "incomplete line buffer dropped on disconnect:"
            << m_lineBuffer.size() << "bytes";
        m_lineBuffer.clear();
    }
    const bool wasHello = m_helloCompleted;
    m_helloCompleted = false;
    m_serverHelloSeen = false;
    m_byeSent = false;
    qCInfo(lcBroadChat) << "disconnected (wasHelloCompleted=" << wasHello << ")";
    emit disconnected();

    // v2-9 재연결 스케줄 — user stop(m_stopping) 아니고 reconnect disabled 아니면
    if (!m_stopping && !m_reconnectDisabled) {
        const int delayMs = scheduleReconnect(m_lastByeReason);
        qCInfo(lcBroadChat) << "reconnect scheduled in" << delayMs << "ms"
                            << "retry=" << m_retryCount
                            << "reason=" << m_lastByeReason;
    }
    // bye reason은 한 번 사용 후 클리어
    m_lastByeReason.clear();
}

void BroadChatConnection::onSocketError()
{
    if (!m_socket) return;
    // PeerClosedError는 정상 close 경로에서도 발생 — error 대신 info 수준.
    const auto err = m_socket->error();
    if (err == QTcpSocket::RemoteHostClosedError) {
        qCInfo(lcBroadChat) << "peer closed:" << m_socket->errorString();
        return;
    }
    qCWarning(lcBroadChatWarn) << "socket error:" << err
                               << m_socket->errorString();
}

void BroadChatConnection::onReadyRead()
{
    if (!m_socket) return;

    while (m_socket->bytesAvailable() > 0) {
        const QByteArray chunk = m_socket->readAll();
        m_lineBuffer.append(chunk);

        // §5.2 v34-17 라인 버퍼 overflow
        if (m_lineBuffer.size() > BroadChatProtocol::kMaxLineBytes) {
            qCCritical(lcBroadChatErr)
                << "line buffer overflow:" << m_lineBuffer.size() << "bytes";
            raiseProtocolError(QStringLiteral("line buffer overflow"));
            return;
        }

        int nlPos;
        while ((nlPos = m_lineBuffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_lineBuffer.left(nlPos);
            m_lineBuffer.remove(0, nlPos + 1);
            if (!line.isEmpty()) {
                handleLine(line);
                if (!m_socket ||
                    m_socket->state() != QTcpSocket::ConnectedState) {
                    return;
                }
            }
        }
    }
}

void BroadChatConnection::handleLine(const QByteArray& line)
{
    const auto env = BroadChatProtocol::parseEnvelope(line);
    if (!env.valid) {
        qCCritical(lcBroadChatErr) << "envelope parse error:" << env.parseError;
        raiseProtocolError(env.parseError);
        return;
    }
    dispatch(env);
}

void BroadChatConnection::dispatch(const BroadChatProtocol::Envelope& env)
{
    if (env.type == QStringLiteral("server_hello")) {
        if (m_serverHelloSeen) {
            raiseProtocolError(QStringLiteral("duplicate server_hello"));
            return;
        }
        m_serverHelloSeen = true;

        const QString serverVersion =
            env.data.value(QStringLiteral("serverVersion")).toString();
        const int protoMin =
            env.data.value(QStringLiteral("protocolMin")).toInt(0);
        const int protoMax =
            env.data.value(QStringLiteral("protocolMax")).toInt(0);

        // §6.4.1: protocolMin <= kProtocolVersion <= protocolMax 검증.
        if (BroadChatProtocol::kProtocolVersion < protoMin ||
            BroadChatProtocol::kProtocolVersion > protoMax) {
            qCCritical(lcBroadChatErr)
                << "protocol version mismatch: server accepts ["
                << protoMin << "," << protoMax << "] client="
                << BroadChatProtocol::kProtocolVersion;
            QJsonObject data;
            data.insert(QStringLiteral("reason"),
                        QStringLiteral("version_mismatch"));
            data.insert(QStringLiteral("detail"),
                        QStringLiteral("client proto=%1 server range=[%2,%3]")
                            .arg(BroadChatProtocol::kProtocolVersion)
                            .arg(protoMin)
                            .arg(protoMax));
            sendEnvelope(QStringLiteral("bye"), data);
            m_byeSent = true;
            m_lastByeReason = QStringLiteral("version_mismatch");
            m_socket->disconnectFromHost();
            emit byeReceived(QStringLiteral("version_mismatch"),
                             data.value(QStringLiteral("detail")).toString());
            return;
        }

        sendClientHello();
        m_helloCompleted = true;
        qCInfo(lcBroadChat) << "hello completed; serverVersion=" << serverVersion
                            << "proto=[" << protoMin << "," << protoMax << "]";
        emit helloCompleted(serverVersion, protoMin, protoMax);
        return;
    }

    if (env.type == QStringLiteral("bye")) {
        const QString reason = env.data.value(QStringLiteral("reason")).toString();
        const QString detail = env.data.value(QStringLiteral("detail")).toString();
        qCInfo(lcBroadChat) << "bye received: reason=" << reason
                            << "detail=" << detail;
        m_lastByeReason = reason; // v2-9 onDisconnected 분기용
        emit byeReceived(reason, detail);
        m_socket->disconnectFromHost();
        return;
    }

    // hello 교환 완료 전 다른 메시지는 프로토콜 오류.
    if (!m_helloCompleted) {
        raiseProtocolError(
            QStringLiteral("message before hello: ") + env.type);
        return;
    }

    if (env.type == QStringLiteral("ping")) {
        // §6.4.10: 서버 ping → 즉시 pong 응답. 응답 없으면 서버 측 3회 miss 후
        // timeout으로 클라를 kick. envelope id(서버 송신 시각)를 그대로 반사해 RTT 추적 가능.
        QJsonObject pongData;
        sendEnvelope(QStringLiteral("pong"), pongData, env.id,
                     QDateTime::currentMSecsSinceEpoch());
        return;
    }

    if (env.type == QStringLiteral("emoji_image")) {
        handleEmojiImage(env);
        return;
    }

    if (env.type == QStringLiteral("viewers")) {
        const int yt = env.data.value(QStringLiteral("youtube")).toInt(-1);
        const int cz = env.data.value(QStringLiteral("chzzk")).toInt(-1);
        // §v7: total은 서버 계산이지만 누락 시 fallback = max(0,y)+max(0,c).
        int total = -1;
        const QJsonValue tv = env.data.value(QStringLiteral("total"));
        if (tv.isDouble()) {
            total = tv.toInt();
        } else {
            total = qMax(0, yt) + qMax(0, cz);
        }
        emit viewersReceived(yt, cz, total);
        return;
    }

    if (env.type == QStringLiteral("platform_status")) {
        emit platformStatusReceived(env.data);
        return;
    }

    if (env.type == QStringLiteral("chat")) {
        const UnifiedChatMessage msg = parseChatData(env.data);
        // v82: per-message trace 제거 — 방송창 UI 에서 직접 확인 가능, 콘솔 노이즈 원인.
        emit chatReceived(msg);
        return;
    }

    // v68 #2: history_chunk 파싱 (서버 BroadChatServer::handleRequestHistory 응답).
    // data = { messages: [chat.data...], hasMore: bool, total: int }.
    if (env.type == QStringLiteral("history_chunk")) {
        const QJsonArray arr = env.data.value(QStringLiteral("messages")).toArray();
        const bool hasMore = env.data.value(QStringLiteral("hasMore")).toBool();
        QVector<UnifiedChatMessage> msgs;
        msgs.reserve(arr.size());
        for (const auto& v : arr) {
            msgs.append(parseChatData(v.toObject()));
        }
        qCInfo(lcBroadChat) << "history_chunk received: count=" << msgs.size()
                            << "hasMore=" << hasMore << "requestId=" << env.id;
        emit historyChunkReceived(env.id, msgs, hasMore);
        return;
    }

    qCDebug(lcBroadChatTrace) << "post-hello message:" << env.type
                              << "id=" << env.id;
}

void BroadChatConnection::sendClientHello()
{
    QJsonObject data;
    data.insert(QStringLiteral("clientId"), m_clientId);
    data.insert(QStringLiteral("protocolVersion"),
                BroadChatProtocol::kProtocolVersion);
    // v10-17: 실제 처리 가능한 capability만 선언 (현 단계는 chat 렌더 기반만).
    data.insert(QStringLiteral("capabilities"), QJsonArray());
    // v37-4·v21-γ-6: authToken이 설정되어 있으면 client_hello에 포함.
    if (!m_authToken.isEmpty()) {
        data.insert(QStringLiteral("authToken"), m_authToken);
    }
    sendEnvelope(QStringLiteral("client_hello"), data, QString(),
                 QDateTime::currentMSecsSinceEpoch());
}

bool BroadChatConnection::sendEnvelope(const QString& type,
                                       const QJsonObject& data,
                                       const QString& id, qint64 timestampMs)
{
    if (!m_socket ||
        m_socket->state() != QTcpSocket::ConnectedState) return false;

    const QByteArray payload =
        BroadChatProtocol::encodeEnvelope(type, data, id, timestampMs);
    if (payload.isEmpty()) {
        qCWarning(lcBroadChatWarn) << "encodeEnvelope failed for" << type;
        return false;
    }

    const qint64 written = m_socket->write(payload);
    if (written < 0 || written < payload.size()) {
        qCWarning(lcBroadChatWarn)
            << "partial write for" << type << "written=" << written
            << "expected=" << payload.size();
        return false;
    }
    return true;
}

void BroadChatConnection::raiseProtocolError(const QString& detail)
{
    if (!m_socket) return;
    emit protocolError(detail);

    if (m_socket->state() == QTcpSocket::ConnectedState && !m_byeSent) {
        m_byeSent = true;
        QJsonObject data;
        data.insert(QStringLiteral("reason"), QStringLiteral("protocol_error"));
        if (!detail.isEmpty()) {
            data.insert(QStringLiteral("detail"), detail.left(200));
        }
        sendEnvelope(QStringLiteral("bye"), data, QString(),
                     QDateTime::currentMSecsSinceEpoch());
        m_socket->waitForBytesWritten(200);
    }
    m_lineBuffer.clear();
    m_socket->disconnectFromHost();
}

void BroadChatConnection::resetState()
{
    m_lineBuffer.clear();
    m_serverHelloSeen = false;
    m_helloCompleted = false;
    m_byeSent = false;

    // Pending 이모지 요청은 세션 간 의미 없음 — 실패 통지 후 클리어.
    const auto ids = m_emojiPending.keys();
    for (const QString& reqId : ids) {
        failEmojiPending(reqId, QStringLiteral("disconnected"));
    }
    m_emojiPending.clear();
    m_emojiPendingByEmoji.clear();
    m_emojiTimer->stop();
}

QString BroadChatConnection::requestHistory(int maxCount,
                                            const QString& beforeMessageId)
{
    if (!m_helloCompleted) return {};

    if (maxCount <= 0) maxCount = 50;
    if (maxCount > 500) maxCount = 500;

    const QString reqId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject data;
    data.insert(QStringLiteral("maxCount"), maxCount);
    if (!beforeMessageId.isEmpty()) {
        data.insert(QStringLiteral("beforeMessageId"), beforeMessageId);
    }
    if (!sendEnvelope(QStringLiteral("request_history"), data, reqId,
                      QDateTime::currentMSecsSinceEpoch())) {
        return {};
    }
    qCInfo(lcBroadChat) << "request_history sent: maxCount=" << maxCount
                        << "requestId=" << reqId;
    return reqId;
}

QString BroadChatConnection::requestEmoji(const QString& emojiId)
{
    if (emojiId.isEmpty() || !m_helloCompleted) return {};

    // Dedup: 이미 pending이면 기존 요청 id 반환.
    const auto existing = m_emojiPendingByEmoji.constFind(emojiId);
    if (existing != m_emojiPendingByEmoji.constEnd()) {
        return existing.value();
    }

    const QString reqId =
        QUuid::createUuid().toString(QUuid::WithoutBraces);

    QJsonObject data;
    data.insert(QStringLiteral("emojiId"), emojiId);
    if (!sendEnvelope(QStringLiteral("request_emoji"), data, reqId,
                      QDateTime::currentMSecsSinceEpoch())) {
        return {};
    }

    EmojiPending p;
    p.emojiId = emojiId;
    p.deadline = QDateTime::currentDateTime().addMSecs(kEmojiTimeoutMs);
    m_emojiPending.insert(reqId, p);
    m_emojiPendingByEmoji.insert(emojiId, reqId);

    if (!m_emojiTimer->isActive()) m_emojiTimer->start();
    return reqId;
}

void BroadChatConnection::handleEmojiImage(const BroadChatProtocol::Envelope& env)
{
    const QString emojiId = env.data.value(QStringLiteral("emojiId")).toString();
    const QString mime = env.data.value(QStringLiteral("mime")).toString();
    const QString error = env.data.value(QStringLiteral("error")).toString();
    const QString b64 =
        env.data.value(QStringLiteral("bytesBase64")).toString();

    const QByteArray bytes =
        b64.isEmpty() ? QByteArray() : QByteArray::fromBase64(b64.toLatin1());

    // Request id가 일치하는 pending 제거. id 미매칭은 drop + TRACE.
    const auto pit = m_emojiPending.constFind(env.id);
    if (pit != m_emojiPending.constEnd()) {
        m_emojiPendingByEmoji.remove(pit.value().emojiId);
        m_emojiPending.remove(env.id);
        if (m_emojiPending.isEmpty()) m_emojiTimer->stop();
    } else if (!env.id.isEmpty()) {
        qCDebug(lcBroadChatTrace)
            << "emoji_image with unknown requestId:" << env.id;
    }

    emit emojiImageReceived(emojiId, bytes, mime, error);
}

void BroadChatConnection::failEmojiPending(const QString& requestId,
                                           const QString& error)
{
    const auto pit = m_emojiPending.constFind(requestId);
    if (pit == m_emojiPending.constEnd()) return;
    const QString emojiId = pit.value().emojiId;
    emit emojiImageReceived(emojiId, QByteArray(), QString(), error);
}

void BroadChatConnection::onEmojiTimerTick()
{
    const QDateTime now = QDateTime::currentDateTime();
    QStringList expired;
    for (auto it = m_emojiPending.constBegin();
         it != m_emojiPending.constEnd(); ++it) {
        if (it.value().deadline <= now) {
            expired.append(it.key());
        }
    }
    for (const QString& reqId : expired) {
        qCWarning(lcBroadChatWarn)
            << "emoji request timed out:" << m_emojiPending.value(reqId).emojiId;
        const QString emojiId = m_emojiPending.value(reqId).emojiId;
        m_emojiPending.remove(reqId);
        m_emojiPendingByEmoji.remove(emojiId);
        emit emojiImageReceived(emojiId, QByteArray(), QString(),
                                QStringLiteral("timeout"));
    }
    if (m_emojiPending.isEmpty()) m_emojiTimer->stop();
}

int BroadChatConnection::scheduleReconnect(const QString& reason)
{
    // v2-9·v16-13 bye reason 별 백오프
    int delayMs = 0;
    if (reason == QStringLiteral("version_mismatch") ||
        reason == QStringLiteral("auth_failed")) {
        m_reconnectDisabled = true;
        return -1; // 중단
    }
    if (reason == QStringLiteral("protocol_error")) {
        delayMs = 30000; // 30s 고정
    } else if (reason == QStringLiteral("duplicate_client_id")) {
        delayMs = 60000; // 60s 고정
    } else if (reason == QStringLiteral("too_many_clients")) {
        delayMs = 30000; // 30s 고정
    } else {
        // 지수 1→2→4→8→16→30 cap (shutdown·disconnect·timeout·기타)
        const int caps[] = {1000, 2000, 4000, 8000, 16000};
        if (m_retryCount < static_cast<int>(sizeof(caps) / sizeof(int))) {
            delayMs = caps[m_retryCount];
        } else {
            delayMs = 30000;
        }
    }
    ++m_retryCount;
    m_reconnectTimer->start(delayMs);
    return delayMs;
}

void BroadChatConnection::cancelReconnect()
{
    if (m_reconnectTimer && m_reconnectTimer->isActive()) {
        m_reconnectTimer->stop();
    }
}

void BroadChatConnection::onReconnectTick()
{
    if (m_stopping || m_reconnectDisabled) return;
    if (m_host.isEmpty() || m_port == 0) return;
    qCInfo(lcBroadChat) << "reconnect attempt" << m_retryCount
                        << "→" << m_host << ":" << m_port;
    resetState();
    m_socket->abort(); // 이전 소켓 상태 확실히 정리
    m_socket->connectToHost(m_host, m_port);
}
