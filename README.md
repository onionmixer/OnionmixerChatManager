# BotManager Qt5

Qt5 기반 YouTube + NAVER CHZZK 통합 채팅/운영 도구 프로젝트입니다.

- 플랫폼별 설정 분리 (`config/app.ini`)
- 토큰 저장 분리 (`config/tokens.ini`, 개발용)
- Configuration 창에서 토큰 갱신/재인증
- Connect/Disconnect 토글
- Security 탭 Token Audit 제공

## 1) 빌드

요구사항:
- CMake 3.16+
- Qt5 (`Core`, `Widgets`, `Network`)
- C++17 컴파일러

```bash
cmake -S . -B build
cmake --build build -j4
```

실행:

```bash
./build/BotManagerQt5
```

## 2) 기본 설정 파일

기본 경로: `config/app.ini`

```ini
[youtube]
enabled=false
client_id=
redirect_uri=http://127.0.0.1:18080/youtube/callback
auth_endpoint=https://accounts.google.com/o/oauth2/v2/auth
token_endpoint=https://oauth2.googleapis.com/token
scope=https://www.googleapis.com/auth/youtube.readonly https://www.googleapis.com/auth/youtube.force-ssl

[chzzk]
enabled=false
client_id=
client_secret=
redirect_uri=http://127.0.0.1:18081/chzzk/callback
auth_endpoint=https://chzzk.naver.com/account-interlock
token_endpoint=https://openapi.chzzk.naver.com/auth/v1/token
```

## 3) YouTube client_id에 들어가야 하는 값

YouTube `client_id`에는 반드시 **Google OAuth 2.0 Client ID**가 들어가야 합니다.

정상 예시:
- `123456789012-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.apps.googleusercontent.com`

허용 도메인 기준:
- `*.googleusercontent.com`

잘못된 예시:
- `onionmixer.iphone` (iOS bundle id / package id)
- `client_secret` 값
- API key 값

## 4) YouTube 인증 오류 대응

### A. `Invalid YouTube client_id format parsed=...`
의미:
- 앱이 브라우저 호출 전에 `client_id` 형식 검증에서 차단한 상태입니다.

조치:
1. Google Cloud Console -> `APIs 및 서비스` -> `사용자 인증 정보`
2. OAuth 2.0 Client의 **Client ID**를 복사
3. Configuration -> YouTube -> `Client ID`에 입력
4. `Apply` 후 `Re-Auth Browser` 재시도

### B. 브라우저에서 `오류 401: invalid_client`
의미:
- Google 측에서 해당 `client_id`를 유효한 OAuth client로 인식하지 못함

주요 원인:
- 다른 프로젝트의 값 복사
- bundle id / package id를 잘못 입력
- 클라이언트 유형/설정 불일치

필수 확인:
- YouTube `client_id`가 실제 OAuth Client ID인지
- `redirect_uri`가 정확히 `http://127.0.0.1:18080/youtube/callback`인지
- Google Console에 동일한 redirect URI가 등록되어 있는지

## 5) Configuration 창 사용 흐름

1. `Configuration` 열기
2. YouTube/CHZZK 필드 입력
3. `Apply`
4. 토큰 액션 수행
- `Token Refresh` (silent refresh)
- `Re-Auth Browser` (브라우저 기반 재인증)
- `Delete Token`
5. 메인 화면 `Connect` 클릭

인증 성공 후 프로필 동기화:
- YouTube `authorization_code`/`refresh_token` 성공 시 `channels?mine=true` 조회로 봇 계정의 `channel_id`/`channel_name`/`handle` 확인
- CHZZK `authorization_code`/`refresh_token` 성공 시 `open/v1/users/me` 조회로 봇 계정 확인
- 프로필 동기화 결과는 **런타임 캐시에만 저장**되며, INI 설정(`config/app.ini`)은 변경하지 않습니다.
- INI의 `channel_id`/`channel_name`/`account_label`은 Configuration 창에서 수동 Apply한 경우에만 변경됩니다.
- 봇 계정과 방송 채널이 다른 경우, INI에 방송 채널의 `account_label`(예: `@onionmixer`)을 설정하면 해당 채널 기준으로 라이브 탐색합니다.

## 6) Security 탭

- Vault 상태 표시
- Token Audit 테이블
- Result/Platform/Action 색상 태그
- Detail 컬럼 동적 elide + 툴팁
- 행 더블클릭 상세 팝업
- `Copy Summary`, `Copy Detail`, `Copy All`

`Copy All` 템플릿에는 아래 메타가 포함됩니다.
- `copied_at_utc` (ISO8601 UTC)
- `copied_at_local` (ISO8601 local)

## 7) 채팅 표시 설정

Configuration → General 탭에서 채팅 대화창의 표시를 설정할 수 있습니다.

| 설정 | 위젯 | 범위 | 기본값 | INI 키 |
|------|------|------|--------|--------|
| Chat Font | QFontComboBox | 시스템 폰트 목록 | 시스템 기본 | `chat_font_family` |
| Chat Font Size | QSpinBox | 8~24px | 11px | `chat_font_size` |
| Chat Font Style | QCheckBox ×2 | Bold / Italic | 둘 다 OFF | `chat_font_bold`, `chat_font_italic` |
| Chat Line Spacing | QSpinBox | 0~20px | 3px | `chat_line_spacing` |

- ���정 변경 시 **Chat Preview** 영역에서 실시간으로 미리보기 가능 (Apply 불필요)
- Apply 클릭 시 메인 채팅창에 즉시 반영 (기존 메시지도 재렌더링)
- 플랫폼 badge 아이콘 크기는 폰트 크기에 비례하여 자동 조절 (`fontSize + 2`)
- 닉네임 오른쪽에 타임스탬프(`yyyy-MM-dd HH:mm:ss`) 표시
- 크로스 플랫폼 폰트 호환: INI에 저장된 폰트가 현 시스템에 없으면 시스템 기본 폰트로 폴백

## 7-1) 설정 파일 경로

config 디렉토리 해석 우선순위:

1. CLI 인수: `--config-dir /path/to/config/`
2. 환경변수: `BOTMANAGER_CONFIG_DIR=/path/to/config/`
3. 기본값: `{실행파일경로}/config/`

```bash
# 기본 (실행 파일 옆 config/ 사용)
./BotManagerQt5

# CLI로 config 디렉토리 지정
./BotManagerQt5 --config-dir /home/user/myconfig

# 환경변수로 지정
BOTMANAGER_CONFIG_DIR=/tmp/test ./BotManagerQt5
```

config 디렉토리 안에 `app.ini`와 `tokens.ini`가 위치합니다.

## 7-2) 메인 윈도우 레이아웃

메인 윈도우는 QSplitter로 영역 크기를 드래그 조절할 수 있습니다.

```
┌─ 버튼 바 (Connect/Disconnect, 설정 등) ─── 고정 ────────────┐
├─ 상태 바 (YouTube/CHZZK 상태) ──────────── 고정 ────────────┤
├─ ═══════════ 좌우 Splitter ��══════════════════════════════ ─┤
│  │ 채팅 테이블 (3)  ║  액션 패널 (2)  │  ← 좌우 드래그     │
├─ ═══════════ 상하 Splitter ═════���═��═══════════════════════ ─┤
│  │ 이벤트 로그                         │  ← 상하 드래그     │
├─ 메시지 입력 + 전송 버튼 ──────────────── 고정 (68px) ──────┤
└──────────────────────────────────────────────────────────────┘
```

- 메시지 입력창과 전송 버튼은 항상 고정 크기로 하단에 유지됩니다.
- 상하 Splitter는 채팅 영역 ↔ 이벤트 로그만 제어합니다.

## 8) 주의

- CHZZK endpoint는 아래 값을 사용해야 합니다.
  - `auth_endpoint=https://chzzk.naver.com/account-interlock`
  - `token_endpoint=https://openapi.chzzk.naver.com/auth/v1/token`
- `config/tokens.ini`는 개발 편의용입니다. 운영에서는 보안 저장소(QtKeychain 등) 사용을 권장합니다.

## 9) 라이브 상태 감지

- 메인 `Actions` 패널의 토큰 상태 박스 아래에 플랫폼별 라이브 상태(YouTube/CHZZK)를 표시합니다.
- 메인 UI의 live probe timer는 3초 주기로 동작합니다.
- 실제 플랫폼 조회 주기는 서로 다릅니다.
  - YouTube: `Connect` 이후 adapter 내부 상태 이벤트 기반
  - CHZZK: `live-detail` API 10초 주기
- 액션 버튼은 `플랫폼 연결됨 + 라이브 ONLINE`일 때만 활성화됩니다.

## 10) 채팅 수신 동작

- YouTube:
  - **1차 수신 경로 (권장)**: YouTube InnerTube 웹 스크래핑 (`YouTubeLiveChatWebClient`)
    - `https://www.youtube.com/live_chat?v={videoId}&is_popout=1` 페이지에서 continuation 기반 폴링
    - Google Cloud API quota를 소모하지 않음
    - OBS Studio와 동일한 YouTube 프론트엔드 내부 API 활용
  - 2차 폴백: `liveChatMessages.streamList` gRPC server-streaming (quota 소모)
  - 3차 폴백: `liveChatMessages.list` REST API 폴링 (quota 소모 많음)
  - `liveChatId` 탐색:
    - 수동 `Live Video URL / ID`
    - `@handle/live`, `@handle/streams`, `embed/live_stream?channel=...` web resolver (180초 주기 재시도)
    - `liveBroadcasts?mine=true&broadcastStatus=active`
    - 봇 계정 ≠ 방송 채널인 경우 `search?channelId={id}&eventType=live` 폴백
    - `videos.liveStreamingDetails.activeLiveChatId` 로 최종 검증
  - 메시지 전송/삭제/제재만 YouTube Data API v3 사용 (quota 소모 최소화)
- CHZZK: `sessions/auth` + Socket 연결 + `sessions/events/subscribe/chat` 흐름으로 실채팅 수신 시도
- 개발 테스트용 mock 메시지는 환경변수 `BOTMANAGER_ENABLE_MOCK_CHAT=1`일 때만 생성

`streamList` 빌드/실행 참고:
- 이 바이너리가 `streamList` 를 포함하면 이벤트 로그에 `INFO_YT_TRANSPORT_STREAMLIST_BUILD` 가 출력됩니다.
- 의존성 미포함 빌드면 `INFO_YT_TRANSPORT_POLLING_ONLY_BUILD` 가 출력됩니다.
- Ubuntu 22.04 개발 패키지 예:
  - `libprotobuf-dev`
  - `protobuf-compiler`
  - `libgrpc-dev`
  - `libgrpc++-dev`
  - `protobuf-compiler-grpc`
  - `grpc-proto`

YouTube 수동 우회:
- `Configuration -> YouTube -> Live Video URL / ID` 에 현재 송출 중인 YouTube 라이브의 watch URL 또는 `videoId` 를 넣을 수 있습니다.
- 이 값이 있으면 `connect` 시 자동 탐색보다 우선해서 `videos.liveStreamingDetails.activeLiveChatId` 를 직접 조회합니다.
- 자동 탐색(`liveBroadcasts/search/uploads`)이 불안정한 채널에서는 이 경로가 가장 확실한 우회 수단입니다.

YouTube visibility/공개 여부 진단:
- 앱이 `videoId` 를 이미 알고 있을 때는 `videos.list(part=liveStreamingDetails,status)` 로 `privacyStatus` 를 확인할 수 있습니다.
- 따라서 수동 `Live Video URL / ID` 를 넣었거나, 자동 탐색이 후보 `videoId` 를 확보한 경우에는 이벤트 로그에 `INFO_YT_VIDEO_PRIVACY_STATUS` 가 출력될 수 있습니다.
- 반대로 자동 탐색 단계에서 `videoId` 자체를 확보하지 못하면, 앱은 라이브의 `public/unlisted/private` 여부를 판정할 수 없습니다.
- 이유: handle/channel/RSS 등의 공개 surface 에 노출되지 않는 live 는 `videoId` 를 알기 전까지는 공식 `videos.list(...status)` 조회 대상으로 확정할 수 없기 때문입니다.
- 특히 `unlisted/private` 라이브는 채널 공개 surface 에 나타나지 않을 수 있으므로, 이 경우 자동 발견보다 `Live Video URL / ID` 수동 입력이 우선입니다.

## 11) YouTube 연결 상태 해석

- `YouTube: CONNECTED`
  - 플랫폼 연결 완료 + `liveChatId` 확보 완료 상태
- `YouTube: CONNECTED_NO_LIVECHAT`
  - 플랫폼 연결은 되었지만 아직 활성 `liveChatId`를 찾지 못한 상태
  - 방송 시작 직후/전환 구간에서 잠시 나타날 수 있으며, 백그라운드로 자동 재탐색합니다.

`CONNECTED_NO_LIVECHAT`에서의 동작:
- 채팅 수신은 대기 상태
- `liveChatId` 확보 시 자동으로 `CONNECTED`로 전환
- YouTube quota/rate 오류 시 즉시 연결 실패로 전환하지 않고 재시도 루프를 유지
- `streamList` quota/rate 오류(`RESOURCE_EXHAUSTED`) 시에는 즉시 polling fallback으로 내리지 않고 300초 backoff를 우선 적용

## 12) CHZZK 연결 상태 해석

- `CHZZK: CONNECTED`
  - 소켓 연결 + 세션 구독 완료(채팅 수신 준비 완료)
- `CHZZK: CONNECTED_NO_SESSIONKEY`
  - 소켓은 연결됐지만 `sessionKey`가 아직 준비되지 않은 상태
- `CHZZK: CONNECTED_NO_SUBSCRIBE`
  - `sessionKey`는 확보됐지만 CHAT 구독 완료 전 상태

보통 정상 흐름:
- `CONNECTED_NO_SESSIONKEY` (또는 바로 `CONNECTED_NO_SUBSCRIBE`)
- `CONNECTED_NO_SUBSCRIBE`
- `CONNECTED`

## 13) 상태 분리 원칙 (중요)

토큰 상태와 라이브 상태는 서로 독립입니다.

- 토큰 상태 박스(YouTube / CHZZK):
  - 판단 기준: 토큰 유효성 + 인증 진행 상태 + 플랫폼 연결 상태
  - 판단 제외: 라이브 API 실패/오프라인/쿼터/방송 상태
- 라이브 상태 박스(YouTube Live / CHZZK Live):
  - 판단 기준: 라이브 탐지 API 결과만 사용
  - 토큰 미사용/만료 시: `ERROR`가 아니라 `UNKNOWN`(확인 스킵)로 처리

즉, 라이브 체크 실패 때문에 토큰 박스가 `토큰비정상`으로 바뀌면 버그입니다.

## 13-1) 토큰 갱신 반영 원칙

- 토큰 refresh / re-auth 성공 시 새 access token은 즉시 adapter 런타임에 반영됩니다.
- YouTube:
  - 실행 중 `streamList` 가 열려 있으면 현재 stream을 중단하고 새 token으로 재연결합니다.
  - polling/discovery 상태이면 다음 요청부터 새 token을 사용합니다.
- CHZZK:
  - 새 token을 adapter 내부 런타임 상태에 즉시 반영합니다.
  - 기존 websocket/session 이 유지 중이면 연결을 강제로 끊지 않고, 이후 재인증/재세션 요청부터 새 token을 사용합니다.

## 14) 다국어 지원

- `Configuration -> General -> Language`의 값으로 UI 언어를 결정합니다.
- 현재 지원 언어:
  - `ko_KR`
  - `en_US`
  - `ja_JP`
- 번역 리소스 소스:
  - `translations/botmanager_ko_KR.ts`
  - `translations/botmanager_en_US.ts`
  - `translations/botmanager_ja_JP.ts`
- 빌드 산출물:
  - `build/translations/botmanager_ko_KR.qm`
  - `build/translations/botmanager_en_US.qm`
  - `build/translations/botmanager_ja_JP.qm`

동작 방식:
- 앱 시작 시 `config/app.ini`의 `[app]language`를 읽고 `QTranslator`를 로드합니다.
- 실행 중 `Language`를 바꾸고 `Apply`하면:
  - 메인 윈도우는 즉시 재번역됩니다.
  - `Configuration` / `ChatterList` 창은 재생성되어 새 언어를 반영합니다.
- 선택한 언어 파일에 번역 항목이 없는 문자열은 원문(소스 문자열)로 fallback 됩니다.

적용 범위:
- 번역됨:
  - 버튼, 라벨, 탭 제목, 그룹박스 제목
  - 테이블 헤더
  - 상태바 사용자 메시지
  - 토큰/OAuth/ActionExecutor의 앱 생성 안내 문구
  - OAuth 로컬 콜백 완료/실패 HTML
- 번역하지 않음:
  - 채팅 본문
  - 닉네임, 채널명, 방송 제목
  - 플랫폼 서버/API가 반환한 원문 오류 메시지
  - 이벤트 로그 prefix/code (`[WARN]`, `TRACE_*`, `INFO_*` 등)

중요 원칙:
- 내부 상태코드(`CONNECTED`, `FAILED`, `CONNECTED_NO_LIVECHAT` 등)는 계속 영문 코드로 유지합니다.
- 화면에 보여줄 때만 번역 문자열로 변환합니다.
- 서버에서 받은 원문 데이터는 사용자가 본래 의미를 확인할 수 있도록 그대로 표시합니다.
