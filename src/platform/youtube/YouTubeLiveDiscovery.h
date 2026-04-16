#ifndef YOUTUBE_LIVE_DISCOVERY_H
#define YOUTUBE_LIVE_DISCOVERY_H

#include "core/AppTypes.h"

#include <QDateTime>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

class YouTubeLiveDiscovery : public QObject {
    Q_OBJECT
public:
    explicit YouTubeLiveDiscovery(QNetworkAccessManager* network,
                                  int* requestInFlight,
                                  int* generation,
                                  QObject* parent = nullptr);

    void start(const QString& channelId, const QString& channelHandle,
               const QString& configuredChannelId, const QString& configuredChannelHandle,
               const QString& manualVideoIdOverride, const QString& accessToken);
    void stop();
    void reset();
    void updateAccessToken(const QString& accessToken);
    void tick();

signals:
    void discoveryCompleted(const QString& liveChatId, const QString& videoId);
    void connectionReady();
    void progress(const QString& code, const QString& detail);
    void requestTick(int delayMs);

private:
    QNetworkRequest createBearerRequest(const QUrl& url) const;
    QNetworkRequest createWebScrapingRequest(const QUrl& url) const;
    int setupRequestGuard(QNetworkReply* reply);

    int connectDiscoveryDelayMs() const;
    bool isBootstrapDiscoveryPhase() const;
    bool isThirdPartyChannel() const;
    void emitLiveStateInfo(const QString& code, const QString& detail);
    void emitLiveChatPendingInfoOnce(const QString& detail);

    void requestOwnChannelProfile();
    void requestActiveBroadcast();
    void requestLiveByHandleWeb();
    void requestRecentStreamByHandleWeb();
    void requestLiveByChannelPageWeb();
    void requestLiveByChannelEmbedWeb();
    void requestPublicFeedForLiveChat();
    void requestLiveByChannelSearch();
    void requestMineUploadsPlaylistForLiveChat();
    void requestPlaylistItemsForLiveChat(const QString& playlistId);
    void requestRecentVideoDetailsForLiveChat(const QStringList& videoIds);
    void requestVideoDetailsForLiveChat(const QString& videoId);

    QNetworkAccessManager* m_network = nullptr;
    int* m_requestInFlight = nullptr;
    int* m_generation = nullptr;

    QString m_accessToken;
    QString m_channelId;
    QString m_channelHandle;
    QString m_channelName;
    QString m_configuredChannelId;
    QString m_configuredChannelHandle;
    QString m_manualVideoIdOverride;

    bool m_running = false;
    int m_bootstrapDiscoverAttempts = 0;
    bool m_bootstrapSearchFallbackTried = false;
    QDateTime m_nextSearchFallbackAllowedAtUtc;
    QDateTime m_nextWebFallbackAllowedAtUtc;
    bool m_announcedLiveChatPending = false;
    QString m_lastLiveStateCode;
    QString m_lastLiveStateDetail;
};

#endif // YOUTUBE_LIVE_DISCOVERY_H
