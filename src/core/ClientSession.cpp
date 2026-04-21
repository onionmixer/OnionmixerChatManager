#include "core/ClientSession.h"

#include "core/BroadChatServer.h"
#include "shared/BroadChatLogging.h"
#include "shared/BroadChatVersion.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTimer>

namespace {
constexpr int kMaxClientIdLength = 64;
constexpr int kHelloTimeoutMs = 5000; // v21-5

bool isValidClientIdChar(QChar c)
{
    const ushort u = c.unicode();
    if (u >= '0' && u <= '9') return true;
    if (u >= 'A' && u <= 'Z') return true;
    if (u >= 'a' && u <= 'z') return true;
    return u == '-' || u == '_' || u == '.';
}

bool isValidClientId(const QString& id)
{
    if (id.isEmpty() || id.size() > kMaxClientIdLength) return false;
    for (QChar c : id) {
        if (!isValidClientIdChar(c)) return false;
    }
    return true;
}
} // namespace

ClientSession::ClientSession(QTcpSocket* socket, QObject* parent)
    : QObject(parent)
    , m_socket(socket)
{
    m_socket->setParent(this);
    m_socket->setReadBufferSize(2 * BroadChatProtocol::kMaxLineBytes);

    connect(m_socket, &QTcpSocket::readyRead, this, &ClientSession::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &ClientSession::onDisconnected);
    connect(m_socket, &QTcpSocket::bytesWritten, this, &ClientSession::onBytesWritten);

    // v21-5 hello timeout 5초
    QTimer::singleShot(kHelloTimeoutMs, this, [this]() {
        if (m_state == State::Connected) {
            qCWarning(lcBroadChatWarn) << "hello timeout (5s) — closing session";
            sendByeAndClose(QStringLiteral("protocol_error"),
                            QStringLiteral("hello timeout"));
        }
    });
}

ClientSession::~ClientSession() = default;

void ClientSession::sendServerHello(bool authRequired)
{
    QJsonObject data;
    data.insert(QStringLiteral("serverVersion"),
                QString::fromLatin1(BroadChatVersion::kAppVersion));
    data.insert(QStringLiteral("protocolMin"), BroadChatProtocol::kProtocolVersion);
    data.insert(QStringLiteral("protocolMax"), BroadChatProtocol::kProtocolVersion);

    QJsonArray caps;
    caps.append(QStringLiteral("chat"));
    caps.append(QStringLiteral("viewers"));
    caps.append(QStringLiteral("platform_status"));
    caps.append(QStringLiteral("emoji_image"));
    caps.append(QStringLiteral("history"));
    data.insert(QStringLiteral("capabilities"), caps);

    // v22-1 authRequired 필드
    if (authRequired) {
        data.insert(QStringLiteral("authRequired"), true);
    }

    sendEnvelope(QStringLiteral("server_hello"), data, QString(),
                 QDateTime::currentMSecsSinceEpoch());
}

bool ClientSession::sendPingAndCheckTimeout()
{
    if (m_state != State::Active) return false;
    if (m_consecutiveMissedPings >= 3) return true;

    const qint64 ts = QDateTime::currentMSecsSinceEpoch();
    QJsonObject data;
    data.insert(QStringLiteral("t"), ts);
    sendEnvelope(QStringLiteral("ping"), data, QString(), ts);
    ++m_consecutiveMissedPings;
    return false;
}

void ClientSession::notifyPongReceived()
{
    m_consecutiveMissedPings = 0;
}

bool ClientSession::sendEnvelope(const QString& type, const QJsonObject& data,
                                 const QString& id, qint64 timestampMs)
{
    if (m_state == State::Closed || m_state == State::Error) return false;
    if (!m_socket || m_socket->state() != QTcpSocket::ConnectedState) return false;

    const QByteArray payload =
        BroadChatProtocol::encodeEnvelope(type, data, id, timestampMs);
    if (payload.isEmpty()) {
        qCWarning(lcBroadChatWarn) << "encodeEnvelope failed for type" << type;
        return false;
    }

    const qint64 written = m_socket->write(payload);
    if (written < 0 || written < payload.size()) {
        qCWarning(lcBroadChatWarn) << "partial write for type" << type
                                   << "written=" << written
                                   << "expected=" << payload.size();
        return false;
    }
    return true;
}

void ClientSession::sendByeAndClose(const QString& reason, const QString& detail)
{
    if (m_byeSent) return;
    m_byeSent = true;

    QJsonObject data;
    data.insert(QStringLiteral("reason"), reason);
    if (!detail.isEmpty()) {
        data.insert(QStringLiteral("detail"), detail.left(200));
    }
    sendEnvelope(QStringLiteral("bye"), data, QString(),
                 QDateTime::currentMSecsSinceEpoch());

    transitionTo(State::Closing);
    if (m_socket) {
        // v19-20 graceful: disconnectFromHost (FIN)
        m_socket->disconnectFromHost();
    }
}

void ClientSession::onReadyRead()
{
    if (!m_socket) return;

    while (m_socket->bytesAvailable() > 0) {
        const QByteArray chunk = m_socket->readAll();
        m_lineBuffer.append(chunk);

        if (m_lineBuffer.size() > BroadChatProtocol::kMaxLineBytes) {
            qCCritical(lcBroadChatErr) << "line buffer overflow:" << m_lineBuffer.size()
                                       << "bytes";
            forceProtocolError(QStringLiteral("line buffer overflow"));
            return;
        }

        int nlPos;
        while ((nlPos = m_lineBuffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_lineBuffer.left(nlPos);
            m_lineBuffer.remove(0, nlPos + 1);
            if (!line.isEmpty()) {
                handleLine(line);
                if (m_state == State::Closed || m_state == State::Error) {
                    return;
                }
            }
        }
    }
}

void ClientSession::onDisconnected()
{
    if (m_state == State::Closed) return;

    if (!m_lineBuffer.isEmpty()) {
        qCDebug(lcBroadChatTrace) << "incomplete line buffer dropped on disconnect:"
                                  << m_lineBuffer.size() << "bytes";
        m_lineBuffer.clear();
    }

    const State prev = m_state;
    transitionTo(State::Closed);
    const QString reason = (prev == State::Error) ? QStringLiteral("protocol_error")
                                                  : QStringLiteral("normal");
    emit closed(reason);
}

void ClientSession::handleLine(const QByteArray& line)
{
    const auto env = BroadChatProtocol::parseEnvelope(line);
    if (!env.valid) {
        qCCritical(lcBroadChatErr) << "envelope parse error:" << env.parseError;
        forceProtocolError(env.parseError);
        return;
    }
    dispatch(env);
}

void ClientSession::dispatch(const BroadChatProtocol::Envelope& env)
{
    if (env.type == QStringLiteral("client_hello")) {
        if (m_state != State::Connected) {
            forceProtocolError(QStringLiteral("duplicate client_hello"));
            return;
        }
        const QString clientId = env.data.value(QStringLiteral("clientId")).toString();
        if (!isValidClientId(clientId)) {
            forceProtocolError(QStringLiteral("invalid clientId"));
            return;
        }
        const int proto = env.data.value(QStringLiteral("protocolVersion")).toInt(0);
        if (proto != BroadChatProtocol::kProtocolVersion) {
            sendByeAndClose(QStringLiteral("version_mismatch"),
                            QStringLiteral("protocolVersion=%1 expected=%2")
                                .arg(proto).arg(BroadChatProtocol::kProtocolVersion));
            return;
        }

        // v19-4 authToken 검증 — proto·clientId 체크 후, duplicate 체크 전
        auto* server = qobject_cast<BroadChatServer*>(parent());
        if (server && server->isAuthRequired()) {
            const QString token = env.data.value(QStringLiteral("authToken")).toString();
            if (!server->isAuthTokenValid(token)) {
                // v21-γ-9 토큰 문자열 로그 금지
                qCCritical(lcBroadChatErr) << "auth_failed clientId=" << clientId
                                           << "(token mismatch)";
                sendByeAndClose(QStringLiteral("auth_failed"),
                                QStringLiteral("invalid or missing authToken"));
                return;
            }
        }

        m_clientId = clientId;
        transitionTo(State::HelloReceived);
        // v79: Active 전환 **이후** helloReceived emit — 슬롯 (onSessionHello) 이 세션에
        // sendEnvelope 을 호출해도 State::Active 체크를 통과하도록. 기존에는 signal 발동
        // 시점에 세션이 HelloReceived 상태라 sendEnvelope 이 early-return 되어 초기 상태
        // push 가 불가능했음.
        transitionTo(State::Active);
        emit helloReceived(m_clientId, proto);
        return;
    }

    if (env.type == QStringLiteral("bye")) {
        if (m_socket) m_socket->disconnectFromHost();
        return;
    }

    if (m_state != State::Active) {
        forceProtocolError(QStringLiteral("message before hello: %1").arg(env.type));
        return;
    }

    emit messageReceived(env.type, env.data, env.id);
}

void ClientSession::forceProtocolError(const QString& detail)
{
    if (m_state == State::Error || m_state == State::Closed) return;
    emit protocolError(detail);
    sendByeAndClose(QStringLiteral("protocol_error"), detail);
    transitionTo(State::Error);
}

void ClientSession::transitionTo(State next)
{
    m_state = next;
}

// v14-10·v21-6 slow 감지: sendEnvelope 직후 호출해 10초 timeout 판정.
bool ClientSession::checkSlowTimeout()
{
    if (!m_socket) return false;
    const qint64 pending = m_socket->bytesToWrite();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (pending > kMaxWriteBufferBytes) {
        if (m_slowEnteredAt == 0) {
            m_slowEnteredAt = now;
            qCWarning(lcBroadChatWarn)
                << "slow write buffer:" << pending << "bytes — start 10s watchdog";
        } else if (now - m_slowEnteredAt > kSlowTimeoutMs) {
            qCWarning(lcBroadChatWarn)
                << "slow write timeout (10s) — closing session";
            return true;
        }
    }
    return false;
}

// bytesWritten 시그널 slot: 버퍼가 복구 임계 아래로 내려오면 slow 해제.
void ClientSession::onBytesWritten(qint64 /*bytes*/)
{
    if (!m_socket || m_slowEnteredAt == 0) return;
    const qint64 pending = m_socket->bytesToWrite();
    if (pending <= kSlowRecoveryBytes) {
        qCDebug(lcBroadChatTrace)
            << "slow buffer recovered, pending=" << pending;
        m_slowEnteredAt = 0;
    }
}
