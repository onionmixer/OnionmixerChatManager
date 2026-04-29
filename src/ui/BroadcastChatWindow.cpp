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
#include <QShowEvent>
#include <QStringList>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

#if ONIONMIXERCHATMANAGER_HAS_QT_X11_EXTRAS
#  include <QX11Info>
#  include <cstring>
#  include <X11/Xatom.h>
#  include <X11/Xlib.h>
#endif

BroadcastChatWindow::BroadcastChatWindow(ChatMessageModel* model, EmojiImageCache* emojiCache, QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(tr("Broadcast Chat"));

    setWindowFlags(windowFlagsForCurrentMode());
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
    // SourceOver 기본 모드는 알파 합성을 누적시켜 이전 프레임 잔상이 남을 수 있다.
    // 배경 채우기 한 회는 Source 로 강제해 매 프레임 기존 픽셀을 완전히 대체한다.
    // 자식 위젯(QListView 등)은 자체 페인터에서 SourceOver 로 진행되므로 영향 없음.
    painter.setCompositionMode(QPainter::CompositionMode_Source);
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

void BroadcastChatWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    applyNativeAlwaysOnTopState();
}

Qt::WindowFlags BroadcastChatWindow::windowFlagsForCurrentMode() const
{
    Qt::WindowFlags flags;
    if (m_transparentMode) {
        flags = Qt::FramelessWindowHint | Qt::Tool;
    } else {
#ifdef Q_OS_WIN
        // Qt 5.15 Windows platform plugin 은 `Qt::Window` 단독으로는 WS_EX_LAYERED 를
        // 부여하지 않아 `WA_TranslucentBackground` 가 무효화된다. FramelessWindowHint 를
        // 강제해 알파 합성을 활성화. 네이티브 타이틀바는 사라지지만 더블클릭 모드 토글,
        // 우클릭 메뉴, 트레이 아이콘으로 조작 가능.
        flags = Qt::Window | Qt::FramelessWindowHint;
#else
        // Linux/macOS: 네이티브 프레임 유지. X11 ARGB visual 잔존성으로 알파 정상 동작.
        flags = Qt::Window;
#endif
    }
    if (m_alwaysOnTop) {
        flags |= Qt::WindowStaysOnTopHint;
    }
    return flags;
}

void BroadcastChatWindow::applyWindowFlagsPreservingGeometry(bool forceShow)
{
    const bool wasVisible = isVisible();
    const QSize savedSize = size();
    const QPoint savedPos = m_lastUserPosition;

    m_draggedDuringSuppress = false;
    m_suppressMoveEvent = true;

    setWindowFlags(windowFlagsForCurrentMode());
    // 방송창은 투명/불투명 모드 모두 스크롤바 비표시 유지 (휠 스크롤은 정상 동작)
    // Use setGeometry (not move) in both modes: Qt5/X11 retains stale frame margin
    // cache across setWindowFlags, which makes move() place the native window offset
    // by the old title-bar height. setGeometry sets crect directly, bypassing it.
    setGeometry(savedPos.x(), savedPos.y(), savedSize.width(), savedSize.height());

    if (forceShow || wasVisible) {
        show();
        if (m_alwaysOnTop) {
            raise();
        }
        applyNativeAlwaysOnTopState();
        QTimer::singleShot(0, this, [this]() {
            applyNativeAlwaysOnTopState();
        });
    }
    update();

    QTimer::singleShot(50, this, [this, savedPos, savedSize]() {
        if (m_transparentMode && !m_draggedDuringSuppress) {
            setGeometry(savedPos.x(), savedPos.y(), savedSize.width(), savedSize.height());
        }
        m_suppressMoveEvent = false;
        m_draggedDuringSuppress = false;
    });
}

void BroadcastChatWindow::setTransparentMode(bool transparent)
{
    m_transparentMode = transparent;

    // 옵션 3: 모드와 무관하게 `WA_TranslucentBackground` 를 항상 true 로 유지한다.
    // - Windows: `WS_EX_LAYERED` 가 항상 on → 불투명 모드에서도 알파 < 255 색상이
    //   DWM 합성을 통해 데스크탑에 비친다.
    // - X11: ARGB visual 이 첫 표시 시 한 번 확정되며 이후 모드 전환과 무관하게 유지.
    // 모드 전환은 창 장식(Frameless+Tool ↔ Window) 만 결정한다.
    applyWindowFlagsPreservingGeometry(true);
}

void BroadcastChatWindow::setAlwaysOnTop(bool enabled)
{
    if (m_alwaysOnTop == enabled) {
        return;
    }
    m_alwaysOnTop = enabled;
    // 옵션 3: `WA_TranslucentBackground` 는 생성자에서 true 로 고정. 모드 전환에 따라
    // 토글하지 않으므로 이 함수에서도 재설정하지 않는다.

    // On X11, a visible window can update _NET_WM_STATE_ABOVE without
    // rebuilding native window flags. This matches GNOME's title-bar menu path
    // and avoids frame/client geometry drift from setWindowFlags().
    if (applyNativeAlwaysOnTopState()) {
        if (m_alwaysOnTop) {
            raise();
        }
        return;
    }

    applyWindowFlagsPreservingGeometry(false);
}

void BroadcastChatWindow::syncAlwaysOnTopState()
{
    bool enabled = false;
    if (queryNativeAlwaysOnTopState(&enabled)) {
        m_alwaysOnTop = enabled;
    }
}

bool BroadcastChatWindow::applyNativeAlwaysOnTopState()
{
#if ONIONMIXERCHATMANAGER_HAS_QT_X11_EXTRAS
    if (!isVisible() || !QX11Info::isPlatformX11()) {
        return false;
    }

    Display* display = QX11Info::display();
    if (!display) {
        return false;
    }

    const WId wid = winId();
    if (!wid) {
        return false;
    }

    const Atom wmState = XInternAtom(display, "_NET_WM_STATE", False);
    const Atom stateAbove = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
    if (wmState == None || stateAbove == None) {
        return false;
    }

    XEvent event;
    std::memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.window = static_cast<Window>(wid);
    event.xclient.message_type = wmState;
    event.xclient.format = 32;
    event.xclient.data.l[0] = m_alwaysOnTop ? 1 : 0;  // _NET_WM_STATE_ADD / REMOVE
    event.xclient.data.l[1] = static_cast<long>(stateAbove);
    event.xclient.data.l[2] = 0;
    event.xclient.data.l[3] = 1;  // normal application source indication
    event.xclient.data.l[4] = 0;

    XSendEvent(display, QX11Info::appRootWindow(), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(display);
    return true;
#else
    return false;
#endif
}

bool BroadcastChatWindow::queryNativeAlwaysOnTopState(bool* enabled) const
{
#if ONIONMIXERCHATMANAGER_HAS_QT_X11_EXTRAS
    if (!enabled || !isVisible() || !QX11Info::isPlatformX11()) {
        return false;
    }

    Display* display = QX11Info::display();
    if (!display) {
        return false;
    }

    const WId wid = winId();
    if (!wid) {
        return false;
    }

    const Atom wmState = XInternAtom(display, "_NET_WM_STATE", False);
    const Atom stateAbove = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
    if (wmState == None || stateAbove == None) {
        return false;
    }

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char* data = nullptr;

    const int status = XGetWindowProperty(
        display,
        static_cast<Window>(wid),
        wmState,
        0,
        1024,
        False,
        XA_ATOM,
        &actualType,
        &actualFormat,
        &itemCount,
        &bytesAfter,
        &data);

    if (status != Success || actualType != XA_ATOM || actualFormat != 32 || !data) {
        if (data) {
            XFree(data);
        }
        return false;
    }

    bool hasAbove = false;
    const auto* atoms = reinterpret_cast<const Atom*>(data);
    for (unsigned long i = 0; i < itemCount; ++i) {
        if (atoms[i] == stateAbove) {
            hasAbove = true;
            break;
        }
    }
    XFree(data);

    *enabled = hasAbove;
    return true;
#else
    Q_UNUSED(enabled);
    return false;
#endif
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
