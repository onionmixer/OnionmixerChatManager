#pragma once

#include "core/AppTypes.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>

class ClientConfigDialog;

class QApplication;
class QTimer;
class MainBroadcastWindow;
class BroadChatConnection;

// v2-7: QObject orchestrator (NOT a QApplication subclass).
// QApplication is owned by main(); this class only references it.
class BroadChatClientApp : public QObject
{
    Q_OBJECT
public:
    explicit BroadChatClientApp(QApplication* app);
    ~BroadChatClientApp() override;

    // §16.12 Critical: 반환값이 exit code (0=성공, 1=runtime, 2=CLI 파싱, 3=환경 에러).
    int initialize(int argc, char** argv);
    void show();

private slots:
    void onHelloCompleted(const QString& serverVersion, int protocolMin, int protocolMax);
    void onDisconnected();
    void onProtocolError(const QString& detail);
    void onByeReceived(const QString& reason, const QString& detail);
    void onChatReceived(const UnifiedChatMessage& message);
    void onHistoryChunkReceived(const QString& requestId,
                                const QVector<UnifiedChatMessage>& messages,
                                bool hasMore);
    void onEmojiImageReceived(const QString& emojiId, const QByteArray& bytes,
                              const QString& mime, const QString& error);
    void onViewersReceived(int youtube, int chzzk, int total);
    void onPlatformStatusReceived(const QJsonObject& data);
    void onAboutToQuit();
    void onSettingsRequested();
    void onQuitRequested();
    // Step 7: 창 geometry 변화를 ini에 persist (debounce 없이 즉시 반영 — QSettings 내부 버퍼링).
    void onWindowResized(int width, int height);
    void onWindowMoved(int x, int y);
    // Step 8: 재연결 타이머 만료 시 connect 재시도.
    void onReconnectTimerFired();

private:
    QApplication* m_app = nullptr;
    // v37-3 TCP endpoint
    QString m_host;
    quint16 m_port = 0;
    QString m_authToken;
    std::unique_ptr<MainBroadcastWindow> m_window;
    std::unique_ptr<BroadChatConnection> m_connection;

    // §11.2 v12-15: 실패 emoji id의 60초 재요청 backoff.
    QHash<QString /*emojiId*/, QDateTime> m_emojiFailedAt;

    // FU-J1 (v51-2 Critical): 설정 다이얼로그 재진입 가드 · 싱글톤 재사용 패턴.
    // 사용자가 우클릭 "세팅"을 연속 누르거나 Shift+F10 반복 시 복수 dialog 생성 방지.
    QPointer<ClientConfigDialog> m_configDialog;

    // v39 설정 파일 경로 (--config-dir CLI로 override 가능, 기본 exe 옆 config.ini)
    QString m_configPath;

    // v68 #1: helloCompleted 이후 자동 전송한 request_history 의 requestId 추적.
    // history_chunk 수신 시 매칭해 append 수행.
    QString m_pendingHistoryReqId;

    // v68 #7: client_id 지속성 — ini [identity] client_id. 없으면 신규 UUID 생성·저장.
    QString m_clientId;

    // v81: v68 #8 QLockFile 제거 — 다중 인스턴스 허용 (last-write-wins). 사용자
    // 설정 분리가 필요하면 `--instance-id` 로 별도 config dir 지정 가능. 서버는 이미
    // duplicate clientId 를 enforcement 하지 않아 여러 프로세스 동시 접속 수용.

    // v72 #8 (§5.6.3 v56-3): in-memory 모드 — 모든 fallback 실패 시 persistence off.
    // loadConfig/saveConfig 가 no-op. UI 배지로 사용자 고지.
    bool m_inMemory = false;

    // Step 7: window geometry (§17.1 스키마 [window] 섹션, §5.6.8 validation)
    int m_windowX = -1;       // -1 = 미지정 (OS가 배치)
    int m_windowY = -1;
    int m_windowWidth = 400;
    int m_windowHeight = 600;

    // v61: chat·broadcast 스타일 snapshot ([chat]·[broadcast] ini 섹션 + BroadcastChatWindow::applySettings)
    AppSettingsSnapshot m_snapshot;

    // Step 8: 재연결 상태 머신 (§18.1)
    QTimer* m_reconnectTimer = nullptr;
    int m_retryCount = 0;            // Connecting 진입 실패마다 +1
    bool m_terminated = false;       // auth_failed·version_mismatch — 재시도 중단 (프로토콜 사유)
    bool m_shuttingDown = false;     // aboutToQuit·사용자 종료 — 앱 종료 중 (재연결 skip)
    QDateTime m_activeSince;         // Active 진입 시점; 10s 유지 시 retryCount=0 (v34-11)
    QString m_lastByeReason;         // Terminated dialog 용

    void loadConfig();
    void saveConfig();

    // Step 8 helpers
    void scheduleReconnect(int delayMs);
    int computeBackoffMs(int retryCount) const;          // 지수 1→30s cap
    int backoffMsForByeReason(const QString& reason) const; // -1 = 재시도 중단
    void showTerminationDialog(const QString& reason, const QString& detail);
};
