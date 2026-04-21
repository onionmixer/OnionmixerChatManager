#include "MainBroadcastWindow.h"

#include "core/EmojiImageCache.h"
#include "shared/BroadChatLogging.h"
#include "ui/BroadcastChatWindow.h"
#include "ui/ChatMessageModel.h"
#include "ui/OverlayLayout.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QLabel>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QResizeEvent>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>

MainBroadcastWindow::MainBroadcastWindow(QObject* parent)
    : QObject(parent)
    , m_model(new ChatMessageModel(this))
    , m_network(new QNetworkAccessManager(this))
    , m_cache(new EmojiImageCache(m_network, this))
{
    // PLAN §v5-11 · 이모지 검토 R2-A: 클라는 서버의 `request_emoji`/`emoji_image`
    // 경유로만 이모지 수신. 그러나 shared lib의 `EmojiImageCache::ensureLoaded`는
    // paint 경로에서 QNAM으로 직접 HTTP fetch를 시도할 수 있음 (LAN-only 운영·
    // auth_token 보호 환경에서 외부 CDN 직접 접속은 설계 위반 + 네트워크 중복).
    //
    // 해결: 이 프로세스의 QNAM 인스턴스만 `NotAccessible`로 설정해 내부 HTTP 시도 시
    // 즉시 `QNetworkReply::finished`(error) 경로로 귀환 → `m_pending` 엔트리만 정리.
    // 외부 트래픽 0. 공유 lib 코드는 변경하지 않음 (메인 앱의 내장 `BroadcastChatWindow`
    // QNAM과는 독립 인스턴스이므로 메인 앱 이모지 렌더에 영향 없음).
    //
    // 주: Qt 5.15에서 `setNetworkAccessible`는 공식 deprecated지만 Qt 6까지 기능 유지.
    // Qt 6 마이그레이션 시점에 대체 API (QNetworkInformation 또는 custom QNAM subclass)
    // 로 교체 필요. v54-22 `-Werror` Release 정책 호환 위해 명시적 suppress.
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    m_network->setNetworkAccessible(QNetworkAccessManager::NotAccessible);
    QT_WARNING_POP

    m_window = std::make_unique<BroadcastChatWindow>(m_model, m_cache, nullptr);
    m_window->setWindowTitle(QStringLiteral("OnionmixerBroadChatClient"));

    // v78 옵션 A: ctor 에서 `setTransparentMode(false)` 호출 **제거**.
    // v60 (구동 시 불투명 모드) 요구는 유지하되, X11 ARGB visual 확보를 위해
    // **첫 native window 는 반드시 투명 모드 (ctor 기본: WA_TBG=true + Frameless) 로 생성**
    // 되어야 한다. ctor 에서 바로 `setTransparentMode(false)` 를 호출하면 첫 native window
    // 가 WA_TBG=false 상태에서 만들어져 non-ARGB visual 이 되고, 이후 사용자 더블클릭
    // 으로 투명 전환 시 X11 이 ARGB visual 을 재선택하지 못해 배경 처리가 깨졌던 v63 이슈
    // 의 진짜 원인. 첫 show 는 `MainBroadcastWindow::show()` 에서 수행되며, 그 직후
    // 같은 함수 안에서 setTransparentMode(false) 로 불투명 모드 전환 (ARGB visual 유지).
    // PLAN §8.1 v1 우클릭 메뉴 — BroadcastChatWindow에 eventFilter 설치
    m_window->installEventFilter(this);
    // Step 7: geometry signal forward — 소유자가 persist.
    connect(m_window.get(), &BroadcastChatWindow::windowResized,
            this, &MainBroadcastWindow::windowResized);
    connect(m_window.get(), &BroadcastChatWindow::windowMoved,
            this, &MainBroadcastWindow::windowMoved);

    // v70 #15: tray 아이콘 — 창을 뒤로 숨겨도 복구 가능. 3 OS 단일 코드.
    setupTrayIcon();
    // v72 #11·#12·#8: chatWindow() 위에 얹는 overlay 위젯들 — 공유 lib 무수정.
    setupOverlays();
}

void MainBroadcastWindow::setupOverlays()
{
    if (!m_window) return;
    QWidget* host = m_window.get();

    // #11: 연결 상태 원형 배지.
    // 사용자 요청에 따라 화면 배지는 비활성화. 연결 상태는 tray tooltip/log 로 확인한다.
    // m_statusBadge = new QLabel(host);
    // m_statusBadge->setFixedSize(12, 12);
    // m_statusBadge->setToolTip(tr("연결 상태"));
    // refreshBadgeStyle();
    // m_statusBadge->raise();
    // m_statusBadge->show();

    // #12: viewer count "—" 플레이스홀더. 5초 타이머로 연결 후에도 viewer 미수신 상태 감지.
    m_viewerPlaceholder = new QLabel(QStringLiteral("—"), host);
    m_viewerPlaceholder->setStyleSheet(QStringLiteral(
        "QLabel { color: rgba(255,255,255,200); "
        "background: rgba(0,0,0,120); "
        "padding: 2px 8px; border-radius: 6px; font-weight: bold; }"));
    m_viewerPlaceholder->setVisible(true);  // 시작 시 미연결이므로 보임
    m_viewerPlaceholder->raise();

    m_viewerTimer = new QTimer(this);
    m_viewerTimer->setInterval(5000);
    connect(m_viewerTimer, &QTimer::timeout,
            this, &MainBroadcastWindow::onViewerPlaceholderTick);
    m_viewerTimer->start();

    // #8: in-memory 배지. default 숨김, setInMemoryMode(true) 호출 시 상시 표시.
    m_inMemoryBadge = new QLabel(QStringLiteral("⚠"), host);
    m_inMemoryBadge->setToolTip(tr("설정 저장 불가 — 종료 시 변경 사항 유실"));
    m_inMemoryBadge->setStyleSheet(QStringLiteral(
        "QLabel { color: #FFCC00; background: rgba(0,0,0,160); "
        "padding: 0px 4px; border-radius: 4px; font-weight: bold; }"));
    m_inMemoryBadge->setVisible(false);
    m_inMemoryBadge->raise();

    // host 크기 변경 감지 → overlay 재배치. installEventFilter 는 context menu 용도로
    // 이미 설치돼 있으므로 resize event 를 함께 처리하도록 기존 eventFilter 확장 대신
    // 주기적 재배치로 단순화 (resize 빈도 낮음).
    repositionOverlays();
}

void MainBroadcastWindow::repositionOverlays()
{
    if (!m_window) return;
    const QSize host = m_window->size();
    const int margin = 6;

    // v73 #14: viewer_count_position 반대 모서리로 배지 배치.
    if (m_statusBadge) {
        const auto corner = OverlayLayout::badgeCornerFor(m_currentViewerPos);
        m_statusBadge->move(OverlayLayout::cornerToPixel(
            corner, m_statusBadge->size(), host, margin));
        m_statusBadge->raise();
    }

    // v73 #14: viewer 자리에 플레이스홀더 배치 (Center*/Hidden 은 숨김).
    if (m_viewerPlaceholder) {
        const auto place =
            OverlayLayout::placeholderPlacementFor(m_currentViewerPos);
        if (!place.visible) {
            m_viewerPlaceholder->setVisible(false);
        } else {
            const QSize phSize =
                QSize(m_viewerPlaceholder->sizeHint().width(),
                      m_viewerPlaceholder->sizeHint().height());
            m_viewerPlaceholder->resize(phSize);
            m_viewerPlaceholder->move(OverlayLayout::cornerToPixel(
                place.corner, phSize, host, margin));
            m_viewerPlaceholder->raise();
        }
    }

    // in-memory ⚠ 배지 — 배지/플레이스홀더와 무관하게 좌상단 고정.
    // 배지가 좌상단에 있을 경우 in-memory 배지를 살짝 아래로 이동해 충돌 회피.
    if (m_inMemoryBadge) {
        const auto badgeCorner = OverlayLayout::badgeCornerFor(m_currentViewerPos);
        int y = margin;
        if (badgeCorner == OverlayLayout::Corner::TopLeft) {
            y = margin + (m_statusBadge ? m_statusBadge->height() + 4 : 16);
        }
        m_inMemoryBadge->move(margin, y);
        m_inMemoryBadge->raise();
    }
}

void MainBroadcastWindow::refreshBadgeStyle()
{
    if (!m_statusBadge) return;
    QString color;
    switch (m_connState) {
    case ConnectionState::Connecting: color = QStringLiteral("#FFCC00"); break;
    case ConnectionState::Connected:  color = QStringLiteral("#33CC66"); break;
    case ConnectionState::Error:      color = QStringLiteral("#E74C3C"); break;
    case ConnectionState::Terminated: color = QStringLiteral("#990000"); break;
    }
    m_statusBadge->setStyleSheet(QStringLiteral(
        "QLabel { background: %1; border-radius: 6px; border: 1px solid rgba(0,0,0,120); }")
        .arg(color));
    // 배지는 mode 변경 시 다시 보이도록 — 이전 hide 상태 복구.
    m_statusBadge->setVisible(true);
}

void MainBroadcastWindow::setConnectionState(ConnectionState state)
{
    m_connState = state;
    refreshBadgeStyle();
    if (state == ConnectionState::Connected) {
        // §8.3 v2-17: Connected 로 진입 시 5초 후 배지 숨김.
        // v74: QGraphicsOpacityEffect 기반 fade 대신 단순 hide (투명 모드 paint 간섭 회피).
        QTimer::singleShot(5000, this, [this]() {
            if (m_connState == ConnectionState::Connected && m_statusBadge) {
                m_statusBadge->setVisible(false);
            }
        });
    }
}

void MainBroadcastWindow::setInMemoryMode(bool inMemory)
{
    m_inMemory = inMemory;
    if (m_inMemoryBadge) {
        m_inMemoryBadge->setVisible(inMemory);
        m_inMemoryBadge->raise();
    }
}

void MainBroadcastWindow::setViewerCountPosition(const QString& pos)
{
    // v73 #14: 유효값만 수용 (9종 enum). 빈 값·잘못된 값은 무시.
    if (pos.isEmpty() || pos == m_currentViewerPos) return;
    m_currentViewerPos = pos;
    repositionOverlays();
    // 플레이스홀더는 placement 에 따라 재평가 — 이미 viewers 수신된 상태면 숨김 유지.
    onViewerPlaceholderTick();
}

void MainBroadcastWindow::onViewerPlaceholderTick()
{
    if (!m_viewerPlaceholder) return;
    // v73 #14: Center*·Hidden 위치에서는 플레이스홀더 자체를 숨김 (회전/비활성 케이스).
    const auto place =
        OverlayLayout::placeholderPlacementFor(m_currentViewerPos);
    // viewer 가 한 번이라도 수신됐으면 플레이스홀더 숨김 유지.
    // 연결 끊김 + 5초 경과 시 다시 표시. 단, placement 상 숨김이면 무조건 숨김.
    const bool wouldShow =
        !m_viewersReceived || m_connState != ConnectionState::Connected;
    const bool shouldShow = wouldShow && place.visible;
    m_viewerPlaceholder->setVisible(shouldShow);
    if (shouldShow) m_viewerPlaceholder->raise();
}

void MainBroadcastWindow::setupTrayIcon()
{
    // §8.1: `QSystemTrayIcon::isSystemTrayAvailable()` 미지원 시 (Linux GNOME w/o extension 등)
    // tray 생성 자체를 생략 — 우클릭 메뉴는 여전히 동작하므로 기능 손실 없음.
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qCInfo(lcBroadChat)
            << "system tray unavailable — tray icon disabled (graceful fallback)";
        return;
    }

    // v71: 전용 tray 아이콘 — PNG 다중 해상도 embed (.qrc `/icons/tray_*.png`).
    // Qt `QIcon` 은 addFile 로 여러 size 등록 시 시스템이 요청하는 해상도에 가장 가까운 PNG 자동 선택.
    // 3 OS 동일 경로. 전용 `.ico`/`.icns` 는 exe/bundle packaging 시점에 별도 링크됨.
    QIcon icon;
    for (const int sz : {16, 24, 32, 48, 64, 128, 256}) {
        icon.addFile(QStringLiteral(":/icons/tray_%1.png").arg(sz),
                     QSize(sz, sz));
    }
    // 빈 QIcon fallback (리소스 로드 실패 시) — QStyle 기본값
    if (icon.isNull()) {
        icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
    }
    m_tray = new QSystemTrayIcon(icon, this);
    // 창 아이콘도 동일하게 — taskbar/dock 표시용
    if (m_window) m_window->setWindowIcon(icon);

    // 메뉴 — 우클릭 context menu 와 동일한 "세팅"/"종료". 사용자가 tray 우클릭 시 접근.
    auto* menu = new QMenu();  // tray 소유 — destroy 시 함께 제거
    QAction* settingsAction = menu->addAction(tr("세팅"));
    QAction* quitAction = menu->addAction(tr("종료"));
    connect(settingsAction, &QAction::triggered,
            this, &MainBroadcastWindow::settingsRequested);
    connect(quitAction, &QAction::triggered,
            this, &MainBroadcastWindow::quitRequested);
    m_tray->setContextMenu(menu);

    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        onTrayActivated(static_cast<int>(r));
    });

    updateTrayStatus(tr("연결 대기 중"));
    m_tray->show();
}

bool MainBroadcastWindow::hasTrayIcon() const
{
    return m_tray != nullptr;
}

void MainBroadcastWindow::updateTrayStatus(const QString& statusText)
{
    m_statusText = statusText;
    if (!m_tray) return;
    // tooltip 형식: "OnionmixerBroadChatClient [instance] — 상태"
    QString base = QStringLiteral("OnionmixerBroadChatClient");
    if (!m_instanceId.isEmpty()) {
        base += QStringLiteral(" [%1]").arg(m_instanceId);
    }
    m_tray->setToolTip(statusText.isEmpty()
                           ? base
                           : base + QStringLiteral(" — ") + statusText);
}

void MainBroadcastWindow::onTrayActivated(int reason)
{
    // Trigger (Linux/Windows 단일 좌클릭) · DoubleClick (Windows 더블클릭) → 창 토글.
    // macOS 는 left-click 에 메뉴 표시가 표준이라 Trigger 이벤트 대신 Context 만 사용 — Qt 가 처리.
    const auto r = static_cast<QSystemTrayIcon::ActivationReason>(reason);
    if (r != QSystemTrayIcon::Trigger && r != QSystemTrayIcon::DoubleClick) return;
    if (!m_window) return;
    if (m_window->isVisible()) {
        m_window->hide();
    } else {
        m_window->show();
        m_window->raise();
        m_window->activateWindow();
    }
}

void MainBroadcastWindow::setInstanceId(const QString& instanceId)
{
    if (!m_window) return;
    m_instanceId = instanceId.trimmed();
    if (m_instanceId.isEmpty()) {
        m_window->setWindowTitle(QStringLiteral("OnionmixerBroadChatClient"));
    } else {
        m_window->setWindowTitle(
            QStringLiteral("OnionmixerBroadChatClient [%1]").arg(m_instanceId));
    }
    // v70 #15: tray tooltip 재구성
    updateTrayStatus(m_statusText);
}

void MainBroadcastWindow::applyInitialGeometry(int x, int y, int width, int height)
{
    if (!m_window) return;

    // 크기 복원 — 범위 검증 (§5.6.8 validation)
    if (width >= 100 && width <= 4000 && height >= 100 && height <= 4000) {
        m_window->resize(width, height);
    }
    // 위치 복원 — §v48-5 멀티 모니터 clamp: 좌표가 어떤 스크린에도 포함되지 않으면
    // primary screen 중앙으로 이동 (show() 이후 Qt가 자동 clamp하지만 보수적 방어).
    // 여기서는 유효 범위 체크만 — clamp는 Qt가 처리.
    if (x > -10000 && y > -10000 && x < 10000 && y < 10000) {
        m_window->move(x, y);
    }
}

bool MainBroadcastWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_window.get()) {
        if (event->type() == QEvent::ContextMenu) {
            auto* ctx = static_cast<QContextMenuEvent*>(event);
            QMenu menu(m_window.get());
            m_window->syncAlwaysOnTopState();
            QAction* actAlwaysOnTop = menu.addAction(tr("Always on Top"));
            actAlwaysOnTop->setCheckable(true);
            actAlwaysOnTop->setChecked(m_window->isAlwaysOnTop());
            menu.addSeparator();
            QAction* actSettings = menu.addAction(tr("세팅"));
            QAction* actQuit = menu.addAction(tr("종료"));
            QAction* chosen = menu.exec(ctx->globalPos());
            if (chosen == actAlwaysOnTop) {
                m_window->setAlwaysOnTop(actAlwaysOnTop->isChecked());
            } else if (chosen == actSettings) {
                emit settingsRequested();
            } else if (chosen == actQuit) {
                emit quitRequested();
            }
            return true;
        }
        // v72 #11·#12: 창 크기 변경 시 overlay 재배치.
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
            repositionOverlays();
        }
    }
    return QObject::eventFilter(watched, event);
}

MainBroadcastWindow::~MainBroadcastWindow() = default;

void MainBroadcastWindow::show()
{
    if (!m_window) return;
    // v78 옵션 A: 첫 호출 시 ctor 기본 (투명 + Frameless + WA_TBG=true) 로 show →
    // X11 이 ARGB visual 선택 → native window 가 ARGB 로 확보된 뒤 즉시 불투명 전환.
    // 사용자 눈에는 1 frame 내 처리되어 투명 상태가 체감되지 않음. 이후 double-click 으로
    // 투명 모드 재진입 시 ARGB visual 이 이미 확보돼 있어 배경 alpha 정상 반영.
    const bool firstShow = !m_window->testAttribute(Qt::WA_WState_Created);
    m_window->show();
    if (firstShow) {
        // native window 생성 (QXcbWindow::create) 이 show 중에 동기 실행되나, X 서버
        // 측 윈도우 매핑은 이벤트 루프 한 틱 이후 완료될 수 있어 processEvents 로
        // ARGB visual 확보 시점을 명확히 한 뒤 불투명 모드 전환.
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        m_window->setTransparentMode(false);
    }
}

void MainBroadcastWindow::close()
{
    if (m_window) m_window->close();
}

void MainBroadcastWindow::setViewerCount(int youtube, int chzzk)
{
    if (m_window) m_window->updateViewerCount(youtube, chzzk);
    // v72 #12: 실제 viewer 값 수신됨 — -1 sentinel 이 아닌 경우만.
    if (youtube >= 0 || chzzk >= 0) {
        m_viewersReceived = true;
        if (m_viewerPlaceholder) m_viewerPlaceholder->setVisible(false);
    }
}

void MainBroadcastWindow::appendChat(const UnifiedChatMessage& message)
{
    // Pre-register emoji metadata so the chat bubble delegate can resolve ids.
    // Actual bytes arrive through request_emoji / emoji_image in a later step.
    for (const ChatEmojiInfo& e : message.emojis) {
        if (!e.emojiId.isEmpty() && !e.imageUrl.isEmpty()) {
            m_cache->registerUrl(e.emojiId, e.imageUrl);
        }
    }
    m_model->appendMessage(message);
}
