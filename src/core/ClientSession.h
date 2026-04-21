#ifndef ONIONMIXERCHATMANAGER_CLIENTSESSION_H
#define ONIONMIXERCHATMANAGER_CLIENTSESSION_H

#include "shared/BroadChatProtocol.h"

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QTcpSocket;
class BroadChatServer;

// ClientSession — PLAN_MAINPROJECT_MIGRATION.md §4.2 (v19-3 TCP)
class ClientSession : public QObject {
    Q_OBJECT
public:
    enum class State {
        Connected,
        HelloReceived,
        Active,
        Closing,
        Closed,
        Error
    };

    // v14-3·v19-3: QTcpSocket 기반. setParent로 소유권 이전.
    explicit ClientSession(QTcpSocket* socket, QObject* parent = nullptr);
    ~ClientSession() override;

    State state() const { return m_state; }
    QString clientId() const { return m_clientId; }

    // v22-1 authRequired 값 포함한 server_hello 송신.
    void sendServerHello(bool authRequired);

    // v6.4.10 ping 관련
    bool sendPingAndCheckTimeout();
    void notifyPongReceived();

    // envelope 송신 — 성공 true.
    bool sendEnvelope(const QString& type,
                      const QJsonObject& data = {},
                      const QString& id = {},
                      qint64 timestampMs = -1);

    // graceful close. §6.5 v12-6 best-effort.
    void sendByeAndClose(const QString& reason, const QString& detail = {});

    // v21-6 slow timeout 감지 — sendEnvelope 직후 호출.
    // 반환 true면 §7.2 10초 timeout 도달 → 호출자가 close 진행.
    bool checkSlowTimeout();

signals:
    void helloReceived(const QString& clientId, int protocolVersion);
    void messageReceived(const QString& type, const QJsonObject& data, const QString& id);
    void protocolError(const QString& detail);
    void closed(const QString& reason);

private slots:
    void onReadyRead();
    void onDisconnected();
    void onBytesWritten(qint64 bytes);

private:
    void transitionTo(State next);
    void handleLine(const QByteArray& line);
    void dispatch(const BroadChatProtocol::Envelope& env);
    void forceProtocolError(const QString& detail);

    QTcpSocket* m_socket = nullptr;
    State m_state = State::Connected;
    QString m_clientId;
    QByteArray m_lineBuffer;
    bool m_byeSent = false;

    int m_consecutiveMissedPings = 0;

    // v14-10·v21-6 slow buffer 감시
    qint64 m_slowEnteredAt = 0; // 0 = normal, >0 = ms epoch when slow started
    static constexpr qint64 kMaxWriteBufferBytes = 8 * 1024 * 1024; // 8 MB
    static constexpr qint64 kSlowRecoveryBytes = 4 * 1024 * 1024;   // 4 MB (hysteresis)
    static constexpr qint64 kSlowTimeoutMs = 10000;
};

#endif
