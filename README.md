# OnionmixerChatManager Qt5

Qt5 기반 YouTube + NAVER CHZZK 통합 채팅/운영 도구입니다.

현재 코드 기준 주요 기능:
- 플랫폼별 설정 분리 (`app.ini`)
- 플랫폼별 토큰 저장 분리 (`tokens.ini`, 개발용 파일 저장소)
- Configuration 창에서 토큰 갱신 / 브라우저 재인증 / 토큰 삭제
- `Connect/Disconnect` 기반 연결 제어
- YouTube / CHZZK 통합 채팅 표시
- 메신저형 / 테이블형 채팅 뷰
- Security 탭 Token Audit
- 다국어 UI (`ko_KR`, `en_US`, `ja_JP`)

주의:
- 이 문서는 실행/설정 중심 안내입니다.

## 1. 요구 사항

- CMake 3.16+
- C++17 컴파일러
- Qt5
  - 필수: `Core`, `Widgets`, `Network`
  - 선택: `WebSockets`, `LinguistTools`, `Test`
- 선택 의존성
  - YouTube `streamList` 사용 시: Protobuf, gRPC, `protoc`, `grpc_cpp_plugin`

## 2. 빌드

### 2.1 Linux

기본 빌드 (Unix Makefiles 명시 — Ninja 미설치 환경에서도 안정 동작 보장):

```bash
cmake -S . -B build -G "Unix Makefiles"
cmake --build build -j4
```

> Generator 를 명시하지 않으면 Linux 의 CMake default 가 Unix Makefiles 이지만, 환경변수 `CMAKE_GENERATOR` 또는 `~/.cmake/` 설정이 우선되어 의도치 않게 Ninja 가 선택될 수 있다. 본 프로젝트는 Linux 빌드에 Unix Makefiles 만 검증·지원하므로 generator 를 명시 권장.

실행:

```bash
./build/OnionmixerChatManagerQt5
```

테스트(Qt5Test가 있을 때):

```bash
ctest --test-dir build --output-on-failure
```

현재 최소 테스트 타깃:
- `YouTubeUrlUtils`

### 2.2 Windows

세부 절차·검증 체크리스트는 `PLAN_COMPILE_WINDOWS.md` 참조.

권장 조합:
- Visual Studio 2022 (17) 또는 2026 (18) — Desktop development with C++ 워크로드
  - 패키지 스크립트는 `vswhere`로 설치된 VS 메이저 버전을 자동 감지해 매칭되는 CMake generator를 선택 (VS 15/16/17/18 지원).
  - **Ninja·MinGW 등 비-VS generator는 미지원** — MSBuild/VS generator 가 패키징·windeployqt 정합성 보장.
- Qt 5.15.2 MSVC 2019 64-bit (`C:\Qt\5.15.2\msvc2019_64`)
- CMake 3.16+ (CMake 4.x 도 호환)
- NSIS (`choco install nsis` — 패키징 시점에만 필요)
- Windows SDK (Visual Studio Installer 의 "Desktop development with C++" 에 포함). `Lib`·`Include` 가 `C:\Program Files (x86)\Windows Kits\10\` 아래에 정상 설치되어 있어야 함 (§2.2.1 트러블슈팅 참조).
- vcpkg (선택 — YouTube `streamList` 활성화 시; `git clone https://github.com/microsoft/vcpkg && bootstrap-vcpkg.bat`)

스크립트 빌드 (PowerShell):

```powershell
.\scripts\package-windows.ps1                      # 빌드 + NSIS 인스톨러 (streamList=OFF, 기본)
.\scripts\package-windows.ps1 -Clean               # build-win\ 삭제 후 처음부터
.\scripts\package-windows.ps1 -NoPackage           # 빌드만 (NSIS 미설치 환경)
.\scripts\package-windows.ps1 -QtRoot "D:\Qt\5.15.2\msvc2019_64"
.\scripts\package-windows.ps1 -Generator "Visual Studio 17 2022"  # 자동 감지 override

# YouTube streamList (gRPC stream) 활성화 — vcpkg 부트스트랩 필요. 첫 빌드 30분+
.\scripts\package-windows.ps1 -VcpkgRoot "C:\dev\vcpkg"
# 또는 환경변수로 미리 설정:
$env:VCPKG_ROOT = "C:\dev\vcpkg"
.\scripts\package-windows.ps1                      # VCPKG_ROOT 인식 시 streamList 자동 ON
.\scripts\package-windows.ps1 -NoStreamList        # vcpkg 활성 환경에서도 OFF 강제
```

스크립트는 Developer Command Prompt 가 아닌 일반 PowerShell 에서도 실행되도록 내부에서 Qt·VS·NSIS 경로를 해석하지만, MSBuild 가 `cl.exe`/`link.exe` 를 호출할 수 있으려면 환경변수 (`PATH`, `LIB`, `INCLUDE`) 가 설정돼 있어야 한다. 가장 안전한 호출 방식:

```powershell
# Developer Command Prompt 또는 vcvars64 prepend
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && powershell -ExecutionPolicy Bypass -File scripts\package-windows.ps1'
```

수동 빌드 (Generator 이름은 설치된 VS 에 맞춰 선택):

```powershell
# 기본 — streamList=OFF (vcpkg 미사용). VS 17 (2022) 기준
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:\Qt\5.15.2\msvc2019_64" `
      -DCMAKE_BUILD_TYPE=Release `
      -DONIONMIXERCHATMANAGER_ENABLE_YT_STREAMLIST=OFF

# VS 18 (2026) 가 설치된 경우
cmake -S . -B build-win -G "Visual Studio 18 2026" -A x64 `
      -DCMAKE_PREFIX_PATH="C:\Qt\5.15.2\msvc2019_64" `
      -DCMAKE_BUILD_TYPE=Release `
      -DONIONMIXERCHATMANAGER_ENABLE_YT_STREAMLIST=OFF

# 또는 — streamList=ON (vcpkg 부트스트랩 후, protobuf/grpc 자동 install)
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_TOOLCHAIN_FILE="C:\dev\vcpkg\scripts\buildsystems\vcpkg.cmake" `
      -DVCPKG_TARGET_TRIPLET=x64-windows `
      -DCMAKE_PREFIX_PATH="C:\Qt\5.15.2\msvc2019_64" `
      -DCMAKE_BUILD_TYPE=Release

cmake --build build-win --config Release --parallel
ctest --test-dir build-win -C Release --output-on-failure
```

빌드 산출물 (multi-config generator — Release 폴더 안에 위치):
- `build-win\Release\OnionmixerChatManagerQt5.exe` (메인 앱, GUI subsystem)
- `build-win\BroadChatClient\Release\OnionmixerBroadChatClient.exe` (오버레이 클라)
- `build-win\Release\Qt5*.dll`, `platforms\`, `imageformats\`, ... (windeployqt 산출)
- `build-win\NSISBUILD\onionmixerchatmanagerqt5-0.1.0-win64.exe` (서버+클라 인스톨러)
- `build-win\NSISBUILD\onionmixerbroadchatclient-0.1.0-win64.exe` (클라 단독 인스톨러)

#### 2.2.1 트러블슈팅 — `LNK1104: 'ucrtd.lib' 파일을 열 수 없습니다`

CMake configure 가 `No CMAKE_CXX_COMPILER could be found` 로 실패하고 상세 로그(`build-win/CMakeFiles/CMakeConfigureLog.yaml`) 에 위 LNK 오류가 보이면 Windows SDK 의 64-bit 레지스트리 등록이 깨진 상태이다. 64-bit MSBuild 가 빈 경로 `C:\Program Files\Windows Kits\10\` 를 SDK 루트로 잘못 해석해 `ucrtd.lib` 을 못 찾는다.

진단:

```powershell
# 32-bit 와 64-bit 레지스트리 KitsRoot10 비교 — 두 값이 일치해야 정상
(Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots" -Name KitsRoot10).KitsRoot10
(Get-ItemProperty "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots" -Name KitsRoot10).KitsRoot10
```

수정 (관리자 권한):

```cmd
reg add "HKLM\SOFTWARE\Microsoft\Windows Kits\Installed Roots" /v KitsRoot10 /t REG_SZ /d "C:\Program Files (x86)\Windows Kits\10\" /f
```

또는 Visual Studio Installer → Modify → Windows SDK 항목 재설치로도 정합성 복원 가능.

YouTube `streamList` (gRPC stream 기반 채팅 수신 경로) 빌드:
- **default**: streamList=OFF — InnerTube WebChat 폴링만으로 채팅 수신 (Linux/Windows 동일).
- **활성화** (선택): vcpkg 부트스트랩 후 `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` 또는 스크립트의 `-VcpkgRoot` 옵션 → `protobuf`/`grpc` 자동 install 후 streamList=ON 빌드. 첫 빌드 30분+ (vcpkg cache miss).
- streamList 미빌드 시에도 InnerTube WebChat / `liveChatMessages.list` 경로는 영향 없음 — 사용자 체감 채팅 수신 동작 동일.

## 3. 설정 파일 위치

config 디렉토리 해석 우선순위:
1. CLI 인수: `--config-dir /path/to/config`
2. 환경변수: `ONIONMIXERCHATMANAGER_CONFIG_DIR=/path/to/config`
3. 기본값: `{실행파일경로}/config`

예시:

```bash
./build/OnionmixerChatManagerQt5
./build/OnionmixerChatManagerQt5 --config-dir /home/user/onionmixerchatmanager-config
ONIONMIXERCHATMANAGER_CONFIG_DIR=/tmp/onionmixerchatmanager ./build/OnionmixerChatManagerQt5
```

config 디렉토리 안에 아래 파일이 사용됩니다.
- `app.ini`
- `tokens.ini`

## 4. 설정 개요

`app.ini`는 앱 설정과 플랫폼 설정을 저장합니다.

주요 `[app]` 키:
- `language`
- `log_level`
- `merge_order`
- `auto_reconnect`
- `detail_log_enabled`
- `chat_font_family`
- `chat_font_size`
- `chat_font_bold`
- `chat_font_italic`
- `chat_line_spacing`
- `chat_max_messages`

플랫폼별 주요 키:
- `[youtube]`
  - `enabled`
  - `client_id`
  - `client_secret`
  - `redirect_uri`
  - `auth_endpoint`
  - `token_endpoint`
  - `scope`
  - `channel_id`
  - `channel_name`
  - `account_label`
  - `live_video_id_override`
- `[chzzk]`
  - `enabled`
  - `client_id`
  - `client_secret`
  - `redirect_uri`
  - `auth_endpoint`
  - `token_endpoint`
  - `scope`
  - `channel_id`
  - `channel_name`
  - `account_label`

예시:

```ini
[app]
language=ko_KR
log_level=info
merge_order=timestamp
auto_reconnect=true
detail_log_enabled=false
chat_font_size=11
chat_line_spacing=3
chat_max_messages=5000

[youtube]
enabled=false
client_id=
client_secret=
redirect_uri=http://127.0.0.1:18080/youtube/callback
auth_endpoint=https://accounts.google.com/o/oauth2/v2/auth
token_endpoint=https://oauth2.googleapis.com/token
scope=https://www.googleapis.com/auth/youtube.readonly https://www.googleapis.com/auth/youtube.force-ssl
channel_id=
channel_name=
account_label=
live_video_id_override=

[chzzk]
enabled=false
client_id=
client_secret=
redirect_uri=http://127.0.0.1:18081/chzzk/callback
auth_endpoint=https://chzzk.naver.com/account-interlock
token_endpoint=https://openapi.chzzk.naver.com/auth/v1/token
scope=
channel_id=
channel_name=
account_label=
```

### 4.1 `[broadchat]` 섹션 — 외부 클라이언트(TCP 서버)

`OnionmixerBroadChatClient` 가 접속하는 TCP 서버 동작을 제어합니다. 메인 앱 시작 시 listen 합니다.

| 키 | 기본값 | 설명 |
|----|--------|------|
| `enabled` | `true` | BroadChat 서버 활성화 여부 |
| `tcp_bind` | `127.0.0.1` | listen 인터페이스. **GUI 비노출 — ini 직접 편집만 가능** |
| `tcp_port` | `47123` | listen 포트 (1024~65535). GUI Configuration → General 에서 변경 가능 |
| `auth_token` | (빈 값) | 사전 공유 토큰. 빈 값 = 인증 없음. UUID v4 권장 (인쇄 가능 ASCII, 1~128자). GUI Generate/Copy/Clear 지원 |
| `max_clients` | `10` | 동시 접속 상한 (1~100) |
| `reject_duplicate_client_id` | `false` | 동일 clientId 중복 접속 차단 여부 |
| `duplicate_kick_target` | `newest` | 중복 차단 시 종료 대상. `newest` 또는 `oldest` |
| `trace` | `false` | `broadchat.trace` 로그 카테고리 활성화 |

예시:

```ini
[broadchat]
enabled=true
tcp_bind=127.0.0.1
tcp_port=47123
auth_token=
max_clients=10
reject_duplicate_client_id=false
duplicate_kick_target=newest
trace=false
```

#### `tcp_bind` 값별 동작

| 값 | listen 범위 | 용도 |
|----|-------------|------|
| `127.0.0.1` (기본) | loopback only | **같은 PC** 의 클라이언트만 접속 가능. 가장 안전 |
| `0.0.0.0` | 모든 인터페이스 (IPv4) | LAN 의 다른 PC, WAN(공인 IP) 모두 도달 가능. **VPN/방화벽 필수** |
| 특정 LAN IP (예: `192.168.1.10`) | 해당 인터페이스만 | 다중 NIC 환경에서 한 인터페이스만 노출 |
| `::` | 모든 인터페이스 (IPv6) | IPv6 전용 노출 |

> **증상 — `BroadChatClient` 가 `127.0.0.1` 로는 연결되는데 메인 PC 의 LAN IP 로는 안 됨**: 서버가 `tcp_bind=127.0.0.1` (기본값) 으로 loopback 에만 listen 중입니다. 외부 인터페이스로 도달하려면 아래 절차로 변경하세요.

#### `tcp_bind` 가 GUI 에 노출되지 않는 이유

Configuration 다이얼로그의 BroadChat 섹션은 **포트와 auth_token만** 표시합니다. `tcp_bind` 은 의도적으로 ini 전용입니다 — 클릭 한 번으로 외부 노출되는 사고를 막고, 변경 시 **명시적으로 ini 를 편집**하게 만들어 운영자가 보안 영향(VPN/방화벽/인증 토큰)을 함께 검토하도록 강제합니다.

#### 외부 노출 절차 (LAN 의 다른 PC 에서 접속)

1. 메인 앱에서 Configuration → General → BroadChat → **Generate** 로 `auth_token` 발급 → **Apply** → 앱 종료
2. `app.ini` 를 텍스트 에디터로 열어 `[broadchat]` 의 `tcp_bind=0.0.0.0` (또는 특정 LAN IP) 로 수정
3. 메인 앱 재시작
4. listen 검증:
   ```bash
   # Linux
   ss -ltn | grep 47123        # 0.0.0.0:47123 이어야 정상
   # Windows (PowerShell)
   Get-NetTCPConnection -LocalPort 47123 -State Listen
   ```
5. `OnionmixerBroadChatClient` 의 우클릭 → 세팅에서 **Host** 에 서버 PC 의 LAN IP, **Port** 47123, **Auth Token** 에 위에서 발급한 동일 값 입력

#### 보안 가드

- `tcp_bind=0.0.0.0` 인데 `auth_token` 이 비어 있는 상태로 Apply 하면 Configuration 다이얼로그가 modal 보안 경고로 차단합니다 (`src/ui/ConfigurationDialog.cpp:635`).
- 잘못된 IP / 호스트 이름 / IPv6 주소 → 로드 시 자동으로 `127.0.0.1` 로 fallback (경고 로그 출력).
- `app.ini` 파일은 저장 시 권한 `0600` (Linux) 으로 자동 설정됩니다.

#### WAN 노출은 권장하지 않음

프로토콜은 **평문 TCP** 이며 TLS 는 도입하지 않습니다 (영구 정책). 공인 IP 로 직접 노출하지 말고 **WireGuard/Tailscale 등 VPN 터널**로 LAN 처럼 묶어서 사용하세요. 자세한 내용은 [VPN 가이드](docs/vpn-guide.md) 와 [트러블슈팅](docs/troubleshooting.md).

## 5. 초기 사용 흐름

1. 앱 실행
2. `Configuration` 열기
3. YouTube / CHZZK 설정 입력
4. `Apply`
5. 필요한 플랫폼에서 토큰 액션 실행
   - `Token Refresh`
   - `Re-Auth Browser`
   - `Delete Token`
6. 메인 화면에서 `Connect`

토큰 액션 성공 시:
- 토큰은 `tokens.ini`에 저장됩니다.
- 프로필 동기화 결과는 런타임 반영이 우선이며, 설정값은 사용자가 `Apply` 하지 않는 한 자동 덮어쓰지 않습니다.

## 6. 플랫폼 메모

### 6.1 YouTube

`client_id`에는 반드시 Google OAuth 2.0 Client ID를 넣어야 합니다.

정상 예시:
- `123456789012-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.apps.googleusercontent.com`

잘못된 예시:
- iOS bundle id / package id
- `client_secret`
- API key

#### Client ID 발급 절차 (Google Cloud Console)

1. https://console.cloud.google.com/ 접속 후 로그인
2. 상단 프로젝트 선택기에서 프로젝트를 새로 만들거나 기존 프로젝트 선택
3. `APIs & Services` → `Library`에서 **YouTube Data API v3** 검색 후 `Enable`
4. `APIs & Services` → `OAuth consent screen`
   - User Type: `External` (개인 사용 시)
   - 앱 이름 / 지원 이메일 등 필수 항목 입력
   - `Scopes`에 아래 두 개 추가:
     - `https://www.googleapis.com/auth/youtube.readonly`
     - `https://www.googleapis.com/auth/youtube.force-ssl`
   - 앱을 테스트 모드로 두는 경우 본인 계정을 `Test users`에 추가
5. `APIs & Services` → `Credentials` → `Create Credentials` → `OAuth client ID`
   - Application type: **Desktop app** 또는 **Web application** 모두 동작 (Desktop app 권장)
   - Authorized redirect URIs에 `http://127.0.0.1:18080/youtube/callback` 정확히 일치하게 등록
6. 발급된 **Client ID**와 **Client secret**을 `config.ini`의 `[youtube]` 섹션에 붙여넣기

> ⚠️ **`client_secret`은 Desktop app 타입에서도 반드시 필요합니다.**
> 구글은 "Desktop app의 secret은 기밀이 아니다"라고 명시하지만, 토큰 교환(`/token`) 요청에는 `client_secret` 파라미터가 필수입니다. 누락되면 브라우저 인증(`OAuth 완료`)까지는 통과하지만 앱에서 `Interactive re-auth failed`로 실패합니다.
>
> `client_secret` 확인:
> `APIs & Services` → `Credentials` → 해당 OAuth 클라이언트 이름 클릭 → 상세 페이지 우측의 `Client secret` (`GOCSPX-...` 로 시작) 또는 상단 `DOWNLOAD JSON` 버튼으로 받은 파일의 `client_secret` 필드.

채팅 수신 우선순위:
1. InnerTube WebChat
2. `liveChatMessages.streamList`
3. `liveChatMessages.list`

InnerTube WebChat 참고:
- `INNERTUBE_API_KEY`는 실행 시 YouTube live chat 페이지에서 추출합니다.
- 이 값은 저장소의 소스/설정 파일에 하드코딩하거나 커밋하지 마십시오.

수동 우회:
- `live_video_id_override`에 watch URL 또는 raw `videoId`를 넣으면 자동 탐색보다 우선 사용합니다.

#### 다중 채널(브랜드 계정) 주의사항

하나의 Google 계정에 여러 YouTube 채널(개인 채널 + 브랜드 계정 채널 등)이 연결돼 있으면, OAuth 브라우저 인증 단계에서 **채널 선택 화면**이 나타납니다. 이때 **실제 라이브 방송을 진행하는 채널을 반드시 선택**해야 합니다.

라이브 중이 아닌 채널을 선택한 경우의 증상:
- `channels.mine`은 해당 채널 정보로 정상 동기화됨 (`[YOUTUBE-PROFILE] channels.mine synchronized channelId=... handle=@xxx`)
- `liveBroadcasts.list`에서 `The user is not enabled for live streaming.` 반환
- `search.list forMine=true`에서 `invalid argument` 반환
- 결국 `오프라인 / No owned recent video result`로 귀결되며 실제 라이브를 잡지 못함

해결:
1. 토큰 삭제 후 재인증: Configuration → YouTube → `Delete Token` → `Re-Auth Browser` → 브라우저에서 **라이브 채널** 선택
2. 즉시 우회: Configuration → YouTube → `Live Video ID Override`에 watch URL 또는 `videoId` (예: `rjNcK2Otr6g`) 입력 → Apply. 이 경로는 discovery API를 완전히 건너뛰므로 다중 채널 문제와 무관하게 동작합니다.

인증 오류 확인 항목:
- `client_id`가 실제 OAuth Client ID인지
- `client_secret`이 `GOCSPX-...` 형태로 채워져 있는지 (Desktop app 타입도 필수)
- `redirect_uri`가 등록된 값과 정확히 일치하는지
- Google Console에 동일 redirect URI가 등록되어 있는지
- OAuth consent screen이 `Testing` 상태면 본인 계정이 `Test users`에 있는지 (테스트 모드의 refresh token은 7일 후 만료)

증상별 원인:
- 브라우저는 `OAuth 완료`로 끝나는데 앱에서 `Interactive re-auth failed` → 대부분 `client_secret` 누락 (`invalid_client`)
- 토큰 갱신만 실패 → refresh token 만료/철회, 또는 consent screen Testing 상태에서 7일 경과

### 6.2 CHZZK

실제 서비스 사용 시 CHZZK endpoint는 명시적으로 설정하는 것이 안전합니다.

권장 값:
- `auth_endpoint=https://chzzk.naver.com/account-interlock`
- `token_endpoint=https://openapi.chzzk.naver.com/auth/v1/token`

채팅 수신 흐름:
- session auth -> socket connect -> subscribe -> chat receive

#### Client ID 발급 절차 (CHZZK 개발자 센터)

1. https://developers.chzzk.naver.com/ 접속 후 네이버 계정으로 로그인
2. `애플리케이션` 메뉴에서 새 애플리케이션 등록
   - 애플리케이션 이름 / 설명 입력
   - **Redirect URI**에 `http://127.0.0.1:18081/chzzk/callback` 등록 (config의 값과 정확히 일치해야 함)
   - 필요한 권한(scope) 선택 — 채팅 수신에 필요한 `chat:read` 등 해당 항목
3. 등록 완료 후 발급된 **Client ID**와 **Client Secret**을 `config.ini`의 `[chzzk]` 섹션에 붙여넣기
4. 필요 시 치지직 개발자 센터의 최신 가이드에서 endpoint / scope 변경 여부 확인

#### Channel ID 확인

본인 채널이라면 `channel_id`는 비워둬도 됩니다. OAuth 인증이 성공하면 앱이 `users/me` 응답으로 자동 동기화합니다 (로그에 `[CHZZK-PROFILE] users/me synchronized channelId=... channelName=...` 형태로 표시).

타인 채널을 지정하려는 경우 수동 입력 형식:
- 치지직 채널 페이지 URL: `https://chzzk.naver.com/{채널ID}` 또는 `https://chzzk.naver.com/live/{채널ID}`
- `{채널ID}` 부분이 **32자리 16진수 문자열** (예: `592df5045eb7d5f8d8bb5d43371a56c4`)
- `config.ini [chzzk] channel_id=` 항목에 하이픈·접두어 없이 그 값만 넣습니다.

## 7. UI 개요

메인 창:
- 상단 버튼/상태 영역
- 채팅 영역
- 액션 패널
- 이벤트 로그
- 하단 컴포저

채팅 뷰:
- Messenger view
- Table view

Configuration 창:
- General
- YouTube
- CHZZK
- Security

주요 UI 기능:
- `Chat Preview`
- `Token Audit`
- `Detail Log` ON/OFF
- 창 크기 / splitter 상태 저장

## 8. 로그와 상태

`detail_log_enabled=false`
- `TRACE_*`, `INFO_*` 로그를 UI에 숨깁니다.
- 내부 상태 처리 자체는 유지됩니다.

YouTube / CHZZK는 일반 연결 상태 외에 runtime phase를 별도로 가집니다.
예:
- `CONNECTED_NO_LIVECHAT`
- `CONNECTED_NO_SESSIONKEY`
- `CONNECTED_NO_SUBSCRIBE`

상태 판정은 번역 문자열이 아니라 내부 영문 코드 기준으로 처리됩니다.

## 9. 토큰 저장소

현재 기본 구현은 파일 기반 저장소입니다.

- 파일: `tokens.ini`
- 클래스: `FileTokenVault`

주의:
- 이것은 개발용 구현입니다.
- 운영 환경에서는 보안 저장소 사용을 권장합니다.

## 10. 번역

번역 파일:
- `translations/onionmixerchatmanager_ko_KR.ts`
- `translations/onionmixerchatmanager_en_US.ts`
- `translations/onionmixerchatmanager_ja_JP.ts`

Qt `LinguistTools`가 있으면 `.qm` 빌드가 활성화됩니다.

앱은 실행 시 다음 위치에서 번역 파일을 찾습니다.
- `{appDir}/translations`
- `{appDir}/../translations`
- `{cwd}/translations`
- `./translations`

## 11. 패키징

운영 정책은 OS 와 무관하게 동일합니다 — **서버 인스톨러 = 서버 + 클라**, **클라 인스톨러 = 클라 단독**, 일방향 의존.

| OS | 산출물 | 생성 도구 |
|----|--------|-----------|
| Linux | `onionmixerchatmanagerqt5_<ver>_amd64.deb`, `onionmixerbroadchatclient_<ver>_amd64.deb` | CPack DEB (§11.3) |
| Windows | `onionmixerchatmanagerqt5-<ver>-win64.exe`, `onionmixerbroadchatclient-<ver>-win64.exe` | CPack NSIS (§11.7) |

### 11.0 Linux ↔ Windows 정책 매핑 (요약)

세부는 `PLAN_COMPILE_WINDOWS.md` §3.5.

- Linux 의 `Depends:` 필드 ↔ Windows NSIS `CPACK_COMPONENT_SERVER_DEPENDS=client`. server 인스톨러는 client 컴포넌트 파일을 동봉하여 단일 인스톨로 서버+클라 동시 설치.
- 클라 단독 인스톨러는 Linux/Windows 모두 렌더링 전용 PC 에서 메인 앱 없이 동작.
- 디렉토리 레이아웃은 양 OS 동일 (Linux FHS `bin/`, `share/<app>/translations`, `share/doc/<app>` 구조 유지).

### 11.1 패키지 구성 (Debian .deb)

CMake + CPack으로 **두 개의 독립 .deb 패키지**를 생성합니다.

### 11.1 패키지 구성

| 패키지 | 포함 바이너리 | 의존 관계 | 용도 |
|--------|----------------|-----------|------|
| `onionmixerchatmanagerqt5` | 메인 앱 + 설정 템플릿 + 번역 | **`Depends: onionmixerbroadchatclient`** | 방송 PC (서버) |
| `onionmixerbroadchatclient` | 클라이언트 앱 + 번역 | 독립 (Qt5 런타임만 의존) | 렌더링 전용 PC |

### 11.2 포함·비포함 규칙 (핵심)

- **`onionmixerchatmanagerqt5` 설치 시** `onionmixerbroadchatclient`도 자동 설치됨 (apt/dpkg 의존성). 방송 PC에서 서버를 돌리면서 같은 PC에 클라 오버레이도 띄우는 일반 운영 시나리오 커버.
- **`onionmixerbroadchatclient` 단독 설치는 허용**. 메인 앱은 포함되지 않음. 다른 PC에서 접속 전용으로만 사용하는 경우.
- 즉 포함 관계는 **한 방향 (서버 → 클라)만 성립**. 클라 패키지가 서버를 끌어오지 않음.
- 기술적 구현: 서버 .deb의 Control 필드 `Depends:` 에 클라 패키지 명시 (`Depends: onionmixerbroadchatclient (>= <version>)`). 클라 .deb의 Depends에는 서버 없음.

### 11.3 빌드·패키지 생성

**권장 (스크립트)**:

```bash
./scripts/package-deb.sh              # 증분 빌드
./scripts/package-deb.sh --clean      # build 디렉토리 삭제 후 처음부터
./scripts/package-deb.sh --verbose    # 상세 출력
./scripts/package-deb.sh --help
```

**수동 (스크립트와 동등)**:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build && cpack -G DEB
```

결과물 (`build/`):

```
onionmixerchatmanagerqt5_0.1.0_amd64.deb
onionmixerbroadchatclient_0.1.0_amd64.deb
```

### 11.4 설치·제거

```bash
# 서버 PC (두 패키지 모두 설치됨)
sudo apt install ./onionmixerchatmanagerqt5_0.1.0_amd64.deb

# 클라이언트 PC (클라만)
sudo apt install ./onionmixerbroadchatclient_0.1.0_amd64.deb

# 제거
sudo apt remove onionmixerchatmanagerqt5       # 클라는 그대로 유지
sudo apt autoremove onionmixerbroadchatclient  # 의존성 없으면 함께 제거
```

### 11.5 설치 후 경로

- 실행파일: `/usr/bin/OnionmixerChatManagerQt5`, `/usr/bin/OnionmixerBroadChatClient`
- 번역 `.qm`: `/usr/share/OnionmixerChatManagerQt5/translations/` (서버), `/usr/share/OnionmixerBroadChatClient/translations/` (클라)
- 설정 템플릿: `/usr/share/OnionmixerChatManagerQt5/config.example/`
- 사용자 설정은 `~/.config/OnionmixerChatManager/` 또는 실행 시 `--config-dir` CLI

### 11.6 운영 문서

- [CHANGELOG](docs/CHANGELOG.md) — 릴리즈 히스토리
- [운영자 체크리스트](docs/operator-checklist.md) — 설치·설정·점검·보안·제거 순서
- [트러블슈팅](docs/troubleshooting.md) — listen 실패·auth_failed·emoji 404 등 증상별 대응
- [VPN 가이드](docs/vpn-guide.md) — WireGuard·Tailscale 예시 (TLS 비도입 대체)

### 11.7 Windows NSIS 인스톨러

Linux .deb 와 동등한 정책으로 **두 개의 독립 NSIS 인스톨러**를 생성합니다.

| 파일 | Linux 등가 | 의존 | 용도 |
|------|-----------|------|------|
| `onionmixerchatmanagerqt5-<ver>-win64.exe` | `onionmixerchatmanagerqt5_*.deb` | client 컴포넌트 동봉 | 방송 PC (서버 + 클라 동시 설치) |
| `onionmixerbroadchatclient-<ver>-win64.exe` | `onionmixerbroadchatclient_*.deb` | 독립 (Qt 런타임 동봉) | 렌더링 전용 PC (클라만) |

빌드·패키지 생성 (스크립트 권장 — cpack 두 번 호출로 두 인스톨러 자동 산출):

```powershell
.\scripts\package-windows.ps1                      # streamList=OFF 빌드 + 두 NSIS 인스톨러
.\scripts\package-windows.ps1 -VcpkgRoot "C:\dev\vcpkg"   # streamList=ON 활성화
```

산출 위치: `build-win\NSISBUILD\` (빌드 산출물과 분리).

> NSIS generator 의 한계 (`COMPONENTS_GROUPING=IGNORE` 가 silent `ONE_PER_GROUP` 처리) 로 단일 cpack 호출은 단일 인스톨러만 생성. 스크립트는 cpack 을 두 번 호출하면서 `-D CPACK_COMPONENTS_ALL=...` 와 `-D CPACK_PACKAGE_FILE_NAME=...` 으로 컴포넌트 조합·파일명을 override — Linux DEB generator 의 `CPACK_DEB_COMPONENT_INSTALL=ON` 동작 재현. 수동 cpack 호출 detail 은 `scripts\package-windows.ps1` 의 `Invoke-CpackProfile` 함수 참조.

설치:

```powershell
# 방송 PC (서버 + 클라 모두 설치됨)
Start-Process -Wait .\build-win\NSISBUILD\onionmixerchatmanagerqt5-0.1.0-win64.exe

# 렌더링 PC (클라만)
Start-Process -Wait .\build-win\NSISBUILD\onionmixerbroadchatclient-0.1.0-win64.exe

# 제거: 제어판 → 프로그램 추가/제거 → 해당 항목 선택
```

설치 후 경로:

서버 인스톨러 (방송 PC) — `C:\Program Files\OnionmixerChatManager\`:
- 실행파일: `bin\OnionmixerChatManagerQt5.exe`, `bin\OnionmixerBroadChatClient.exe`
- Qt 런타임: `bin\Qt5*.dll`, `bin\platforms\qwindows.dll`, `bin\imageformats\`, `bin\styles\`, `bin\tls\` (windeployqt 산출)
- streamList 의존 DLL (streamList=ON 빌드 시): `bin\libprotobuf.dll`, `bin\libssl-3-x64.dll`, `bin\libcrypto-3-x64.dll`, `bin\abseil_dll.dll`, `bin\cares.dll`, `bin\re2.dll`, `bin\z.dll`
- 번역: `share\OnionmixerChatManagerQt5\translations\`, `share\OnionmixerBroadChatClient\translations\`
- 설정 템플릿: `share\OnionmixerChatManagerQt5\config.example\`
- 사용자 설정: `<exe>\config\` (메인 앱 기본) / `%APPDATA%\OnionmixerBroadChatClient\<bucket>\` (클라)

클라 단독 인스톨러 (렌더링 PC) — `C:\Program Files\OnionmixerBroadChatClient\`:
- 실행파일: `bin\OnionmixerBroadChatClient.exe` (메인 앱 미설치)
- Qt 런타임 + streamList 의존 DLL 동일 (windeployqt + vcpkg 산출 동봉으로 자체 동작 보장)
- 번역: `share\OnionmixerBroadChatClient\translations\`
- 사용자 설정: `%APPDATA%\OnionmixerBroadChatClient\<bucket>\`

## 12. OBS Studio 캡처 호환성 (Windows)

방송창 (`BroadcastChatWindow`) 과 `OnionmixerBroadChatClient` 는 알파 채널이 있는 배경(투명/반투명)을 그리기 위해 Windows에서 **레이어드 윈도우 (`WS_EX_LAYERED`)** 로 동작합니다. 이 때문에 OBS Studio의 Window Capture 기본값(BitBlt)으로는 화면이 캡처되지 않고 마우스 커서 자국만 남는 증상이 나타납니다 — Windows GDI BitBlt 가 layered window 의 픽셀에 접근할 수 없는 OS API 차원의 한계입니다.

해결: OBS의 캡처 방식을 **Windows Graphics Capture (WGC)** 로 변경하면 정상 캡처됩니다.

설정 절차:
1. OBS Studio 의 Sources 패널에서 BroadChatClient (또는 메인 앱 방송창) 을 캡처하는 Window Capture 소스를 우클릭 → **Properties**
2. **Capture Method** 드롭다운을 **`Windows 10 (1903 and up)`** 으로 변경
3. (선택) **Capture Cursor** 체크 해제 — 방송 화면에 마우스 커서가 보이는 것을 원치 않을 때
4. (선택) **Client Area** 체크 — 본 앱은 이미 frameless라 효과 없음

요구 사항:
- Windows 10 버전 1903 (2019년 5월) 이상
- Direct3D 11 지원 GPU (현재 일반 PC에서 사실상 모두 충족)

WGC 사용 시 부수 효과:
- 일부 Windows 빌드에서 캡처 대상 창에 노란색 외곽선이 표시될 수 있음 (보안 표시). OBS 캡처 결과(방송 송출) 자체에는 보이지 않음.

WGC 를 사용할 수 없는 환경의 대안:
- **Display Capture** + 방송창을 일정 위치/크기에 고정 → 해당 영역만 사용
- 모니터 전체를 캡처하므로 다른 창 노출에 주의

기술 배경:
- Qt 5.15 의 Windows 플랫폼 플러그인은 `WA_TranslucentBackground` 활성화 시 `WS_EX_LAYERED` 비트를 부여해 DWM 합성을 통한 per-pixel alpha 를 지원합니다.
- OBS의 BitBlt 캡처(`plugins/win-capture/dc-capture.c`)는 GDI DC를 통해 픽셀을 읽지만, layered window 는 픽셀이 GDI DC에 존재하지 않고 DWM의 합성 surface 에만 존재합니다.
- WGC (`libobs-winrt/winrt-capture.cpp`)는 Direct3D 11 + `Windows.Graphics.Capture` API 를 사용해 DWM 합성 결과를 직접 받으므로 layered window 도 정확히 캡처합니다.

## 13. 개발자 참고

핵심 기준:
- YouTube / CHZZK adapter는 통합하지 않음
- 공통화는 데이터 계약, 유틸리티, 공통 UI 계층까지만 허용
- adapter hot-apply, runtime phase, dedup, action id 규약 유지
