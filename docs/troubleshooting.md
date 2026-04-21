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

## 로그 수집 (이슈 제보 시)

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

**민감정보 주의**: 로그에 peer IP는 포함되나 auth_token은 제외됨 (v21-γ-9). 그러나 LAN IP·clientId가 노출되므로 public 이슈 첨부 시 마스킹 권장.
