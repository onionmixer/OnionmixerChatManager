#include "platform/youtube/YouTubeLiveDiscovery.h"

#include <QNetworkAccessManager>

YouTubeLiveDiscovery::YouTubeLiveDiscovery(QNetworkAccessManager* network,
                                            int* requestInFlight,
                                            int* generation,
                                            QObject* parent)
    : QObject(parent)
    , m_network(network)
    , m_requestInFlight(requestInFlight)
    , m_generation(generation)
{
}

void YouTubeLiveDiscovery::start(const QString& channelId, const QString& channelHandle,
                                  const QString& configuredChannelId, const QString& configuredChannelHandle,
                                  const QString& manualVideoIdOverride, const QString& accessToken)
{
    m_channelId = channelId;
    m_channelHandle = channelHandle;
    m_configuredChannelId = configuredChannelId;
    m_configuredChannelHandle = configuredChannelHandle;
    m_manualVideoIdOverride = manualVideoIdOverride;
    m_accessToken = accessToken;
    m_running = true;
    m_bootstrapDiscoverAttempts = 0;
    m_bootstrapSearchFallbackTried = false;
    m_nextSearchFallbackAllowedAtUtc = QDateTime();
    m_nextWebFallbackAllowedAtUtc = QDateTime();
    m_announcedLiveChatPending = false;
}

void YouTubeLiveDiscovery::stop()
{
    m_running = false;
}

void YouTubeLiveDiscovery::reset()
{
    m_bootstrapDiscoverAttempts = qMin(m_bootstrapDiscoverAttempts, 3);
    m_bootstrapSearchFallbackTried = false;
    m_nextWebFallbackAllowedAtUtc = QDateTime();
    m_announcedLiveChatPending = false;
}

void YouTubeLiveDiscovery::updateAccessToken(const QString& accessToken)
{
    m_accessToken = accessToken;
}

bool YouTubeLiveDiscovery::isRequestInFlight() const
{
    return m_requestInFlight && *m_requestInFlight;
}

void YouTubeLiveDiscovery::tick()
{
    // Placeholder — discovery tick logic will be moved from YouTubeAdapter::onLoopTick
    // in subsequent steps. For now, this is a no-op.
}
