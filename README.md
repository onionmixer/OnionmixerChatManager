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

인증 성공 후 자동 채움:
- YouTube `authorization_code` 성공 시 `channels?mine=true` 조회로 `channel_id`/`channel_name`/`channel handle(account_label)` 자동 반영
- CHZZK `authorization_code` 성공 시 `open/v1/users/me` 조회로 `channel_id`/`channel_name` 자동 반영
- 자동 동기화는 기존 값이 있어도 최신 조회값으로 기본 덮어쓰기 합니다.
- `Token Refresh` 성공 시에도 동일한 프로필 동기화를 다시 수행하여 누락된 채널 필드를 보완합니다.

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

## 7) 주의

- CHZZK endpoint는 아래 값을 사용해야 합니다.
  - `auth_endpoint=https://chzzk.naver.com/account-interlock`
  - `token_endpoint=https://openapi.chzzk.naver.com/auth/v1/token`
- `config/tokens.ini`는 개발 편의용입니다. 운영에서는 보안 저장소(QtKeychain 등) 사용을 권장합니다.

## 8) 라이브 상태 감지

- 메인 `Actions` 패널의 토큰 상태 박스 아래에 플랫폼별 라이브 상태(YouTube/CHZZK)를 표시합니다.
- 라이브 상태는 3초 주기로 갱신됩니다.
- 액션 버튼은 `플랫폼 연결됨 + 라이브 ONLINE`일 때만 활성화됩니다.

## 9) 채팅 수신 동작 (현재)

- YouTube: 실제 `liveBroadcasts` + `liveChat/messages` API polling으로 채팅 수신
- CHZZK: `sessions/auth` + Socket 연결 + `sessions/events/subscribe/chat` 흐름으로 실채팅 수신 시도
- 개발 테스트용 mock 메시지는 환경변수 `BOTMANAGER_ENABLE_MOCK_CHAT=1`일 때만 생성

## 10) YouTube 연결 상태 해석

- `YouTube: CONNECTED`
  - 플랫폼 연결 완료 + `liveChatId` 확보 완료 상태
- `YouTube: CONNECTED_NO_LIVECHAT`
  - 플랫폼 연결은 되었지만 아직 활성 `liveChatId`를 찾지 못한 상태
  - 방송 시작 직후/전환 구간에서 잠시 나타날 수 있으며, 백그라운드로 자동 재탐색합니다.

`CONNECTED_NO_LIVECHAT`에서의 동작:
- 채팅 수신은 대기 상태
- `liveChatId` 확보 시 자동으로 `CONNECTED`로 전환
- YouTube quota/rate 오류 시 즉시 연결 실패로 전환하지 않고 재시도 루프를 유지

## 11) CHZZK 연결 상태 해석

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

## 12) 상태 분리 원칙 (중요)

토큰 상태와 라이브 상태는 서로 독립입니다.

- 토큰 상태 박스(YouTube / CHZZK):
  - 판단 기준: 토큰 유효성 + 인증 진행 상태 + 플랫폼 연결 상태
  - 판단 제외: 라이브 API 실패/오프라인/쿼터/방송 상태
- 라이브 상태 박스(YouTube Live / CHZZK Live):
  - 판단 기준: 라이브 탐지 API 결과만 사용
  - 토큰 미사용/만료 시: `ERROR`가 아니라 `UNKNOWN`(확인 스킵)로 처리

즉, 라이브 체크 실패 때문에 토큰 박스가 `토큰비정상`으로 바뀌면 버그입니다.

## 13) 다국어 지원

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
