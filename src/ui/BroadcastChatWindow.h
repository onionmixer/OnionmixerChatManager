#ifndef BROADCAST_CHAT_WINDOW_H
#define BROADCAST_CHAT_WINDOW_H

#include "core/AppTypes.h"

#include <QColor>
#include <QPoint>
#include <QWidget>

class ChatBubbleDelegate;
class ChatMessageModel;
class EmojiImageCache;
class QLabel;
class QListView;

class BroadcastChatWindow : public QWidget {
    Q_OBJECT
public:
    explicit BroadcastChatWindow(ChatMessageModel* model, EmojiImageCache* emojiCache, QWidget* parent = nullptr);

    void applySettings(const AppSettingsSnapshot& snapshot);
    void updateViewerCount(int youtubeCount, int chzzkCount);

    // PLAN §7.1·§18.2: 투명/불투명 모드 외부 제어.
    // 기본 생성자는 m_transparentMode=true (투명). 외부에서 초기값 설정이 필요하면
    // 이 메서드를 호출. 내부 mouseDoubleClickEvent 토글 로직은 동일 경로 재사용.
    // (과거 private → public 승격. 메인 앱의 내부 사용 경로는 영향 없음.)
    void setTransparentMode(bool transparent);
    bool isTransparentMode() const { return m_transparentMode; }

signals:
    void windowResized(int width, int height);
    void windowMoved(int x, int y);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

private:
    void repositionViewerCountOverlay();

    // Viewer count rendering (Phase 2: CenterLeft/CenterRight rotation support)
    QString buildViewerCountText() const;
    bool isRotatedViewerPosition() const;
    void renderViewerCountLabel();
    QPixmap renderViewerCountPixmap(const QString& text, bool rotated) const;

    QListView* m_listView = nullptr;
    ChatBubbleDelegate* m_delegate = nullptr;
    QLabel* m_viewerCountLabel = nullptr;
    bool m_transparentMode = true;
    QPoint m_dragStartPos;
    bool m_dragging = false;
    bool m_suppressMoveEvent = false;
    bool m_draggedDuringSuppress = false;
    QPoint m_lastUserPosition;
    QString m_viewerCountPosition = QStringLiteral("TopLeft");
    QColor m_transparentBgColor = QColor(0, 0, 0, 0);
    QColor m_opaqueBgColor = QColor(255, 255, 255, 255);

    // Cached viewer counts for re-rendering on position change
    int m_youtubeViewerCount = -1;
    int m_chzzkViewerCount = -1;
    bool m_viewerCountHasData = false;
};

#endif // BROADCAST_CHAT_WINDOW_H
