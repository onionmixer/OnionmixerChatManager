#ifndef ONIONMIXERCHATMANAGER_CONSTANTS_H
#define ONIONMIXERCHATMANAGER_CONSTANTS_H

namespace OnionmixerChatManager {

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
    constexpr int kYouTubeViewerCountIntervalMs = 15000;
    constexpr int kQuotaBackoffMs = 300000;
}

namespace Viewers {
    // YouTube Data API `liveStreamingDetails.concurrentViewers`는 저시청자·라이브 경계
    // 구간에서 간헐적으로 누락된다. 즉시 placeholder("—")로 전환하면 UI가 깜박이므로
    // 연속 결측이 이 값 이상 누적된 뒤에만 리셋한다. (15s interval × 3 ≈ 45s grace)
    constexpr int kYouTubeViewerMissGraceCount = 3;
    // fresh 값 도착 후 이 시간이 지나면 tooltip에 stale 표기.
    constexpr int kYouTubeViewerStaleThresholdMs = 20000;
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

} // namespace OnionmixerChatManager

#endif // ONIONMIXERCHATMANAGER_CONSTANTS_H
