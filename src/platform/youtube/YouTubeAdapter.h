#ifndef YOUTUBE_ADAPTER_H
#define YOUTUBE_ADAPTER_H

#include "core/IChatPlatformAdapter.h"

#include <QDateTime>
#include <QSet>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

class YouTubeAdapter : public IChatPlatformAdapter {
    Q_OBJECT
public:
    explicit YouTubeAdapter(QObject* parent = nullptr);

    PlatformId platformId() const override;
    void start(const PlatformSettings& settings) override;
    void stop() override;
    bool isConnected() const override;
    QString currentLiveChatId() const;

private:
    int connectDiscoveryDelayMs() const;
    bool isBootstrapDiscoveryPhase() const;
    void emitLiveChatPendingInfoOnce(const QString& detail);
    void scheduleNextTick(int delayMs);
    void onLoopTick();
    void requestActiveBroadcast();
    void requestLiveByChannelSearch();
    void requestVideoDetailsForLiveChat(const QString& videoId);
    void requestLiveChatMessages();
    void handleRequestFailure(const QString& code, const QString& message);

    bool m_connected = false;
    bool m_running = false;
    bool m_pendingConnectResult = false;
    QTimer* m_loopTimer = nullptr;
    QNetworkAccessManager* m_network = nullptr;
    int m_generation = 0;
    bool m_requestInFlight = false;

    QString m_accessToken;
    QString m_channelId;
    QString m_channelName;
    QString m_liveChatId;
    QString m_nextPageToken;
    QSet<QString> m_seenMessageIds;
    int m_bootstrapDiscoverAttempts = 0;
    bool m_bootstrapSearchFallbackTried = false;
    bool m_announcedLiveChatPending = false;
    QDateTime m_nextSearchFallbackAllowedAtUtc;
};

#endif // YOUTUBE_ADAPTER_H
