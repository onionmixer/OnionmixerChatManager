#include "ui/BroadcastChatWindow.h"
#include "core/EmojiImageCache.h"
#include "core/PlatformTraits.h"
#include "ui/ChatBubbleDelegate.h"
#include "ui/ChatMessageModel.h"

#include <QAbstractItemView>
#include <QLabel>
#include <QListView>
#include <QLocale>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>

BroadcastChatWindow::BroadcastChatWindow(ChatMessageModel* model, EmojiImageCache* emojiCache, QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(tr("Broadcast Chat"));

    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    m_transparentMode = true;

    m_delegate = new ChatBubbleDelegate(this);
    m_delegate->setEmojiCache(emojiCache);

    m_listView = new QListView(this);
    m_listView->setModel(model);
    m_listView->setItemDelegate(m_delegate);
    m_listView->setSelectionMode(QAbstractItemView::NoSelection);
    m_listView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_listView->setUniformItemSizes(false);
    m_listView->setFrameShape(QFrame::NoFrame);
    m_listView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listView->setStyleSheet(QStringLiteral("background: transparent;"));
    m_listView->viewport()->setAutoFillBackground(false);
    m_listView->viewport()->installEventFilter(this);

    connect(model, &QAbstractItemModel::rowsInserted, this, [this]() {
        m_listView->scrollToBottom();
    });

    m_viewerCountLabel = new QLabel(this);
    m_viewerCountLabel->setStyleSheet(QStringLiteral(
        "background: rgba(0,0,0,160); color: #ffffff; padding: 4px 8px; border-radius: 4px; font-weight: bold;"));
    m_viewerCountLabel->installEventFilter(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_listView);
}

bool BroadcastChatWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_listView->viewport() || watched == m_viewerCountLabel) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            setTransparentMode(!m_transparentMode);
            return true;
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_dragging = true;
                m_dragStartPos = me->globalPos() - frameGeometry().topLeft();
                return true;
            }
        }
        if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (m_dragging && (me->buttons() & Qt::LeftButton)) {
                move(me->globalPos() - m_dragStartPos);
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            m_dragging = false;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void BroadcastChatWindow::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), m_transparentMode ? m_transparentBgColor : m_opaqueBgColor);
}

void BroadcastChatWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    repositionViewerCountOverlay();
    emit windowResized(width(), height());
}

void BroadcastChatWindow::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    if (!m_suppressMoveEvent) {
        m_lastUserPosition = pos();
        const QPoint contentPos = geometry().topLeft();
        emit windowMoved(contentPos.x(), contentPos.y());
    }
}

void BroadcastChatWindow::setTransparentMode(bool transparent)
{
    m_transparentMode = transparent;
    const QSize savedSize = size();

    m_suppressMoveEvent = true;

    if (transparent) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        m_listView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    } else {
        setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
        m_listView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
    resize(savedSize);
    move(m_lastUserPosition);
    show();
    update();

    QTimer::singleShot(50, this, [this]() {
        m_suppressMoveEvent = false;
    });
}

void BroadcastChatWindow::repositionViewerCountOverlay()
{
    if (!m_viewerCountLabel || m_viewerCountLabel->text().isEmpty()) return;
    m_viewerCountLabel->adjustSize();

    const int margin = 8;
    const int lw = m_viewerCountLabel->width();
    const int lh = m_viewerCountLabel->height();
    const int ww = width();
    const int wh = height();

    int x = margin;
    if (m_viewerCountPosition.endsWith(QStringLiteral("Right")))       x = ww - lw - margin;
    else if (m_viewerCountPosition.endsWith(QStringLiteral("Center"))) x = (ww - lw) / 2;

    int y = margin;
    if (m_viewerCountPosition.startsWith(QStringLiteral("Bottom")))      y = wh - lh - margin;
    else if (m_viewerCountPosition.startsWith(QStringLiteral("Center"))) y = (wh - lh) / 2;

    m_viewerCountLabel->move(x, y);
    m_viewerCountLabel->raise();
}

void BroadcastChatWindow::updateViewerCount(int youtubeCount, int chzzkCount)
{
    auto formatCount = [](int count) -> QString {
        return count >= 0 ? QLocale().toString(count) : QStringLiteral("\u2014");
    };

    QStringList parts;
    parts << QStringLiteral("%1 %2").arg(PlatformTraits::badgeSymbol(PlatformId::YouTube), formatCount(youtubeCount));
    parts << QStringLiteral("%1 %2").arg(PlatformTraits::badgeSymbol(PlatformId::Chzzk), formatCount(chzzkCount));

    int total = 0;
    bool hasAny = false;
    if (youtubeCount >= 0) { total += youtubeCount; hasAny = true; }
    if (chzzkCount >= 0) { total += chzzkCount; hasAny = true; }
    parts << QStringLiteral("\u03A3 %1").arg(hasAny ? QLocale().toString(total) : QStringLiteral("\u2014"));

    m_viewerCountLabel->setText(parts.join(QStringLiteral("  ")));
    m_viewerCountLabel->show();
    repositionViewerCountOverlay();
}

void BroadcastChatWindow::applySettings(const AppSettingsSnapshot& snapshot)
{
    m_delegate->setFontFamily(snapshot.chatFontFamily);
    m_delegate->setFontSize(snapshot.chatFontSize);
    m_delegate->setFontBold(snapshot.chatFontBold);
    m_delegate->setFontItalic(snapshot.chatFontItalic);
    m_delegate->setLineSpacing(snapshot.chatLineSpacing);

    m_viewerCountPosition = snapshot.broadcastViewerCountPosition;

    QColor transparentCandidate(snapshot.broadcastTransparentBgColor);
    m_transparentBgColor = transparentCandidate.isValid() ? transparentCandidate : QColor(0, 0, 0, 0);
    QColor opaqueCandidate(snapshot.broadcastOpaqueBgColor);
    m_opaqueBgColor = opaqueCandidate.isValid() ? opaqueCandidate : QColor(255, 255, 255, 255);

    resize(snapshot.broadcastWindowWidth, snapshot.broadcastWindowHeight);
    repositionViewerCountOverlay();

    if (m_listView) {
        emit m_listView->model()->layoutChanged();
        m_listView->viewport()->update();
    }
    update();
}
