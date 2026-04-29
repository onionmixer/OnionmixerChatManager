#include "ClientConfigDialog.h"

#include "core/PlatformTraits.h"
#include "ui/ChatBubbleDelegate.h"
#include "ui/ChatMessageModel.h"
#include "ui/ViewerCountStyle.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {
// 9종 뷰어 카운트 위치 enum 문자열 (§5.6.8 validation 표)
const QStringList kViewerPositions = {
    QStringLiteral("TopLeft"),     QStringLiteral("TopCenter"),
    QStringLiteral("TopRight"),    QStringLiteral("CenterLeft"),
    QStringLiteral("CenterRight"), QStringLiteral("BottomLeft"),
    QStringLiteral("BottomCenter"), QStringLiteral("BottomRight"),
    QStringLiteral("Hidden"),
};

} // namespace

ClientConfigDialog::ClientConfigDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("BroadChat Client Settings"));
    setModal(true);
    // v62: 다이얼로그가 닫힐 때 app quit 트리거되지 않도록 명시. parent=nullptr
    // 시나리오 방어 — `quitOnLastWindowClosed`와 독립.
    setAttribute(Qt::WA_QuitOnClose, false);
    // v68 #17 (§v51): 사용자가 창을 너무 작게 줄여 위젯이 잘리는 것 방지.
    setMinimumSize(560, 440);
    resize(560, 440);

    m_tabs = new QTabWidget(this);
    auto* tabs = m_tabs;  // 지역 alias — 기존 build* 메서드 수정 최소화

    // Connection tab
    auto* connTab = new QWidget(this);
    m_edtHost = new QLineEdit(connTab);
    m_edtHost->setPlaceholderText(QStringLiteral("127.0.0.1"));
    m_edtHost->setToolTip(tr("메인 앱 서버 주소 (IP 또는 호스트명)"));

    m_spnPort = new QSpinBox(connTab);
    m_spnPort->setRange(1024, 65535);
    m_spnPort->setValue(47123);
    m_spnPort->setToolTip(tr("메인 앱 서버 TCP 포트"));

    m_edtAuthToken = new QLineEdit(connTab);
    m_edtAuthToken->setEchoMode(QLineEdit::Password);
    m_edtAuthToken->setPlaceholderText(tr("(empty = no authentication)"));
    m_edtAuthToken->setToolTip(tr("메인 앱에서 생성한 토큰. 빈 값이면 인증 안 함"));

    m_chkShow = new QCheckBox(tr("Show"), connTab);
    m_btnPaste = new QPushButton(tr("Paste"), connTab);
    m_btnClear = new QPushButton(tr("Clear"), connTab);

    auto* tokenRow = new QWidget(connTab);
    auto* tokenLayout = new QHBoxLayout(tokenRow);
    tokenLayout->setContentsMargins(0, 0, 0, 0);
    tokenLayout->setSpacing(4);
    tokenLayout->addWidget(m_edtAuthToken, 1);
    tokenLayout->addWidget(m_chkShow);
    tokenLayout->addWidget(m_btnPaste);
    tokenLayout->addWidget(m_btnClear);

    auto* connForm = new QFormLayout(connTab);
    connForm->addRow(tr("Host"), m_edtHost);
    connForm->addRow(tr("Port"), m_spnPort);
    connForm->addRow(tr("Auth Token"), tokenRow);

    tabs->addTab(connTab, tr("Connection"));

    // Chat tab — 폰트·크기·bold·italic·line spacing·max messages
    auto* chatTab = new QWidget(this);
    m_fontFamily = new QFontComboBox(chatTab);
    m_fontFamily->setToolTip(tr("채팅 본문 폰트. 빈 값(system default)은 상단 'System' 선택으로는 반영 안 됨 — 명시 선택 권장"));

    m_fontSize = new QSpinBox(chatTab);
    m_fontSize->setRange(6, 48);
    m_fontSize->setValue(11);

    m_fontBold = new QCheckBox(tr("Bold"), chatTab);
    m_fontItalic = new QCheckBox(tr("Italic"), chatTab);

    m_lineSpacing = new QSpinBox(chatTab);
    m_lineSpacing->setRange(0, 20);
    m_lineSpacing->setValue(3);
    m_lineSpacing->setSuffix(QStringLiteral(" px"));

    m_maxMessages = new QSpinBox(chatTab);
    m_maxMessages->setRange(100, 100000);
    m_maxMessages->setSingleStep(100);
    m_maxMessages->setValue(5000);
    m_maxMessages->setToolTip(tr("모델에 보관할 최근 메시지 상한. 초과 시 가장 오래된 메시지 제거"));

    auto* styleRow = new QWidget(chatTab);
    auto* styleLayout = new QHBoxLayout(styleRow);
    styleLayout->setContentsMargins(0, 0, 0, 0);
    styleLayout->addWidget(m_fontBold);
    styleLayout->addWidget(m_fontItalic);
    styleLayout->addStretch(1);

    auto* chatForm = new QFormLayout(chatTab);
    chatForm->addRow(tr("Font Family"), m_fontFamily);
    chatForm->addRow(tr("Font Size"), m_fontSize);
    chatForm->addRow(tr("Style"), styleRow);
    chatForm->addRow(tr("Line Spacing"), m_lineSpacing);
    chatForm->addRow(tr("Max Messages"), m_maxMessages);

    tabs->addTab(chatTab, tr("Chat"));

    // Broadcast tab — viewer 위치 · 4색상
    auto* bcTab = new QWidget(this);
    m_viewerPos = new QComboBox(bcTab);
    m_viewerPos->addItems(kViewerPositions);
    m_viewerPos->setToolTip(tr("뷰어 카운트 오버레이 위치. Hidden이면 표시 안 함"));

    // v75·v76: 메인 앱 `applyColorButtonStyle` 패턴 — 버튼 자체가 색 패드 + hex 표시.
    // v76 에서 클라 고유 "투명" quick preset 제거 — QColorDialog alpha 로 선택 (메인 앱 동등).
    m_btnBgTransparent = new QPushButton(bcTab);
    m_btnBgTransparent->setToolTip(tr("투명 모드 배경색 (#AARRGGBB). 기본 완전 투명"));
    connect(m_btnBgTransparent, &QPushButton::clicked, this, [this]() {
        pickBroadcastColor(m_btnBgTransparent, tr("Transparent Mode Background"));
    });

    m_btnBgOpaque = new QPushButton(bcTab);
    m_btnBgOpaque->setToolTip(tr("불투명 모드 배경색 (#AARRGGBB). 기본 흰색"));
    connect(m_btnBgOpaque, &QPushButton::clicked, this, [this]() {
        pickBroadcastColor(m_btnBgOpaque, tr("Opaque Mode Background"));
    });

    m_btnBodyColor = new QPushButton(bcTab);
    m_btnBodyColor->setToolTip(tr("채팅 본문 폰트 색 override. 빈 값 = 테마 기본"));
    connect(m_btnBodyColor, &QPushButton::clicked, this, [this]() {
        pickBroadcastColor(m_btnBodyColor, tr("Chat Body Font Color"));
    });
    auto* btnBodyClear = new QPushButton(tr("Clear"), bcTab);
    connect(btnBodyClear, &QPushButton::clicked, this,
            [this]() { clearBroadcastColor(m_btnBodyColor); });

    m_btnOutlineColor = new QPushButton(bcTab);
    m_btnOutlineColor->setToolTip(tr("채팅 텍스트 윤곽선 색. 빈 값 = 윤곽선 없음"));
    connect(m_btnOutlineColor, &QPushButton::clicked, this, [this]() {
        pickBroadcastColor(m_btnOutlineColor, tr("Chat Text Outline Color"));
    });
    auto* btnOutlineClear = new QPushButton(tr("Clear"), bcTab);
    connect(btnOutlineClear, &QPushButton::clicked, this,
            [this]() { clearBroadcastColor(m_btnOutlineColor); });

    // v66: 방송창 가로·세로 크기 명시 지정 (메인 앱 ConfigurationDialog 대칭)
    m_windowWidth = new QSpinBox(bcTab);
    m_windowWidth->setRange(100, 1900);
    m_windowWidth->setSuffix(QStringLiteral(" px"));
    m_windowWidth->setValue(400);
    m_windowWidth->setToolTip(tr("방송창 가로 크기. 드래그로 크기 변경 시 자동 갱신"));

    m_windowHeight = new QSpinBox(bcTab);
    m_windowHeight->setRange(150, 1000);
    m_windowHeight->setSuffix(QStringLiteral(" px"));
    m_windowHeight->setValue(600);
    m_windowHeight->setToolTip(tr("방송창 세로 크기. 드래그로 크기 변경 시 자동 갱신"));

    // v75: 색 버튼 + 보조 버튼 (quick preset / Clear) 을 한 행에 배치.
    auto makeColorRow = [](QPushButton* colorBtn, QPushButton* extra) {
        auto* row = new QWidget();
        auto* lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(4);
        lay->addWidget(colorBtn, 1);
        if (extra) lay->addWidget(extra);
        return row;
    };

    // v76: Broadcast 탭 레이아웃 — 상단 form (고정 높이) + 하단 preview wrap (stretch=1).
    auto* bcPageLayout = new QVBoxLayout(bcTab);
    bcPageLayout->setContentsMargins(0, 0, 0, 0);

    auto* bcFormContainer = new QWidget(bcTab);
    auto* bcForm = new QFormLayout(bcFormContainer);
    bcForm->addRow(tr("Window Width"), m_windowWidth);
    bcForm->addRow(tr("Window Height"), m_windowHeight);
    bcForm->addRow(tr("Viewer Count Position"), m_viewerPos);
    bcForm->addRow(tr("Transparent BG"), makeColorRow(m_btnBgTransparent, nullptr));
    bcForm->addRow(tr("Opaque BG"), makeColorRow(m_btnBgOpaque, nullptr));
    bcForm->addRow(tr("Body Font Color"), makeColorRow(m_btnBodyColor, btnBodyClear));
    bcForm->addRow(tr("Text Outline Color"), makeColorRow(m_btnOutlineColor, btnOutlineClear));
    bcPageLayout->addWidget(bcFormContainer, 0);

    // v76: 프리뷰 infrastructure — 오프스크린 renderer + delegate + listview + 샘플 모델.
    // 메인 앱 `ConfigurationDialog::createBroadcastTab` 과 동등.
    m_chatPreviewModel = new ChatMessageModel(this);
    m_broadcastPreviewRenderer = new QWidget(this);
    m_broadcastPreviewRenderer->hide();
    m_broadcastPreviewRenderer->setAttribute(Qt::WA_TranslucentBackground, true);
    m_broadcastPreviewRenderer->setAutoFillBackground(false);
    m_broadcastPreviewDelegate = new ChatBubbleDelegate(this);
    m_broadcastPreviewList = new QListView(m_broadcastPreviewRenderer);
    m_broadcastPreviewList->setModel(m_chatPreviewModel);
    m_broadcastPreviewList->setItemDelegate(m_broadcastPreviewDelegate);
    m_broadcastPreviewList->setSelectionMode(QAbstractItemView::NoSelection);
    m_broadcastPreviewList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_broadcastPreviewList->setUniformItemSizes(false);
    m_broadcastPreviewList->setFrameShape(QFrame::NoFrame);
    m_broadcastPreviewList->setStyleSheet(QStringLiteral("background: transparent;"));
    m_broadcastPreviewList->viewport()->setAutoFillBackground(false);
    auto* rendererLayout = new QVBoxLayout(m_broadcastPreviewRenderer);
    rendererLayout->setContentsMargins(0, 0, 0, 0);
    rendererLayout->addWidget(m_broadcastPreviewList);

    // 하단 preview wrap: 두 GroupBox 수평 배치
    auto* previewWrap = new QWidget(bcTab);
    auto* previewLayout = new QHBoxLayout(previewWrap);
    previewLayout->setContentsMargins(0, 0, 0, 0);

    m_transparentPreviewGroup = new QGroupBox(tr("Transparent Preview"), previewWrap);
    auto* transparentPreviewLayout = new QVBoxLayout(m_transparentPreviewGroup);
    m_lblBroadcastPreviewTransparent = new QLabel(m_transparentPreviewGroup);
    m_lblBroadcastPreviewTransparent->setAlignment(Qt::AlignCenter);
    m_lblBroadcastPreviewTransparent->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    transparentPreviewLayout->addWidget(m_lblBroadcastPreviewTransparent);

    m_opaquePreviewGroup = new QGroupBox(tr("Opaque Preview"), previewWrap);
    auto* opaquePreviewLayout = new QVBoxLayout(m_opaquePreviewGroup);
    m_lblBroadcastPreviewOpaque = new QLabel(m_opaquePreviewGroup);
    m_lblBroadcastPreviewOpaque->setAlignment(Qt::AlignCenter);
    m_lblBroadcastPreviewOpaque->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    opaquePreviewLayout->addWidget(m_lblBroadcastPreviewOpaque);

    previewLayout->addWidget(m_transparentPreviewGroup);
    previewLayout->addWidget(m_opaquePreviewGroup);
    bcPageLayout->addWidget(previewWrap, 1);

    // v76: debounce 타이머 (150ms single-shot) — resize/show/tab change 시 재생성.
    m_broadcastPreviewDebounce = new QTimer(this);
    m_broadcastPreviewDebounce->setSingleShot(true);
    m_broadcastPreviewDebounce->setInterval(150);
    connect(m_broadcastPreviewDebounce, &QTimer::timeout,
            this, &ClientConfigDialog::updateBroadcastPreview);

    // v76: 실시간 갱신 시그널 연결 — 모든 broadcast/chat 필드 변경 시 preview 재생성.
    connect(m_windowWidth, QOverload<int>::of(&QSpinBox::valueChanged),
            m_broadcastPreviewDebounce, QOverload<>::of(&QTimer::start));
    connect(m_windowHeight, QOverload<int>::of(&QSpinBox::valueChanged),
            m_broadcastPreviewDebounce, QOverload<>::of(&QTimer::start));
    connect(m_viewerPos, QOverload<int>::of(&QComboBox::currentIndexChanged),
            m_broadcastPreviewDebounce, QOverload<>::of(&QTimer::start));
    connect(m_fontFamily, &QFontComboBox::currentFontChanged,
            m_broadcastPreviewDebounce, QOverload<>::of(&QTimer::start));
    connect(m_fontSize, QOverload<int>::of(&QSpinBox::valueChanged),
            m_broadcastPreviewDebounce, QOverload<>::of(&QTimer::start));
    connect(m_fontBold, &QCheckBox::toggled,
            m_broadcastPreviewDebounce, QOverload<>::of(&QTimer::start));
    connect(m_fontItalic, &QCheckBox::toggled,
            m_broadcastPreviewDebounce, QOverload<>::of(&QTimer::start));
    connect(m_lineSpacing, QOverload<int>::of(&QSpinBox::valueChanged),
            m_broadcastPreviewDebounce, QOverload<>::of(&QTimer::start));

    tabs->addTab(bcTab, tr("Broadcast"));

    // v68 #16: Reset 버튼은 RestoreDefaultsRole — OK/Cancel 과 독립 슬롯에 연결.
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel
        | QDialogButtonBox::RestoreDefaults, this);

    // v72 #8: in-memory 모드 경고 라벨. 기본 숨김, 소유자가 setInMemoryMode(true) 호출 시 노출.
    m_inMemoryWarn = new QLabel(
        tr("⚠ 설정 저장 불가 — 변경 사항이 메모리에만 저장됨 (재시작 시 손실)"), this);
    m_inMemoryWarn->setStyleSheet(QStringLiteral(
        "QLabel { color: #CC6600; font-weight: bold; padding: 4px; "
        "background: rgba(255,204,0,40); border: 1px solid #CC6600; border-radius: 3px; }"));
    m_inMemoryWarn->setWordWrap(true);
    m_inMemoryWarn->setVisible(false);

    auto* root = new QVBoxLayout(this);
    root->addWidget(tabs, 1);
    root->addWidget(m_inMemoryWarn);
    root->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &ClientConfigDialog::onOkClicked);
    connect(buttons, &QDialogButtonBox::rejected, this, &ClientConfigDialog::onCancelClicked);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults),
            &QPushButton::clicked, this, &ClientConfigDialog::onResetToDefaults);
    connect(m_btnPaste, &QPushButton::clicked, this, &ClientConfigDialog::onPasteClicked);
    connect(m_btnClear, &QPushButton::clicked, this, &ClientConfigDialog::onClearTokenClicked);
    connect(m_chkShow, &QCheckBox::toggled, this, [this](bool shown) {
        m_edtAuthToken->setEchoMode(shown ? QLineEdit::Normal : QLineEdit::Password);
    });

    // v76: 탭 전환 시 preview 재생성 (Broadcast 탭 가시 시점 group contentsRect 가 확정됨).
    connect(m_tabs, &QTabWidget::currentChanged, this,
            [this](int) { if (m_broadcastPreviewDebounce) m_broadcastPreviewDebounce->start(); });

    // v76: body/outline 우클릭 = Clear (메인 앱 대칭 context menu).
    m_btnBodyColor->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_btnBodyColor, &QPushButton::customContextMenuRequested, this,
            [this]() { clearBroadcastColor(m_btnBodyColor); });
    m_btnOutlineColor->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_btnOutlineColor, &QPushButton::customContextMenuRequested, this,
            [this]() { clearBroadcastColor(m_btnOutlineColor); });
}

// v76: resize/show → preview debounce 재시작 (메인 앱 대칭).
void ClientConfigDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    if (m_broadcastPreviewDebounce) m_broadcastPreviewDebounce->start();
}

void ClientConfigDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (m_broadcastPreviewDebounce) m_broadcastPreviewDebounce->start();
}

ClientConfigDialog::~ClientConfigDialog() = default;

void ClientConfigDialog::setInMemoryMode(bool inMemory)
{
    if (m_inMemoryWarn) m_inMemoryWarn->setVisible(inMemory);
}

// v75: 메인 앱 `ConfigurationDialog::applyColorButtonStyle` 이식.
// 버튼 자체가 색 패드 역할 — background 에 rgba 지정 + hex 표시 + 가독성 위해
// lightness 에 따라 text color 자동 대비.
void ClientConfigDialog::applyColorButtonStyle(QPushButton* button, const QColor& color)
{
    if (!button) return;
    const QString hexArgb = color.name(QColor::HexArgb).toUpper();
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: rgba(%1,%2,%3,%4); "
        "border: 1px solid #888; min-width: 100px; min-height: 22px; color: %5; }")
        .arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha())
        .arg(color.lightness() > 128 ? QStringLiteral("#000000")
                                     : QStringLiteral("#FFFFFF")));
    button->setText(hexArgb);
    button->setProperty("colorValue", hexArgb);
}

// v75: body/outline 전용 "미설정" 스타일 — 점선 + 회색. 텍스트 "Not set".
void ClientConfigDialog::applyUnsetColorButtonStyle(QPushButton* button)
{
    if (!button) return;
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background: #F4F4F4; border: 1px dashed #888; "
        "min-width: 100px; min-height: 22px; color: #666; }"));
    button->setText(tr("Not set"));
    button->setProperty("colorValue", QString());
}

// 메인 앱 `ConfigurationDialog::pickBroadcastColor`와 동일 정책.
// Linux: KDE/GNOME 네이티브 색상 팔레트(알파 슬라이더 지원) 사용.
// Windows/macOS: 네이티브 ChooseColor / NSColorPanel은 알파 채널을 지원하지 않으므로
// `DontUseNativeDialog`로 Qt 자체 다이얼로그를 강제해 `#00000000` 등 투명값 선택 가능.
void ClientConfigDialog::pickBroadcastColor(QPushButton* button, const QString& title)
{
    if (!button) return;
    const QColor initial(button->property("colorValue").toString());
    QColorDialog::ColorDialogOptions opts = QColorDialog::ShowAlphaChannel;
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    opts |= QColorDialog::DontUseNativeDialog;
#endif
    const QColor chosen = QColorDialog::getColor(initial, this, title, opts);
    if (chosen.isValid()) {
        applyColorButtonStyle(button, chosen);
        if (m_broadcastPreviewDebounce) m_broadcastPreviewDebounce->start();
    }
}

void ClientConfigDialog::clearBroadcastColor(QPushButton* button)
{
    applyUnsetColorButtonStyle(button);
    if (m_broadcastPreviewDebounce) m_broadcastPreviewDebounce->start();
}

void ClientConfigDialog::setValues(const AppSettingsSnapshot& snapshot,
                                   const QString& host, quint16 port,
                                   const QString& authToken)
{
    m_baseline = snapshot;

    // Connection
    m_edtHost->setText(host);
    m_spnPort->setValue(port >= 1024 ? port : 47123);
    m_edtAuthToken->setText(authToken);

    // Chat
    if (!snapshot.chatFontFamily.isEmpty()) {
        m_fontFamily->setCurrentFont(QFont(snapshot.chatFontFamily));
    }
    m_fontSize->setValue(snapshot.chatFontSize);
    m_fontBold->setChecked(snapshot.chatFontBold);
    m_fontItalic->setChecked(snapshot.chatFontItalic);
    m_lineSpacing->setValue(snapshot.chatLineSpacing);
    m_maxMessages->setValue(snapshot.chatMaxMessages);

    // Broadcast
    const int posIdx = kViewerPositions.indexOf(snapshot.broadcastViewerCountPosition);
    m_viewerPos->setCurrentIndex(posIdx >= 0 ? posIdx : 0);
    // v75: 색상 버튼 styling — hex text + 색 bg. body/outline 은 빈 값이면 "Not set".
    const QColor tCol(snapshot.broadcastTransparentBgColor);
    applyColorButtonStyle(m_btnBgTransparent,
                          tCol.isValid() ? tCol : QColor(0, 0, 0, 0));
    const QColor oCol(snapshot.broadcastOpaqueBgColor);
    applyColorButtonStyle(m_btnBgOpaque,
                          oCol.isValid() ? oCol : QColor(255, 255, 255, 255));
    const QColor bodyCol(snapshot.broadcastChatBodyFontColor);
    if (bodyCol.isValid()) applyColorButtonStyle(m_btnBodyColor, bodyCol);
    else                   applyUnsetColorButtonStyle(m_btnBodyColor);
    const QColor outCol(snapshot.broadcastChatOutlineColor);
    if (outCol.isValid()) applyColorButtonStyle(m_btnOutlineColor, outCol);
    else                  applyUnsetColorButtonStyle(m_btnOutlineColor);

    // v66: 방송창 크기 (스핀박스 값으로는 유효 범위 내만 반영)
    const int w = snapshot.broadcastWindowWidth;
    const int h = snapshot.broadcastWindowHeight;
    m_windowWidth->setValue((w >= 100 && w <= 1900) ? w : 400);
    m_windowHeight->setValue((h >= 150 && h <= 1000) ? h : 600);
}

QString ClientConfigDialog::host() const
{
    const QString h = m_edtHost->text().trimmed();
    return h.isEmpty() ? QStringLiteral("127.0.0.1") : h;
}

quint16 ClientConfigDialog::port() const
{
    return static_cast<quint16>(m_spnPort->value());
}

QString ClientConfigDialog::authToken() const
{
    return m_edtAuthToken->text().trimmed();
}

AppSettingsSnapshot ClientConfigDialog::currentSnapshot() const
{
    AppSettingsSnapshot s = m_baseline;      // window geometry 등 non-edit 필드 유지
    s.chatFontFamily = m_fontFamily->currentFont().family();
    s.chatFontSize = m_fontSize->value();
    s.chatFontBold = m_fontBold->isChecked();
    s.chatFontItalic = m_fontItalic->isChecked();
    s.chatLineSpacing = m_lineSpacing->value();
    s.chatMaxMessages = m_maxMessages->value();

    s.broadcastViewerCountPosition = m_viewerPos->currentText();
    // v75: 색상 값은 버튼 `colorValue` property 에서 읽음. body/outline 은 "Not set" 시 빈 문자열.
    s.broadcastTransparentBgColor = m_btnBgTransparent->property("colorValue").toString();
    s.broadcastOpaqueBgColor = m_btnBgOpaque->property("colorValue").toString();
    s.broadcastChatBodyFontColor = m_btnBodyColor->property("colorValue").toString();
    s.broadcastChatOutlineColor = m_btnOutlineColor->property("colorValue").toString();
    s.broadcastWindowWidth = m_windowWidth->value();
    s.broadcastWindowHeight = m_windowHeight->value();
    return s;
}

void ClientConfigDialog::onOkClicked()
{
    emit valuesAccepted(currentSnapshot(), host(), port(), authToken());
    accept();
}

void ClientConfigDialog::onCancelClicked()
{
    reject();
}

void ClientConfigDialog::onPasteClicked()
{
    const QString clip = QApplication::clipboard()->text().trimmed();
    if (clip.isEmpty()) return;
    m_edtAuthToken->setText(clip);
}

void ClientConfigDialog::onClearTokenClicked()
{
    if (!m_edtAuthToken->text().trimmed().isEmpty()) {
        const auto r = QMessageBox::question(this, tr("Clear Token"),
            tr("인증 기능을 비활성화합니다. 계속하시겠습니까?"),
            QMessageBox::Yes | QMessageBox::No);
        if (r != QMessageBox::Yes) return;
    }
    m_edtAuthToken->clear();
}

void ClientConfigDialog::onResetToDefaults()
{
    // v68 #16: 확인 프롬프트 후 모든 위젯을 기본값 복원. connection 필드는 유지
    // (사용자가 호스트/포트/토큰까지 같이 초기화 원하는 경우는 드물며, 실수로 삭제 시
    // 복구 불가). Chat·Broadcast 스타일만 초기화 대상.
    const auto r = QMessageBox::question(this, tr("Reset to Defaults"),
        tr("Chat 및 Broadcast 설정을 기본값으로 복원합니다. 계속하시겠습니까?"),
        QMessageBox::Yes | QMessageBox::No);
    if (r != QMessageBox::Yes) return;

    m_fontFamily->setCurrentFont(QFont());
    m_fontSize->setValue(11);
    m_fontBold->setChecked(false);
    m_fontItalic->setChecked(false);
    m_lineSpacing->setValue(3);
    m_maxMessages->setValue(5000);

    m_viewerPos->setCurrentText(QStringLiteral("TopLeft"));
    // v75: 버튼 기반 색상 필드 기본값 복원.
    applyColorButtonStyle(m_btnBgTransparent, QColor(0, 0, 0, 0));
    applyColorButtonStyle(m_btnBgOpaque, QColor(255, 255, 255, 255));
    applyColorButtonStyle(m_btnBodyColor, QColor(255, 255, 255, 255));
    applyColorButtonStyle(m_btnOutlineColor, QColor(0, 0, 0, 255));
    m_windowWidth->setValue(400);
    m_windowHeight->setValue(600);
    if (m_broadcastPreviewDebounce) m_broadcastPreviewDebounce->start();
}

// v76: 투명/불투명 프리뷰 재생성 — 메인 앱 `ConfigurationDialog::updateBroadcastPreview` 동등.
// 체커보드 배경 → transparent_bg overlay → 실제 chat renderer → viewer count 오버레이
// (회전 포함) → 비율 유지 스케일링. 샘플 메시지 3건을 첫 호출 시 모델에 주입.
void ClientConfigDialog::updateBroadcastPreview()
{
    if (!m_broadcastPreviewRenderer || !m_broadcastPreviewDelegate
        || !m_lblBroadcastPreviewTransparent || !m_lblBroadcastPreviewOpaque
        || !m_chatPreviewModel) {
        return;
    }

    // 샘플 메시지 초기화 (1회만) — 메인 앱과 동일 내용.
    if (m_chatPreviewModel->messageCount() == 0) {
        UnifiedChatMessage m1;
        m1.platform = PlatformId::YouTube;
        m1.authorName = QStringLiteral("SampleUser");
        m1.text = QStringLiteral("Hello, this is a YouTube message!");
        m1.timestamp = QDateTime::fromString(
            QStringLiteral("2026-04-11 15:30:00"),
            QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        m_chatPreviewModel->appendMessage(m1);

        UnifiedChatMessage m2;
        m2.platform = PlatformId::Chzzk;
        m2.authorName = QString::fromUtf8(u8"치지직스트리머");
        m2.text = QString::fromUtf8(u8"치지직에서 보낸 메시지입니다.");
        m2.timestamp = QDateTime::fromString(
            QStringLiteral("2026-04-11 15:30:05"),
            QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        m_chatPreviewModel->appendMessage(m2);

        UnifiedChatMessage m3;
        m3.platform = PlatformId::YouTube;
        m3.authorName = QString::fromUtf8(u8"さくら");
        m3.text = QString::fromUtf8(u8"こんにちは!配信楽しみにしています。");
        m3.timestamp = QDateTime::fromString(
            QStringLiteral("2026-04-11 15:30:10"),
            QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        m_chatPreviewModel->appendMessage(m3);
    }

    // delegate 에 현재 font/body/outline 설정 주입.
    m_broadcastPreviewDelegate->setFontFamily(m_fontFamily->currentFont().family());
    m_broadcastPreviewDelegate->setFontSize(m_fontSize->value());
    m_broadcastPreviewDelegate->setFontBold(m_fontBold->isChecked());
    m_broadcastPreviewDelegate->setFontItalic(m_fontItalic->isChecked());
    m_broadcastPreviewDelegate->setLineSpacing(m_lineSpacing->value());

    auto parseColor = [](QPushButton* btn) -> QColor {
        if (!btn) return QColor();
        const QString s = btn->property("colorValue").toString().trimmed();
        if (s.isEmpty()) return QColor();
        const QColor c(s);
        return c.isValid() ? c : QColor();
    };
    m_broadcastPreviewDelegate->setBodyOverrideColor(parseColor(m_btnBodyColor));
    m_broadcastPreviewDelegate->setTextOutlineColor(parseColor(m_btnOutlineColor));

    const int w = m_windowWidth->value();
    const int h = m_windowHeight->value();
    m_broadcastPreviewRenderer->resize(w, h);
    emit m_broadcastPreviewList->model()->layoutChanged();

    // viewer count 오버레이 렌더 헬퍼 — ViewerCountStyle 로 플랫폼 색상 HTML 생성.
    const QVector<QPair<PlatformId, int>> previewEntries = {
        { PlatformId::YouTube, -1 },
        { PlatformId::Chzzk, -1 },
    };
    const QString viewerHtml = ViewerCountStyle::buildViewerHtml(previewEntries);
    const QString position = m_viewerPos->currentText();

    auto drawViewerOverlay = [&](QPainter& painter, int pw, int ph) {
        QFont overlayFont = QGuiApplication::font();
        overlayFont.setBold(true);

        QTextDocument doc;
        doc.setDefaultFont(overlayFont);
        doc.setDefaultStyleSheet(QStringLiteral("body { color: %1; }")
            .arg(QColor::fromRgb(ViewerCountStyle::kFg).name()));
        doc.setHtml(viewerHtml);
        doc.setTextWidth(-1);
        const QSizeF docSize = doc.size();
        const int textW = static_cast<int>(docSize.width()) + ViewerCountStyle::kPaddingX * 2;
        const int textH = static_cast<int>(docSize.height()) + ViewerCountStyle::kPaddingY * 2;
        const int margin = 8;

        const bool rotate = (position == QStringLiteral("CenterLeft")
                             || position == QStringLiteral("CenterRight"));
        const int bbW = rotate ? textH : textW;
        const int bbH = rotate ? textW : textH;

        int ox = margin;
        if (position.endsWith(QStringLiteral("Right")))       ox = pw - bbW - margin;
        else if (position.endsWith(QStringLiteral("Center"))) ox = (pw - bbW) / 2;

        int oy = margin;
        if (position.startsWith(QStringLiteral("Bottom")))      oy = ph - bbH - margin;
        else if (position.startsWith(QStringLiteral("Center"))) oy = (ph - bbH) / 2;

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        if (rotate) {
            painter.translate(ox + bbW / 2.0, oy + bbH / 2.0);
            painter.rotate(90);
            painter.translate(-textW / 2.0, -textH / 2.0);
            painter.setBrush(QColor::fromRgba(ViewerCountStyle::kBg));
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(0, 0, textW, textH,
                                    ViewerCountStyle::kRadius,
                                    ViewerCountStyle::kRadius);
            painter.save();
            painter.translate(ViewerCountStyle::kPaddingX,
                              ViewerCountStyle::kPaddingY);
            doc.drawContents(&painter);
            painter.restore();
        } else {
            painter.setBrush(QColor::fromRgba(ViewerCountStyle::kBg));
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(ox, oy, textW, textH,
                                    ViewerCountStyle::kRadius,
                                    ViewerCountStyle::kRadius);
            painter.save();
            painter.translate(ox + ViewerCountStyle::kPaddingX,
                              oy + ViewerCountStyle::kPaddingY);
            doc.drawContents(&painter);
            painter.restore();
        }
        painter.restore();
    };

    // GroupBox contentsRect 기준 비율 유지 스케일링.
    const QRect groupRect = m_transparentPreviewGroup->contentsRect();
    const int availW = qMax(groupRect.width() - 8, 50);
    const int availH = qMax(groupRect.height() - 8, 50);
    const qreal scaleW = static_cast<qreal>(availW) / w;
    const qreal scaleH = static_cast<qreal>(availH) / h;
    const qreal scale = qMin(scaleW, scaleH);
    const int scaledW = qMax(static_cast<int>(w * scale), 1);
    const int scaledH = qMax(static_cast<int>(h * scale), 1);

    // 투명 모드 프리뷰 — 16x16 체커보드 타일 → transparent_bg overlay → renderer.render.
    {
        QPixmap pm(w, h);
        pm.fill(Qt::transparent);
        QPainter painter(&pm);
        QPixmap tile(16, 16);
        tile.fill(QColor(255, 255, 255));
        {
            QPainter tp(&tile);
            tp.fillRect(0, 0, 8, 8, QColor(204, 204, 204));
            tp.fillRect(8, 8, 8, 8, QColor(204, 204, 204));
        }
        painter.drawTiledPixmap(0, 0, w, h, tile);
        const QColor transparentBg(
            m_btnBgTransparent->property("colorValue").toString());
        painter.fillRect(0, 0, w, h, transparentBg);
        painter.end();
        m_broadcastPreviewRenderer->render(&pm);
        { QPainter op(&pm); drawViewerOverlay(op, w, h); }
        m_lblBroadcastPreviewTransparent->setPixmap(
            pm.scaled(scaledW, scaledH, Qt::KeepAspectRatio,
                      Qt::SmoothTransformation));
    }

    // 불투명 모드 프리뷰 — opaque_bg fill → renderer.render.
    {
        QPixmap pm(w, h);
        const QColor opaqueBg(m_btnBgOpaque->property("colorValue").toString());
        pm.fill(opaqueBg.isValid() ? opaqueBg : QColor(255, 255, 255));
        m_broadcastPreviewRenderer->render(&pm);
        { QPainter op(&pm); drawViewerOverlay(op, w, h); }
        m_lblBroadcastPreviewOpaque->setPixmap(
            pm.scaled(scaledW, scaledH, Qt::KeepAspectRatio,
                      Qt::SmoothTransformation));
    }
}
