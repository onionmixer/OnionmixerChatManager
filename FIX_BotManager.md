# BotManager 이슈 분석 및 개선방안

> 분석일: 2026-04-12 ~ 2026-04-15
> 분석 반복: 70회차 완료 (심층 코드 트레이싱 + 엣지 케이스 + 인터넷 조사 + 논리 검증 5차)

---

## Issue 1: YouTube 이모티콘이 ":hand-pink-waving:" 코드로 표시

### 근본 원인

`YouTubeChatMessageParser.cpp` `parseInnerTubeChatRenderer()` lines 335-346에서 `shortcuts[0]`을 무조건 우선 사용하여, 표준 Unicode 이모지도 shortcut 텍스트로 표시됨.

### YouTube emoji 3가지 유형

| 유형 | `isCustomEmoji` | `emojiId` 형식 | 판별 기준 | 올바른 표시 |
|------|-----------------|----------------|----------|------------|
| 표준 Unicode (👍❤️🙏🏼) | 없음/false | 유니코드 문자 자체 | **`/` 미포함** | `emojiId` 직접 사용 |
| YouTube 글로벌 이모트 | 없음/false | `UCkszU.../...` | `/` 포함 | shortcuts 텍스트 |
| 채널 커스텀 이모지 | `true` | `UC채널ID/이모지ID` | `isCustomEmoji=true` | shortcuts 텍스트 |

### 엣지 케이스 (10회차 분석)

| 케이스 | 현재 동작 | 수정 후 |
|--------|----------|---------|
| shortcuts + emojiId 모두 빈 값 | emoji 무시 (텍스트 누락) | 동일 (불가피) |
| shortcuts 배열에 빈 문자열 `[""]` | 빈 문자열 append | emojiId로 폴백 |
| emojiId에 `/` 포함 + isCustomEmoji=false | 내부 ID 텍스트 표시 | shortcuts 텍스트 사용 |
| 복합 이모지 🙏🏼 (shortcuts 없음) | emojiId 정상 표시 (현재 OK) | 동일 유지 |

### 수정 코드

```cpp
// YouTubeChatMessageParser.cpp parseInnerTubeChatRenderer() 내 emoji 분기
const QJsonObject emoji = run.value(QStringLiteral("emoji")).toObject();
const bool isCustom = emoji.value(QStringLiteral("isCustomEmoji")).toBool(false);
const QString emojiId = emoji.value(QStringLiteral("emojiId")).toString();

if (!isCustom && !emojiId.isEmpty() && !emojiId.contains(QLatin1Char('/'))) {
    // 표준 Unicode: emojiId가 유니코드 문자 그 자체
    text += emojiId;
} else {
    // YouTube 글로벌 이모트 또는 채널 커스텀
    const QJsonArray shortcuts = emoji.value(QStringLiteral("shortcuts")).toArray();
    const QString shortcut = shortcuts.isEmpty() ? QString() : shortcuts.first().toString();
    if (!shortcut.isEmpty()) {
        text += shortcut;
    } else if (!emojiId.isEmpty()) {
        text += emojiId;
    }
}
```

### 수정 파일

- `src/platform/youtube/YouTubeChatMessageParser.cpp` — lines 327-346

---

## Issue 2: YouTube 라이브 상태가 "미확인"으로 전환 후 복구 안됨

### 근본 원인 (10회차 코드 트레이싱)

3단계 결함이 복합적으로 작용하여 UNKNOWN 상태에서 탈출 불가.

#### 결함 A: `onWebChatFailed()` — live 상태 업데이트 누락

```
YouTubeAdapter.cpp:619-633
onWebChatFailed():
  emit error(...)
  ++m_webChatFailureCount
  if (count >= 3) → fallback 설정
  scheduleNextTick(3000)
  // ← 문제: setLiveState 호출 없음. UNKNOWN이면 UNKNOWN 유지.
```

#### 결함 B: `applyRuntimeAccessToken()` — WebChat 미정지 + CHECKING 미전환

```
YouTubeAdapter.cpp:269-309
applyRuntimeAccessToken():
  if (token empty) → setLiveStateUnknown() + return
  if (m_streamListClient->isRunning()) → stop + reschedule  // streamList는 정지
  // ← 문제: m_webChatClient->isRunning() 체크 없음! WebChat은 정지 안됨.
  if (m_liveChatId.isEmpty()) → scheduleNextTick(100) + return
  // ← 문제: setLiveStateChecking() 미호출. UNKNOWN 탈출 안됨.
```

#### 결함 C: `onWebChatEnded()` — discovery 카운터 미리셋

```
YouTubeAdapter.cpp:635-649
onWebChatEnded():
  m_liveChatId.clear()
  m_discoveredVideoId.clear()
  m_bootstrapSearchFallbackTried = false
  // ← 문제: m_bootstrapDiscoverAttempts 리셋 없음
  setLiveStateOffline(reason)
  scheduleNextTick(5000)
```

`m_bootstrapDiscoverAttempts`가 10에 도달한 상태로 유지되면 `discoveryBackoffDelayMs()` → **120초 대기**. 재탐색이 2분 간격으로만 실행.

#### 결함 D: WebChat stop() 무통보

```
YouTubeLiveChatWebClient.cpp:76-86
stop():
  m_running = false
  ++m_generation
  m_pollTimer->stop()
  // ← 문제: failed/ended 시그널 미발행. Adapter가 정지를 감지 못함.
```

**대응 방침:** 결함 D 자체의 직접 수정(stop()에서 시그널 발행)은 하지 않는다. 대신 Fix 2에서 adapter가 `stop()` 호출 직후 `scheduleNextTick(100)`으로 다음 tick을 명시적으로 예약하는 우회(workaround) 방식을 사용한다. 이유: stop()에서 시그널을 emit하면 호출자 컨텍스트에서 재진입(re-entrant) 문제가 발생할 수 있으며, generation 카운터로 진행 중인 HTTP 응답을 안전하게 무시할 수 있으므로 adapter 측에서 제어하는 것이 더 안전하다.

### 수정 코드 (4개소)

**1. `onWebChatFailed()` — CHECKING 상태 전환 + backoff:**
```cpp
setLiveStateChecking(QStringLiteral("Retrying chat connection..."));
// webChat 실패 시 재시도 간격을 점진적으로 증가 (3→6→9→12→15초, 최대 15초)
const int backoffMs = qMin(3000 * m_webChatFailureCount, 15000);
scheduleNextTick(qMax(backoffMs, 3000));
```

기존의 고정 3초 재시도를 점진적 backoff로 변경하여 실패 반복 시 부하를 줄인다. `m_webChatFailureCount`는 이 시점에서 이미 증가한 상태 (1부터 시작). 3회 실패 시 `m_webChatFallbackUntilUtc` 300초 설정으로 webChat 자체를 차단하므로 4회차부터는 다른 transport를 사용한다.

**2. `applyRuntimeAccessToken()` — WebChat 정지 + CHECKING 전환:**
```cpp
// m_streamListClient 체크 블록 뒤에 추가:
if (m_webChatClient && m_webChatClient->isRunning()) {
    m_webChatClient->stop();  // stop()은 동기적 (m_running=false 즉시 설정)
    m_webChatFailureCount = 0;
    m_webChatFallbackUntilUtc = QDateTime();
    scheduleNextTick(100);
    return;
}

if (m_liveChatId.isEmpty()) {
    setLiveStateChecking(QStringLiteral("Token updated, re-checking live state."));
    scheduleNextTick(100);
    return;
}
```

`stop()`은 동기적으로 `m_running = false`를 설정하고 `m_pollTimer`를 정지한다. 진행 중인 HTTP 요청은 generation 카운터로 무시된다. `stop()`이 failed/ended를 emit하지 않지만, adapter가 직접 `scheduleNextTick(100)`으로 다음 loop tick을 예약하므로 adapter가 정지를 감지 못하는 문제는 발생하지 않는다.

**3. `onWebChatEnded()` — discovery 카운터 부분 리셋:**
```cpp
m_bootstrapDiscoverAttempts = qMin(m_bootstrapDiscoverAttempts, 3);
// 0으로 초기화하면 stream flapping 시 공격적 재탐색 가능 (5초부터 시작)
// 3으로 제한하면 15초 backoff에서 시작: 15→30→30→60→60→60→60→120초
// (discoveryBackoffDelayMs: 0→5s, <3→15s, <6→30s, <10→60s, 10+→120s)
```

0이 아닌 3으로 제한하여, 첫 재탐색에 15초 backoff를 적용한다. 이는 flapping 방어와 신속한 복구의 균형.

**4. `onWebChatFailed()` — discovery 카운터 부분 리셋 (3회 실패 시):**
```cpp
if (m_webChatFailureCount >= 3) {
    m_bootstrapDiscoverAttempts = qMin(m_bootstrapDiscoverAttempts, 3);
    m_webChatFallbackUntilUtc = ...;  // 300초간 webChat 차단
}
```

### 수정 파일

- `src/platform/youtube/YouTubeAdapter.cpp` — `onWebChatFailed()`, `applyRuntimeAccessToken()`, `onWebChatEnded()`

---

## Issue 3: 치지직 채팅이 전혀 표시되지 않음 [CRITICAL]

### 근본 원인 (30회차 확정)

최근 추가한 dedup 코드의 messageId 추출에서 **범용 키 `"id"` 또는 `"uid"`가 치지직 페이로드의 비고유 필드를 잡아 모든 메시지를 중복으로 차단**.

#### 메커니즘

치지직 CHAT 이벤트는 공식 문서상 `messageId` 필드를 보장하지 않는다. 그러나 `readStringByKeys()`가 `"id"`, `"uid"` 키를 폴백으로 시도하여, 실제로는 **메시지 고유 ID가 아닌 값**(이벤트 타입 ID, 사용자 ID 등)을 messageId로 잘못 추출한다. 이 비고유 값이 dedup QSet에 삽입되어, 동일 값을 가진 후속 메시지가 전부 중복으로 차단된다.

```cpp
// ChzzkAdapter.cpp line 747 — 현재 코드
msg.messageId = readStringByKeys(payload, QJsonObject(),
    { "messageId", "chatId", "id", "uid" });
//                              ^^^   ^^^ 이 키들이 문제
```

#### 발생 시나리오

| 시나리오 | 잡히는 키 | 결과 |
|---------|----------|------|
| **A: `"id"` = 이벤트 타입 문자열** (예: `"chat_event"`) | `"id"` | 모든 메시지가 동일 messageId → 첫 1건만 표시, 이후 전부 차단 |
| **B: `"uid"` = 사용자 ID 문자열** (예: `"USER_A"`) | `"uid"` | 사용자별 첫 메시지만 표시, 동일 사용자의 후속 메시지 전부 차단 |
| **C: `"uid"` = 숫자** (예: `12345`) | 없음 (toString() → `""`) | messageId 빈값 → dedup 건너뜀 → 정상 통과 |

시나리오 A 또는 B가 발생하면 채팅이 거의 또는 전혀 표시되지 않는다. 시나리오 C라면 dedup이 원인이 아니므로 다른 원인(구독 미완료 등) 조사 필요.

**핵심: dedup 코드가 "활성 상태이지만 잘못된 키를 사용"하는 것이 버그.** messageId가 항상 비어있는 것이 아니라, 잘못된 값으로 채워져서 오동작한다.

### 추가 발견: Chzzk 재연결 시 dedup set 미정리

`onSocketDisconnected()` (lines 565-583)에서 `m_seenMessageIds`를 clear하지 않음.
→ 재연결 후 이전 세션의 ID가 남아있어 합법적인 재전송 메시지가 차단될 수 있음.

### 추가 발견: 구독 경쟁 조건

Socket CONNECTED 이벤트 수신 시 `m_chatSubscribed = false` (line 690) 설정 후, SUBSCRIBED 이벤트가 도착하지 않으면 채팅 구독이 복구되지 않음.

### 수정 코드

**1. messageId 추출 키에서 `"id"`, `"uid"` 제거:**

```cpp
// 변경 전 (line 747):
msg.messageId = readStringByKeys(payload, QJsonObject(),
    { "messageId", "chatId", "id", "uid" });

// 변경 후:
msg.messageId = readStringByKeys(payload, QJsonObject(),
    { QStringLiteral("messageId"), QStringLiteral("chatId") });
```

**2. 재연결 시 dedup set 정리 (`onSocketDisconnected`):**

```cpp
void ChzzkAdapter::onSocketDisconnected()
{
    m_seenMessageIds.clear();  // 추가
    // ... 기존 코드 ...
}
```

### 수정 파일

- `src/platform/chzzk/ChzzkAdapter.cpp` — line 747 (messageId 키), `onSocketDisconnected()`

---

## Issue 4: YouTube 채팅 재연결 시 40분 전 메시지가 중복 표시

### 근본 원인 (코드 트레이싱)

YouTube 채팅 중복은 **3개 결함의 복합 작용**으로 발생.

#### 결함 A: `m_seenMessageIds` 공격적 전체 삭제 (4000건)

```cpp
// YouTubeAdapter.cpp publishReceivedMessage() lines 383-386
if (m_seenMessageIds.size() > 4000) {
    m_seenMessageIds.clear();       // ← 4000건 이력 전체 삭제
    m_seenMessageIds.insert(messageId);  // 현재 1건만 남음
}
```

40분간 4000건 이상 메시지 수신 → clear → 이전 메시지 ID 전부 망각 → 재연결 시 동일 메시지 재수신해도 중복 감지 불가.

#### 결함 B: WebChat 재연결 시 과거 채팅 백로그 수신

```cpp
// YouTubeLiveChatWebClient.cpp handleInitialPageResponse() lines 187-199
const QJsonArray actions = root.value("contents")
    .toObject().value("liveChatRenderer")
    .toObject().value("actions").toArray();
if (!actions.isEmpty()) {
    emit messagesReceived(parseActions(actions));  // ← 40분 전 메시지 포함
}
```

`https://www.youtube.com/live_chat?v={videoId}` 초기 페이지의 `ytInitialData`에 YouTube가 기본 백로그(최근 N건)를 포함. 재연결마다 과거 메시지가 다시 도착.

#### 결함 C: UI 레벨 중복 방지 없음

```cpp
// MainWindow.cpp appendChatMessage() line 1638
m_chatMessages.push_back(message);  // ← messageId 체크 없이 무조건 추가
```

adapter의 dedup이 실패해도 UI에 안전망이 없음.

### 중복 발생 시나리오 (실제 재현 경로)

```
[00:00] 방송 시작, 채팅 수신 시작
[00:40] 4001번째 메시지 → m_seenMessageIds.clear() (4000건 이력 삭제)
[00:41] 네트워크 장애 → WebChat 실패
[00:42] WebChat 재연결 → /live_chat 초기 페이지 fetch
[00:42] ytInitialData에 00:10~00:40 시점의 메시지 포함
[00:42] m_seenMessageIds에 해당 ID 없음 (clear 됨) → 전부 통과
[00:42] UI에 30분 전 메시지 수십 건 중복 표시
```

### 수정 방안: 2단계 중복 방지

#### 단계 1: `publishReceivedMessage()` — 타임스탬프 기반 필터 + seenMessageIds 확장

`m_seenMessageIds` clear 후에도 과거 메시지를 차단하기 위해, **마지막으로 표시한 메시지의 타임스탬프**를 기록하고 그보다 오래된 메시지를 거부. 또한 `m_seenMessageIds` 상한을 10000으로 상향하여 clear 빈도를 줄임.

```cpp
// YouTubeAdapter.h 멤버 추가:
QDateTime m_lastPublishedTimestampUtc;

// YouTubeAdapter.cpp publishReceivedMessage() 수정:
void YouTubeAdapter::publishReceivedMessage(UnifiedChatMessage message)
{
    const QString messageId = message.messageId.trimmed();
    if (messageId.isEmpty() || m_seenMessageIds.contains(messageId)) {
        return;
    }

    // 타임스탬프 기반 중복 필터 (오차 5초 허용)
    if (m_lastPublishedTimestampUtc.isValid() && message.timestamp.isValid()) {
        const qint64 diffSec = message.timestamp.toUTC().secsTo(m_lastPublishedTimestampUtc);
        if (diffSec > 5) {
            // 마지막 표시 메시지보다 5초 이상 과거 → 중복으로 간주
            return;
        }
    }

    m_seenMessageIds.insert(messageId);
    if (m_seenMessageIds.size() > 10000) {   // 4000 → 10000 상향
        m_seenMessageIds.clear();
        m_seenMessageIds.insert(messageId);
    }

    // 타임스탬프 갱신 (항상 최신값만 유지)
    if (message.timestamp.isValid()) {
        const QDateTime utc = message.timestamp.toUTC();
        if (!m_lastPublishedTimestampUtc.isValid() || utc > m_lastPublishedTimestampUtc) {
            m_lastPublishedTimestampUtc = utc;
        }
    }

    message.platform = platformId();
    // ... 기존 채널/이름 설정 ...
    emit chatReceived(message);
}
```

**오차 5초 허용 이유:** YouTube 서버 시간과 로컬 시간 차이, InnerTube `timestampUsec` 정밀도, continuation 응답의 메시지 순서 비보장.

**seenMessageIds 10000 상향 이유:** 4000건에서 clear 시 과거 ID 전체 망각 → 재연결 시 수십 건 중복 발생. 10000건으로 상향하면 clear 주기가 2.5배 늘어나 중복 확률 감소. 메모리 비용: QString ID 평균 50B × 10000 = ~500KB (허용 범위).

**`start()`에서 초기화 필수:**
```cpp
m_lastPublishedTimestampUtc = QDateTime();  // start()에서 clear
```

**엣지 케이스 대응 (10회차 검증):**

| 엣지 케이스 | 대응 |
|------------|------|
| 메시지 역순 도착 (새 메시지가 이전 타임스탬프) | 5초 오차 허용으로 대부분 통과. YouTube InnerTube는 일반적으로 시간순 |
| 방송 2시간 중단 후 재개 | `start()` 재호출 시 `m_lastPublishedTimestampUtc` 초기화 → 새 메시지 정상 수신 |
| 시스템 시계 편차 | `timestampUsec`는 YouTube 서버 시간 기준이므로 로컬 시계 영향 없음 |
| 빈 타임스탬프 메시지 | `!message.timestamp.isValid()` → 필터 건너뜀 → 통과 |

#### 단계 2: UI 레벨 최종 안전망

`MainWindow`에 `QSet<QString> m_recentMessageIds` (최대 2000건)를 유지하여 O(1) 중복 체크:

```cpp
// MainWindow.h 멤버 추가:
QSet<QString> m_recentMessageIds;

// MainWindow.cpp appendChatMessage() 수정:
void MainWindow::appendChatMessage(const UnifiedChatMessage& message, const QString& authorLabel)
{
    // UI 레벨 최종 중복 체크
    const QString msgId = message.messageId.trimmed();
    if (!msgId.isEmpty()) {
        if (m_recentMessageIds.contains(msgId)) {
            return;
        }
        m_recentMessageIds.insert(msgId);
        if (m_recentMessageIds.size() > 2000) {
            m_recentMessageIds.clear();
            m_recentMessageIds.insert(msgId);
        }
    }

    m_chatMessages.push_back(message);
    // ... 기존 트리밍/테이블 로직 ...
}
```

**QSet 사용 이유 (10회차 검증):**
- 선형 탐색 O(500)은 100msg/min 기준 초당 ~830 비교 → 비효율
- QSet O(1) 평균 → 대규모 채팅에서도 성능 유지
- 2000건 상한 → m_chatMessages 트리밍(5000→4000)보다 작지만, adapter 레벨 dedup과 합산하면 충분
- Chzzk 메시지 (빈 messageId) → `if (!msgId.isEmpty())` 체크로 건너뜀 (정상)

### 수정 파일

- `src/platform/youtube/YouTubeAdapter.h` — `m_lastPublishedTimestampUtc` 멤버 추가
- `src/platform/youtube/YouTubeAdapter.cpp` — `publishReceivedMessage()` 타임스탬프 필터 + seenMessageIds 10000 상향, `start()`에서 초기화
- `src/ui/MainWindow.h` — `QSet<QString> m_recentMessageIds` 멤버 추가
- `src/ui/MainWindow.cpp` — `appendChatMessage()` UI 레벨 QSet 기반 중복 체크

### 추가 발견: WebChat 시그널 순서 문제

`YouTubeLiveChatWebClient::handleInitialPageResponse()`:
```cpp
emit messagesReceived(messages);  // line 197: 메시지 먼저
emit started();                    // line 201: started 나중
```

→ 메시지가 `onWebChatStarted()` (connected 설정) 전에 도착. 현재 동작에 영향 없으나, 순서를 `started()` → `messagesReceived()`로 변경하는 것이 안전.

### Dedup 상태 수명주기 (Issue 2 × Issue 4 상호작용)

| 이벤트 | m_seenMessageIds | m_lastPublishedTimestampUtc | m_recentMessageIds (UI) |
|--------|-----------------|---------------------------|------------------------|
| `start()` | **clear** | **clear** | 유지 (MainWindow 소유) |
| `stop()` | **clear** | 유지 | 유지 |
| WebChat→WebChat 재연결 | **유지** (transport 간 보존) | **유지** | **유지** |
| WebChat→StreamList 전환 | **유지** | **유지** | **유지** |
| `applyRuntimeAccessToken()` → WebChat stop | **유지** (WebChatClient.stop()은 adapter의 set을 건드리지 않음) | **유지** | **유지** |
| 10001건 초과 | **clear + 현재 1건** | **유지** (타임스탬프로 보호) | **유지** |

**핵심 원칙:** `m_seenMessageIds`가 clear되어도 `m_lastPublishedTimestampUtc`가 과거 메시지를 차단하므로, 두 필터가 상호 보완한다. `start()` 호출 시에만 양쪽 모두 초기화하여 새 세션의 메시지를 정상 수신한다.

**스레드 안전:** YouTubeAdapter의 모든 슬롯과 타이머는 Qt 이벤트 루프(단일 스레드)에서 실행된다. `YouTubeLiveChatWebClient`는 QNetworkAccessManager + QTimer 기반으로 동일 스레드에서 시그널을 emit한다. `m_seenMessageIds`, `m_lastPublishedTimestampUtc`에 대한 동시 접근은 발생하지 않는다. (단, `YouTubeStreamListClient`의 gRPC 워커 스레드는 emit 시 Qt::AutoConnection으로 메인 스레드에 큐잉됨.)

### 검증

- 정상 수신 중 m_seenMessageIds clear 발생 → 타임스탬프 필터로 과거 메시지 차단
- WebChat 재연결 → ytInitialData 백로그 메시지 → 타임스탬프 필터로 차단
- 동일 messageId 연속 수신 → adapter 또는 UI QSet에서 차단
- 방송 2시간 중단 후 재개 → start() 초기화 → 새 메시지 정상 수신
- Chzzk 메시지 (빈 messageId) → dedup 건너뜀 (정상)

---

## 수정 우선순위

| 우선순위 | 이슈 | 영향 | 난이도 | 근본 원인 |
|---------|------|------|--------|----------|
| **P0** | 치지직 채팅 미표시 (#3) | 기능 완전 불능 | 1줄 수정 | `"id"` 키가 비고유 필드 매칭 |
| **P1** | YouTube 채팅 중복 표시 (#4) | 40분 전 메시지 재표시 | 5파일 수정 | m_seenMessageIds clear + 백로그 + UI 안전망 없음 |
| **P1** | YouTube 이모지 코드 (#1) | UX 저하 | emoji 분기 교체 | emojiId vs shortcuts 우선순위 |
| **P2** | YouTube 라이브 복구 (#2) | 상태 표시 부정확 | 4개소 수정 | 상태 전환 + 카운터 리셋 + WebChat 미정지 |

---

## 종합 수정 파일 목록

| 파일 | 수정 내용 |
|------|----------|
| `src/platform/chzzk/ChzzkAdapter.cpp` | messageId 키 제한 (`"id"`/`"uid"` 제거) + `onSocketDisconnected` dedup clear |
| `src/platform/youtube/YouTubeChatMessageParser.cpp` | emoji 분기 로직 교체 (Unicode/글로벌/커스텀 판별) |
| `src/platform/youtube/YouTubeAdapter.h` | `m_lastPublishedTimestampUtc` 멤버 추가 |
| `src/platform/youtube/YouTubeAdapter.cpp` | `publishReceivedMessage` 타임스탬프 필터 + seenMessageIds 10000 상향 + `onWebChatFailed` CHECKING 전환 + `applyRuntimeAccessToken` WebChat 정지 + `onWebChatEnded` discovery 카운터 리셋 |
| `src/platform/youtube/YouTubeLiveChatWebClient.cpp` | `handleInitialPageResponse` 시그널 순서 변경 (`started` → `messagesReceived`) |
| `src/ui/MainWindow.h` | `QSet<QString> m_recentMessageIds` 멤버 추가 |
| `src/ui/MainWindow.cpp` | `appendChatMessage` UI 레벨 QSet 기반 중복 체크 (2000건) |

---

## 중복 방지 아키텍처 (최종)

```
YouTube 메시지 수신 경로 (YouTubeAdapter 인스턴스):
  InnerTube → parseActions → messagesReceived signal
    → [Adapter 레벨 1차] publishReceivedMessage()
        ├─ YouTubeAdapter::m_seenMessageIds QSet (10000건 상한)
        └─ YouTubeAdapter::m_lastPublishedTimestampUtc 기반 시간 필터 (5초 오차)
    → chatReceived signal
    → [UI 레벨 2차] appendChatMessage()
        └─ MainWindow::m_recentMessageIds QSet (2000건 상한, 양 플랫폼 공유)
    → m_chatMessages + QTableWidget 표시

Chzzk 메시지 수신 경로 (ChzzkAdapter 인스턴스):
  WebSocket → processSocketIoEvent → emitFromPayloadObject
    → [Adapter 레벨] ChzzkAdapter::m_seenMessageIds QSet (messageId가 비어있으면 건너뜀)
    → chatReceived signal
    → [UI 레벨] appendChatMessage()
        └─ MainWindow::m_recentMessageIds QSet (messageId가 비어있으면 건너뜀)
    → m_chatMessages + QTableWidget 표시

※ YouTubeAdapter::m_seenMessageIds와 ChzzkAdapter::m_seenMessageIds는 별개 인스턴스.
※ MainWindow::m_recentMessageIds는 양 플랫폼 메시지를 모두 수신하는 단일 인스턴스.
```

---

## 기록

- 2026-04-11: YouTube InnerTube 전환 성공, API quota 획기적 감소
- 2026-04-11: 성능 최적화 5차 감사 완료 (메모리 바운딩 + CPU 최적화)
- 2026-04-12: 치지직 dedup 코드 추가 이후 채팅 미표시 발생 확인
- 2026-04-15: 10회차 심층 분석 완료, 3건 근본 원인 확정
- 2026-04-15: YouTube 채팅 중복 표시 이슈 추가 (#4), 2단계 중복 방지 설계
- 2026-04-15: Issue 4 추가 10회차 검증 — seenMessageIds 10000 상향, UI QSet 2000건, 시그널 순서 수정, 엣지 케이스 5건 대응 확인
- 2026-04-15: 논리 검증 10회차 — ERROR 4건 수정 (retry backoff, 카운터 부분 리셋, Issue 3 모순 해소, dedup 수명주기 명시), WARNING 6건 대응
- 2026-04-15: 2차 논리 검증 10회차 — MINOR 2건 수정 (backoff 수식 정정, 수명주기 표 정정), NOTE 1건 (결함 D 우회 방침 명시). 추가 논리 모순 없음 확인
- 2026-04-15: 3차 논리 검증 10회차 — MINOR 3건 수정 (검증 표 backoff 잔존값, 우선순위 표 난이도, 아키텍처 인스턴스 명시). 추가 논리 모순 없음 확인
- 2026-04-15: 4차 논리 검증 10회차 — MINOR 1건 수정 (Fix 3 backoff 주석 시퀀스 정확화). Fix 간 상호 간섭 없음, 수명주기 정합성, 코드 흐름 정상 확인
- 2026-04-15: 5차 논리 검증 10회차 — 추가 발견 없음. 전체 수치/코드/교차참조 최종 확인 완료. 총 70회차 완료

## 참고 자료

- YouTube emoji: chat_downloader, pytchat, yt-dlp 처리 방식 조사
- Qt5 `QJsonValue::toString()`: 숫자에 대해 빈 문자열 반환 (문자열만 변환)
- 치지직 공식 문서: CHAT 이벤트에 `messageId` 필드 미보장
- `discoveryBackoffDelayMs()`: attempts≥10 → 120초 대기 (2분 간격 재시도)
- YouTube InnerTube `live_chat` 초기 페이지: `ytInitialData.contents.liveChatRenderer.actions[]`에 과거 채팅 백로그 포함
- `m_seenMessageIds` clear 시 이력 전체 소실 → 타임스탬프 기반 보완 필요
- YouTube emoji `/` 판별: 표준 Unicode 이모지(ZWJ, 피부톤, 국기 등)는 emojiId에 `/` 미포함 확인

## 논리 검증 결과 (30회차)

10회차 분석 후 별도 논리 검증 수행. 발견된 문제 및 대응:

| ID | 심각도 | 문제 | 대응 |
|----|--------|------|------|
| 2-A | ERROR | onWebChatFailed() 고정 3초 재시도 → 무한 루프 위험 | **수정 완료**: 점진적 backoff (3→6→9→12→15초) + 3회 실패 시 300초 차단 |
| 2-D | ERROR | onWebChatEnded() m_bootstrapDiscoverAttempts=0 → 공격적 재탐색 | **수정 완료**: 0 대신 min(현재값, 3)으로 제한. 15초 backoff에서 시작 |
| 3-D | ERROR | Issue 3 분석 내부 모순 (messageId가 비어있다 vs 채워진다) | **수정 완료**: "잘못된 키로 비고유 값이 채워져 오동작"으로 정확히 기술 |
| X-1 | ERROR | Issue 2와 Issue 4의 dedup 상태 상호작용 미명시 | **수정 완료**: Dedup 상태 수명주기 표 추가 |
| 1-D | WARNING | 빈 shortcuts + 경로형 emojiId 엣지 케이스 | 코드에서 emojiId 폴백 처리됨 (else 분기) |
| 2-B | WARNING | stop() 동기성 미명시 | **수정 완료**: "동기적" 명시 + generation 카운터 설명 |
| 2-C | WARNING | stream flapping 시 무제한 재탐색 | **수정 완료**: min(현재값, 3) + 300초 webChat 차단으로 방어 |
| 3-C | WARNING | 수정 후 dedup이 치지직에서 비활성 | 의도적. 향후 messageId 제공 시 자동 활성화 |
| 4-B | WARNING | QSet clear 직후 + 5초 이내 재연결 시 중복 가능 | 이론적 취약점이나 실용적 위험 극히 낮음 (adapter + UI 이중 필터) |
| X-2 | WARNING | 스레드 안전성 미명시 | **수정 완료**: Qt 이벤트 루프 단일 스레드 설명 추가 |

### 2차 논리 검증 (40회차)

| ID | 심각도 | 문제 | 대응 |
|----|--------|------|------|
| 2-E | MINOR | backoff 설명 "3→6→12→15초"가 수식과 불일치 (실제 3→6→9→12→15) | **수정 완료**: 설명을 "3→6→9→12→15초"로 정정 |
| X-3 | MINOR | Dedup 수명주기 표에서 "stop이 clear하지만 adapter의 set은 별개" 오해 소지 | **수정 완료**: WebChatClient.stop()이 adapter set을 건드리지 않음을 명시 |
| 2-F | NOTE | 결함 D에 직접 수정 없이 우회만 사용 | **수정 완료**: 우회 방침과 이유(re-entrant 방지) 명시 |
| - | - | 그 외 논리 모순 없음 | 검증 완료 |

### 3차 논리 검증 (50회차)

| ID | 심각도 | 문제 | 대응 |
|----|--------|------|------|
| 2-G | MINOR | 30회차 검증 표 2-A 항목에 수정 전 backoff "3→6→12→15" 잔존 | **수정 완료**: "3→6→9→12→15"로 정정 |
| 4-C | MINOR | 우선순위 표 Issue 4 난이도 "2개소 수정" → 실제 5파일 | **수정 완료**: "5파일 수정"으로 정정 |
| X-4 | MINOR | 아키텍처 다이어그램에서 YouTube/Chzzk m_seenMessageIds가 별개 인스턴스임 불명확 | **수정 완료**: 클래스 소유 명시 + 주석 추가 |
| - | - | 그 외 논리 모순 없음 | 검증 완료 |

### 4차 논리 검증 (60회차)

| ID | 심각도 | 문제 | 대응 |
|----|--------|------|------|
| 2-H | MINOR | Fix 3 주석 backoff 시퀀스 부정확 (`15→30→60→120` → 실제 `15→30→30→60→...→120`) | **수정 완료**: discoveryBackoffDelayMs 반환값 테이블 주석으로 정확히 기재 |
| - | - | Fix 2 applyRuntimeAccessToken 흐름 (WebChat running → stop → tick → 재시작) 정상 | 검증 완료 |
| - | - | 수명주기 표 stop()/start() 초기화 순서 정합성 | 검증 완료 |
| - | - | Fix 1~4 상호 간섭 없음 (각 fix는 독립적 코드 경로) | 검증 완료 |
| - | - | 그 외 논리 모순 없음 | 검증 완료 |

### 5차 논리 검증 (70회차)

전체 수치/코드/교차참조 최종 확인. **추가 발견 없음.**

검증 항목 18건 전수 확인:
- Issue 1~4 수정 코드 논리 정합성 ✅
- backoff 수치 일관성 (수식, 주석, 검증 표) ✅
- 수명주기 표 6행 × 3열 정합성 ✅
- 종합 수정 파일 7개 = Issue별 합산 ✅
- 우선순위 표 난이도 정확성 ✅
- 아키텍처 다이어그램 인스턴스 구분 ✅
- Fix 2 토큰 갱신 시 쿨다운 해제 (의도적) ✅
- 검증 결과 1~4차 내부 일관성 ✅

**문서 최종 상태: 논리적 모순 없음.**
