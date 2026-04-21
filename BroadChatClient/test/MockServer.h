// PLAN_DEV_BROADCHATCLIENT §16.13 · §부록 K FU-K4: Mock TCP server.
// `QTcpServer::listen(LocalHost, 0)`로 임의 포트 바인딩 후 클라 1대 accept.
// BroadChatProtocol NDJSON 프레이밍으로 송수신, 테스트가 시나리오 스크립팅.

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;

class MockServer : public QObject
{
    Q_OBJECT
public:
    explicit MockServer(QObject* parent = nullptr);
    ~MockServer() override;

    // 서버 시작. 성공 시 true + port() 반환.
    bool start();
    void stop();
    quint16 port() const;
    bool isListening() const;
    bool isClientConnected() const;

    // 접속된 클라를 서버 측에서 강제 종료 (bye 없이 FIN).
    void disconnectClient();
    // 접속된 클라를 즉시 RST로 끊기 (abort — protocol_error 시뮬레이션).
    void abortClient();

    // BroadChatProtocol envelope 송신. 편의 메서드.
    void sendEnvelope(const QString& type,
                      const QJsonObject& data = QJsonObject(),
                      const QString& id = QString(),
                      qint64 timestampMs = -1);

    // 흔한 서버 메시지 helpers.
    void sendServerHello(int protoMin = 1, int protoMax = 1,
                         const QString& serverVersion = QStringLiteral("0.1.0-mock"));
    void sendBye(const QString& reason, const QString& detail = QString());

    // Malformed 또는 raw bytes 주입 (프로토콜 테스트용).
    void sendRawBytes(const QByteArray& bytes);

signals:
    void clientConnected();
    void clientDisconnected();
    void clientHelloReceived(const QString& clientId, int protocolVersion,
                             const QString& authToken);
    void envelopeReceived(const QString& type, const QJsonObject& data,
                          const QString& id);

private slots:
    void onNewConnection();
    void onClientReadyRead();
    void onClientDisconnectedInternal();

private:
    void handleLine(const QByteArray& line);

    QTcpServer* m_server = nullptr;
    QTcpSocket* m_client = nullptr;
    QByteArray m_rxBuffer;
};
