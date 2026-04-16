#ifndef YOUTUBE_LIVE_DISCOVERY_H
#define YOUTUBE_LIVE_DISCOVERY_H

#include "core/AppTypes.h"

#include <QDateTime>
#include <QNetworkRequest>
#include <QObject>
#include <QString>

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

    bool isRequestInFlight() const;
    void tick();

signals:
    void discoveryCompleted(const QString& liveChatId, const QString& videoId);
    void connectionReady();
    void progress(const QString& code, const QString& detail);
    void requestTick(int delayMs);

private:
    // Placeholder — methods will be moved from YouTubeAdapter in subsequent steps
    QNetworkAccessManager* m_network = nullptr;
    int* m_requestInFlight = nullptr;
    int* m_generation = nullptr;

    QString m_accessToken;
    QString m_channelId;
    QString m_channelHandle;
    QString m_configuredChannelId;
    QString m_configuredChannelHandle;
    QString m_manualVideoIdOverride;
    QString m_channelName;

    int m_bootstrapDiscoverAttempts = 0;
    bool m_bootstrapSearchFallbackTried = false;
    QDateTime m_nextSearchFallbackAllowedAtUtc;
    QDateTime m_nextWebFallbackAllowedAtUtc;
    bool m_announcedLiveChatPending = false;
    bool m_running = false;
};

#endif // YOUTUBE_LIVE_DISCOVERY_H
