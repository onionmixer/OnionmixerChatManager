# VPN 터널링 가이드

OnionmixerChatManagerQt5는 **TLS·암호화를 도입하지 않습니다 (v23 확정)**. 암호화된 원격 운영이 필요하면 외부 VPN 솔루션으로 네트워크 레벨 터널링을 구성합니다.

이 문서는 두 가지 권장 VPN을 예시로 제시. 어느 쪽이든 VPN이 "LAN-equivalent"를 제공하므로 앱은 `tcp_bind=127.0.0.1` 또는 VPN 인터페이스 IP로만 listen하면 됩니다.

## 왜 VPN인가

| 위협 | 앱 내장 대응 | VPN으로 해결 |
|------|-------------|--------------|
| LAN sniffer (평문 노출) | ❌ (TLS 비도입) | ✅ VPN 암호화 |
| WAN 공개 노출 | ❌ | ✅ VPN 전용 터널 |
| MITM 공격 | ❌ | ✅ VPN 인증 기반 |
| Replay 공격 | ❌ (v22-2) | ✅ VPN nonce/sequence |
| 방화벽 포트 노출 실수 | 설정 창 경고 | ✅ VPN으로 공개 포트 0 |

## WireGuard 예시

### 개요
- peer-to-peer VPN
- 커널 모듈 (Linux 5.6+)·또는 `wireguard-go`
- 단순 설정·빠른 성능

### 시나리오
- 방송 PC (서버): `10.0.0.1` WireGuard 인터페이스
- 렌더 PC (클라): `10.0.0.2`

### 서버 PC 설정 (`/etc/wireguard/wg0.conf`)

```ini
[Interface]
PrivateKey = <server-private-key>
Address = 10.0.0.1/24
ListenPort = 51820

[Peer]
PublicKey = <client-public-key>
AllowedIPs = 10.0.0.2/32
```

### 클라이언트 PC 설정 (`/etc/wireguard/wg0.conf`)

```ini
[Interface]
PrivateKey = <client-private-key>
Address = 10.0.0.2/24

[Peer]
PublicKey = <server-public-key>
Endpoint = <server-public-ip>:51820
AllowedIPs = 10.0.0.1/32
PersistentKeepalive = 25
```

### 활성화

```bash
# 양쪽 PC
sudo wg-quick up wg0
sudo systemctl enable wg-quick@wg0
```

### 앱 설정

메인 앱 (서버 PC):
```ini
[broadchat]
tcp_bind=10.0.0.1       # WireGuard 인터페이스만 listen
tcp_port=47123
auth_token=              # VPN 신뢰 환경이면 빈 값도 OK
```

BroadChatClient (클라 PC):
```bash
OnionmixerBroadChatClient --host 10.0.0.1 --port 47123
```

**외부 인터페이스 `0.0.0.0`에는 listen하지 않음** — WAN 노출 완전 차단.

## Tailscale 예시

### 개요
- WireGuard 기반 managed mesh VPN
- 계정 기반 peer 관리·MagicDNS
- 설정 복잡도 매우 낮음

### 설정

```bash
# 양쪽 PC
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up
# 브라우저 인증 → 동일 Tailscale 계정으로 로그인

# 서버 PC IP 확인
tailscale ip -4
# 예: 100.64.1.5
```

### 앱 설정

메인 앱 (서버 PC):
```ini
[broadchat]
tcp_bind=100.64.1.5     # Tailscale IP
tcp_port=47123
```

클라 PC:
```bash
OnionmixerBroadChatClient --host 100.64.1.5 --port 47123
# 또는 MagicDNS
OnionmixerBroadChatClient --host my-server-pc --port 47123
```

### ACL (Tailscale admin console)

Tailscale admin에서 `47123` 포트 트래픽을 방송 PC → 클라 PC 방향으로만 허용하도록 ACL 설정 권장.

## SSH 터널 (대안)

WireGuard·Tailscale 설정이 부담스러운 경우 SSH 포트 포워딩으로 임시 터널링:

```bash
# 클라 PC에서 실행 — 서버 PC의 47123을 로컬 47123으로 매핑
ssh -N -L 47123:127.0.0.1:47123 <server-user>@<server-ip>

# 동일 터미널 유지 상태에서 클라 실행
OnionmixerBroadChatClient --host 127.0.0.1 --port 47123
```

**제약**: SSH 세션 유지 필요. 연결 끊김 시 클라도 재연결 실패. 자동화엔 `autossh` 고려.

## 선택 가이드

| 시나리오 | 권장 |
|----------|------|
| 홈 LAN (같은 라우터) | VPN 불필요 (bind=127.0.0.1 또는 192.168.x.x) |
| 사무실 내부 LAN | VPN 선택 (auth_token으로 완화 가능) |
| 다른 건물·WAN | **WireGuard 또는 Tailscale 필수** |
| 일회성·임시 | SSH 터널 |
| 멀티 운영자 (팀 단위) | Tailscale (ACL·SSO 관리 용이) |

## 비교

| 항목 | WireGuard | Tailscale | SSH 터널 |
|------|-----------|-----------|---------|
| 초기 설정 복잡도 | 중 (key 교환) | 낮음 (계정 로그인) | 매우 낮음 |
| 무료 사용 | 무제한 | 100 device (Personal) | 무제한 |
| 외부 서비스 의존 | 없음 | Tailscale coordinator | SSH 서버 |
| NAT 뚫기 | 수동 (port forward) | 자동 (NAT traversal) | SSH 서버만 공개 |
| 성능 | 최고 | 매우 좋음 | 보통 (TCP-over-TCP) |

## 참고

- WireGuard 공식: https://www.wireguard.com/
- Tailscale 공식: https://tailscale.com/
- SSH 터널: `man ssh` `-L` 옵션
