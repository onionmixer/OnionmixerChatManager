#ifndef YOUTUBE_ADAPTER_H
#define YOUTUBE_ADAPTER_H

#include "core/IChatPlatformAdapter.h"

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

private:
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
};

#endif // YOUTUBE_ADAPTER_H
