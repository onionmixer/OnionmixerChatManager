#pragma once

#include "core/AppTypes.h"

#include <QDialog>
#include <QString>

class ChatBubbleDelegate;
class ChatMessageModel;
class QCheckBox;
class QColor;
class QComboBox;
class QFontComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QShowEvent;
class QResizeEvent;
class QSpinBox;
class QTabWidget;
class QTimer;

// PLAN_DEV_BROADCHATCLIENT §5.5 · §17.2 확장 (v61).
// 3개 탭: Connection · Chat · Broadcast.
//   - Connection: host·port·auth_token (Paste/Clear/Show)
//   - Chat: font family·size·bold·italic·line spacing·max messages
//   - Broadcast: viewer count position·4색상 pickers (transparent/opaque bg·body/outline)
// OK 시 valuesAccepted(snapshot, host, port, authToken) 발행 — 소유자가 ini 저장·reconnect 담당.
class ClientConfigDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ClientConfigDialog(QWidget* parent = nullptr);
    ~ClientConfigDialog() override;

    // 로드 시 초기값 주입. snapshot.broadcast*·chat* 필드가 UI에 반영됨.
    void setValues(const AppSettingsSnapshot& snapshot,
                   const QString& host, quint16 port, const QString& authToken);

    // v72 #8 (§5.6.3): in-memory 모드 경고 라벨 on/off.
    // 소유자가 초기화 시 호출. true 일 때 다이얼로그 하단에 "메모리에만 저장됨" 경고.
    void setInMemoryMode(bool inMemory);

    QString host() const;
    quint16 port() const;
    QString authToken() const;
    AppSettingsSnapshot currentSnapshot() const;

signals:
    // OK 버튼 확정 시 발행. 소유자(BroadChatClientApp)가 snapshot 저장 + 창에 apply + 필요 시 reconnect.
    void valuesAccepted(const AppSettingsSnapshot& snapshot,
                        const QString& host, quint16 port, const QString& authToken);

protected:
    // v76: dialog resize/show 시 preview debounce 재시작 (메인 앱 대칭).
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onOkClicked();
    void onCancelClicked();
    void onPasteClicked();
    void onClearTokenClicked();
    // v68 #16 (§v51): 모든 필드를 기본값으로 복원 (dialog 내 preview 만 바뀜; OK 눌러야 적용).
    void onResetToDefaults();
    // v76: 투명/불투명 프리뷰 재생성 (메인 앱 `updateBroadcastPreview` 동등).
    void updateBroadcastPreview();

private:
    void buildConnectionTab();
    void buildChatTab();
    void buildBroadcastTab();

    // v75: 메인 앱 `ConfigurationDialog::applyColorButtonStyle` 이식.
    // 버튼에 hex text + 색상 background + 가독성 위한 자동 대비 text color 적용.
    void applyColorButtonStyle(QPushButton* button, const QColor& color);
    // body/outline color 전용 "Not set" 스타일 — 점선 테두리 + 회색 배경.
    void applyUnsetColorButtonStyle(QPushButton* button);
    // 공통 색상 선택 dialog — custom colors 팔레트 pre-populate 후 실행.
    void pickBroadcastColor(QPushButton* button, const QString& title);
    void clearBroadcastColor(QPushButton* button);

    // Connection tab
    QLineEdit* m_edtHost = nullptr;
    QSpinBox* m_spnPort = nullptr;
    QLineEdit* m_edtAuthToken = nullptr;
    QCheckBox* m_chkShow = nullptr;
    QPushButton* m_btnPaste = nullptr;
    QPushButton* m_btnClear = nullptr;

    // Chat tab
    QFontComboBox* m_fontFamily = nullptr;
    QSpinBox* m_fontSize = nullptr;
    QCheckBox* m_fontBold = nullptr;
    QCheckBox* m_fontItalic = nullptr;
    QSpinBox* m_lineSpacing = nullptr;
    QSpinBox* m_maxMessages = nullptr;

    // Broadcast tab — v75: 메인 앱 스타일 `QPushButton` (hex text + color bg) 로 교체.
    // 버튼의 `colorValue` property 에 현재 hex 값 저장. 빈 값 = "Not set" (body/outline 전용).
    QComboBox* m_viewerPos = nullptr;
    QPushButton* m_btnBgTransparent = nullptr;    // "#AARRGGBB"
    QPushButton* m_btnBgOpaque = nullptr;
    QPushButton* m_btnBodyColor = nullptr;        // 빈 문자열 = off
    QPushButton* m_btnOutlineColor = nullptr;     // 빈 문자열 = off
    QSpinBox* m_windowWidth = nullptr;
    QSpinBox* m_windowHeight = nullptr;

    // Tabs store snapshot baseline (connection·window 필드 유지용)
    AppSettingsSnapshot m_baseline;

    // v72 #8: in-memory 모드 경고 라벨 (dialog 하단 고정). 기본 숨김.
    QLabel* m_inMemoryWarn = nullptr;

    // v76: 프리뷰 infrastructure — 메인 앱 `ConfigurationDialog` 동등 이식.
    QTabWidget* m_tabs = nullptr;                              // tab 전환 시 preview refresh
    QWidget* m_broadcastPreviewRenderer = nullptr;             // 오프스크린 QWidget (WA_TranslucentBackground)
    ChatBubbleDelegate* m_broadcastPreviewDelegate = nullptr;  // 메인 창 동일 delegate
    QListView* m_broadcastPreviewList = nullptr;               // 프리뷰 리스트
    ChatMessageModel* m_chatPreviewModel = nullptr;            // 샘플 메시지 3건
    QGroupBox* m_transparentPreviewGroup = nullptr;            // "Transparent Preview"
    QGroupBox* m_opaquePreviewGroup = nullptr;                 // "Opaque Preview"
    QLabel* m_lblBroadcastPreviewTransparent = nullptr;        // 스케일된 pixmap 표시
    QLabel* m_lblBroadcastPreviewOpaque = nullptr;
    QTimer* m_broadcastPreviewDebounce = nullptr;              // 150ms debounce
};
