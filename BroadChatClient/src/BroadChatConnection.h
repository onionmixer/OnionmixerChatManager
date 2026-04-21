#pragma once

#include "core/AppTypes.h"
#include "shared/BroadChatProtocol.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

class QTimer;

class QTcpSocket;

// BroadChatConnection — PLAN_DEV_BROADCHATCLIENT §5.2 (v37 TCP).
// QTcpSocket wrapper + NDJSON framing + server_hello/client_hello handshake.
// Step 2 scope: connect · hello exchange · bye handling · overflow guard.
// Reconnect backoff (§5.2 v2-9), request_history, request_emoji, ping/pong land in later steps.
class BroadChatConnection : public QObject
{
    Q_OBJECT
public:
    explicit BroadChatConnection(QObject* parent = nullptr);
    ~BroadChatConnection() override;

    // Optional override of the auto-derived clientId (otherwise a fresh UUID suffix is used).
    void setClientId(const QString& id);
    QString clientId() const { return m_clientId; }

    // v37-3 TCP: connect to host:port. authToken은 선택 (서버가 인증 요구 시 필수).
    // Returns false only on invalid arguments; actual connect result is asynchronous.
    bool start(const QString& host, quint16 port,
               const QString& authToken = QString());

    // Graceful close — sends bye(reason=normal) best-effort before disconnect.
    void stop();

    bool isConnected() const;
    bool isHelloCompleted() const { return m_helloCompleted; }

    // v34-6: 요청 id 반환. pending 매칭은 내부에서 처리.
    // 같은 emojiId가 이미 pending이면 기존 requestId 재사용 (중복 송신 방지).
    QString requestEmoji(const QString& emojiId);

    // v34-6·v34-10 (v68 #1): history 요청. helloCompleted 이후 호출.
    // maxCount 1~500 권장. 0 이하면 기본값 50 사용.
    // 응답은 historyChunkReceived(requestId, messages, hasMore) signal.
    QString requestHistory(int maxCount = 50, const QString& beforeMessageId = {});

    // v2-9·v16-13 재연결 매트릭스.
    // 현재 정책 상태.
    bool isReconnectEnabled() const { return !m_reconnectDisabled; }
    int retryCount() const { return m_retryCount; }
    QString lastByeReason() const { return m_lastByeReason; }

signals:
    void connected();
    void disconnected();
    void protocolError(const QString& detail);
    // v34-5: hello round-trip complete. Emitted after server_hello accepted and client_hello sent.
    void helloCompleted(const QString& serverVersion, int protocolMin, int protocolMax);
    void byeReceived(const QString& reason, const QString& detail);

    // Placeholders wired but currently only log — full payload handling lands in later steps.
    void chatReceived(const UnifiedChatMessage& message);
    void viewersReceived(int youtube, int chzzk, int total);
    void platformStatusReceived(const QJsonObject& data);
    void emojiImageReceived(const QString& emojiId, const QByteArray& bytes,
                            const QString& mime, const QString& error);
    void historyChunkReceived(const QString& requestId,
                              const QVector<UnifiedChatMessage>& messages, bool hasMore);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError();
    void onEmojiTimerTick();
    void onReconnectTick();

private:
    void resetState();
    void handleLine(const QByteArray& line);
    void dispatch(const BroadChatProtocol::Envelope& env);
    void sendClientHello();
    bool sendEnvelope(const QString& type, const QJsonObject& data = {},
                      const QString& id = {}, qint64 timestampMs = -1);
    void raiseProtocolError(const QString& detail);

    struct EmojiPending {
        QString emojiId;
        QDateTime deadline;
    };

    void handleEmojiImage(const BroadChatProtocol::Envelope& env);
    void failEmojiPending(const QString& requestId, const QString& error);

    QTcpSocket* m_socket = nullptr;
    QString m_host;
    quint16 m_port = 0;
    QString m_authToken;
    QString m_clientId;
    QByteArray m_lineBuffer;
    bool m_serverHelloSeen = false;
    bool m_helloCompleted = false;
    bool m_byeSent = false;
    bool m_stopping = false;

    // v5-12 emoji flow: requestId → pending entry. emojiId → requestId reverse map
    // avoids duplicate in-flight requests for the same id.
    QHash<QString /*requestId*/, EmojiPending> m_emojiPending;
    QHash<QString /*emojiId*/, QString /*requestId*/> m_emojiPendingByEmoji;
    QTimer* m_emojiTimer = nullptr;

    static constexpr int kEmojiTimeoutMs = 5000; // §11.2 v12-15

    // v2-9·v16-13 재연결 백오프
    int scheduleReconnect(const QString& reason);
    void cancelReconnect();

    QTimer* m_reconnectTimer = nullptr;
    int m_retryCount = 0;
    bool m_reconnectDisabled = false;
    QString m_lastByeReason; // onByeReceived에서 저장
};
