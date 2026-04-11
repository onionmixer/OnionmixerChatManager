#ifndef CHZZK_ADAPTER_H
#define CHZZK_ADAPTER_H

#include "core/IChatPlatformAdapter.h"

#include <QJsonValue>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class QWebSocket;

class ChzzkAdapter : public IChatPlatformAdapter {
    Q_OBJECT
public:
    explicit ChzzkAdapter(QObject* parent = nullptr);

    PlatformId platformId() const override;
    void start(const PlatformSettings& settings) override;
    void stop() override;
    bool isConnected() const override;
    void applyRuntimeAccessToken(const QString& accessToken);

private:
    void resetProgressAnnouncements();
    void requestSessionAuth();
    void connectSocket(const QString& sessionUrl);
    QUrl buildSocketUrl(const QString& sessionUrl) const;
    void subscribeChatEvent(const QString& sessionKey);
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketTextMessageReceived(const QString& payload);
    void processSocketIoPacket(const QString& packet);
    void processSocketIoEvent(const QString& eventName, const QJsonValue& payload);
    void handleConnectFailure(const QString& code, const QString& message);

    bool m_connected = false;
    bool m_stopping = false;
    bool m_connectSignalEmitted = false;
    int m_socketGeneration = 0;
    QTimer* m_connectWatchdog = nullptr;
    QNetworkAccessManager* m_network = nullptr;
    QWebSocket* m_socket = nullptr;

    QString m_accessToken;
    QString m_clientId;
    QString m_clientSecret;
    QString m_channelId;
    QString m_channelName;
    QString m_sessionKey;
    bool m_useClientSessionAuth = false;
    bool m_clientSessionFallbackTried = false;
    bool m_pendingConnectResult = false;
    bool m_chatSubscribed = false;
    bool m_subscribeInFlight = false;
    QString m_subscribeInFlightSessionKey;
    int m_subscribeRetryCount = 0;
    QString m_subscribeSessionKey;
    int m_subscribeRecoverCount = 0;
    bool m_announcedSessionPending = false;
    bool m_announcedSubscribePending = false;
    bool m_announcedChatReady = false;
};

#endif // CHZZK_ADAPTER_H
