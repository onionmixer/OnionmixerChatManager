#ifndef BOTMANAGER_CONSTANTS_H
#define BOTMANAGER_CONSTANTS_H

namespace BotManager {

namespace Timings {
    constexpr int kImmediateRetickMs = 100;
    constexpr int kDefaultPollIntervalMs = 3000;
    constexpr int kLiveProbeIntervalMs = 3000;
    constexpr int kStatusBarDisplayMs = 3000;
    constexpr int kChzzkReconnectDelayMs = 3000;
    constexpr int kDiscoveryRetryMs = 5000;
    constexpr int kHttpRequestTimeoutMs = 8000;
    constexpr int kChzzkSessionAuthTimeoutMs = 8000;
    constexpr int kChzzkConnectWatchdogMs = 12000;
    constexpr int kDiscoveryBackoffMaxMs = 15000;
    constexpr int kQuotaBackoffMs = 300000;
}

namespace Limits {
    constexpr int kYouTubeSeenMessageIdsMax = 10000;
    constexpr int kChzzkSeenMessageIdsMax = 4000;
    constexpr int kRecentMessageIdsMax = 2000;
    constexpr int kChatterStatsMax = 10000;
    constexpr int kEventLogMaxBlocks = 10000;
    constexpr int kSendHistoryMax = 100;
    constexpr int kAuthorHandleCacheMax = 5000;
    constexpr int kAuthorLookupQueueMax = 500;
    constexpr int kEmojiImageCacheMax = 500;
}

} // namespace BotManager

#endif // BOTMANAGER_CONSTANTS_H
