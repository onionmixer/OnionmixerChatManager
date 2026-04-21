#ifndef ONIONMIXERCHATMANAGER_BROADCHATSERVER_H
#define ONIONMIXERCHATMANAGER_BROADCHATSERVER_H

#include "core/AppTypes.h"

#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVector>

class QTcpServer;
class QTcpSocket;
class ChatMessageModel;
class EmojiImageCache;
class ClientSession;

// BroadChatServer — PLAN_MAINPROJECT_MIGRATION.md §4.1 (v18~v22 TCP + auth)
class BroadChatServer : public QObject {
    Q_OBJECT
public:
    explicit BroadChatServer(const ChatMessageModel* model,
                             EmojiImageCache* emojiCache,
                             QObject* parent = nullptr);
    ~BroadChatServer() override;

    // v19-1 TCP 전환: start(QHostAddress, port, authToken).
    // v22-10: authToken은 m_authToken 멤버에 저장. stop+start 경유만 교체.
    bool start(const QHostAddress& bindAddress, quint16 port,
               const QString& authToken = QString());

    void stop();

    int activeClientCount() const;
    bool isListening() const;

    // v19-4 ClientSession이 hello 수신 시 호출하여 토큰 비교.
    bool isAuthTokenValid(const QString& candidate) const;

    // v22-1 server_hello 송신 시 ClientSession이 조회.
    bool isAuthRequired() const { return !m_authToken.isEmpty(); }

    // v13-16 lifecycle hook 설치 확인용. MainWindow가 stateChanged connect 직후 호출.
    void markLifecycleHookInstalled() { m_lifecycleHookInstalled = true; }

public slots:
    void broadcastChat(const UnifiedChatMessage& message);
    void broadcastViewerCount(int youtube, int chzzk);
    void broadcastPlatformStatus(PlatformId platform, const QString& state, bool live,
                                 const QString& runtimePhase = QString());

signals:
    void clientConnected(const QString& clientId);
    void clientDisconnected(const QString& clientId, const QString& reason);
    void protocolError(const QString& clientId, const QString& detail);
    void listenFailed(const QString& detail);
    // v20-1 listening=true 시 실제 port, false 시 0
    void listeningChanged(bool listening, quint16 port = 0);
    void clientCountChanged(int count);

private slots:
    void onNewConnection();
    void onSessionHello(const QString& clientId, int protocolVersion);
    void onSessionClosed(const QString& reason);
    void onSessionProtocolError(const QString& detail);
    void onSessionMessage(const QString& type, const QJsonObject& data, const QString& id);
    void onEmojiImageReady(const QString& emojiId);
    void onPingTick();

private:
    void removeSession(ClientSession* session, const QString& reason);
    void handleRequestHistory(ClientSession* session, const QString& requestId,
                              const QJsonObject& data);
    void handleRequestEmoji(ClientSession* session, const QString& requestId,
                            const QJsonObject& data);
    void sendEmojiResponse(ClientSession* session, const QString& requestId,
                           const QString& emojiId, const QString& error = QString());
    // v79: 새로 Active 상태가 된 세션에 캐시된 viewer/platform_status 를 일회성 push.
    // broadcastXxx() 는 "값이 바뀔 때만" 호출되므로 신규 접속 클라가 현재 상태를
    // 수신하지 못하는 이슈 해소 (OnionmixerBroadChatClient 시청자 카운터 미표시 원인).
    void sendInitialStateToSession(ClientSession* session);

    const ChatMessageModel* m_model = nullptr;
    EmojiImageCache* m_emojiCache = nullptr;
    QTcpServer* m_server = nullptr;
    QString m_authToken; // v22-10 stop+start 경유만 교체

    QVector<ClientSession*> m_sessions;

    struct EmojiPending {
        ClientSession* session = nullptr;
        QString requestId;
    };
    QHash<QString, QVector<EmojiPending>> m_emojiPending;

    static constexpr int kMaxClients = 10;
    static constexpr int kDefaultHistoryCount = 50;
    static constexpr int kMaxHistoryCount = 500;
    static constexpr int kPingIntervalMs = 30000;

    QTimer* m_pingTimer = nullptr;

    // v13-16 hook 설치 감지 — start() 최초 호출 시 false면 WARN.
    bool m_lifecycleHookInstalled = false;
    bool m_startWarnedOnce = false;

    // v79: broadcastViewerCount/PlatformStatus 호출 시 값 캐싱.
    // 신규 세션이 Active 전환될 때 sendInitialStateToSession 에서 push.
    bool m_hasCachedViewers = false;
    int m_lastYoutubeViewers = -1;
    int m_lastChzzkViewers = -1;
    QHash<int /*PlatformId*/, QJsonObject> m_lastPlatformStatus;
};

#endif
