# Qt5 기반 YouTube + NAVER 치지직 통합 채팅 봇 개발 계획

## 1. 목표

Qt5 기반 데스크톱 클라이언트로 다음 기능을 제공하는 봇/운영 도구를 개발한다.

- YouTube Live Chat 지원
- NAVER 치지직 채팅 지원
- 두 플랫폼 채팅을 하나의 화면에서 통합 조회
- YouTube ID와 NAVER/치지직 ID를 별도로 관리
- 설정은 INI 파일 기반으로 관리
- 토큰은 기본적으로 Qt5 클라이언트 내부 저장소에 보관
- 토큰 만료 시 갱신 기능 제공
- 향후 자동 응답/봇 로직 확장이 가능한 구조로 설계

## 2. 요구사항 해석

### 필수 요구사항

- 플랫폼별 계정 식별 정보는 분리 저장해야 한다.
- 설정 파일은 INI 포맷을 사용해야 한다.
- Access Token / Refresh Token 은 평문 INI에 두지 않고, 클라이언트 내부 저장소에서 관리해야 한다.
- 두 플랫폼의 채팅 메시지를 시간순으로 합쳐서 한 화면에서 볼 수 있어야 한다.
- 나중에 토큰 재발급 또는 강제 갱신 기능을 UI에서 실행할 수 있어야 한다.
- 치지직은 Open API 기준으로 실제 식별값이 `NAVER 로그인 ID 문자열`이 아니라 `channelId` 중심이라는 점을 반영해야 한다.
- 프로그램 시작 시 `Connect` 버튼이 있어야 하며, `Configuration`에 저장된 INI 설정으로 플랫폼 접속을 시작해야 한다.
- 접속 세션이 활성화되면 버튼 캡션은 `Disconnect`로 전환되어야 한다.

### 권장 요구사항

- 기본 브라우저를 이용한 OAuth 인증
- 연결 상태, 토큰 만료 상태, 재연결 상태를 UI에서 확인 가능
- 플랫폼별 ON/OFF, 필터, 색상 구분 지원
- 향후 봇 자동응답 엔진을 붙일 수 있도록 플랫폼 어댑터 분리

## 3. 조사 결과 요약

### 3.1 YouTube

- 현재 라이브 채팅 수신은 `liveChatMessages.streamList` 사용이 가장 적합하다. 공식 문서상 이 방식은 저지연으로 신규 메시지를 푸시하며, 일반 polling 보다 quota 소모와 과도한 요청 위험을 줄이는 방향으로 안내된다.
- 폴백으로 `liveChatMessages.list` 를 사용할 수 있으며, 응답의 `pollingIntervalMillis` 를 반드시 따라야 한다.
- 활성 방송 탐색은 `liveBroadcasts.list?mine=true&broadcastStatus=active` 로 시작하는 것이 적절하다.
- 방송의 채팅 식별자는 `liveBroadcast.snippet.liveChatId` 를 통해 확보한다.
- 메시지 전송은 `liveChatMessages.insert` 로 가능하다.
- 데스크톱 OAuth는 Google 권장 방식상 브라우저 기반 Authorization Code + PKCE + loopback redirect(`127.0.0.1`) 조합으로 설계하는 것이 안전하다.
- Google 문서 기준으로 refresh token 은 `access_type=offline` 요청 시 발급되며, 안전한 장기 저장이 필요하다.

### 3.2 NAVER 치지직

- 치지직 Open API 는 Authorization Code 기반 인증을 제공한다.
- `redirectUri` 는 개발자 등록 정보와 정확히 일치해야 한다.
- Access Token 만료는 1일, Refresh Token 만료는 30일이다.
- 치지직 Refresh Token 은 일회용으로 사용되므로, 갱신 성공 시 새 refresh token 으로 원자적으로 교체해야 한다.
- 로그인 사용자 정보 조회 API(`/open/v1/users/me`) 에서 `channelId` 를 얻을 수 있으며, 문서상 이 값은 유저 고유 식별자로 사용할 수 있다.
- 실시간 채팅 수신은 Session API + Socket.IO 연결 기반이다.
- Session 연결 완료 후 `sessionKey` 를 받아 채팅 이벤트를 구독한다.
- 채팅 이벤트에는 `profile.nickname`, `content`, `messageTime` 등이 포함되어 통합 UI 모델링에 충분하다.
- 치지직 채팅 전송 API도 별도로 존재하므로, 추후 수동 발송/봇 응답 기능 확장이 가능하다.

### 3.3 Qt5 구현 관점

- `QSettings` 는 `IniFormat` 을 공식 지원하므로 ID, 채널 설정, UI 옵션 저장에 적합하다.
- Qt 5.15 의 `QOAuth2AuthorizationCodeFlow` 는 Authorization Code flow 와 refresh 를 지원한다.
- 다만 Google native app 권장 PKCE 파라미터를 Qt5 기본 API가 직접 노출하지 않으므로, YouTube OAuth 는 수동 구현 또는 wrapper 확장이 더 안전하다.
- 따라서 OAuth 처리 계층은 플랫폼별로 분리하고, 공통 UI만 공유하는 구조가 맞다.

### 3.4 운영자 액션 조사 결과

#### YouTube에서 공식 문서로 확인된 운영 액션

- 채팅 메시지 전송: `liveChatMessages.insert`
- 메시지 삭제: `liveChatMessages.delete`
- 유저 제재: `liveChatBans.insert`
- 제재 해제: `liveChatBans.delete`
- 운영자 추가: `liveChatModerators.insert`
- 운영자 제거: `liveChatModerators.delete`
- 운영자 목록 조회: `liveChatModerators.list`
- 활성 투표 종료: `liveChatMessages.transition`

YouTube 채팅 메시지 리소스에는 다음이 포함된다.

- 메시지 ID: `liveChatMessage.id`
- 메시지 작성자 채널 ID: `snippet.authorChannelId`
- 작성자 표시명/프로필/권한: `authorDetails.displayName`, `authorDetails.profileImageUrl`, `authorDetails.isChatOwner`, `authorDetails.isChatModerator`, `authorDetails.isChatSponsor`

즉, YouTube는 `메시지 단위 액션`과 `사용자 단위 액션` 모두를 구현하기 좋은 편이다.

#### 치지직에서 공식 문서로 확인된 운영 액션

- 채팅 메시지 전송: `POST /open/v1/chats/send`
- 채팅 공지 등록: `POST /open/v1/chats/notice`
- 채팅 설정 조회: `GET /open/v1/chats/settings`
- 채팅 설정 변경: `PUT /open/v1/chats/settings`
- 활동 제한 추가: `POST /open/v1/restrict-channels`
- 활동 제한 삭제: `DELETE /open/v1/restrict-channels`
- 활동 제한 목록 조회: `GET /open/v1/restrict-channels`
- 채널 관리자 조회: `GET /open/v1/channels/streaming-roles`
- 채널 팔로워 조회: `GET /open/v1/channels/followers`
- 채널 구독자 조회: `GET /open/v1/channels/subscribers`

치지직 채팅 이벤트 메시지에는 다음이 포함된다.

- 작성자 채널 ID: `senderChannelId`
- 닉네임: `profile.nickname`
- 배지: `profile.badges`
- 인증 여부: `profile.verifiedMark`
- 권한: `profile.userRoleCode`
- 메시지 내용: `content`
- 메시지 시간: `messageTime`

문서 기준으로 치지직은 `사용자 단위 액션`과 `채널 설정 액션`은 강하지만, 수신 `CHAT` 이벤트에 `messageId` 필드가 명시되어 있지 않아 `특정 수신 메시지 자체를 직접 조작하는 액션`은 제한된다.

#### 공통 가능 여부 검토

- 공통으로 바로 구현 가능한 것
  - 운영자 메시지 전송
  - 채팅 사용자 닉네임/권한/배지 표시
  - 채팅 사용자 클릭 후 대상 액션 메뉴 열기
- 개념적으로 공통이지만 API 의미가 완전히 같지는 않은 것
  - 사용자 제재
  - 제재 해제
- 한쪽만 공식 문서로 명확히 확인된 것
  - YouTube: 특정 메시지 삭제, 운영자 추가/삭제, 임시 timeout, 영구 ban, 투표 종료
  - 치지직: 채팅 공지 등록, 슬로우 모드/이모티콘 모드/팔로워 모드 등 채팅 설정 변경, 활동 제한 목록 조회

중요한 차이는 다음과 같다.

- YouTube `unban` 은 `ban id` 가 필요하고, 리소스 문서상 `list` 메서드가 없어 앱이 직접 생성한 ban ID 를 보관하거나 ban 이벤트를 추적해야 한다.
- YouTube 운영자 제거는 `moderator id` 가 필요하므로 `liveChatModerators.list` 로 채널 ID -> moderator ID 매핑을 유지해야 한다.
- 치지직 활동 제한 해제는 `targetChannelId` 기반이라 사용자 클릭 액션과 바로 연결하기 쉽다.
- 치지직 공지 등록은 문서상 `message` 또는 기존 `messageId` 로 가능하지만, 수신 CHAT 이벤트 문서에는 `messageId` 가 없으므로 임의의 수신 메시지를 곧바로 공지로 승격하는 기능은 문서만으로는 보장되지 않는다.

## 4. 핵심 설계 원칙

- 설정과 비밀정보를 분리한다.
- 플랫폼별 인증/연결 방식을 공통 인터페이스 뒤로 숨긴다.
- UI는 통합 뷰를 제공하되, 내부적으로는 플랫폼별 상태를 독립 관리한다.
- 토큰 갱신 실패, 세션 끊김, 권한 회수 같은 운영 이슈를 UI에서 즉시 식별 가능해야 한다.
- 봇 로직은 채팅 수집부와 분리하여 이후 자동응답 기능 추가 시 재작성 비용을 줄인다.

## 5. 권장 아키텍처

### 5.1 계층 구조

1. `UI Layer`
- 메인 윈도우
- 통합 채팅 뷰
- 플랫폼 상태 패널
- 인증/설정 대화상자

2. `Application Layer`
- 채팅 통합 서비스
- 명령/봇 엔진
- 토큰 갱신 오케스트레이터
- 연결 오케스트레이터(`ConnectionCoordinator`)
- 로그/알림 서비스

3. `Platform Adapter Layer`
- `YouTubeAdapter`
- `ChzzkAdapter`
- 공통 인터페이스 `IChatPlatformAdapter`

4. `Infrastructure Layer`
- `QNetworkAccessManager`
- OAuth callback 로컬 서버
- Socket.IO client
- 설정 저장소(`QSettings`)
- 보안 토큰 저장소(`TokenVault`)

### 5.2 권장 인터페이스

```cpp
struct UnifiedChatMessage {
    QString platform;          // youtube | chzzk
    QString messageId;         // platform native message id if available
    QString channelId;
    QString channelName;
    QString authorId;          // YouTube authorChannelId | CHZZK senderChannelId
    QString authorName;        // displayName | nickname
    QString authorAvatarUrl;
    QString authorRole;
    QStringList authorBadges;
    bool authorVerified = false;
    QString text;
    QDateTime timestamp;
    QVariantMap rawMeta;
};

struct ActionCapability {
    bool enabled = false;
    QString disabledReason;    // e.g. MISSING_MESSAGE_ID, NO_PERMISSION
};

class IChatPlatformAdapter : public QObject {
    Q_OBJECT
public:
    virtual QString platformId() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void refreshTokenIfNeeded() = 0;
    virtual bool canSendMessage() const = 0;
    virtual void sendMessage(const QString& text) = 0;
    virtual QStringList supportedActions() const = 0;
    virtual QMap<QString, ActionCapability> evaluateActionCapabilities(
        const UnifiedChatMessage& target) const = 0;
    virtual void executeAction(const QString& actionId, const UnifiedChatMessage& target) = 0;

signals:
    void chatReceived(const UnifiedChatMessage& msg);
    void statusChanged(const QString& status);
    void authRequired();
    void tokenExpired();
    void fatalError(const QString& message);
};
```

### 5.3 ConnectionCoordinator 구현 인터페이스

`ConnectionCoordinator`는 `Connect/Disconnect` 토글의 단일 실행 지점을 제공한다.  
UI는 직접 adapter를 호출하지 않고 이 클래스만 호출한다.

```cpp
enum class ConnectionState {
    IDLE,
    CONNECTING,
    PARTIALLY_CONNECTED,
    CONNECTED,
    DISCONNECTING,
    ERROR
};

struct ConnectSessionResult {
    ConnectionState state;
    QStringList connectedPlatforms;
    QMap<QString, QString> failedPlatforms; // platform -> reason
};

class ConnectionCoordinator : public QObject {
    Q_OBJECT
public:
    explicit ConnectionCoordinator(QObject* parent = nullptr);

    ConnectionState state() const;
    bool isBusy() const; // CONNECTING or DISCONNECTING
    void bindAdapters(const QMap<QString, IChatPlatformAdapter*>& adapters);

public slots:
    void connectAll(const AppSettingsSnapshot& snapshot); // INI snapshot 기반
    void disconnectAll();

signals:
    void stateChanged(ConnectionState state);
    void connectProgress(QString platform, QString phase); // e.g. STARTING, CONNECTED, FAILED
    void connectFinished(const ConnectSessionResult& result);
    void disconnectFinished();
    void warningRaised(const QString& code, const QString& message);

private:
    void setState(ConnectionState next);
    void finalizeConnectResult();
};
```

MainWindow 권장 배선:

- `btnConnectToggle.clicked` -> `onConnectToggleClicked()`
- `onConnectToggleClicked()`는 현재 `ConnectionState`에 따라 `connectAll(...)` 또는 `disconnectAll()` 호출
- `stateChanged` 수신 시 버튼 캡션/활성화 상태 갱신
- `connectFinished` 수신 시 실패 목록을 배너/로그에 표시

동시 실행 가드:

- `isBusy()==true`면 추가 connect/disconnect 요청을 무시하고 `warningRaised(BUSY, ...)` 발행
- `CONNECTING` 중 `Disconnect` 클릭은 `pendingDisconnect=true` 플래그로 큐잉 후 connect 종료 직후 해제 실행
- `DISCONNECTING` 중 `Connect` 클릭은 차단하고 완료 후 재시도 안내

## 6. 설정 및 토큰 저장 정책

### 6.1 저장 분리 원칙

- INI 파일: 비민감 설정 저장
- 클라이언트 내부 보안 저장소: Access Token / Refresh Token 저장

이 분리는 필수다. 치지직 refresh token 이 일회용이고 YouTube refresh token 이 장기 보관 대상이기 때문에, 평문 INI 저장은 피해야 한다.

### 6.2 INI 저장 대상

예시:

```ini
[app]
language=ko_KR
merge_order=timestamp
log_level=info

[youtube]
enabled=true
client_id=...
channel_id=UCxxxxxxxx
channel_handle=@example
redirect_uri=http://127.0.0.1:18080/youtube/callback
auth_endpoint=https://accounts.google.com/o/oauth2/v2/auth
token_endpoint=https://oauth2.googleapis.com/token
scope=https://www.googleapis.com/auth/youtube.readonly https://www.googleapis.com/auth/youtube.force-ssl

[chzzk]
enabled=true
client_id=...
client_secret=...
channel_id=
channel_name=
account_label=
redirect_uri=http://127.0.0.1:18081/chzzk/callback
auth_endpoint=https://chzzk.example.com/oauth/authorize
token_endpoint=https://chzzk.example.com/oauth/token
scope=유저 정보 조회 채팅 메시지 조회 채팅 메시지 전송
```

- YouTube 데스크톱 앱은 `client_id` 중심으로 운용하고 `client_secret` 은 두지 않거나 미사용 처리한다.
- 치지직은 API 식별값으로 `channel_id` 를 저장하고, 사용자가 구분하기 쉬운 값은 `account_label` 로 별도 저장한다.

### 6.3 토큰 저장 대상

플랫폼별 키를 분리한다.

- `token.youtube.access`
- `token.youtube.refresh`
- `token.youtube.expire_at`
- `token.chzzk.access`
- `token.chzzk.refresh`
- `token.chzzk.expire_at`

### 6.4 권장 저장 방식

우선순위는 다음과 같다.

1. `QtKeychain` 사용
- Windows Credential Manager
- macOS Keychain
- Linux libsecret/KWallet

2. 대체안
- 앱 전용 암호화 파일(`tokens.dat`) + OS 저장소 기반 마스터 키

결론적으로 `ID/채널 설정은 INI`, `토큰은 TokenVault` 로 분리한다.

### 6.5 Configuration 창 설계

설정은 메인 화면과 분리된 `Configuration` 창에서 관리한다.  
토큰 갱신/재인증은 반드시 이 창에서 시작되도록 통일한다.

권장 탭 구성:

1. `General`
- 언어/로그 레벨/시간표시 포맷

2. `YouTube`
- 활성화 스위치
- `client_id`, `redirect_uri`, scope 표시
- 현재 계정/채널 표시
- 토큰 상태(`유효`, `만료 임박`, `만료`, `없음`)
- `토큰 갱신`, `브라우저 재인증`, `토큰 삭제` 버튼

3. `CHZZK`
- 활성화 스위치
- `client_id`, `redirect_uri`, scope 표시
- 현재 계정/채널 표시
- 토큰 상태(`유효`, `만료 임박`, `만료`, `없음`)
- `토큰 갱신`, `브라우저 재인증`, `토큰 삭제` 버튼

4. `Security`
- 토큰 저장소 상태(예: QtKeychain 연결 여부)
- 마지막 토큰 갱신 시각/실패 원인

### 6.6 토큰 갱신 전략(2단계)

토큰 갱신은 아래 두 모드를 분리한다.

1. `Silent Refresh`
- 저장된 refresh token 으로 토큰 엔드포인트 호출
- 사용자 브라우저 개입 없음
- 성공 시 access token/만료시각 갱신

2. `Interactive Re-Auth`
- refresh 실패(만료/폐기/권한회수) 시 전환
- 시스템 웹 브라우저를 호출해 Authorization Code flow 재수행
- 새 access/refresh token 을 저장하고 기존 토큰 폐기

정책:

- `토큰 갱신` 버튼: `Silent Refresh` 우선
- `브라우저 재인증` 버튼: 즉시 `Interactive Re-Auth`
- 자동 복구: 연결 시작 시 refresh 가능한 경우 선시도, 실패 시 `Auth Required` 표시

### 6.7 시스템 브라우저 호출 및 콜백 처리

브라우저 호출은 OS 기본 브라우저를 사용한다.

- 1순위: `QDesktopServices::openUrl(authUrl)`
- 실패 시 fallback: 플랫폼별 `startDetached` 실행

콜백 수신 설계:

- `OAuthLocalServer`가 loopback 주소만 바인딩(`127.0.0.1`)
- 플랫폼별 고정 경로 사용
  - YouTube: `/youtube/callback`
  - CHZZK: `/chzzk/callback`
- `state` 검증 실패/누락 시 즉시 중단
- YouTube는 PKCE `code_verifier`/`code_challenge` 검증 체인 유지
- 인증 완료 페이지는 로컬 HTML로 `"인증 완료, 앱으로 돌아가세요"` 안내

포트 정책:

- 기본값: YouTube `18080`, CHZZK `18081`
- 포트 충돌 시 임시 포트 탐색 후 인증 URL에 반영
- 개발자 콘솔 redirect URI와 불일치가 발생하지 않도록 운영 모드에서는 고정 포트 사용 권장

### 6.8 토큰 상태 모델

토큰 상태는 아래 enum으로 통일한다.

- `NO_TOKEN`
- `VALID`
- `EXPIRING_SOON` (예: 10분 이내 만료)
- `EXPIRED`
- `REFRESHING`
- `AUTH_REQUIRED`
- `ERROR`

`Configuration` 창은 플랫폼별 현재 상태와 다음 권장 액션을 항상 같이 표시한다.

### 6.9 INI 기반 접속 시작 정책

`Connect` 버튼을 누르는 시점에 현재 INI 값을 스냅샷으로 읽어 접속을 시작한다.

- `enabled=true` 인 플랫폼만 접속 대상에 포함
- 필수 값 누락(`client_id`, `redirect_uri` 등)이면 해당 플랫폼은 건너뛰고 오류 목록에 기록
- 접속 시도 순서는 무관하지만 결과 집계는 하나의 세션으로 관리
- 접속 중에 Configuration 값이 바뀌어도 현재 세션에는 반영하지 않고, 다음 `Connect`부터 반영

### 6.10 Configuration 창 위젯 구현 명세

#### 6.10.1 클래스/위젯 구조

`ConfigurationDialog(QDialog)`를 기준으로 구성한다.

- `tabConfig(QTabWidget)`
  - `GeneralConfigPage(QWidget)`
  - `YouTubeConfigPage(PlatformConfigPageBase)`
  - `ChzzkConfigPage(PlatformConfigPageBase)`
  - `SecurityConfigPage(QWidget)`
- 하단 버튼 바
  - `btnCfgApply(QPushButton)`
  - `btnCfgClose(QPushButton)`

권장 objectName:

- 다이얼로그: `dlgConfiguration`
- 탭: `tabConfig`
- 적용 버튼: `btnCfgApply`
- 닫기 버튼: `btnCfgClose`

#### 6.10.2 General 탭 필드

| objectName | 위젯 타입 | 저장 키(INI) | 비고 |
|---|---|---|---|
| `cmbLanguage` | `QComboBox` | `[app]language` | 예: `ko_KR`, `en_US` |
| `cmbLogLevel` | `QComboBox` | `[app]log_level` | `debug/info/warn/error` |
| `cmbMergeOrder` | `QComboBox` | `[app]merge_order` | `timestamp` 고정 가능 |
| `chkAutoReconnect` | `QCheckBox` | `[app]auto_reconnect` | 선택 기능 |

#### 6.10.3 Platform 탭 공통 필드 (YouTube/CHZZK)

`YouTubeConfigPage`, `ChzzkConfigPage`는 동일 UI 골격을 공유한다.

| objectName 패턴 | 위젯 타입 | 저장/조회 대상 | 비고 |
|---|---|---|---|
| `{pfx}ChkEnabled` | `QCheckBox` | `[platform]enabled` | `pfx=yt/chz` |
| `{pfx}EdtClientId` | `QLineEdit` | `[platform]client_id` | 필수 |
| `{pfx}EdtClientSecret` | `QLineEdit` | `[platform]client_secret` | CHZZK만 사용 |
| `{pfx}EdtRedirectUri` | `QLineEdit` | `[platform]redirect_uri` | loopback 권장 |
| `{pfx}EdtAuthEndpoint` | `QLineEdit` | `[platform]auth_endpoint` | https URL |
| `{pfx}EdtTokenEndpoint` | `QLineEdit` | `[platform]token_endpoint` | https URL |
| `{pfx}EdtScope` | `QPlainTextEdit` | `[platform]scope` | 공백 구분 |
| `{pfx}LblAccount` | `QLabel` | 런타임 | 현재 계정/채널 표시 |
| `{pfx}LblTokenState` | `QLabel` | 런타임 | `VALID/EXPIRED/...` |
| `{pfx}LblAccessExpireAt` | `QLabel` | 런타임 | access 만료시각 |
| `{pfx}LblLastRefreshResult` | `QLabel` | 런타임 | 마지막 갱신 결과 |
| `{pfx}BtnTokenRefresh` | `QPushButton` | 액션 | Silent Refresh |
| `{pfx}BtnReauthBrowser` | `QPushButton` | 액션 | 브라우저 재인증 |
| `{pfx}BtnTokenDelete` | `QPushButton` | 액션 | TokenVault 삭제 |
| `{pfx}BtnTestConfig` | `QPushButton` | 액션 | 필드 검증만 수행 |

YouTube 추가 필드:

- `ytEdtChannelId` -> `[youtube]channel_id`
- `ytEdtChannelHandle` -> `[youtube]channel_handle`

CHZZK 추가 필드:

- `chzEdtChannelId` -> `[chzzk]channel_id`
- `chzEdtChannelName` -> `[chzzk]channel_name`
- `chzEdtAccountLabel` -> `[chzzk]account_label`

#### 6.10.4 Security 탭 필드

| objectName | 위젯 타입 | 데이터 소스 | 비고 |
|---|---|---|---|
| `lblVaultProvider` | `QLabel` | TokenVault | 예: QtKeychain |
| `lblVaultHealth` | `QLabel` | TokenVault | OK / ERROR |
| `tblTokenAudit` | `QTableWidget` | 런타임 로그 | 플랫폼별 갱신 이력 |
| `btnClearTokenAudit` | `QPushButton` | 액션 | 이력 화면 초기화 |

#### 6.10.5 신호-슬롯 명세 (핵심)

`ConfigurationDialog` signals:

```cpp
signals:
    void configApplyRequested(const AppSettingsSnapshot& snapshot);
    void tokenRefreshRequested(PlatformId platform, const PlatformSettings& settings);
    void interactiveAuthRequested(PlatformId platform, const PlatformSettings& settings);
    void tokenDeleteRequested(PlatformId platform);
    void platformConfigValidationRequested(PlatformId platform, const PlatformSettings& settings);
```

`ConfigurationDialog` public slots:

```cpp
public slots:
    void onTokenStateUpdated(PlatformId platform, TokenState state, const QString& detail);
    void onTokenActionFinished(PlatformId platform, bool ok, const QString& message);
    void onTokenRecordUpdated(PlatformId platform, TokenState state, const TokenRecord& record, const QString& detail);
```

버튼 -> 슬롯 매핑:

| 버튼 objectName | 연결 슬롯 | 후속 시그널 |
|---|---|---|
| `btnCfgApply` | `ConfigurationDialog::onApplyClicked()` | `configApplyRequested(...)` |
| `ytBtnTokenRefresh` | `onYouTubeRefreshClicked()` | `tokenRefreshRequested(YOUTUBE, collectPlatformSettings(YOUTUBE))` |
| `ytBtnReauthBrowser` | `onYouTubeReauthClicked()` | `interactiveAuthRequested(YOUTUBE, collectPlatformSettings(YOUTUBE))` |
| `ytBtnTokenDelete` | `onYouTubeDeleteTokenClicked()` | `tokenDeleteRequested(YOUTUBE)` |
| `chzBtnTokenRefresh` | `onChzzkRefreshClicked()` | `tokenRefreshRequested(CHZZK, collectPlatformSettings(CHZZK))` |
| `chzBtnReauthBrowser` | `onChzzkReauthClicked()` | `interactiveAuthRequested(CHZZK, collectPlatformSettings(CHZZK))` |
| `chzBtnTokenDelete` | `onChzzkDeleteTokenClicked()` | `tokenDeleteRequested(CHZZK)` |
| `ytBtnTestConfig` | `onYouTubeTestConfigClicked()` | `platformConfigValidationRequested(...)` |
| `chzBtnTestConfig` | `onChzzkTestConfigClicked()` | `platformConfigValidationRequested(...)` |

#### 6.10.6 검증 규칙

저장(`Apply`) 전에 아래 규칙을 검사한다.

- `enabled=true` 플랫폼은 `client_id`, `redirect_uri`, `auth_endpoint`, `token_endpoint`, `scope` 필수
- YouTube `client_id`는 `*.googleusercontent.com` 도메인 기준 검증을 수행한다.
- YouTube `client_id`는 따옴표/JSON line 붙여넣기 입력에서도 실제 client id를 추출(normalize)한 뒤 검증한다.
- YouTube: `redirect_uri` path가 `/youtube/callback`인지 확인
- CHZZK: `client_secret` 필수, `redirect_uri` path가 `/chzzk/callback`인지 확인
- `auth_endpoint`/`token_endpoint`는 `https://...` 형식만 허용
- `redirect_uri`는 `http://127.0.0.1:{port}/...` 형식만 허용(운영 기본)

검증 실패 시:

- 저장 중단
- 해당 필드 red highlight
- 탭 헤더에 에러 아이콘 표시
- 하단 상태줄에 첫 번째 오류 메시지 출력

#### 6.10.7 상태 잠금(Disabled) 규칙

- `token state == REFRESHING`이면 해당 플랫폼 탭의 입력/버튼 잠금
- `interactive auth 진행 중`이면 다른 플랫폼 토큰 버튼도 잠금(중복 콜백 충돌 방지)
- `Connect 상태 == Connected/Connecting`에서는 `enabled`, `redirect_uri` 변경 시 경고 배너 표시

현재 구현 메모:

- 버튼 단위 토큰 액션은 동작하며, `REFRESHING`/`AUTH_REQUIRED`/`ERROR` 상태 문자열은 즉시 반영된다.
- `onTokenOperationStarted(...)` / `onTokenActionFinished(...)`를 통해 토큰 작업 중 플랫폼 탭 입력/버튼 잠금이 동작한다.
- `interactive_auth` 진행 중에는 타 플랫폼의 토큰 버튼(`refresh`/`reauth`/`delete`)도 잠겨 중복 콜백 충돌을 방지한다.
- 각 플랫폼 상태 카드의 `Operation` 라벨(`ytLblOperation`/`chzLblOperation`)로 `IDLE` 또는 `BUSY:<op>`를 표시한다.
- 잠금된 토큰 버튼에는 잠금 사유 툴팁을 표시한다.
- `Operation` 라벨은 상태별 색상 규칙을 사용한다.
  - 진행 중: 주황(`BUSY`)
  - 최근 성공 후 유휴: 녹색(`IDLE`)
  - 최근 실패 후 유휴: 적색(`IDLE`)
- Security 탭 `tblTokenAudit`/`btnClearTokenAudit`는 구현 완료되었고, refresh/reauth/delete 성공·실패 이력이 누적된다.
- `tblTokenAudit`의 `Result` 컬럼은 `OK/FAIL` 색상 태그(녹색/적색)를 표시한다.
- `tblTokenAudit`의 `Platform` 컬럼은 YouTube/CHZZK 색상 태그를 표시한다.
- `tblTokenAudit`의 `Action` 컬럼은 refresh/auth/delete/grant 유형별 색상 태그를 표시한다.
- `tblTokenAudit`의 `Detail` 컬럼은 컬럼 폭 기준 동적 축약(elide) 표시를 사용하고, 전체 원문은 툴팁으로 제공한다.
- `tblTokenAudit` 행 더블클릭 시 `Detail` 원문을 `QDialog + read-only QTextEdit` 팝업으로 확인할 수 있다.
- 팝업에는 `Copy Detail` 버튼이 있어 원문을 클립보드로 복사할 수 있다.
- 팝업에는 `Copy Summary` 버튼이 있어 `Time/Platform/Action/Result` 메타를 복사할 수 있다.
- 팝업에는 `Copy All` 버튼이 있어 `Summary + Detail` 합본을 복사할 수 있다.
- `Copy All`은 `=== TOKEN AUDIT ===` / `=== DETAIL ===` 구분선이 포함된 공유용 템플릿 형식으로 복사된다.
- `Copy All`에는 `copied_at_utc`(ISO8601 UTC) 메타가 자동 포함된다.
- `Copy All`에는 `copied_at_local`(ISO8601 local time) 메타도 함께 포함된다.

#### 6.10.8 Configuration와 MainWindow 연동

- `configApplyRequested` 성공 시 `MainWindow`는 `Connect` 버튼 옆에 `Config Updated` 표시(짧은 토스트)
- 연결 활성 상태에서 설정이 바뀌면 즉시 재연결하지 않고, `Disconnect 후 Connect` 안내를 표시
- `Disconnect` 완료 후 다음 `Connect`에서 새 INI 스냅샷을 사용

#### 6.10.9 브라우저 재인증 UI 상태

`interactiveAuthRequested(platform, settings)` 호출 이후 UI는 단계별 상태를 표시한다.

1. `ValidatingConfig`
2. `StartingLocalCallbackServer`
3. `OpeningSystemBrowser`
4. `WaitingCallback`
5. `ValidatingState`
6. `ExchangingToken`
7. `SavingToken`
8. `Completed` 또는 `Failed`

각 단계는 `{pfx}LblLastRefreshResult`와 상태 배지에 반영한다.

#### 6.10.10 OAuthLocalServer 연동 명세

`MainWindow`가 `OAuthLocalServer`를 소유하고, Configuration에서 올라온 요청을 처리한다.

```cpp
struct OAuthSessionConfig {
    PlatformId platform;
    QUrl redirectUri;      // e.g. http://127.0.0.1:18080/youtube/callback
    QString expectedState; // CSRF 방지
    int timeoutMs;         // 기본 120000
};

class OAuthLocalServer : public QObject {
    Q_OBJECT
public:
    bool startSession(const OAuthSessionConfig& config, QString* errorMessage = nullptr);
    void cancelSession(PlatformId platform, const QString& reason = QString());
signals:
    void callbackReceived(PlatformId platform, const QString& code,
        const QString& state, const QString& errorCode, const QString& errorDescription);
    void sessionFailed(PlatformId platform, const QString& reason);
};
```

처리 규칙:

- `redirect_uri`는 `http://127.0.0.1:{port}/{platform}/callback` 형식만 허용
- callback path 불일치 시 `404` 응답 후 세션 유지
- `state` 불일치/누락 시 실패 처리(`OAUTH_STATE_MISMATCH`)
- `code`와 `error`가 모두 없으면 실패 처리(`OAUTH_CODE_MISSING`)
- 성공/실패 응답 HTML을 브라우저에 반환한 뒤 세션 정리

`MainWindow`는 플랫폼별 `pendingOAuthState`를 유지하고, callback 수신 시 다음 순서로 처리한다.

1. `state` 재검증
2. YouTube는 `pendingPkceVerifier` 재검증
3. token endpoint 호출(`OAuthTokenClient`)
4. TokenVault 저장
5. `ConfigurationDialog` 상태/결과 라벨 갱신
6. 이벤트 로그 기록

#### 6.10.11 OAuthTokenClient 연동 명세

`OAuthTokenClient`는 `application/x-www-form-urlencoded`로 token endpoint를 호출한다.

- Authorization Code:
  - `grant_type=authorization_code`
  - `code`, `client_id`, `redirect_uri`
  - YouTube는 `code_verifier` 필수(PKCE S256)
  - CHZZK는 문서 요구사항에 따라 `client_secret` 포함
- Refresh:
  - `grant_type=refresh_token`
  - `refresh_token`, `client_id`
  - 필요 시 `client_secret`

응답 처리:

- 성공: `access_token`, `refresh_token(옵션)`, `expires_in` 파싱
- 실패: HTTP status + `error`/`error_description` 파싱 후 UI에 표시
- refresh 응답에 `refresh_token`이 없으면 기존 refresh token을 유지

## 7. 플랫폼별 구현 계획

### 7.1 YouTubeAdapter

#### 책임

- OAuth 인증 시작
- OAuth callback 처리
- access/refresh token 저장 및 갱신
- 현재 활성 방송 탐색
- `liveChatId` 확보
- 라이브 채팅 수신
- 선택 기능: 메시지 전송
- 운영 액션 처리

#### 권장 흐름

1. 설정에서 client id / redirect uri 로드
2. PKCE용 `code_verifier`, `state` 생성
3. 기본 브라우저로 인증 URL 실행
4. localhost callback 수신
5. code 교환 후 access/refresh token 저장
6. `liveBroadcasts.list(mine=true, broadcastStatus=active)` 호출
7. `snippet.liveChatId` 확보
8. `liveChatMessages.streamList` 연결
9. 수신 메시지를 `UnifiedChatMessage` 로 변환

#### 비고

- 활성 방송이 없으면 polling 연결을 시도하지 않고 대기 상태로 전환
- `streamList` 실패 시 `list + pollingIntervalMillis` 폴백 제공
- 봇 발송이 필요하면 `liveChatMessages.insert` 추가
- 메시지 삭제 버튼은 `messageId` 가 있는 메시지에만 활성화한다.
- 임시 timeout / 영구 ban 은 `authorId` 와 `liveChatId` 로 직접 수행할 수 있다.
- unban 버튼은 `banId` 를 알고 있거나 앱이 관리 중인 ban 캐시에 존재할 때만 활성화한다.
- 운영자 제거 버튼은 `liveChatModerators.list` 결과를 캐시하여 `authorId -> moderatorId` 매핑이 있을 때만 활성화한다.

### 7.2 ChzzkAdapter

#### 책임

- Authorization Code 인증
- `/open/v1/users/me` 로 본인 `channelId` 확보
- Session URL 발급
- Socket.IO 연결
- `sessionKey` 수신 후 채팅 이벤트 구독
- access/refresh token 저장 및 갱신
- 선택 기능: 채팅 발송
- 운영 액션 처리

#### 권장 흐름

1. 설정에서 client id / redirect uri 로드
2. 기본 브라우저 인증 시작
3. callback 에서 code/state 수신
4. `/auth/v1/token` 으로 토큰 발급
5. `/open/v1/users/me` 호출로 channelId, channelName 동기화
6. `/open/v1/sessions/auth` 호출로 연결 URL 확보
7. Socket.IO 접속
8. `SYSTEM.connected` 에서 `sessionKey` 확보
9. `/open/v1/sessions/events/subscribe/chat` 호출
10. `CHAT` 이벤트를 `UnifiedChatMessage` 로 변환

#### 비고

- 토큰 갱신 시 refresh token 이 교체되므로 저장 순서를 안전하게 설계해야 한다.
- 세션당 이벤트 구독 수 제한과 유저당 연결 수 제한을 고려해야 한다.
- Socket.IO 버전 호환성 검증이 초기 프로토타입의 핵심 리스크다.
- 활동 제한 추가/삭제는 `senderChannelId` 로 직접 수행 가능하다.
- 채팅 설정 변경 액션은 특정 사용자 클릭이 아니라 채널 단위 액션 패널에 배치하는 것이 맞다.
- 수신 CHAT 이벤트 문서에는 `messageId` 가 없어 특정 수신 메시지 기준의 삭제/공지승격 UI는 제한적이다.

## 8. 통합 채팅 UI 계획

### 8.1 메인 화면 구성

1. 좌측 사이드바
- 플랫폼 연결 상태
- 현재 로그인 계정/채널
- 토큰 만료 예정 시간
- `Configuration` 열기 버튼 (토큰 작업은 설정 창에서만 수행)

2. 상단 연결 컨트롤
- `Connect` / `Disconnect` 단일 토글 버튼
- 버튼 우측에 연결 진행 상태(`Idle`, `Connecting`, `Connected`, `Disconnecting`) 표시
- 클릭 시 `ConnectionCoordinator`가 INI 기반 접속/해제를 수행

3. 중앙 통합 채팅 뷰
- 시간순 병합 메시지 목록
- 플랫폼별 색상 배지
- 채널명, 작성자 닉네임, 권한 배지, 메시지, 시간 표시
- 시스템 메시지/오류 메시지 별도 스타일
- 선택한 메시지 또는 닉네임 클릭 가능

4. 상단 필터 바
- 전체 / YouTube / 치지직
- 채널별 필터
- 운영자/일반 유저 필터
- 검색

5. 하단 입력 영역
- MVP에서는 비활성 또는 수동 발송만 지원
- 추후 봇 명령 테스트용 콘솔로 확장 가능

6. 우측 액션 패널
- 선택한 메시지의 플랫폼, 채널, 닉네임, 채널 ID, 역할, 배지 표시
- 상단에 플랫폼 상태 사각형 인디케이터 2개(YouTube/치지직) 배치
- 공통 액션 버튼 영역
- 플랫폼 전용 액션 버튼 영역
- 실행 전 확인 다이얼로그 및 결과 로그 표시

7. Configuration 진입 버튼
- 전역 설정/인증/토큰 갱신을 여는 버튼
- 토큰 만료 또는 권한 오류 발생 시 강조 상태로 표시

### 8.2 통합 메시지 정책

- 내부 시간 기준은 UTC 또는 epoch ms 로 통일
- 표시 시 로컬 시간대로 렌더링
- 플랫폼 고유 메타데이터는 `rawMeta` 에 유지
- 메시지 중복 방지를 위해 플랫폼별 원본 message id 또는 조합 키를 보관
- 클릭 가능한 대상 식별자를 위해 `messageId`, `authorId`, `authorRole`, `authorBadges` 를 정규화한다.

### 8.3 상태 표시 정책

- `Connected`
- `Refreshing Token`
- `Auth Required`
- `Reconnecting`
- `No Active Broadcast`
- `Socket Error`

### 8.3.1 전역 연결 상태 모델

전역 연결 상태(`ConnectionState`)는 아래 enum으로 관리한다.

- `IDLE`
- `CONNECTING`
- `PARTIALLY_CONNECTED`
- `CONNECTED`
- `DISCONNECTING`
- `ERROR`

판정 규칙:

- `CONNECTING`: Connect 클릭 후 1개 이상 플랫폼 접속 시도 중
- `PARTIALLY_CONNECTED`: 일부 플랫폼 성공, 일부 실패
- `CONNECTED`: 접속 대상으로 선정된 모든 플랫폼 성공
- `DISCONNECTING`: Disconnect 클릭 후 해제 진행 중
- `ERROR`: 접속 대상 전부 실패 또는 치명 오류

### 8.3.2 Connect 버튼 상태 전이

버튼은 전역 연결 세션 상태에 따라 캡션이 바뀐다.

1. 앱 시작 직후
- 기본 캡션: `Connect`
- 상태: `IDLE`

2. `Connect` 클릭
- `Configuration`의 INI 스냅샷 로드
- 대상 플랫폼(`enabled=true`)에 대해 `adapter.start()` 호출
- 진행 중 캡션: `Connecting...` (버튼 비활성)

3. 접속 결과 집계
- 1개 이상 플랫폼이 연결 활성 상태가 되면 캡션: `Disconnect`
- 모든 플랫폼 연결 실패 시 캡션: `Connect` 복귀 + 실패 사유 표시
- 일부 플랫폼만 성공하면 전역 상태는 `PARTIALLY_CONNECTED`로 표기하고 캡션은 `Disconnect` 유지

4. `Disconnect` 클릭
- 연결된/연결중 플랫폼에 `adapter.stop()` 호출
- 진행 중 캡션: `Disconnecting...` (버튼 비활성)
- 종료 완료 시 캡션: `Connect` 복귀

### 8.3.3 이벤트-상태 전이표

| 현재 상태 | 이벤트 | 가드 | 다음 상태 | 처리 |
|---|---|---|---|---|
| `IDLE` | `EV_CONNECT_CLICK` | 대상 플랫폼 >= 1 | `CONNECTING` | INI 스냅샷 로드, adapter.start |
| `IDLE` | `EV_CONNECT_CLICK` | 대상 플랫폼 0 | `ERROR` | `NO_ENABLED_PLATFORM` 경고 |
| `CONNECTING` | `EV_PLATFORM_CONNECTED` | 일부 성공 | `PARTIALLY_CONNECTED` | 진행 상태 업데이트 |
| `CONNECTING` | `EV_ALL_CONNECTED` | - | `CONNECTED` | 캡션 `Disconnect` |
| `CONNECTING` | `EV_ALL_FAILED` | - | `ERROR` | 실패 목록 표시, 캡션 `Connect` |
| `PARTIALLY_CONNECTED` | `EV_REMAINING_CONNECTED` | 나머지 성공 | `CONNECTED` | 전부 성공 처리 |
| `PARTIALLY_CONNECTED` | `EV_CONNECT_STABILIZED` | 일부 실패 유지 | `PARTIALLY_CONNECTED` | `Disconnect` 유지 |
| `CONNECTED` | `EV_DISCONNECT_CLICK` | - | `DISCONNECTING` | adapter.stop |
| `PARTIALLY_CONNECTED` | `EV_DISCONNECT_CLICK` | - | `DISCONNECTING` | adapter.stop |
| `DISCONNECTING` | `EV_ALL_DISCONNECTED` | - | `IDLE` | 캡션 `Connect` |
| `DISCONNECTING` | `EV_DISCONNECT_TIMEOUT` | - | `ERROR` | 강제 세션 정리/경고 |
| `ERROR` | `EV_CONNECT_CLICK` | 대상 플랫폼 >= 1 | `CONNECTING` | 재시도 |
| `ERROR` | `EV_RESET` | - | `IDLE` | 오류 상태 클리어 |

타임아웃 권장값:

- 플랫폼별 connect timeout: 20초
- 전체 connect timeout: 35초
- 전체 disconnect timeout: 10초

### 8.3.4 Configuration 창 내 토큰 UX

플랫폼별 토큰 카드에 아래 항목을 고정 표시한다.

- `access token 만료 시각`
- `refresh token 존재 여부`
- `마지막 갱신 결과`
- `최근 실패 사유`

버튼 동작:

- `토큰 갱신`: Silent Refresh 실행, 성공/실패를 즉시 표시
- `브라우저 재인증`: 시스템 브라우저 호출 -> 콜백 대기 -> 토큰 저장
- `토큰 삭제`: 저장소에서 즉시 삭제 후 `NO_TOKEN` 상태 전환

실패 처리:

- 네트워크 오류: `재시도` 버튼 활성화
- 권한 오류/무효 토큰: `브라우저 재인증 필요` 배너 표시
- 콜백 타임아웃: 인증 세션 폐기 후 재시작 안내

### 8.3.5 액션 패널 상단 플랫폼 상태 사각형 인디케이터

요구사항:

- 메인 화면 우측 `Actions` 패널 상단에 상태 사각형 2개를 둔다.
- 좌측 사각형은 `YouTube`, 우측 사각형은 `치지직`을 나타낸다.
- 각 사각형은 플랫폼별 런타임 상태를 색상으로 표시한다.

권장 위젯 구조:

- `MainWindow` 우측 패널 최상단 `QHBoxLayout`
  - `ytStatusBox(QFrame)` + `ytStatusLabel(QLabel: "YouTube")`
  - `chzStatusBox(QFrame)` + `chzStatusLabel(QLabel: "CHZZK")`
- 사각형 크기: `16x16` 또는 `18x18`
- 스타일: `border-radius: 2px; border: 1px solid #666;`

색상 정책:

| 상태 | 의미 | 색상(권장) |
|---|---|---|
| `AUTH_IN_PROGRESS` | Configuration에서 브라우저 인증 진행 중 | `#FFB74D` (주황) |
| `ONLINE` | 플랫폼 연결 활성(채팅 수신 가능) | `#81D4FA` (하늘색) |
| `TOKEN_OK` | 토큰 상태 정상(연결 전/미연결) | `#66BB6A` (초록) |
| `TOKEN_BAD` | 토큰 상태 비정상/인증 필요/오류 | `#EF5350` (빨강) |

상태 우선순위(상위가 우선):

1. `AUTH_IN_PROGRESS`
2. `ONLINE`
3. `TOKEN_OK` / `TOKEN_BAD`

토큰 정상 판정 기준:

- `TOKEN_OK`: `VALID`, `EXPIRING_SOON`
- `TOKEN_BAD`: `NO_TOKEN`, `EXPIRED`, `AUTH_REQUIRED`, `ERROR`
- `REFRESHING`은 운영 정책상 비정상으로 보지 않고 별도 색상으로 분리할 수 있으나, MVP에서는 `TOKEN_BAD`로 단순화해도 무방하다.

시그널-슬롯/업데이트 트리거:

- 연결 상태 변화:
  - `onConnectionStateChanged(...)`
  - `onConnectFinished(...)`
  - `onDisconnectFinished(...)`
- 토큰 상태 변화:
  - `refreshTokenUi(...)`, `onTokenGranted(...)`, `onTokenFailed(...)`
- 인증 진행 상태 변화:
  - `onInteractiveAuthRequested(...)` 시작 시 `AUTH_IN_PROGRESS=true`
  - `onOAuthCallbackReceived(...)`, `onOAuthSessionFailed(...)`, `onTokenGranted/Failed(...)` 종료 시 false

구현 권장 API:

```cpp
enum class PlatformVisualState {
    TOKEN_BAD,
    TOKEN_OK,
    ONLINE,
    AUTH_IN_PROGRESS,
};

void updatePlatformVisualState(PlatformId platform);
void applyPlatformIndicatorColor(PlatformId platform, PlatformVisualState state);
```

검증 항목:

- YouTube/CHZZK 각각 토큰 정상 상태에서 초록 표시 확인
- Connect 완료 시 해당 플랫폼 인디케이터가 하늘색으로 변경되는지 확인
- 브라우저 인증 진행 중 주황색으로 변경되고, 완료/실패 시 기존 규칙으로 복귀하는지 확인
- 토큰 삭제 또는 인증 실패 시 빨간색 전환 확인
- 두 플랫폼 상태가 서로 독립적으로 갱신되는지 확인

### 8.4 운영자 액션 버튼 계획

채팅창에서 닉네임 또는 메시지 행을 클릭하면 우측 액션 패널 또는 팝업 메뉴를 연다. 버튼은 `지원됨`, `조건부`, `미지원` 상태를 갖고, 미지원 이유를 tooltip 으로 보여준다.

#### 공통 액션 버튼

- `프로필 보기`
- `채널 열기`
- `운영자 메시지 보내기`
- `대상 제재`

주의:
- `대상 제재` 는 UI 라벨은 공통으로 유지하되, 실제 구현은 플랫폼별로 다르다.
- YouTube 는 `temporary` 또는 `permanent` ban 을 선택하게 하고, 치지직은 `활동 제한 추가`로 매핑한다.

#### YouTube 전용 버튼

- `메시지 삭제`
- `5분 타임아웃`
- `영구 밴`
- `밴 해제`
- `운영자 추가`
- `운영자 제거`
- `투표 종료`

#### 치지직 전용 버튼

- `활동 제한 추가`
- `활동 제한 해제`
- `채팅 공지 등록`
- `슬로우 모드 변경`
- `이모티콘 모드 토글`
- `채팅 참여 범위 변경`
- `팔로워 기간 제한 변경`

### 8.5 액션 가능성 리뷰

#### 닉네임 클릭 기반 액션

구현 가능하다.

- YouTube는 메시지 리소스에 작성자 채널 ID와 표시명, 역할 정보가 포함된다.
- 치지직은 채팅 이벤트에 `senderChannelId`, `profile.nickname`, `userRoleCode` 가 포함된다.

따라서 닉네임 클릭 -> 대상 사용자 컨텍스트 메뉴 오픈 -> 제재/정보조회 버튼 노출 흐름은 두 플랫폼 모두 가능하다.

#### 메시지 클릭 기반 액션

부분적으로 가능하다.

- YouTube는 `liveChatMessage.id` 가 있으므로 특정 메시지 삭제 같은 per-message 액션을 구현할 수 있다.
- 치지직은 수신 CHAT 이벤트 문서에 `messageId` 가 없으므로 특정 수신 메시지에 직접 연결되는 액션은 제한된다.

따라서 UI 정책은 다음이 적절하다.

- `사용자 대상 액션`은 두 플랫폼 공통으로 닉네임 클릭에 연결
- `메시지 대상 액션`은 플랫폼 capability 체크 후 표시
- 치지직은 메시지 클릭 시에도 실제 버튼은 사용자/채널 단위 액션 위주로 제공

### 8.6 권장 액션 매트릭스

| 액션 | YouTube | 치지직 | 비고 |
|---|---|---|---|
| 운영자 메시지 전송 | 가능 | 가능 | 공통 |
| 사용자 닉네임/권한/배지 표시 | 가능 | 가능 | 공통 |
| 사용자 클릭 후 제재 액션 | 가능 | 가능 | 공통 UI, 다른 API 의미 |
| 제재 해제 | 조건부 | 가능 | YouTube는 `banId` 필요 |
| 특정 메시지 삭제 | 가능 | 문서상 불명확/제한 | 치지직 CHAT 이벤트에 `messageId` 미표기 |
| 운영자 추가 | 가능 | 문서상 미확인 | YouTube owner 전용 |
| 운영자 제거 | 조건부 | 문서상 미확인 | YouTube는 `moderatorId` 필요 |
| 채팅 공지 등록 | 문서상 미확인 | 가능 | 치지직 전용 |
| 슬로우 모드 변경 | 문서상 미확인 | 가능 | 치지직 전용 |
| 이모티콘 모드 변경 | 문서상 미확인 | 가능 | 치지직 전용 |
| 참여 범위/팔로워 제한 변경 | 문서상 미확인 | 가능 | 치지직 전용 |
| 투표 종료 | 가능 | 문서상 미확인 | YouTube 전용 |

### 8.7 기능 검증 체크리스트

`개발문서 기준 검증`을 위해 아래 체크리스트를 구현/QA 항목으로 고정한다.

#### 데이터 검증

- YouTube 수신 메시지에 `messageId`, `authorId`, `authorName`, `authorRole` 매핑이 채워지는지 확인
- 치지직 수신 메시지에 `authorId(senderChannelId)`, `authorName(profile.nickname)`, `authorRole(profile.userRoleCode)` 매핑이 채워지는지 확인
- 치지직 수신 이벤트는 문서상 `messageId` 미제공이므로 `messageId` 가 비어도 정상 처리되는지 확인

#### UI 검증

- 닉네임 클릭 시 액션 패널이 열리고 대상 정보(플랫폼/닉네임/채널 ID/권한)가 정확히 표시되는지 확인
- 버튼 상태(`지원됨`/`조건부`/`미지원`)가 매트릭스 및 런타임 capability 와 일치하는지 확인
- `미지원` 버튼 tooltip 에 원인(API 미지원/권한부족/식별자부족)이 노출되는지 확인

#### 액션 실행 검증

- 공통 액션 1개 이상(예: 운영자 메시지 전송)을 두 플랫폼에서 모두 성공시키는지 확인
- YouTube 전용 액션 1개 이상(예: 메시지 삭제 또는 타임아웃) 실행 성공 확인
- 치지직 전용 액션 1개 이상(예: 활동 제한 추가 또는 채팅 설정 변경) 실행 성공 확인
- YouTube `unban`/`운영자 제거`는 식별자(`banId`, `moderatorId`)가 없을 때 비활성 또는 안내 처리되는지 확인
- 치지직에서 메시지 단위 액션 요청 시 사용자 단위 액션으로 유도되는지 확인

### 8.8 Action ID 규격

버튼/메뉴 구현 시 하드코딩 텍스트 대신 `actionId` 중심으로 연결한다.

#### 네이밍 규칙

- 공통: `common.*`
- YouTube 전용: `youtube.*`
- 치지직 전용: `chzzk.*`

#### 권장 Action ID 목록

| actionId | targetType | 필수 필드 | 비고 |
|---|---|---|---|
| `common.open_profile` | `user` | `platform`, `authorId` | 브라우저로 프로필 열기 |
| `common.open_channel` | `channel` | `platform`, `channelId` | 방송 채널 페이지 열기 |
| `common.send_message` | `channel` | `platform`, `channelId` | 모달 입력 후 발송 |
| `common.restrict_user` | `user` | `platform`, `authorId` | 플랫폼별 제재 API로 라우팅 |
| `common.unrestrict_user` | `user` | `platform`, `authorId` | 플랫폼별 해제 API로 라우팅 |
| `youtube.delete_message` | `message` | `messageId`, `liveChatId` | `liveChatMessages.delete` |
| `youtube.timeout_5m` | `user` | `authorId`, `liveChatId` | `temporary ban(300초)` |
| `youtube.ban_permanent` | `user` | `authorId`, `liveChatId` | `permanent ban` |
| `youtube.unban` | `user` | `banId` | `liveChatBans.delete` |
| `youtube.add_moderator` | `user` | `authorId`, `liveChatId` | owner 권한 필요 |
| `youtube.remove_moderator` | `user` | `moderatorId` | `liveChatModerators.delete` |
| `youtube.close_poll` | `message` | `messageId` | `liveChatMessages.transition(status=closed)` |
| `chzzk.add_restriction` | `user` | `authorId` | `restrict-channels POST` |
| `chzzk.remove_restriction` | `user` | `authorId` | `restrict-channels DELETE` |
| `chzzk.create_notice` | `channel` | `channelId` | `message` 또는 `messageId` 사용 |
| `chzzk.set_slow_mode` | `channel` | `channelId`, `chatSlowModeSec` | `chats/settings PUT` |
| `chzzk.toggle_emoji_mode` | `channel` | `channelId`, `chatEmojiMode` | `chats/settings PUT` |
| `chzzk.set_chat_group` | `channel` | `channelId`, `chatAvailableGroup` | `chats/settings PUT` |
| `chzzk.set_follower_minute` | `channel` | `channelId`, `minFollowerMinute` | `chats/settings PUT` |

### 8.9 버튼 활성화 조건표

버튼 활성화는 `ActionCapabilityResolver`가 판단하며, 결과는 `enabled`, `disabledReason`으로 UI에 전달한다.

| actionId | 활성화 조건 | 비활성 사유 코드 |
|---|---|---|
| `common.send_message` | adapter 연결됨, 토큰 유효 | `NOT_CONNECTED`, `TOKEN_EXPIRED` |
| `common.restrict_user` | `authorId` 존재 | `MISSING_AUTHOR_ID` |
| `common.unrestrict_user` | YouTube: `banId` 존재, 치지직: `authorId` 존재 | `MISSING_BAN_ID`, `MISSING_AUTHOR_ID` |
| `youtube.delete_message` | `messageId`, `liveChatId` 존재 | `MISSING_MESSAGE_ID`, `MISSING_LIVE_CHAT_ID` |
| `youtube.remove_moderator` | `moderatorId` 존재 | `MISSING_MODERATOR_ID` |
| `youtube.close_poll` | `messageId` 존재, poll 상태 active | `NOT_POLL_MESSAGE`, `POLL_NOT_ACTIVE` |
| `chzzk.add_restriction` | `authorId(senderChannelId)` 존재 | `MISSING_AUTHOR_ID` |
| `chzzk.create_notice` | `message` 입력 또는 `messageId` 보유 | `MISSING_NOTICE_SOURCE` |
| `chzzk.set_slow_mode` | 허용값(0/3/5/10/30/60/120/300) 선택 | `INVALID_SLOW_MODE` |

### 8.10 액션 실행 파이프라인

1. 사용자가 닉네임/메시지를 클릭하여 `UnifiedChatMessage` 선택
2. `ActionCapabilityResolver`가 액션별 가능 여부 계산
3. UI가 버튼 상태 및 비활성 사유 표시
4. 사용자가 액션 실행 시 확인 다이얼로그 표시
5. `IChatPlatformAdapter::executeAction(actionId, target)` 호출
6. 성공/실패를 토스트와 운영 로그에 기록

권장 운영 로그 포맷:

```json
{
  "timestamp": "2026-04-10T17:20:11Z",
  "platform": "youtube",
  "actionId": "youtube.timeout_5m",
  "targetAuthorId": "UCxxxx",
  "messageId": "LCC.xxxx",
  "result": "success",
  "httpStatus": 200,
  "errorCode": ""
}
```

### 8.11 권한 및 실패 처리 정책

- 권한 관련 403/401 응답은 `NO_PERMISSION` 또는 `AUTH_REQUIRED`로 정규화한다.
- 조건부 액션은 사전 식별자 부족 시 API 호출 전에 UI에서 차단한다.
- 동일 대상에 대한 중복 제재 요청은 409/400 오류를 사용자 친화 메시지로 변환한다.
- 치지직 메시지 단위 액션 불가 케이스는 `사용자 단위 액션으로 전환` 안내를 고정 문구로 제공한다.

### 8.12 Action ID -> API 요청/응답 스펙(JSON 예시)

이 절은 `Adapter` 구현을 위한 계약 문서다.  
치지직 응답은 문서 공개 형식이 변경될 수 있으므로 `필드 중심 예시`로 관리하고, 실제 envelope 파싱은 어댑터 내부에서 흡수한다.

#### 8.12.1 공통 HTTP 규칙

- 인증 헤더: `Authorization: Bearer <access_token>`
- JSON API 요청 시 `Content-Type: application/json`
- 성공 판단: HTTP 2xx + 플랫폼별 필수 필드 존재
- 액션 결과는 내부 공통 포맷으로 정규화

정규화 결과 예시:

```json
{
  "actionId": "youtube.timeout_5m",
  "platform": "youtube",
  "ok": true,
  "httpStatus": 200,
  "platformResultId": "ban_12345",
  "errorCode": "",
  "errorMessage": ""
}
```

#### 8.12.2 YouTube 액션 매핑

베이스 URL: `https://www.googleapis.com/youtube/v3`

1. `common.send_message` (`youtube` 라우팅)

- Method/Path: `POST /liveChat/messages?part=snippet`
- Request:

```json
{
  "snippet": {
    "liveChatId": "LIVE_CHAT_ID",
    "type": "textMessageEvent",
    "textMessageDetails": {
      "messageText": "관리자 안내 메시지"
    }
  }
}
```

- Response(핵심 필드):

```json
{
  "id": "LCC.xxx",
  "snippet": {
    "type": "textMessageEvent"
  }
}
```

2. `youtube.delete_message`

- Method/Path: `DELETE /liveChat/messages?id={messageId}`
- Request Body: 없음
- Response: 빈 바디(204) 또는 성공 코드만 확인

3. `youtube.timeout_5m`

- Method/Path: `POST /liveChat/bans?part=snippet`
- Request:

```json
{
  "snippet": {
    "liveChatId": "LIVE_CHAT_ID",
    "type": "temporary",
    "banDurationSeconds": 300,
    "bannedUserDetails": {
      "channelId": "TARGET_AUTHOR_CHANNEL_ID"
    }
  }
}
```

- Response(핵심 필드):

```json
{
  "id": "BAN_ID",
  "snippet": {
    "type": "temporary",
    "banDurationSeconds": 300
  }
}
```

4. `youtube.ban_permanent`

- Method/Path: `POST /liveChat/bans?part=snippet`
- Request:

```json
{
  "snippet": {
    "liveChatId": "LIVE_CHAT_ID",
    "type": "permanent",
    "bannedUserDetails": {
      "channelId": "TARGET_AUTHOR_CHANNEL_ID"
    }
  }
}
```

- Response(핵심 필드): `id(BAN_ID)`, `snippet.type=permanent`

5. `youtube.unban` 또는 `common.unrestrict_user` (`youtube` 라우팅)

- Method/Path: `DELETE /liveChat/bans?id={banId}`
- Request Body: 없음
- Response: 빈 바디(204) 또는 성공 코드만 확인

6. `youtube.add_moderator`

- Method/Path: `POST /liveChat/moderators?part=snippet`
- Request:

```json
{
  "snippet": {
    "liveChatId": "LIVE_CHAT_ID",
    "moderatorDetails": {
      "channelId": "TARGET_AUTHOR_CHANNEL_ID"
    }
  }
}
```

- Response(핵심 필드): `id(MODERATOR_ID)`

7. `youtube.remove_moderator`

- Method/Path: `DELETE /liveChat/moderators?id={moderatorId}`
- Request Body: 없음
- Response: 빈 바디(204) 또는 성공 코드만 확인

8. `youtube.close_poll`

- Method/Path: `POST /liveChat/messages/transition?id={messageId}&status=closed&part=snippet`
- Request Body: 없음
- Response(핵심 필드):

```json
{
  "id": "POLL_MESSAGE_ID",
  "snippet": {
    "type": "pollEvent"
  }
}
```

#### 8.12.3 치지직 액션 매핑

베이스 경로: `/open/v1`

1. `common.send_message` (`chzzk` 라우팅)

- Method/Path: `POST /chats/send`
- Request:

```json
{
  "message": "관리자 안내 메시지"
}
```

- Response(필드 중심):

```json
{
  "messageId": "CHAT_MESSAGE_ID"
}
```

2. `chzzk.add_restriction` 또는 `common.restrict_user` (`chzzk` 라우팅)

- Method/Path: `POST /restrict-channels`
- Request:

```json
{
  "targetChannelId": "TARGET_SENDER_CHANNEL_ID"
}
```

- Response: 성공 코드(200) 중심 검증

3. `chzzk.remove_restriction` 또는 `common.unrestrict_user` (`chzzk` 라우팅)

- Method/Path: `DELETE /restrict-channels`
- Request:

```json
{
  "targetChannelId": "TARGET_SENDER_CHANNEL_ID"
}
```

- Response: 성공 코드(200) 중심 검증

4. `chzzk.create_notice`

- Method/Path: `POST /chats/notice`
- Request(신규 메시지 공지):

```json
{
  "message": "공지 메시지"
}
```

- Request(기존 메시지 공지):

```json
{
  "messageId": "CHAT_MESSAGE_ID"
}
```

- Response: 성공 코드(200) 중심 검증

5. `chzzk.set_slow_mode`

- Method/Path: `PUT /chats/settings`
- Request:

```json
{
  "chatSlowModeSec": 10
}
```

6. `chzzk.toggle_emoji_mode`

- Method/Path: `PUT /chats/settings`
- Request:

```json
{
  "chatEmojiMode": true
}
```

7. `chzzk.set_chat_group`

- Method/Path: `PUT /chats/settings`
- Request:

```json
{
  "chatAvailableGroup": "FOLLOWER"
}
```

8. `chzzk.set_follower_minute`

- Method/Path: `PUT /chats/settings`
- Request:

```json
{
  "chatAvailableGroup": "FOLLOWER",
  "minFollowerMinute": 60,
  "allowSubscriberInFollowerMode": true
}
```

`PUT /chats/settings` 계열 응답은 성공 코드(200)와 변경 후 `GET /chats/settings` 재조회값 일치로 검증한다.

## 9. 봇 엔진 확장 계획

초기 MVP는 "통합 채팅 수집 + 상태 관리 + 토큰 갱신"에 집중한다. 자동응답은 2단계로 둔다.

### 9.1 봇 엔진 인터페이스

```cpp
class IBotRule {
public:
    virtual bool matches(const UnifiedChatMessage& msg) const = 0;
    virtual QList<BotAction> buildActions(const UnifiedChatMessage& msg) = 0;
};
```

### 9.2 이후 추가 가능한 기능

- 키워드 자동응답
- 플랫폼 공통 금칙어 필터
- 특정 유저/운영자 전용 명령
- 플랫폼별 발송 rate limit 관리
- 로그 저장 및 재생

## 10. 개발 단계 계획

### Phase 1. 프로젝트 골격

- Qt5 Widgets 프로젝트 생성
- 공통 디렉터리 구조 작성
- `QSettings` 기반 설정 로더 구현
- `TokenVault` 인터페이스 작성
- `ConfigurationDialog` 및 탭 위젯 골격 구현
- Configuration 신호-슬롯 배선(`Apply`, `Token Refresh`, `Re-Auth`)
- 로그 시스템 추가

산출물:
- 실행 가능한 빈 셸 앱
- Configuration 창 기본 동작
- 플랫폼 상태 더미 UI

### Phase 2. 인증/토큰 계층

- localhost callback 서버 구현
- YouTube OAuth 구현(PKCE 포함)
- 치지직 OAuth 구현
- 토큰 저장/조회/삭제/만료시간 계산
- `Configuration` 창의 수동 갱신/브라우저 재인증 버튼 연결
- 시스템 브라우저 호출 및 callback 완료 UI 연결

산출물:
- 두 플랫폼 모두 로그인 성공
- 토큰 영속화 확인
- 앱 재시작 후 로그인 상태 복원
- Configuration 창에서 토큰 상태/갱신 결과 확인 가능

### Phase 3. YouTube 실시간 수집

- 활성 방송 탐색
- liveChatId 획득
- `streamList` 연결
- 메시지 파싱 및 통합 모델 변환
- 실패 시 폴백 polling

산출물:
- YouTube 채팅 수신
- 재연결 처리

### Phase 4. 치지직 실시간 수집

- `/users/me` 연동
- session URL 발급
- Socket.IO 연동
- chat subscribe
- 메시지 파싱 및 통합 모델 변환

산출물:
- 치지직 채팅 수신
- 세션 재생성 처리

### Phase 5. 통합 UI 완성

- 두 플랫폼 메시지 병합
- 필터/검색/상태 뷰
- 전역 `Connect/Disconnect` 토글 버튼
- 닉네임/메시지 클릭 기반 선택 모델
- 우측 액션 패널
- 오류/재인증 알림
- 로그 뷰

산출물:
- 운영 가능한 통합 모니터링 UI

### Phase 6. 발송 및 봇 기능

- YouTube 메시지 발송
- 치지직 메시지 발송
- YouTube 운영 액션 구현
- 치지직 운영 액션 구현
- capability 기반 버튼 활성화 정책 구현
- 규칙 기반 자동응답 엔진
- 발송 rate limit / queue

산출물:
- 기본 봇 기능 탑재
- 운영자 액션 UI 탑재

## 11. 권장 디렉터리 구조

```text
BotManager/
  PLAN_DEV.md
  src/
    app/
      main.cpp
      AppController.*
    ui/
      MainWindow.*
      ConfigurationDialog.*
      config/
        GeneralConfigPage.*
        PlatformConfigPageBase.*
        YouTubeConfigPage.*
        ChzzkConfigPage.*
        SecurityConfigPage.*
      widgets/
        TokenStatusCard.*
        ValidationSummaryBanner.*
    core/
      UnifiedChatMessage.h
      IChatPlatformAdapter.h
      ChatAggregator.*
      BotEngine.*
      ActionDescriptor.h
      ActionCapabilityResolver.*
      ConnectionCoordinator.*
    auth/
      OAuthLocalServer.*
      TokenVault.*
      QtKeychainTokenVault.*
    config/
      AppSettings.*
    platform/
      youtube/
        YouTubeAdapter.*
        YouTubeOAuth.*
        YouTubeChatClient.*
        YouTubeActionExecutor.*
      chzzk/
        ChzzkAdapter.*
        ChzzkOAuth.*
        ChzzkSessionClient.*
        ChzzkActionExecutor.*
    infra/
      HttpClient.*
      Logger.*
  config/
    app.ini
  docs/
    api-notes/
```

## 12. 기술 선택안

### 필수

- Qt5 Widgets
- `QNetworkAccessManager`
- `QSettings`
- JSON 파서(`QJsonDocument`, `QJsonObject`)

### 권장

- `QtKeychain` for secure token store
- Socket.IO C++ client 라이브러리
- 단위 테스트: `Qt Test`

### 판단

- YouTube는 순수 HTTP 기반이라 Qt 기본 네트워크만으로 충분하다.
- 치지직은 Session 연결에 Socket.IO가 필요하므로 외부 라이브러리 검토가 필요하다.
- 따라서 프로젝트 초기에 치지직 Socket.IO 프로토타입을 먼저 검증해야 한다.

## 13. 주요 리스크와 대응

### 리스크 1. Google OAuth 와 Qt5 PKCE 처리

문제:
- Google desktop OAuth 는 PKCE 사용이 권장된다.
- Qt5 기본 OAuth helper 는 최신 PKCE 편의 API가 부족하다.

대응:
- YouTube 인증은 별도 `YouTubeOAuth` 클래스로 수동 구현
- 브라우저 실행 + localhost callback + token exchange 직접 처리

### 리스크 2. 치지직 Socket.IO 호환성

문제:
- 치지직 문서는 Socket.IO 클라이언트 버전 호환 조건을 제시한다.
- Qt/C++ 에서 바로 붙일 때 handshake 호환성이 문제될 수 있다.

대응:
- 개발 초기 1주차에 독립 프로토타입 작성
- 연결/이벤트 수신 확인 전까지 UI 개발을 병렬 진행
- 필요 시 websocket 레벨 디버깅 로그 확보

### 리스크 3. 토큰 갱신 실패/권한 철회

문제:
- 치지직 refresh token 은 일회용이다.
- Google refresh token 도 사용자 철회 시 무효가 될 수 있다.

대응:
- 갱신 실패 시 `Auth Required` 상태 전환
- 토큰 교체는 성공 응답 수신 후 원자적 저장
- 마지막 성공 시각, 마지막 실패 원인 저장

### 리스크 4. 활성 방송 없음

문제:
- YouTube는 방송이 없으면 liveChatId 가 없다.

대응:
- 어댑터를 실패로 처리하지 않고 `No Active Broadcast` 상태로 유지
- 사용자가 수동 갱신 또는 주기적 재탐색 가능하게 설계

## 14. 우선 구현 범위(MVP)

다음 범위를 1차 목표로 잡는다.

- YouTube 로그인
- 치지직 로그인
- 설정 INI 저장
- 토큰 보안 저장
- 프로그램 시작 시 `Connect` 버튼 표시
- `Connect` 클릭 시 INI 기반 플랫폼 접속 시작
- 접속 성공 시 버튼 캡션 `Disconnect` 전환
- YouTube 채팅 수신
- 치지직 채팅 수신
- 통합 채팅 뷰
- 작성자 닉네임/권한/배지 표시
- 클릭형 액션 패널
- 토큰 수동 갱신
- 연결 상태 표시

MVP에서 제외 가능 항목:

- 자동응답
- 후원/구독 이벤트 통합
- 고급 관리자 기능

단, `관리자 기능`은 전부 제외가 아니라 아래 수준까지는 MVP 포함이 적절하다.

- 대상 사용자 클릭
- 공통 액션 버튼 렌더링
- 최소 1개 이상의 플랫폼별 실제 액션 실행

복잡도가 큰 기능은 후순위로 둔다.

- YouTube unban 전면 지원
- YouTube moderator remove 전체 지원
- 치지직 공지의 기존 수신 메시지 승격

## 15. 즉시 실행 가능한 구현 순서

1. Qt5 프로젝트 생성
2. `AppSettings` + `TokenVault` 먼저 구현
3. localhost OAuth callback 서버 구현
4. YouTube 인증 성공까지 고정
5. 치지직 인증 성공까지 고정
6. YouTube 채팅 수신 연결
7. 치지직 Socket.IO 수신 연결
8. 통합 채팅 모델/뷰 연결
9. 전역 `Connect/Disconnect` 토글 및 `ConnectionCoordinator` 연결
10. 클릭형 액션 패널 연결
11. 플랫폼별 운영 액션 최소 구현
12. Configuration 창의 토큰 갱신/브라우저 재인증 연결
13. 콜백 성공/실패 상태 UI 및 로그 연결
14. 이후 봇 엔진 착수

## 16. 결론

이 프로젝트의 핵심은 UI 자체보다 `플랫폼별 인증/세션/토큰 관리 차이`를 공통 인터페이스 뒤에 숨기는 것이다.

설계 방향은 다음으로 확정하는 것이 적절하다.

- 설정값과 토큰은 저장소를 분리한다.
- `YouTubeAdapter`, `ChzzkAdapter` 를 독립 구현한다.
- UI는 `UnifiedChatMessage` 기준으로만 동작한다.
- MVP는 "통합 채팅 수집기"로 완성하고, 이후 봇 발송/자동응답을 올린다.

이 방향이면 사용자 요구사항인 `플랫폼별 ID 분리`, `INI 기반 설정`, `클라이언트 내부 토큰 저장`, `토큰 갱신 가능`, `두 플랫폼 채팅 통합 조회`를 모두 충족할 수 있다.

## 17. 조사 출처

- YouTube LiveChatMessages streamList
  - https://developers.google.com/youtube/v3/live/docs/liveChatMessages/streamList
- YouTube LiveChatMessages list
  - https://developers.google.com/youtube/v3/live/docs/liveChatMessages/list
- YouTube LiveChatMessages insert
  - https://developers.google.com/youtube/v3/live/docs/liveChatMessages/insert
- YouTube LiveChatMessages delete
  - https://developers.google.com/youtube/v3/live/docs/liveChatMessages/delete
- YouTube LiveChatMessages transition
  - https://developers.google.com/youtube/v3/live/docs/liveChatMessages/transition
- YouTube LiveChatMessages resource
  - https://developers.google.com/youtube/v3/live/docs/liveChatMessages
- YouTube LiveBroadcasts list
  - https://developers.google.com/youtube/v3/live/docs/liveBroadcasts/list
- YouTube LiveChatBans
  - https://developers.google.com/youtube/v3/live/docs/liveChatBans
- YouTube LiveChatBans insert
  - https://developers.google.com/youtube/v3/live/docs/liveChatBans/insert
- YouTube LiveChatBans delete
  - https://developers.google.com/youtube/v3/live/docs/liveChatBans/delete
- YouTube LiveChatModerators list
  - https://developers.google.com/youtube/v3/live/docs/liveChatModerators/list
- YouTube LiveChatModerators insert
  - https://developers.google.com/youtube/v3/live/docs/liveChatModerators/insert
- YouTube LiveChatModerators delete
  - https://developers.google.com/youtube/v3/live/docs/liveChatModerators/delete
- Google OAuth 2.0 for iOS & Desktop Apps
  - https://developers.google.com/identity/protocols/oauth2/native-app
- Google OAuth 2.0 for Web Server Applications
  - https://developers.google.com/youtube/v3/guides/auth/server-side-web-apps
- CHZZK Authorization
  - https://chzzk.gitbook.io/chzzk/chzzk-api/authorization
- CHZZK Session
  - https://chzzk.gitbook.io/chzzk/chzzk-api/session
- CHZZK User
  - https://chzzk.gitbook.io/chzzk/chzzk-api/user
- CHZZK Restriction
  - https://chzzk.gitbook.io/chzzk/chzzk-api/restriction
- CHZZK Channel
  - https://chzzk.gitbook.io/chzzk/chzzk-api/channel
- CHZZK Chat
  - https://chzzk.gitbook.io/chzzk/chzzk-api/chat
- Qt 5.15 QSettings
  - https://doc.qt.io/qt-5/qsettings.html
- Qt 5.15 QOAuth2AuthorizationCodeFlow
  - https://doc.qt.io/qt-5/qoauth2authorizationcodeflow.html

## 18. 구현 수용 기준(DoD)

아래 조건을 모두 만족하면 1차 구현 완료로 판단한다.

1. 앱 실행 시 `Connect` 버튼이 기본 캡션으로 노출된다.
2. `Connect` 클릭 시 현재 INI 스냅샷을 로드해 `enabled=true` 플랫폼만 접속 시도한다.
3. 최소 1개 플랫폼 접속 성공 시 버튼 캡션이 `Disconnect`로 바뀐다.
4. `Disconnect` 클릭 시 활성 플랫폼 세션이 종료되고 캡션이 `Connect`로 복귀한다.
5. Configuration 창에서 `토큰 갱신` 버튼으로 Silent Refresh가 동작한다.
6. Silent Refresh 실패 시 `브라우저 재인증`으로 시스템 브라우저를 호출해 토큰을 재발급한다.
7. Configuration의 `Apply` 검증 규칙이 동작하며 invalid 설정 저장이 차단된다.
8. 채팅 항목에 닉네임/권한 정보가 표시되고 클릭 시 액션 패널이 열린다.
9. 공통 액션 1개 이상, YouTube 전용 1개 이상, 치지직 전용 1개 이상이 실행된다.
10. 토큰/연결/액션 실패는 사용자에게 원인 코드와 함께 표시된다.
11. 우측 `Actions` 상단의 YouTube/CHZZK 상태 사각형이 `인증중/온라인/토큰정상/토큰비정상` 규칙에 맞게 색상 전환된다.

## 19. 통합 테스트 시나리오

### 19.1 연결 토글 시나리오

1. 앱 실행
2. `Connect` 클릭
3. 상태가 `CONNECTING`으로 전환되는지 확인
4. 성공 시 `CONNECTED` 또는 `PARTIALLY_CONNECTED`로 전환되는지 확인
5. 버튼 캡션이 `Disconnect`인지 확인
6. `Disconnect` 클릭 후 `IDLE` 복귀 및 캡션 `Connect` 확인

### 19.2 INI 스냅샷 적용 시나리오

1. Configuration에서 `enabled` 및 `redirect_uri` 변경 후 `Apply`
2. 연결 중이면 즉시 적용되지 않고 안내가 표시되는지 확인
3. Disconnect 후 Connect 시 새 설정이 반영되는지 확인

### 19.3 토큰 갱신 시나리오

1. 정상 refresh token 상태에서 `토큰 갱신` 실행
2. access token 만료시각이 갱신되는지 확인
3. refresh 강제 실패 상태에서 `브라우저 재인증` 실행
4. 시스템 브라우저 오픈, callback 수신, 토큰 저장 완료까지 확인

### 19.4 액션 패널 시나리오

1. YouTube 채팅 메시지 선택 후 `메시지 삭제` 실행
2. CHZZK 채팅 유저 선택 후 `활동 제한 추가` 실행
3. 조건 미충족 액션이 비활성 + 이유 tooltip으로 표시되는지 확인

### 19.5 오류 복구 시나리오

1. 네트워크 차단 상태에서 Connect 실행
2. 오류 코드/메시지가 UI에 표시되는지 확인
3. 네트워크 복구 후 재시도 시 정상 연결되는지 확인

### 19.6 플랫폼 상태 사각형 시나리오

1. 토큰 정상 + 미연결 상태에서 두 플랫폼 사각형이 초록색인지 확인
2. `Connect` 성공 후 해당 플랫폼 사각형이 하늘색으로 전환되는지 확인
3. Configuration에서 브라우저 인증 시작 시 해당 플랫폼 사각형이 주황색으로 전환되는지 확인
4. 인증 완료 후 사각형이 `하늘색(연결중)` 또는 `초록색(미연결)`으로 복귀하는지 확인
5. 토큰 삭제/인증 실패 시 사각형이 빨간색으로 전환되는지 확인

## 20. 데이터 계약 상세

### 20.1 설정 스냅샷 계약

```cpp
enum class PlatformId { YOUTUBE, CHZZK };

struct PlatformSettings {
    bool enabled = false;
    QString clientId;
    QString clientSecret;      // CHZZK only
    QString redirectUri;
    QString scope;
    QString channelId;
    QString channelName;
    QString accountLabel;
};

struct AppSettingsSnapshot {
    QString language;
    QString logLevel;
    QString mergeOrder;
    bool autoReconnect = true;
    PlatformSettings youtube;
    PlatformSettings chzzk;
    QDateTime loadedAtUtc;
};
```

계약 규칙:

- `connectAll()` 호출 시점에 `AppSettingsSnapshot`은 immutable로 취급
- 런타임은 이 스냅샷만 참조하고 원본 INI를 재조회하지 않음
- `loadedAtUtc`는 문제 재현 시점 추적용으로 로그에 포함

### 20.2 토큰 상태/진행 상태 계약

```cpp
enum class TokenState {
    NO_TOKEN,
    VALID,
    EXPIRING_SOON,
    EXPIRED,
    REFRESHING,
    AUTH_REQUIRED,
    ERROR
};

struct TokenStateView {
    PlatformId platform;
    TokenState state;
    bool hasRefreshToken = false;
    QDateTime accessExpireAtUtc;
    QString lastResultCode;
    QString lastResultMessage;
    QDateTime lastUpdatedAtUtc;
};

enum class AuthStage {
    PREPARING_AUTH_URL,
    OPENING_SYSTEM_BROWSER,
    WAITING_CALLBACK,
    EXCHANGING_TOKEN,
    SAVING_TOKEN,
    COMPLETED,
    FAILED
};

struct AuthProgress {
    PlatformId platform;
    AuthStage stage;
    QString code;
    QString message;
};
```

### 20.3 검증 결과 계약

```cpp
struct ValidationIssue {
    QString fieldName;     // objectName 기준
    QString code;          // e.g. INVALID_REDIRECT_URI
    QString message;
};

struct ValidationResult {
    PlatformId platform;
    bool ok = false;
    QList<ValidationIssue> issues;
};
```

## 21. 에러 코드 표준화

플랫폼 원본 오류를 아래 앱 공통 코드로 정규화한다.

| 앱 코드 | 의미 | UI 기본 안내 |
|---|---|---|
| `AUTH_REQUIRED` | 재인증 필요 | 브라우저 재인증을 진행하세요 |
| `TOKEN_EXPIRED` | access token 만료 | 토큰 갱신 또는 재인증 필요 |
| `NO_PERMISSION` | 권한 부족(401/403) | 계정 권한 또는 scope를 확인하세요 |
| `INVALID_CONFIG` | 설정값 오류 | Configuration 필드를 확인하세요 |
| `NETWORK_ERROR` | 네트워크 장애 | 네트워크 연결 후 재시도하세요 |
| `TIMEOUT` | 요청 시간 초과 | 잠시 후 다시 시도하세요 |
| `RATE_LIMITED` | 호출 제한 초과 | 잠시 대기 후 재시도하세요 |
| `NOT_FOUND` | 대상 리소스 없음 | 대상이 존재하는지 확인하세요 |
| `CONFLICT` | 상태 충돌/중복 요청 | 현재 상태를 새로고침 후 재시도하세요 |
| `UNKNOWN` | 미분류 오류 | 로그를 확인하고 재시도하세요 |

정규화 규칙:

- HTTP status + 플랫폼별 에러 필드를 함께 평가
- 동일 오류라도 액션 문맥(connect/refresh/action)에 따라 메시지 템플릿 분리
- UI에는 사용자 메시지, 로그에는 원본 code/message 모두 기록

## 22. 재시도/타임아웃/복구 정책

### 22.1 공통 네트워크 정책

- 기본 요청 timeout: 10초
- OAuth/token 요청 timeout: 20초
- Connect 단계 최대 허용: 35초
- Disconnect 단계 최대 허용: 10초

### 22.2 재시도 정책

| 대상 | 재시도 횟수 | 백오프 |
|---|---|---|
| 일반 HTTP(GET) | 최대 3회 | 1s, 2s, 4s |
| token refresh | 최대 1회 | 2s |
| OAuth code exchange | 재시도 없음 | 사용자 재시작 |
| 치지직 세션 연결 | 최대 5회 | 1s, 2s, 4s, 8s, 15s |
| YouTube streamList 연결 | 최대 5회 | 1s, 2s, 4s, 8s, 15s |

재시도 제외:

- `4xx` 중 명백한 설정/권한 오류(`401/403/400`)는 즉시 실패
- 사용자 취소/수동 disconnect 상황은 재시도 금지

### 22.3 자동 복구 정책

- `autoReconnect=true`일 때만 네트워크 단절 후 재연결 수행
- `AUTH_REQUIRED` 상태는 자동 복구하지 않고 사용자 액션 대기
- `PARTIALLY_CONNECTED` 상태에서는 실패 플랫폼만 백그라운드 재시도

## 23. 로그/메트릭 상세

### 23.1 로그 파일 정책

- 로그 형식: JSON Lines
- 기본 경로: `logs/app-YYYYMMDD.log`
- 로테이션: 1일 1파일 + 최근 14일 보관
- 민감정보(token, client_secret) 마스킹 필수

### 23.2 최소 로그 이벤트

| event | 필수 필드 |
|---|---|
| `connect.start` | `sessionId`, `enabledPlatforms`, `snapshotLoadedAt` |
| `connect.platform.result` | `platform`, `ok`, `errorCode`, `latencyMs` |
| `connect.finish` | `state`, `connectedPlatforms`, `failedPlatforms` |
| `token.refresh.start` | `platform`, `mode(silent/interactive)` |
| `token.refresh.finish` | `platform`, `ok`, `errorCode`, `expireAt` |
| `action.execute` | `platform`, `actionId`, `targetId`, `ok`, `errorCode` |

### 23.3 운영 메트릭

- `connect_success_rate` (1시간/1일)
- `token_refresh_success_rate` (플랫폼별)
- `average_connect_latency_ms`
- `action_failure_rate_by_actionId`
- `auth_required_count` (일별)

## 24. 보안 강화 체크리스트

- TokenVault 저장값은 평문 로그/예외 메시지에 노출 금지
- `redirect_uri`는 loopback만 허용, 외부 도메인 금지
- OAuth `state` 미일치 시 즉시 인증 세션 폐기
- PKCE verifier는 메모리 수명 최소화, 사용 후 파기
- 브라우저 재인증 중 복수 세션 동시 진행 금지
- INI 파일 권한은 사용자 전용 읽기/쓰기 권장

## 25. 릴리즈 전 점검 목록

1. DoD 10개 항목 전부 통과
2. 통합 테스트 시나리오 19.1~19.5 통과
3. YouTube/CHZZK 실제 계정으로 Connect/Disconnect 실험 기록 확보
4. 토큰 만료/권한 회수/네트워크 단절 장애 주입 테스트 통과
5. 로그에 민감정보 미노출 샘플 점검 완료
6. 문서의 actionId와 코드 상수 일치 검증 완료
