# Changelog

All notable changes to this project are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- **YouTube 시청자 카운터 안정화** — 라벨 깜박임("—" flicker) 완화. YouTube Data API v3 `videos.list?part=liveStreamingDetails`의 `concurrentViewers` 필드는 저시청자·라이브 경계 구간에서 간헐적으로 누락되는데, 기존에는 결측 1회에 즉시 placeholder로 전환했음. 4가지 개선 혼합:
  - **Miss-grace 히스테리시스** — 연속 결측 `kYouTubeViewerMissGraceCount=3` (≈45s) 미만이면 마지막 유효값 유지
  - **Stale tooltip** — 라벨 tooltip에 API 설명·fresh/stale 경과초·miss streak·누적 miss rate% 표기
  - **State-transition reply 수용** — timer stopped 체크 대신 `LiveBroadcastState != OFFLINE` 체크로 교체. ONLINE↔UNKNOWN↔ONLINE 전이 중 도착한 응답 drop 방지
  - **진단 카운터** — `m_youtubeViewerMissTotal`·`m_youtubeViewerTotalTicks` 누적 (tooltip 노출)
  - OFFLINE(명확한 종료)은 hysteresis 우회하여 즉시 리셋, UNKNOWN/CHECKING/ERROR(일시적)만 grace 대상
  - 관련 파일: `src/core/Constants.h` (Viewers namespace), `src/ui/MainWindow.{h,cpp}`

## [0.1.0] — 2026-04-18

First public pre-release. Project moves from personal pre-release prototyping to versioned public distribution.

### Added

- **BroadChat 서버 (§4·§5)** — 외부 `OnionmixerBroadChatClient` 앱이 접속할 수 있는 TCP 서버 모듈 (`BroadChatServer`, `ClientSession`). 동일 PC 또는 LAN/VPN 원격 PC에서 방송 오버레이 창을 독립 실행 가능.
- **프로토콜 v1 (§6)** — NDJSON envelope (`v`·`type`·`id`·`t`·`data`) 기반 `server_hello`·`client_hello`·`chat`·`viewers`·`platform_status`·`history_chunk`·`emoji_image`·`request_history`·`request_emoji`·`ping`/`pong`·`bye`. 라인당 1 MB 상한, envelope 단일 상수 공유.
- **공유 정적 라이브러리 `onionmixer_chat_shared`** — 메인 앱과 BroadChatClient가 공통 사용하는 UI·모델·캐시·프로토콜 코드 단일 진원지 (`ChatMessageModel`·`ChatBubbleDelegate`·`EmojiTextDocument`·`ViewerCountStyle`·`BroadcastChatWindow`·`EmojiImageCache`·`BroadChat*`).
- **인증 (§5.1·§20.8)** — 선택 `auth_token` (UUID v4 권장). 서버 ini `[broadchat] auth_token` 설정 시 `client_hello.data.authToken` 필수. 불일치는 `bye reason=auth_failed`로 close + 재연결 중단.
- **설정 창 (General 탭)** — `BroadChat Port` QSpinBox + `BroadChat Auth Token` 입력 (Password echo) + Generate(UUID v4)/Copy/Clear 버튼. `tcp_bind=0.0.0.0` + 빈 토큰 조합 Apply 시 modal 보안 경고.
- **상태바 배지** — `BroadChat: listening :47123` / `off` / `error` + `clients: N` 영구 라벨. `listeningChanged`·`clientCountChanged`·`listenFailed` 시그널 구독.
- **BroadChatClient 앱** — 별도 실행파일. 우클릭 메뉴 `세팅`·`종료`, `ClientConfigDialog`, Settings 변경 시 자동 재연결, bye reason별 재연결 백오프 매트릭스.
- **로깅 카테고리** — `broadchat`·`broadchat.warn`·`broadchat.err`·`broadchat.trace`. `QT_LOGGING_RULES` 환경변수로 런타임 제어.
- **CLI `--version`·`--help`** — 메인 앱·클라이언트 양쪽 지원. QApplication 생성 전 조기 exit. git SHA + build date 포함.
- **i18n** — 3개 로케일 (ko_KR·en_US·ja_JP). v0.1.0 기준 신규 45 string 추가. `AppLanguage` helper가 공유 lib에 위치 (메인·클라 모두 사용). 언어 결정 4단계: CLI `--language` > `config.ini [app] language` > `QLocale::system()` > 소스 원본.
- **BroadChatClient i18n** — `/usr/share/OnionmixerBroadChatClient/translations/*.qm` 자동 탐색. `--language` CLI 옵션 지원.

### Changed

- **Transport**: 초기 설계의 QLocalSocket에서 **QTcpSocket 단일**로 전환 (v18~v19). 동일 PC와 원격 PC를 같은 경로로 커버. `BroadChatEndpoint::serverName()` 레거시 API 제거, `normalizeBindAddress`·`normalizePort`·`kDefaultTcpPort=47123`·`kDefaultBindAddress="127.0.0.1"` helper 체계.
- **EmojiImageCache**: 500 상한 초과 시 전체 clear → **LRU eviction**으로 교체 (v24 D4). get/contains 접근 시 move-to-front, tail evict.
- **ini 유효성 검증 강화** — bind 주소·포트 범위·auth_token ASCII 규약·max_clients 1~100·duplicate_kick_target enum 모두 로드 시 검증·정규화 (v24 D1).
- **listenFailed UX** — 상태바 latch에 `QMessageBox::warning` 모달 추가 (v24 D2).
- **serverVersion CMake 자동 주입** — `BroadChatVersion.h.in` → `configure_file` → `kAppVersion`·`kGitCommitShort`·`kBuildDate`. 하드코딩 제거 (v24 D6).

### Security

- `config/app.ini`·`config/tokens.ini` **.gitignore 추가 및 추적 제거** (v25 C1). 과거 커밋에 실제 토큰 값은 없었음 (v25 C2 검증).
- `config/app.ini` 저장 시 파일 권한 **0600** 자동 설정 (v21-γ-8).
- auth_token 로그 마스킹 — 어떤 로그 레벨에도 토큰 평문 노출 금지 (v21-γ-9).

### Not Adopted (의도적 비도입)

- **TLS·암호화** (v23 영구 확정) — 프로토콜은 평문 TCP. 암호화 요구는 WireGuard·Tailscale 같은 외부 VPN 터널링 권장. QSslSocket 전환 계획 없음.
- **Challenge-response·HMAC 인증** (v23) — 단순 공유 토큰만.
- **WebSocket**·**OAuth·SSO**·**메시지 persistence**·**다중 auth_token rolling** — v1 범위 외.
- **로그 category 전면 migrate** (v25) — BroadChat 계열만 category 사용. 기존 `[CHAT]`·`[LIVE]`·`[TOKEN-OK]` 수동 prefix는 현 상태 유지.
- **GUI ↔ ini 외부 변경 감지** (`QFileSystemWatcher`) — 재시작 기반 유지.

### Known Limitations

- Linux/X11 주 타겟. Windows/macOS는 검증 대상 외 (이론적 동작).
- `tcp_bind=0.0.0.0`으로 외부 노출 시 VPN 또는 방화벽 규칙 필수 (앱 내장 방어 없음).
- BroadChat 기능은 `ONIONMIXERCHATMANAGER_ENABLE_BROADCHAT_SERVER=OFF` 컴파일 옵션으로 제거 가능 (§16.5).

### Upgrade Notes (첫 릴리즈)

해당 없음 — 첫 공개 태그. 이후 breaking change는 SemVer에 따라 MAJOR bump + `ProtocolVersion` 정수 증가를 동반.

[Unreleased]: https://github.com/onionmixer/OnionmixerChatManager/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/onionmixer/OnionmixerChatManager/releases/tag/v0.1.0
