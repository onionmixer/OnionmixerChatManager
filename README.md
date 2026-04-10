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
