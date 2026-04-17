#include "ui/BroadcastChatWindow.h"
#include "core/EmojiImageCache.h"
#include "core/PlatformTraits.h"
#include "ui/ChatBubbleDelegate.h"
#include "ui/ChatMessageModel.h"
#include "ui/ViewerCountStyle.h"

#include <QAbstractItemView>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QLabel>
#include <QListView>
#include <QLocale>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPixmap>
#include <QStringList>
#include <QTextDocument>
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
    // 방송창에서는 스크롤바를 항상 숨긴다 (마우스 휠 스크롤은 policy와 무관하게 유지됨)
    m_listView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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
                if (m_suppressMoveEvent) {
                    m_draggedDuringSuppress = true;
                }
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
        m_lastUserPosition = geometry().topLeft();
        emit windowMoved(m_lastUserPosition.x(), m_lastUserPosition.y());
    }
}

void BroadcastChatWindow::setTransparentMode(bool transparent)
{
    m_transparentMode = transparent;
    const QSize savedSize = size();

    m_draggedDuringSuppress = false;
    m_suppressMoveEvent = true;

    if (transparent) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    } else {
        setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    }
    // 방송창은 투명/불투명 모드 모두 스크롤바 비표시 유지 (휠 스크롤은 정상 동작)
    // Use setGeometry (not move) in both modes: Qt5/X11 retains stale frame margin
    // cache across setWindowFlags, which makes move() place the native window offset
    // by the old title-bar height. setGeometry sets crect directly, bypassing it.
    setGeometry(m_lastUserPosition.x(), m_lastUserPosition.y(),
                savedSize.width(), savedSize.height());
    show();
    update();

    QTimer::singleShot(50, this, [this, savedPos = m_lastUserPosition, savedSize = size()]() {
        if (m_transparentMode && !m_draggedDuringSuppress) {
            setGeometry(savedPos.x(), savedPos.y(), savedSize.width(), savedSize.height());
        }
        m_suppressMoveEvent = false;
        m_draggedDuringSuppress = false;
    });
}

void BroadcastChatWindow::repositionViewerCountOverlay()
{
    // Phase 2: text().isEmpty() 대신 m_viewerCountHasData 가드 (pixmap 상태에서 text().isEmpty() true 반환)
    if (!m_viewerCountLabel || !m_viewerCountHasData) return;
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

QString BroadcastChatWindow::buildViewerCountText() const
{
    // 플랫폼 색상이 적용된 rich-text HTML. 플랫폼 확장 시 아래 엔트리 벡터에만
    // 추가하면 자동으로 반영됨 (ViewerCountStyle::buildViewerHtml 참조).
    const QVector<QPair<PlatformId, int>> entries = {
        { PlatformId::YouTube, m_youtubeViewerCount },
        { PlatformId::Chzzk, m_chzzkViewerCount },
    };
    return ViewerCountStyle::buildViewerHtml(entries);
}

bool BroadcastChatWindow::isRotatedViewerPosition() const
{
    return m_viewerCountPosition == QStringLiteral("CenterLeft")
        || m_viewerCountPosition == QStringLiteral("CenterRight");
}

QPixmap BroadcastChatWindow::renderViewerCountPixmap(const QString& text, bool rotated) const
{
    // text 인자는 buildViewerCountText()가 반환한 rich-text HTML.
    QFont font = QGuiApplication::font();
    font.setBold(true);

    // HTML을 QTextDocument로 측정·렌더해 아이콘별 색상 반영.
    QTextDocument doc;
    doc.setDefaultFont(font);
    doc.setDefaultStyleSheet(
        QStringLiteral("body { color: %1; }").arg(QColor::fromRgb(ViewerCountStyle::kFg).name()));
    doc.setHtml(text);
    doc.setTextWidth(-1);
    const QSizeF docSize = doc.size();
    const int textW = static_cast<int>(docSize.width()) + ViewerCountStyle::kPaddingX * 2;
    const int textH = static_cast<int>(docSize.height()) + ViewerCountStyle::kPaddingY * 2;

    const int pmW = rotated ? textH : textW;
    const int pmH = rotated ? textW : textH;

    // High-DPI support
    const qreal dpr = m_viewerCountLabel ? m_viewerCountLabel->devicePixelRatio() : 1.0;
    QPixmap pm(static_cast<int>(pmW * dpr), static_cast<int>(pmH * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    if (rotated) {
        // 90° CW rotation centered on pixmap
        p.translate(pmW / 2.0, pmH / 2.0);
        p.rotate(90);
        p.translate(-textW / 2.0, -textH / 2.0);
    }
    p.setBrush(QColor::fromRgba(ViewerCountStyle::kBg));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(0, 0, textW, textH, ViewerCountStyle::kRadius, ViewerCountStyle::kRadius);

    // 본문 영역(padding 내부)에 document 렌더
    p.save();
    p.translate(ViewerCountStyle::kPaddingX, ViewerCountStyle::kPaddingY);
    doc.drawContents(&p);
    p.restore();
    p.end();
    return pm;
}

void BroadcastChatWindow::renderViewerCountLabel()
{
    if (!m_viewerCountLabel) return;
    const QString html = buildViewerCountText();  // rich-text HTML
    if (isRotatedViewerPosition()) {
        // 회전: pixmap 기반 (CSS clear, text clear, pixmap 설정)
        m_viewerCountLabel->setStyleSheet(QString());
        m_viewerCountLabel->setText(QString());
        m_viewerCountLabel->setPixmap(renderViewerCountPixmap(html, true));
    } else {
        // 비회전: QLabel이 RichText 모드에서 span style="color"를 respect
        m_viewerCountLabel->setStyleSheet(QStringLiteral(
            "background: rgba(0,0,0,160); color: #ffffff; padding: 4px 8px; border-radius: 4px; font-weight: bold;"));
        m_viewerCountLabel->setPixmap(QPixmap());  // pixmap 해제
        m_viewerCountLabel->setTextFormat(Qt::RichText);
        m_viewerCountLabel->setText(html);
    }
    m_viewerCountLabel->adjustSize();
}

void BroadcastChatWindow::updateViewerCount(int youtubeCount, int chzzkCount)
{
    m_youtubeViewerCount = youtubeCount;
    m_chzzkViewerCount = chzzkCount;
    m_viewerCountHasData = true;
    renderViewerCountLabel();
    m_viewerCountLabel->show();
    repositionViewerCountOverlay();
}

namespace {
QColor parseColorOrInvalid(const QString& s)
{
    if (s.trimmed().isEmpty()) return QColor();
    const QColor c(s);
    return c.isValid() ? c : QColor();
}
} // namespace

void BroadcastChatWindow::applySettings(const AppSettingsSnapshot& snapshot)
{
    m_delegate->setFontFamily(snapshot.chatFontFamily);
    m_delegate->setFontSize(snapshot.chatFontSize);
    m_delegate->setFontBold(snapshot.chatFontBold);
    m_delegate->setFontItalic(snapshot.chatFontItalic);
    m_delegate->setLineSpacing(snapshot.chatLineSpacing);
    // 방송창 전용 스타일 (UPDATE_BROADCAHT_STYLE.md)
    m_delegate->setBodyOverrideColor(parseColorOrInvalid(snapshot.broadcastChatBodyFontColor));
    m_delegate->setTextOutlineColor(parseColorOrInvalid(snapshot.broadcastChatOutlineColor));

    m_viewerCountPosition = snapshot.broadcastViewerCountPosition;

    QColor transparentCandidate(snapshot.broadcastTransparentBgColor);
    m_transparentBgColor = transparentCandidate.isValid() ? transparentCandidate : QColor(0, 0, 0, 0);
    QColor opaqueCandidate(snapshot.broadcastOpaqueBgColor);
    m_opaqueBgColor = opaqueCandidate.isValid() ? opaqueCandidate : QColor(255, 255, 255, 255);

    resize(snapshot.broadcastWindowWidth, snapshot.broadcastWindowHeight);
    // Phase 2: 위치 변경 시 회전/비회전 전환 (pixmap↔text)
    if (m_viewerCountHasData) {
        renderViewerCountLabel();
    }
    repositionViewerCountOverlay();

    if (m_listView) {
        emit m_listView->model()->layoutChanged();
        m_listView->viewport()->update();
    }
    update();
}
