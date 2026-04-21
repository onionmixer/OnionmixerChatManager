# OnionmixerBroadChatClient

외부 방송 오버레이 앱. 메인 앱(`OnionmixerChatManagerQt5`)이 수신한 YouTube·CHZZK 채팅을 TCP로 받아 투명/불투명 채팅 창으로 렌더링합니다. 동일 PC(`127.0.0.1:47123`) · LAN · WAN(auth_token 필수) 모두 동일 바이너리로 지원.

자세한 설계는 상위 디렉토리의 `PLAN_DEV_BROADCHATCLIENT.md` 참조.

## 빌드

메인 프로젝트와 함께 한 번에 빌드됩니다.

```bash
cmake -S . -B build
cmake --build build -j
```

산출물: `build/BroadChatClient/OnionmixerBroadChatClient`

개별 빌드:

```bash
cmake --build build --target OnionmixerBroadChatClient
```

CMake 옵션으로 비활성 가능:

```bash
cmake -S . -B build -DONIONMIXERCHATMANAGER_BUILD_BROADCHATCLIENT=OFF
```

요구사항: Qt 5.15.2+ · C++17 · CMake 3.16+ · GCC 11+ 또는 Clang 14+.

## 실행

기본 — 같은 PC에서 메인 앱이 실행 중이라고 가정:

```bash
./build/BroadChatClient/OnionmixerBroadChatClient
```

다른 PC(LAN):

```bash
./OnionmixerBroadChatClient --host 192.168.1.10 --port 47123
```

인증 토큰 요구 환경:

```bash
./OnionmixerBroadChatClient --host 192.168.1.10 --auth-token <token>
```

3개 인스턴스 동시 실행:

```bash
./OnionmixerBroadChatClient --instance-id main
./OnionmixerBroadChatClient --instance-id obs-overlay
./OnionmixerBroadChatClient --instance-id mobile-mirror
```

각 인스턴스는 독립된 config 디렉토리·창 위치·스타일을 가집니다.

## CLI 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `--host <addr>` | `127.0.0.1` | 서버 주소 (IPv4/IPv6/hostname 허용) |
| `--port <n>` | `47123` | TCP 포트 (1024~65535) |
| `--auth-token <str>` | (없음) | 서버 `auth_token` 설정 시 필수 |
| `--config-dir <path>` | (자동) | config.ini 저장 디렉토리 명시 |
| `--instance-id <name>` | `default` | 다중 인스턴스 구분자 |
| `--version` | — | 버전·git hash·빌드 날짜 출력 후 종료 |
| `--help` | — | 사용법 출력 후 종료 |

**주의**: `--auth-token`은 shell 이력·프로세스 리스트에 평문 노출. 운영 환경에서는 설정창 또는 `config.ini` 저장 권장.

## 설정 파일 (`config.ini`)

경로는 CLI `--config-dir` 또는 자동 결정:

- Linux 개발 빌드: `<exe>/BroadChatClient/<instance>/config.ini`
- Linux 시스템 설치: `~/.config/onionmixer-bcc/<instance>/config.ini`
- Windows: `%APPDATA%\OnionmixerBroadChatClient\<instance>\config.ini`
- macOS: `~/Library/Application Support/OnionmixerBroadChatClient/<instance>/config.ini`

스키마:

```ini
[connection]
host=127.0.0.1
port=47123
auth_token=

[window]
x=100
y=200
width=400
height=600
```

auth_token 포함 시 권한은 자동으로 `0600`(POSIX) 로 설정됩니다.

## 종료 코드

| 코드 | 의미 |
|------|------|
| 0 | 정상 종료 (우클릭 "종료" · 창 close · `auth_failed`/`version_mismatch` 모달 "종료" 선택) |
| 1 | 런타임 에러 (config 로드 실패 등) |
| 2 | CLI 파싱 에러 (알 수 없는 인자 · invalid port) |
| 3 | 환경 에러 (config dir 쓰기 불가 · instance-id 충돌) |

## 조작

- **우클릭** → `세팅` / `종료` 메뉴
- **더블클릭** → 투명/불투명 모드 전환
- **드래그** → 창 이동 (프레임리스 모드)

## 트러블슈팅 (FAQ)

**1. `연결 대기 중…` 상태가 계속됨**

메인 앱이 켜져있고 Connect 버튼을 눌렀는지 확인. 메인 앱 서버는 방송 연결 수명 내에서만 listen됩니다 (IDLE 상태에서는 안 들어옴).

```bash
# 포트 listen 확인 (Linux)
ss -ltn | grep 47123
```

**2. `인증 실패` 다이얼로그**

메인 앱의 `[broadchat] auth_token` 설정과 클라의 `--auth-token` / `config.ini`의 `auth_token` 이 정확히 일치해야 합니다. 공백·대소문자 주의.

**3. `버전 불일치` 다이얼로그**

메인 앱과 클라의 protocol version 불일치. 양쪽을 같은 릴리즈로 맞추세요 (`--version` 으로 확인).

**4. Docker/컨테이너에서 실행 시 config 저장 실패**

read-only 파일시스템이면 `--config-dir /tmp/bcc` 로 명시:

```bash
docker run --tmpfs /tmp ... OnionmixerBroadChatClient --config-dir /tmp/bcc
```

**5. 외부 호스트에서 접속 안 됨**

메인 앱의 `tcp_bind`가 기본 `127.0.0.1` (loopback)이라 외부에서 접속 불가. 운영자가 메인 앱의 `[broadchat] tcp_bind=0.0.0.0` 으로 변경 + 방화벽 + `auth_token` 필수.

**6. 투명 모드에서 X 버튼 없음**

프레임리스 전용. 우클릭 → `종료` 또는 시스템 트레이 (v2+). 현재는 우클릭만 지원.

**7. 이모지가 `?` 또는 broken-image로 표시**

서버 측 `EmojiImageCache`가 아직 이미지를 다운로드하지 못한 경우. 수 초 대기. 지속되면 서버 로그 확인 (네트워크 문제 또는 CDN 차단).

**8. 한글 폰트 렌더 깨짐 (Windows)**

시스템 기본 폰트가 CJK 미지원. 설정 창에서 `Malgun Gothic` 또는 `Noto Sans CJK` 지정.

**9. `instance-id already in use` 에러 (exit 3)**

같은 `--instance-id`로 이미 다른 클라가 실행 중. `.lock` 파일이 잔존하면 수동 삭제:

```bash
rm <config-dir>/<instance>/.lock
```

**10. 로그 확인**

기본적으로 stderr 출력. 파일로 redirect:

```bash
./OnionmixerBroadChatClient 2>> ~/bcc.log
```

상세 로그 활성화:

```bash
QT_LOGGING_RULES="broadchat.trace=true" ./OnionmixerBroadChatClient
```

## 플랫폼별 참고

### Linux — GNOME system tray 지원

GNOME Shell 3.26+ 은 레거시 system tray 를 기본 미지원합니다. Tray 아이콘 사용을 원하면 **AppIndicator** 확장을 설치:

```bash
# Fedora / RHEL
sudo dnf install gnome-shell-extension-appindicator

# Ubuntu / Debian
sudo apt install gnome-shell-extension-appindicator

# 이후 GNOME Extensions 앱에서 활성화하거나:
gnome-extensions enable appindicatorsupport@rgcjonas.gmail.com
# 재로그인 또는 `Alt+F2 → r → Enter` 로 Shell 재시작
```

설치하지 않아도 앱은 정상 동작합니다 — `QSystemTrayIcon::isSystemTrayAvailable()` 가 false 를 반환하면 tray 생성을 생략하고 **방송창 우클릭 메뉴**로 "세팅"·"종료" 접근 가능 (graceful fallback).

KDE Plasma·XFCE·Cinnamon·MATE 는 기본적으로 tray 지원.

### Linux — `.desktop` 파일 · 아이콘 설치

`make install` (또는 CPack DEB 패키지 설치) 시 자동 배치:

- `/usr/share/applications/onionmixer-bcc.desktop` — 데스크톱 런처
- `/usr/share/icons/hicolor/{48,128,256}x{48,128,256}/apps/onionmixer-bcc.png` — XDG Icon Theme

설치 후 `update-desktop-database` · `gtk-update-icon-cache` 가 필요할 수 있습니다 (패키지 관리자가 자동 실행).

### Windows · macOS

현재 검증 주 타겟은 Linux. Qt 5.15 기반이라 이론적으로 크로스빌드 가능하며, `QSystemTrayIcon` · 아이콘 리소스 (`.ico` · `.icns`) 는 이미 배포 준비됨. OS별 installer (NSIS / WiX / DMG) 는 후속 릴리즈 예정.

## 버그 리포트

이슈 제출 시 다음 정보 포함 권장:

1. `--version` 전체 출력 (git hash · 빌드 날짜 포함)
2. OS · Qt 버전 (`dpkg -l libqt5core5a` 또는 상당)
3. 재현 단계
4. stderr 로그 마지막 100 라인 (**auth_token 값은 삭제**)
5. `config.ini` 내용 (**auth_token 값은 삭제**)

## 라이선스

메인 프로젝트와 동일 라이선스 적용. Qt 5.15는 LGPLv3 / GPLv3 / Commercial triple-license, 동적 링크 사용.
