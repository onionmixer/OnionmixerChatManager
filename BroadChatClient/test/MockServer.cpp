#include "MockServer.h"

#include "shared/BroadChatProtocol.h"

#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>
#include <QTcpServer>
#include <QTcpSocket>

MockServer::MockServer(QObject* parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection,
            this, &MockServer::onNewConnection);
}

MockServer::~MockServer() = default;

bool MockServer::start()
{
    if (m_server->isListening()) return true;
    // 임의 포트 바인딩 — OS가 사용 가능한 포트 할당.
    return m_server->listen(QHostAddress::LocalHost, 0);
}

void MockServer::stop()
{
    if (m_client) {
        m_client->abort();
        m_client->deleteLater();
        m_client = nullptr;
    }
    m_server->close();
    m_rxBuffer.clear();
}

quint16 MockServer::port() const
{
    return m_server->serverPort();
}

bool MockServer::isListening() const
{
    return m_server->isListening();
}

bool MockServer::isClientConnected() const
{
    return m_client && m_client->state() == QAbstractSocket::ConnectedState;
}

void MockServer::disconnectClient()
{
    if (m_client) m_client->disconnectFromHost();
}

void MockServer::abortClient()
{
    if (m_client) m_client->abort();
}

void MockServer::sendEnvelope(const QString& type, const QJsonObject& data,
                              const QString& id, qint64 timestampMs)
{
    if (!isClientConnected()) return;
    const QByteArray payload =
        BroadChatProtocol::encodeEnvelope(type, data, id, timestampMs);
    if (payload.isEmpty()) return;
    m_client->write(payload);
    m_client->flush();
}

void MockServer::sendServerHello(int protoMin, int protoMax,
                                  const QString& serverVersion)
{
    QJsonObject data;
    data.insert(QStringLiteral("serverVersion"), serverVersion);
    data.insert(QStringLiteral("protocolMin"), protoMin);
    data.insert(QStringLiteral("protocolMax"), protoMax);
    QJsonArray caps;
    caps.append(QStringLiteral("chat"));
    caps.append(QStringLiteral("viewers"));
    caps.append(QStringLiteral("platform_status"));
    caps.append(QStringLiteral("emoji_image"));
    caps.append(QStringLiteral("history"));
    data.insert(QStringLiteral("capabilities"), caps);
    sendEnvelope(QStringLiteral("server_hello"), data, QString(),
                 QDateTime::currentMSecsSinceEpoch());
}

void MockServer::sendBye(const QString& reason, const QString& detail)
{
    QJsonObject data;
    data.insert(QStringLiteral("reason"), reason);
    if (!detail.isEmpty()) {
        data.insert(QStringLiteral("detail"), detail);
    }
    sendEnvelope(QStringLiteral("bye"), data);
}

void MockServer::sendRawBytes(const QByteArray& bytes)
{
    if (!isClientConnected()) return;
    m_client->write(bytes);
    m_client->flush();
}

void MockServer::onNewConnection()
{
    QTcpSocket* socket = m_server->nextPendingConnection();
    if (!socket) return;

    if (m_client) {
        // 이미 1대 접속 중이면 새 접속 거부 (테스트 단순화).
        socket->abort();
        socket->deleteLater();
        return;
    }

    m_client = socket;
    m_client->setParent(this);
    connect(m_client, &QTcpSocket::readyRead,
            this, &MockServer::onClientReadyRead);
    connect(m_client, &QTcpSocket::disconnected,
            this, &MockServer::onClientDisconnectedInternal);
    emit clientConnected();
}

void MockServer::onClientReadyRead()
{
    if (!m_client) return;

    while (m_client->bytesAvailable() > 0) {
        m_rxBuffer.append(m_client->readAll());
        int nl;
        while ((nl = m_rxBuffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_rxBuffer.left(nl);
            m_rxBuffer.remove(0, nl + 1);
            if (!line.isEmpty()) {
                handleLine(line);
            }
        }
    }
}

void MockServer::onClientDisconnectedInternal()
{
    m_rxBuffer.clear();
    if (m_client) {
        m_client->deleteLater();
        m_client = nullptr;
    }
    emit clientDisconnected();
}

void MockServer::handleLine(const QByteArray& line)
{
    const auto env = BroadChatProtocol::parseEnvelope(line);
    if (!env.valid) {
        // 테스트에서 raw bytes 주입 시 여기 도달 — 조용히 drop.
        return;
    }

    // client_hello를 전용 signal로 분리 발행 (테스트 편의).
    if (env.type == QStringLiteral("client_hello")) {
        const QString clientId = env.data.value(QStringLiteral("clientId")).toString();
        const int proto = env.data.value(QStringLiteral("protocolVersion")).toInt(0);
        const QString token = env.data.value(QStringLiteral("authToken")).toString();
        emit clientHelloReceived(clientId, proto, token);
    }

    emit envelopeReceived(env.type, env.data, env.id);
}
