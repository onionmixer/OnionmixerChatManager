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

signals:
    void windowResized(int width, int height);
    void windowMoved(int x, int y);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

private:
    void setTransparentMode(bool transparent);
    void repositionViewerCountOverlay();

    QListView* m_listView = nullptr;
    ChatBubbleDelegate* m_delegate = nullptr;
    QLabel* m_viewerCountLabel = nullptr;
    bool m_transparentMode = true;
    QPoint m_dragStartPos;
    bool m_dragging = false;
    bool m_suppressMoveEvent = false;
    QPoint m_lastUserPosition;
    QString m_viewerCountPosition = QStringLiteral("TopLeft");
    QColor m_transparentBgColor = QColor(0, 0, 0, 0);
    QColor m_opaqueBgColor = QColor(255, 255, 255, 255);
};

#endif // BROADCAST_CHAT_WINDOW_H
