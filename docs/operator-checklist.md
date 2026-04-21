# Operator Checklist

운영자가 OnionmixerChatManagerQt5 v0.1.0 배포·설치·운영할 때 순서대로 확인할 체크리스트.

## 사전 준비

- [ ] 대상 OS: Ubuntu 22.04+ (또는 호환 Debian 계열)
- [ ] Qt5 런타임 설치 가능 (`apt` repository 접근)
- [ ] 방송 PC와 렌더 PC 모두 동일 네트워크 (LAN/VPN) 또는 SSH 접근 가능
- [ ] YouTube·CHZZK OAuth Client ID 발급 완료 (README §4 참고)

## 1. 설치

### 1.1 방송 PC (서버)

```bash
sudo apt install ./onionmixerchatmanagerqt5_0.1.0_amd64.deb
# 의존성으로 onionmixerbroadchatclient도 자동 설치됨
```

- [ ] `/usr/bin/OnionmixerChatManagerQt5` 존재
- [ ] `/usr/bin/OnionmixerBroadChatClient` 존재 (의존성)
- [ ] `OnionmixerChatManagerQt5 --version` → `0.1.0 (sha) proto=1 built=...` 출력

### 1.2 렌더 PC (클라만)

```bash
sudo apt install ./onionmixerbroadchatclient_0.1.0_amd64.deb
```

- [ ] `/usr/bin/OnionmixerBroadChatClient` 존재
- [ ] `/usr/bin/OnionmixerChatManagerQt5` **미설치** (클라만 설치됐음 확인)
- [ ] `OnionmixerBroadChatClient --version` 정상 출력

## 2. 방송 PC — 초기 설정

### 2.1 config.ini 생성

```bash
mkdir -p ~/.config/OnionmixerChatManager
cp /usr/share/OnionmixerChatManagerQt5/config.example/app.ini.example \
   ~/.config/OnionmixerChatManager/app.ini
cp /usr/share/OnionmixerChatManagerQt5/config.example/tokens.ini.example \
   ~/.config/OnionmixerChatManager/tokens.ini
chmod 0600 ~/.config/OnionmixerChatManager/*.ini
```

- [ ] `~/.config/OnionmixerChatManager/app.ini` 권한 `-rw-------` (0600)

### 2.2 플랫폼 OAuth

- [ ] 메인 앱 실행 → Configuration → Platform 탭
- [ ] YouTube `client_id`·`client_secret` 입력 → Apply
- [ ] YouTube 브라우저 인증 완료 → tokens.ini에 access/refresh token 저장
- [ ] CHZZK 동일 절차

### 2.3 BroadChat 서버 설정

- [ ] Configuration → General 탭에서 `BroadChat Port` 확인 (기본 47123)
- [ ] 원격 접속 필요하면 `~/.config/OnionmixerChatManager/app.ini` 직접 편집:
  ```ini
  [broadchat]
  tcp_bind=0.0.0.0        # 또는 특정 NIC IP
  ```
- [ ] 원격 접속 시 `BroadChat Auth Token` → Generate 버튼 → UUID v4 생성 → Copy
- [ ] 메인 앱 재시작

### 2.4 방화벽 (원격 접속 시)

```bash
# Ubuntu (ufw)
sudo ufw allow from <client-subnet> to any port 47123 proto tcp
sudo ufw reload

# iptables
sudo iptables -A INPUT -s <client-subnet> -p tcp --dport 47123 -j ACCEPT
```

- [ ] `ss -tlnp | grep 47123` — listen 상태 확인
- [ ] 원격 PC에서 `nc -zv <server-ip> 47123` — 도달 가능 확인

## 3. 렌더 PC — 클라 설정

### 3.1 config.ini 생성

```bash
mkdir -p ~/.config/OnionmixerBroadChatClient/default
cat > ~/.config/OnionmixerBroadChatClient/default/config.ini <<EOF
[connection]
host=<서버 PC IP>
port=47123
auth_token=<서버에서 Copy한 UUID>
EOF
chmod 0600 ~/.config/OnionmixerBroadChatClient/default/config.ini
```

- [ ] 파일 권한 0600

### 3.2 실행 + 접속 확인

```bash
OnionmixerBroadChatClient --config-dir ~/.config/OnionmixerBroadChatClient/default
```

- [ ] 3초 이내 연결 성공 (상태 배지 녹색)
- [ ] 메인 앱 상태바 `clients: 1` 녹색 표시

### 3.3 다중 인스턴스 (같은 PC에 여러 창)

```bash
OnionmixerBroadChatClient --instance-id overlay1 &
OnionmixerBroadChatClient --instance-id overlay2 &
```

- [ ] 각 인스턴스 독립 config·창 위치·토큰 저장
- [ ] 메인 앱 `clients: N` 카운트 정확

## 4. 운영 중 확인

### 4.1 일상 점검 (주 1회 권장)

- [ ] 메인 앱 로그 `journalctl --user -u OnionmixerChatManagerQt5` — error 없음
- [ ] 클라 접속 수 예상 범위
- [ ] TIME_WAIT 누적 정상 수준 (< 10)
  ```bash
  ss -tan | awk '$1=="TIME-WAIT"' | grep ':47123' | wc -l
  ```

### 4.2 auth_token 교체 (월 1회 권장)

- [ ] 메인 앱 설정 창 → Generate 새 UUID → Copy → OK → 재시작
- [ ] 모든 클라 PC의 config.ini `auth_token` 갱신 → 클라 재시작

### 4.3 업그레이드

```bash
# v0.1.1 등 패치 릴리즈 수신 시
sudo apt install ./onionmixerchatmanagerqt5_0.1.1_amd64.deb
# 클라도 자동 업그레이드 (의존성)
```

- [ ] 서버·클라 **동시 업그레이드** (protocol freeze 이전엔 같은 버전 권장)
- [ ] 업그레이드 후 재연결 정상

## 5. 보안 체크

- [ ] `config/app.ini`·`tokens.ini`가 git에 push되지 않음 (`.gitignore` 확인)
- [ ] `tcp_bind=0.0.0.0` 사용 시 auth_token **반드시 설정**
- [ ] WAN 노출 시 **VPN 필수** ([docs/vpn-guide.md](vpn-guide.md))
- [ ] 토큰 전달은 안전 채널 (암호화 메신저·대면) — 평문 이메일·공용 Slack 금지
- [ ] 로그 공유 전 IP·clientId 마스킹

## 6. 제거

```bash
# 서버만 제거 (클라는 유지)
sudo apt remove onionmixerchatmanagerqt5

# 클라도 함께 제거 (의존성 없으면 자동)
sudo apt autoremove onionmixerbroadchatclient
```

- [ ] 설정·토큰 파일은 `~/.config/` 수동 삭제 필요:
  ```bash
  rm -rf ~/.config/OnionmixerChatManager ~/.config/OnionmixerBroadChatClient
  ```

## 7. 이슈 발생 시

1. [docs/troubleshooting.md](troubleshooting.md)에서 증상·원인·대응 확인
2. 로그 수집 (trace 포함):
   ```bash
   ONIONMIXERCHATMANAGER_BROADCHAT_TRACE=1 OnionmixerChatManagerQt5 2>&1 | tee /tmp/debug.log
   ```
3. GitHub Issue 제보 시 첨부 **전에 마스킹** (peer IP·clientId 등)
