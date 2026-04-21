#pragma once

#include "core/AppTypes.h"

#include <QObject>
#include <QString>

#include <memory>

class BroadcastChatWindow;
class ChatMessageModel;
class EmojiImageCache;
class QLabel;
class QNetworkAccessManager;
class QSystemTrayIcon;
class QTimer;

// PLAN_DEV_BROADCHATCLIENT §5.4 v2-11·v34-1 wrapper.
// Owns a local ChatMessageModel + EmojiImageCache and composes the shared-lib
// BroadcastChatWindow. Client-specific logic (context menu §8.1, status badge
// §8.3) lands on this wrapper in later steps — kept as a thin façade for now.
class MainBroadcastWindow : public QObject
{
    Q_OBJECT
public:
    explicit MainBroadcastWindow(QObject* parent = nullptr);
    ~MainBroadcastWindow() override;

    void show();
    void close();

    BroadcastChatWindow* chatWindow() { return m_window.get(); }
    ChatMessageModel* model() { return m_model; }
    EmojiImageCache* emojiCache() { return m_cache; }

    // Step 7: 저장된 geometry 복원. show() 호출 전에 적용.
    // 유효하지 않은 좌표/크기는 무시 (plan §7.1·§18.2·§16.7 v56 clamp).
    void applyInitialGeometry(int x, int y, int width, int height);

    // v68 #13 (§8.3): 타이틀에 [instance-id] suffix 추가 — 다중 인스턴스 구별용.
    // 빈 문자열이면 기본 제목만 사용.
    void setInstanceId(const QString& instanceId);

    // v70 #15 (§8.1): tray icon 연결 상태 tooltip 업데이트.
    // 인자는 사용자 가시용 한 줄 메시지 — 로그 용어 그대로 노출 금지.
    void updateTrayStatus(const QString& statusText);

    // v70 #15: tray 가용 여부 — 테스트·UX 분기용.
    bool hasTrayIcon() const;

    // v72 #11 (§8.3 v2-17): 연결 상태 시각 표시.
    // - Connecting/Reconnecting: 노랑
    // - Connected: 녹색 + 5초 자동 페이드아웃
    // - Error / Terminated: 빨강 (상시)
    enum class ConnectionState { Connecting, Connected, Error, Terminated };
    void setConnectionState(ConnectionState state);

    // v72 #8 (§5.6.3 v56-3): in-memory 모드 ⚠ 상시 표시 on/off.
    // true 일 때 설정 저장 불가 아이콘 배지 유지.
    void setInMemoryMode(bool inMemory);

    // v73 #14 (§8.3 v34-13 · §0.7 권장안 B): viewer_count_position 변경 시 호출.
    // overlay 배치가 반대 모서리로 동적 재배치됨. 공유 lib 무수정.
    // 소유자(BroadChatClientApp) 가 `applySettings` 호출 전후에 동기 호출해 drift 방지.
    void setViewerCountPosition(const QString& pos);

public slots:
    // Append a chat message to the local model and pre-register its emoji urls.
    void appendChat(const UnifiedChatMessage& message);

    // Forward viewer counts to the shared BroadcastChatWindow overlay.
    // -1 values are rendered as "–" per §v7.
    void setViewerCount(int youtube, int chzzk);

signals:
    // PLAN §8.1 v1 우클릭 메뉴: "세팅" 선택 시 emit.
    void settingsRequested();
    // "종료" 선택 시 emit. 소유자가 QCoreApplication::quit 호출.
    void quitRequested();
    // Step 7: 창 크기·위치 변경 시 forward. 소유자가 ini에 persist.
    void windowResized(int width, int height);
    void windowMoved(int x, int y);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    // v70 #15: tray 아이콘 활성화 (좌클릭 등) → 창 토글.
    void onTrayActivated(int reason);
    // v72 #12 (§8.3): 5초 미연결 시 viewer count 자리에 — 표시.
    void onViewerPlaceholderTick();

private:
    // v70 #15: tray 아이콘 초기화. isSystemTrayAvailable() 실패 시 no-op (graceful fallback).
    void setupTrayIcon();
    // v72 #11·#12·#8: 창 위에 얹는 overlay 위젯들. chatWindow() 를 parent 로 가져
    // 공유 `BroadcastChatWindow` 수정 없이 UI 확장.
    void setupOverlays();
    void repositionOverlays();
    void refreshBadgeStyle();

    ChatMessageModel* m_model = nullptr;
    QNetworkAccessManager* m_network = nullptr;
    EmojiImageCache* m_cache = nullptr;
    std::unique_ptr<BroadcastChatWindow> m_window;
    QSystemTrayIcon* m_tray = nullptr;     // v70 #15: nullptr = tray 미지원 환경
    QString m_instanceId;                  // v70 #15: tray tooltip 구성용
    QString m_statusText;                  // v70 #15: tray tooltip 구성용

    // v72 overlays (chatWindow child 로 소유권 이전)
    QLabel* m_statusBadge = nullptr;       // #11: 연결 상태 원형 배지
    QLabel* m_viewerPlaceholder = nullptr; // #12: "—" 라벨
    QTimer* m_viewerTimer = nullptr;       // #12: 5s 감시 타이머
    QLabel* m_inMemoryBadge = nullptr;     // #8: "⚠ 저장 불가" 배지

    ConnectionState m_connState = ConnectionState::Connecting;
    bool m_inMemory = false;
    bool m_viewersReceived = false;
    // v73 #14: 현재 viewer_count_position 값 — 배지/플레이스홀더 배치 결정에 사용.
    QString m_currentViewerPos = QStringLiteral("TopLeft");
};
