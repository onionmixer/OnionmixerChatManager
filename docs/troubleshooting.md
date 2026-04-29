# Troubleshooting

v0.1.0 기준 운영 중 자주 발생하는 상황과 대응책. 각 항목의 **증상 → 원인 → 진단 커맨드 → 해결** 순서.

## 1. BroadChat 서버 listen 실패

### 증상
- 메인 앱 Connect 버튼 후 상태바 `BroadChat: error` (빨강)
- `QMessageBox::warning` 모달 "BroadChat Listen Failed"
- 로그: `[broadchat.warn] listen(...) failed: ...`

### 원인·대응

| detail 메시지 | 원인 | 해결 |
|---------------|------|------|
| `bind ...:<port> failed: port in use` | 이전 인스턴스 TIME_WAIT 또는 다른 프로세스 점유 | `ss -tlnp sport = :47123`로 점유 프로세스 확인. 본인 프로세스면 kill 후 재시도 (`SO_REUSEADDR`가 60s TIME_WAIT 완화). 다른 포트로 변경도 가능 |
| `bind 0.0.0.0:<1024 failed: permission denied` | 1024 미만 포트는 root 권한 필요 | 설정 창 General 탭 → `BroadChat Port` 1024 이상으로 변경 |
| `bind <ip>:<port> failed: interface absent` | ini `tcp_bind`에 기록된 NIC IP가 현재 머신에 없음 (IP 변경·NIC 제거) | `config/app.ini`의 `[broadchat] tcp_bind`를 `127.0.0.1` 또는 현재 NIC IP로 수정 |

### 일반 진단
```bash
# 포트 점유 확인
ss -tlnp sport = :47123
lsof -iTCP:47123 -sTCP:LISTEN

# 현재 NIC 주소
ip addr show

# TIME_WAIT 상태 확인
ss -tan | grep -E "TIME-WAIT.*:47123"
```

## 2. 클라이언트 `auth_failed` bye 수신

### 증상
- BroadChatClient가 접속 직후 즉시 끊김
- 다이얼로그: "Authentication failed. Settings에서 토큰 확인..."
- 재연결 시도 중단 (v19-6)

### 원인·대응

| 케이스 | 해결 |
|--------|------|
| 클라 토큰 공백·오타 | BroadChatClient 우클릭 → 세팅 → Auth Token 확인. 메인 앱 설정 창의 Copy 버튼으로 정확한 값 복사·붙여넣기 |
| 메인 앱이 토큰 교체 후 클라 미반영 | 양쪽 앱을 같은 토큰으로 동기화 후 클라 재시작 |
| 메인 앱 토큰 Clear (인증 비활성화) 후 클라만 토큰 보유 | 클라도 Clear |
| 토큰에 앞뒤 공백 포함 | v21-γ-6 `trim` 정책으로 자동 제거되나 여전히 문제 시 수동 재생성 |

### 진단
```bash
# 메인 앱 서버가 authRequired인지 로그 확인
journalctl --user -u OnionmixerChatManagerQt5 | grep 'listening.*auth=enabled'

# 클라 로그에서 auth_failed
journalctl --user -u OnionmixerBroadChatClient | grep auth_failed
```

## 3. TIME_WAIT 소켓 누적

### 증상
- Connect/Disconnect 반복 후 재bind 실패
- `ss -tan | grep TIME-WAIT`에서 동일 포트 여러 개

### 원인
- 정상 — TCP 표준 동작 (RFC 793, 2*MSL ≈ 60s)
- Qt `QTcpServer`는 `SO_REUSEADDR` 기본 적용 → 동일 포트 재bind 허용

### 대응
- 정상: 그대로 진행 (listen 문제 없음)
- 누적 수백 개: `sysctl net.ipv4.tcp_tw_reuse=1` (선택, 고빈도 연결 환경)

## 4. emoji 이미지 broken-image 표시

### 증상
- 채팅 메시지 이모지 위치에 `?` 아이콘
- 로그: `[broadchat.warn] emoji ... error=http_404`

### 원인·대응

| error 코드 | 의미 | 대응 |
|-----------|------|------|
| `http_404` | 플랫폼이 이모지 제거·URL 변경 | 플랫폼 정책 — 앱에서 대응 불가 |
| `http_timeout` | HTTP 10초 초과 (v14-9) | 네트워크 상태 확인. 60s 후 자동 재시도 |
| `http_5xx` | 플랫폼 API 일시 장애 | 시간 경과 후 자동 재시도 |
| `unknown_id` | 서버 캐시에 URL 미등록 (오래된 채팅 이모지) | 정상 — 이전 메시지의 이모지가 서버 재시작으로 사라짐. 다음 동일 이모지 수신 시 복구 |

## 5. BroadChatClient가 서버에 접속 못함

### 증상
- 클라 상태 배지 지속적 "연결 대기 중"
- 재연결 백오프 1s→30s cap

### 진단 순서

1. **같은 PC 시나리오 (host=127.0.0.1)**:
   ```bash
   ss -tlnp | grep 47123       # 서버 listen 중인지
   nc -zv 127.0.0.1 47123      # TCP 연결 가능한지
   ```

2. **다른 PC 시나리오**:
   ```bash
   # 클라 PC에서 서버 PC로 도달 가능한지
   ping <server-ip>
   nc -zv <server-ip> 47123

   # 서버 PC에서 bind 주소 확인
   ss -tlnp | grep 47123
   # tcp_bind=127.0.0.1이면 원격 접속 불가 → 0.0.0.0 또는 특정 NIC IP로 변경
   ```

3. **방화벽**:
   ```bash
   sudo ufw status                       # Ubuntu
   sudo iptables -L INPUT -n | grep 47123
   ```

4. **서버 `config/app.ini`**:
   - `[broadchat] enabled=true`
   - `tcp_port` 클라와 동일
   - 원격 접속 시 `tcp_bind=0.0.0.0` (또는 NIC IP)

## 6. 메인 앱 시작 즉시 종료 (--version 이외)

### 증상
- 실행 직후 창이 안 뜨고 종료

### 원인·대응

| 로그 | 원인 | 해결 |
|------|------|------|
| `[broadchat.err] envelope parse error: ...` | config 손상 | `config/app.ini` 백업 후 `app.ini.example`로 교체 |
| Qt: "Could not load ..." | Qt 플러그인 누락 (특히 `platforms/libqxcb.so`) | `sudo apt install libqt5gui5` (deb 의존성 누락 복구) |
| 로그 없음 + segfault | stale build · 크래시 | `rm -rf build && cmake -S . -B build && cmake --build build` |

## 7. 번역이 한국어 원본 그대로 표시

### 증상
- 영어 locale인데 UI가 한국어 (세팅·종료 등)

### 원인·대응
- `.qm` 파일 미로드

### 진단
```bash
# 경로에 .qm 있는지
ls /usr/share/OnionmixerChatManagerQt5/translations/
ls /usr/share/OnionmixerBroadChatClient/translations/

# 환경변수로 명시 지정
LANG=en_US.UTF-8 OnionmixerBroadChatClient
# 또는 CLI
OnionmixerBroadChatClient --language en_US
```

### 해결
- 번역 파일 없으면: deb 재설치 `sudo apt install --reinstall onionmixerchatmanagerqt5`
- ini로 고정: `[app] language=en_US`

## 8. `config/app.ini` 권한 경고

### 증상
- Apply 후 `chmod 0600`이 적용 안 됨 (share·group 쓰기 가능 상태)

### 원인
- umask이 0022·0002 이외로 설정됨 (실무상 드물음)
- `noatime`·`ro` 마운트

### 대응
```bash
chmod 0600 config/app.ini config/tokens.ini
ls -l config/  # rw------- 확인
```

## 9. YouTube 시청자 카운터 깜박임 ("—" flicker)

### 증상

- YouTube 라이브 방송 중인데 시청자 수가 **값 ↔ "—"** 를 반복
- 수치가 간헐적으로 0/공백으로 꺾였다가 복구

### 원인

YouTube Data API v3 `videos.list?part=liveStreamingDetails` 호출이 반환하는
`concurrentViewers` 필드는 Google 측 집계 캐시의 근사값으로, **저시청자 구간·라이브
경계(시작/종료) 근처**에서 필드 자체가 누락되는 경우가 있음. 메인 앱은 15초 간격으로
폴링하므로 결측 tick이 바로 UI에 노출되면 깜박임처럼 보임.

v0.1.0 이후(Unreleased): 4중 완화 적용 — 연속 결측 3회(≈45s) 미만은 마지막 값 유지,
tooltip으로 상태 표기, 방송 종료(OFFLINE)만 즉시 리셋.

### 진단

YouTube 라벨 위에 **마우스 hover** → tooltip에서 현재 상태 확인:

```
YouTube viewers (via Data API v3 concurrentViewers, polled every 15s)
Fresh: 8s ago                          ← 정상
Miss streak: 2 / 3 (grace)             ← 2회 연속 결측, 유지 중
Miss rate: 3.4% (12/350 ticks)         ← 누적 결측 비율
```

- `Fresh: Ns ago` → 정상 폴링
- `Stale: last fresh Ns ago` → 20초 이상 신규 값 없음
- `Miss streak: X / 3` → 연속 결측 누적 (≥3이면 placeholder 전환)
- `Miss rate`가 10% 이상이면 YouTube 측 캐시 이슈 지속 가능성 — 방송 자체에는 문제 없음

### 대응

| 케이스 | 해결 |
|--------|------|
| `Miss rate < 5%`, 간헐 flicker | **정상 범위**. 추가 조치 불필요 |
| `Miss rate > 20%`, 지속 | YouTube quota/API 상태 확인 ([status.cloud.google.com](https://status.cloud.google.com)). 토큰 재발급 고려 |
| `Stale N > 60s` | 토큰 만료·네트워크 단절. Configuration 창에서 토큰 재인증 |
| 방송 종료 즉시 "—" | **정상 동작** (OFFLINE 확정 시 hysteresis 우회) |
| 방송 중인데 장시간 "—" | Live probe가 OFFLINE/UNKNOWN 오판정 가능성. 이벤트 로그 `[LIVE] YouTube state=...` 확인 |

### 관련 코드

- `src/ui/MainWindow.cpp:requestYouTubeViewerCount` — API 폴링
- `src/ui/MainWindow.cpp:updateViewerCount` — 히스테리시스 분기
- `src/core/Constants.h` — `Viewers::kYouTubeViewerMissGraceCount=3`, `kYouTubeViewerStaleThresholdMs=20000`

## 10. 공용 Wi-Fi·WAN 노출 경고

- **TLS 영구 비도입 (v23)** — 앱 자체 암호화 없음
- 원격 운영 필수 시 **VPN 터널링**: [docs/vpn-guide.md](vpn-guide.md) 참조
- 경고: `tcp_bind=0.0.0.0` + auth_token 빈 값 조합은 위험 — 설정 창이 Apply 시 modal 경고

## 11. Windows 환경 특이사항

빌드·설치·실행 경로는 [README §2.2](../README.md#22-windows) 참조.
이하는 Windows 에서만 발생하는 증상.

### 11.1 Windows Defender 방화벽 첫 실행 prompt

#### 증상
- 메인 앱 처음 실행 시 "Windows Defender 방화벽이 이 앱의 일부 기능을 차단했습니다" 다이얼로그
- BroadChat 서버(기본 47123)·OAuth 콜백(127.0.0.1:18080) listen 시점

#### 원인
- 정상 — Windows 방화벽이 새 listening 소켓을 가진 unsigned 실행파일에 대해 사용자 승인 요청
- NSIS 인스톨러는 현재 방화벽 룰을 자동 등록하지 않음 (의도)

#### 해결
- 다이얼로그에서 **"개인 네트워크"** 만 체크 후 "액세스 허용" — 같은 LAN 의 클라 PC 접속 허용
- "공용 네트워크" 는 체크 해제 (외부 노출 방지). VPN 운영은 [vpn-guide.md](vpn-guide.md) 참조
- 프롬프트를 실수로 Cancel 한 경우: 제어판 → Windows Defender 방화벽 → 앱 또는 기능 허용 → "Onionmixer..." 항목 찾아 개인 체크박스 활성화. 항목이 없으면 "다른 앱 허용..." 으로 exe 직접 추가
- 진단:
  ```powershell
  # 현재 listen 중인 포트 확인 (Linux ss 등가)
  Get-NetTCPConnection -State Listen | Where-Object LocalPort -in 47123,18080
  netstat -ano | findstr :47123
  ```

### 11.2 빌드 시 한글 주석 깨짐 / `warning C4819`

#### 증상
- MSVC 빌드 로그에 다량의 `warning C4819: The file contains a character that cannot be represented in the current code page (949)`
- 실행파일 UI 한글 문자열 일부 깨짐 (`???` 또는 다른 글자)

#### 원인
- 본 프로젝트 소스는 BOM 없는 UTF-8 + 한글 주석/리터럴 다수
- MSVC 의 기본 source/exec charset 은 시스템 codepage (한국어 Windows 는 CP949) 라서 UTF-8 문자열이 잘못 해석됨

#### 해결
- CMakeLists.txt 에 `MSVC` 가드로 `/utf-8` 가 이미 추가되어 있음 (PLAN §2.2). 정상 빌드는 `target_compile_options(... PRIVATE /utf-8 /W3 /permissive-)` 가 실제 적용되는지 확인:
  ```powershell
  # 빌드 로그에서 /utf-8 인용 확인
  cmake --build build-win --config Release -- /verbosity:detailed | Select-String "/utf-8"
  ```
- 외부 IDE(VS) 에서 직접 cl 호출 시: 프로젝트 속성 → C/C++ → Command Line → `/utf-8` 추가
- 디렉토리 경로에 한글이 포함되면 일부 도구 실패 가능 — 빌드 디렉토리는 ASCII 만 사용 (예: `C:\dev\omcm`)

### 11.3 실행 시 `vcruntime140.dll`·`MSVCP140.dll` 누락

#### 증상
- exe 더블클릭 시 "이 앱을 시작할 수 없음 — `VCRUNTIME140.dll` 이 없습니다"

#### 원인
- 클린 Windows 또는 VS 미설치 PC 에는 Visual C++ Redistributable 이 부재. windeployqt 는 Qt DLL 만 동봉하며 MSVC 런타임은 별도

#### 해결
- 사용자 PC 에 **Visual C++ Redistributable for Visual Studio 2015–2022 (x64)** 설치
  - 다운로드: https://aka.ms/vs/17/release/vc_redist.x64.exe
  - `winget install Microsoft.VCRedist.2015+.x64`
- NSIS 인스톨러로 설치한 경우 일반적으로 `vc_redist.x64.exe` 가 동봉되도록 windeployqt 가 처리하나, 누락 의심 시 위 수동 설치 권장. 향후 NSIS pre-install 단계에서 자동 실행 옵션 검토 (대상 외)

### 11.4 Qt platform plugin `qwindows.dll` 미발견

#### 증상
- exe 실행 시 "This application failed to start because no Qt platform plugin could be initialized. Reinstalling the application may fix this problem."
- 또는 `qt.qpa.plugin: Could not find the Qt platform plugin "windows" in ""`

#### 원인
- 실행파일과 같은 디렉토리의 `platforms\qwindows.dll` 가 없음
- 보통 build 디렉토리에서 직접 실행 시 windeployqt POST_BUILD 가 실패했거나, 인스톨러가 아닌 단순 exe 복사로 배포한 경우

#### 해결
- Build 트리 실행: `cmake --build build-win --config Release` 가 windeployqt 단계까지 포함되었는지 확인. 로그에 `windeployqt: OnionmixerChatManagerQt5` 라인이 있어야 함. 누락 시 Qt5_DIR 환경변수가 정확한지 점검
  ```powershell
  $env:Qt5_DIR  # 예: C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5
  Get-Command windeployqt    # PATH 에 잡혀 있는지
  ```
- 수동 deploy:
  ```powershell
  & "C:\Qt\5.15.2\msvc2019_64\bin\windeployqt.exe" --release `
      --no-translations --no-system-d3d-compiler --no-opengl-sw `
      --no-quick-import --no-virtualkeyboard `
      .\build-win\Release\OnionmixerChatManagerQt5.exe
  ```
- 인스톨러로 설치한 경우엔 `C:\Program Files\OnionmixerChatManager\bin\platforms\qwindows.dll` 존재 확인. 부재 시 인스톨러 재실행 (`CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL` 로 깨끗이 재설치됨)

### 11.5 사용자 설정 디렉토리 위치

#### 메인 앱 (OnionmixerChatManagerQt5)
- 기본: `<exe와 같은 폴더>\config\` (Linux 와 동일 우선순위)
  - NSIS 기본 설치 시 `C:\Program Files\OnionmixerChatManager\bin\config\` — UAC 보호 영역이라 일반 사용자 권한으로 쓰기 불가 가능
- 권장: `--config-dir` CLI 옵션으로 사용자 영역 지정
  ```powershell
  & "C:\Program Files\OnionmixerChatManager\bin\OnionmixerChatManagerQt5.exe" `
      --config-dir "$env:APPDATA\OnionmixerChatManagerQt5"
  ```

#### BroadChatClient (OnionmixerBroadChatClient)
- 기본: `%APPDATA%\OnionmixerBroadChatClient\<bucket>\` (`ConfigPathResolver` Windows 분기, Linux `~/.config/onionmixer-bcc/<bucket>/` 등가)
- 시작 메뉴 바로가기로 실행 시 자동 사용 — 일반적으로 별도 설정 불필요

### 11.6 BroadChatClient 트레이 아이콘 미표시

#### 증상
- BroadChatClient 실행 후 작업 표시줄·트레이 어디에도 아이콘 없음

#### 원인
- Windows 11 의 트레이 영역은 기본적으로 새 앱 아이콘을 **숨김 영역(▲)** 에 둠
- (드물게) `QSystemTrayIcon::isSystemTrayAvailable()` false — 보통은 시스템 GUI 세션이 아닌 경우

#### 해결
- 작업 표시줄 트레이의 `▲` 클릭 → BroadChat 아이콘을 메인 영역으로 드래그 (Windows 11 기준)
- 또는 설정 → 개인 설정 → 작업 표시줄 → 시스템 트레이 아이콘 → BroadChat 활성화

## 로그 수집 (이슈 제보 시)

### Linux

```bash
# 서버 앱 (GUI 프로세스)
OnionmixerChatManagerQt5 2>&1 | tee /tmp/main-app.log

# 환경변수로 trace 활성화
ONIONMIXERCHATMANAGER_BROADCHAT_TRACE=1 OnionmixerChatManagerQt5 2>&1 | tee /tmp/main-trace.log

# 클라
OnionmixerBroadChatClient 2>&1 | tee /tmp/client.log

# 동시
tail -F /tmp/main-app.log /tmp/client.log
```

### Windows (PowerShell)

GUI subsystem 빌드(`WIN32_EXECUTABLE`)이므로 stdout/stderr 가 콘솔에 직접 출력되지 않는다. 두 가지 방법:

```powershell
# 1) cmd 의 redirection 으로 파일 캡처 (PowerShell 의 `2>&1` 은 GUI 앱에서 제한적)
cmd /c '"C:\Program Files\OnionmixerChatManager\bin\OnionmixerChatManagerQt5.exe" 2>&1 > %TEMP%\main-app.log'

# 2) trace 활성화 + tail 등가 (Get-Content -Wait)
$env:ONIONMIXERCHATMANAGER_BROADCHAT_TRACE = "1"
$env:QT_LOGGING_RULES = "broadchat*=true"
Start-Process -FilePath "C:\Program Files\OnionmixerChatManager\bin\OnionmixerChatManagerQt5.exe" `
    -RedirectStandardOutput "$env:TEMP\main-app.log" `
    -RedirectStandardError "$env:TEMP\main-app.err"
Get-Content "$env:TEMP\main-app.log" -Wait
```

DebugView (Sysinternals) 로 `OutputDebugString` 도 함께 캡처하면 Qt 의 `qWarning`/`qDebug` 가 보인다 — 다만 본 프로젝트는 stdout 경로를 우선 사용하므로 보조 수단.

**민감정보 주의**: 로그에 peer IP는 포함되나 auth_token은 제외됨 (v21-γ-9). 그러나 LAN IP·clientId가 노출되므로 public 이슈 첨부 시 마스킹 권장.
