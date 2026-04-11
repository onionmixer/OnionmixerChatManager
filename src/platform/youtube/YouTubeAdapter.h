#ifndef YOUTUBE_ADAPTER_H
#define YOUTUBE_ADAPTER_H

#include "core/IChatPlatformAdapter.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QSet>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class YouTubeLiveChatWebClient;
class YouTubeStreamListClient;

class YouTubeAdapter : public IChatPlatformAdapter {
    Q_OBJECT
public:
    explicit YouTubeAdapter(QObject* parent = nullptr);

    PlatformId platformId() const override;
    void start(const PlatformSettings& settings) override;
    void stop() override;
    bool isConnected() const override;
    QString currentLiveChatId() const;
    void applyRuntimeAccessToken(const QString& accessToken);

private:
    bool shouldUseWebChatTransport() const;
    void startWebChatTransport();
    void onWebChatStarted();
    void onWebChatMessagesReceived(const QVector<UnifiedChatMessage>& messages);
    void onWebChatFailed(const QString& code, const QString& detail);
    void onWebChatEnded(const QString& reason);
    bool shouldUseStreamListTransport() const;
    int streamReconnectDelayMs() const;
    void startStreamListTransport();
    void clearStreamRuntimeState();
    void publishReceivedMessage(UnifiedChatMessage message);
    void markStreamTransportReady();
    void onStreamListStarted();
    void onStreamListResponseObserved(int itemCount, bool hasNextPageToken, bool hasOfflineAt);
    void onStreamListMessagesReceived(const QVector<UnifiedChatMessage>& messages);
    void onStreamListCheckpoint(const QString& nextPageToken);
    void onStreamListEnded(const QString& reason);
    void onStreamListFailed(const QString& code, const QString& detail);
    void emitLiveStateInfo(const QString& code, const QString& detail);
    void setLiveStateChecking(const QString& detail);
    void setLiveStateOnline(const QString& detail);
    void setLiveStateOffline(const QString& detail);
    void setLiveStateUnknown(const QString& detail);
    int connectDiscoveryDelayMs() const;
    bool isBootstrapDiscoveryPhase() const;
    bool isThirdPartyChannel() const;
    void emitLiveChatPendingInfoOnce(const QString& detail);
    void scheduleNextTick(int delayMs);
    void onLoopTick();
    void requestOwnChannelProfile();
    void requestActiveBroadcast();
    void requestLiveByHandleWeb();
    void requestRecentStreamByHandleWeb();
    void requestLiveByChannelPageWeb();
    void requestLiveByChannelEmbedWeb();
    void requestPublicFeedForLiveChat();
    void requestLiveByChannelSearch();
    void requestOwnedRecentVideosForLiveChat();
    void requestMineUploadsPlaylistForLiveChat();
    void requestPlaylistItemsForLiveChat(const QString& playlistId);
    void requestRecentVideoDetailsForLiveChat(const QStringList& videoIds);
    void requestVideoDetailsForLiveChat(const QString& videoId);
    QString parseManualVideoIdOverride(const QString& raw) const;
    QString normalizeHandleForUrl(const QString& raw) const;
    bool isLikelyYouTubeVideoIdCandidate(const QString& value) const;
    QString extractYouTubeVideoIdFromUrl(const QUrl& url) const;
    QString extractYouTubeVideoIdFromHtml(const QString& html) const;
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
    QString m_channelHandle;
    QString m_configuredChannelId;
    QString m_configuredChannelHandle;
    QString m_manualVideoIdOverride;
    QString m_liveChatId;
    QString m_nextPageToken;
    QSet<QString> m_seenMessageIds;
    int m_bootstrapDiscoverAttempts = 0;
    bool m_bootstrapSearchFallbackTried = false;
    bool m_announcedLiveChatPending = false;
    QDateTime m_nextWebFallbackAllowedAtUtc;
    QDateTime m_nextSearchFallbackAllowedAtUtc;
    QString m_lastLiveStateCode;
    QString m_lastLiveStateDetail;
    YouTubeLiveChatWebClient* m_webChatClient = nullptr;
    QString m_discoveredVideoId;
    int m_webChatFailureCount = 0;
    QDateTime m_webChatFallbackUntilUtc;
    YouTubeStreamListClient* m_streamListClient = nullptr;
    QString m_streamResumeToken;
    int m_streamFailureCount = 0;
    QDateTime m_streamFallbackUntilUtc;
    bool m_streamTransportReady = false;
    quint64 m_streamConnectWatchdogNonce = 0;
};

#endif // YOUTUBE_ADAPTER_H
