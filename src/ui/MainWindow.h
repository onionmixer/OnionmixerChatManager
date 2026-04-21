#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include "auth/TokenManager.h"
#include "config/AppSettings.h"
#include "core/ActionExecutor.h"
#include "core/ConnectionCoordinator.h"
#include "platform/chzzk/ChzzkAdapter.h"
#include "platform/youtube/YouTubeAdapter.h"
#include "ui/ChatterListDialog.h"

#include <QHash>
#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QSet>
#include <QUrl>
#include <QVector>

class BroadcastChatWindow;
class ConfigurationDialog;
class ChatterListDialog;
class QEvent;
class QFrame;
class QGroupBox;
class QLabel;
class QNetworkReply;
class QPlainTextEdit;
class QPushButton;
class ChatBubbleDelegate;
class ChatDisplayController;
class ChatMessageModel;
class ChatterStatsManager;
class EmojiImageCache;
#ifdef ONIONMIXERCHATMANAGER_BROADCHAT_SERVER_ENABLED
class BroadChatServer;
#endif
class QListView;
class QSplitter;
class QStackedWidget;
class QTableWidget;
class QTextEdit;
class QTimer;
class QWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString& configDir, QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onConnectToggleClicked();
    void onToggleChatViewClicked();
    void onOpenConfiguration();
    void onOpenChatterList();
    void onResetChatterList();

    void onConfigApplyRequested(const AppSettingsSnapshot& snapshot);
    void onPlatformConfigValidationRequested(PlatformId platform, const PlatformSettings& settings);

    void onConnectionStateChanged(ConnectionState state);
    void onConnectProgress(PlatformId platform, const QString& phase);
    void onConnectFinished(const ConnectSessionResult& result);
    void onDisconnectFinished();
    void onWarningRaised(const QString& code, const QString& message);
    void onChatReceived(const UnifiedChatMessage& message);
    void onChatSelectionChanged();
    void onCopySelectedChat();
    void onActionSendMessage();
    void onActionRestrictUser();
    void onActionYoutubeDeleteMessage();
    void onActionYoutubeTimeout();
    void onActionChzzkRestrict();
    void onComposerHistoryPrevRequested();
    void onComposerHistoryNextRequested();
    void onLiveProbeTimeout();

private:
    void setupUi();
    QLayout* setupTopBar(QWidget* root);
    QLayout* setupStatusBar(QWidget* root);
    void setupChatTable(QWidget* root);
    void setupActionPanel(QWidget* root);
    QWidget* setupComposer(QWidget* root);
    void retranslateUi();
    void recreateAuxiliaryDialogs(bool reopenConfiguration, bool reopenChatterList);
    void connectConfigurationDialogSignals();
    void refreshConnectButton();
    void refreshChatViewToggleButton();
    void setPlatformStatus(PlatformId platform, const QString& statusText);
    void setPlatformRuntimePhase(PlatformId platform, const QString& phase);
    void setPlatformRuntimeError(PlatformId platform, const QString& code, const QString& message);
    void clearPlatformRuntimeError(PlatformId platform);
    void reconcileApiStatus();
    QString connectionStateText(ConnectionState state) const;
    QString messengerAuthorLabel(const UnifiedChatMessage& message) const;
    QString displayAuthorLabel(const UnifiedChatMessage& message) const;
    QString normalizeYouTubeHandle(const QString& value) const;
    void maybeQueueYouTubeAuthorHandleLookup(const UnifiedChatMessage& message);
    void flushYouTubeAuthorHandleLookupQueue();
    void refreshChatterListDialog();
    void updateActionPanel();
    void updateComposerUiState();
    void setActionButtonState(QPushButton* button, bool enabled, const QString& reason);
    int selectedChatRow() const;
    const UnifiedChatMessage* selectedChatMessage() const;
    void executeAction(const QString& actionId);
    void sendComposedMessage();
    void onMessageSent(PlatformId platform, bool ok, const QString& detail);
    void pushComposerHistory(const QString& text);
    void applyComposerHistoryText(const QString& text);
    void connectTokenManagerSignals();
    void refreshPlatformIndicators();
    void refreshPlatformIndicator(PlatformId platform);
    enum class PlatformVisualState {
        TOKEN_BAD,
        TOKEN_OK,
        ONLINE,
        AUTH_IN_PROGRESS,
    };
    PlatformVisualState resolvePlatformVisualState(PlatformId platform) const;
    void applyPlatformIndicatorStyle(QFrame* indicator, PlatformId platform, PlatformVisualState state);
    enum class LiveBroadcastState {
        UNKNOWN,
        CHECKING,
        ONLINE,
        OFFLINE,
        ERROR,
    };
    void initializeLiveProbe();
    void probeLiveStatus(PlatformId platform);
    void probeChzzkLiveStatus(const QString& accessToken);
    void setLiveBroadcastState(PlatformId platform, LiveBroadcastState state, const QString& detail);
    QString liveBroadcastStateText(LiveBroadcastState state) const;
    QString displayPlatformPhase(const QString& phase) const;
    void refreshLiveBroadcastIndicators();
    void refreshLiveBroadcastIndicator(PlatformId platform);
    void applyLiveBroadcastIndicatorStyle(QFrame* indicator, QLabel* label, PlatformId platform);
    bool isPlatformLiveOnline(PlatformId platform) const;
    void requestYouTubeViewerCount();
    void updateViewerCount(PlatformId platform, int count);
    void refreshViewerCountDisplay();
    void onOpenBroadcast();
    void onBroadcastWindowResized(int width, int height);
    void onBroadcastWindowMoved(int x, int y);
    void syncYouTubeProfileFromAccessToken(const QString& accessToken);
    void syncChzzkProfileFromAccessToken(const QString& accessToken);
    QMap<PlatformId, bool> currentConnections() const;
    AppSettingsSnapshot buildRuntimeConnectSnapshot(const AppSettingsSnapshot& base) const;
    void applyRuntimeAccessTokenToAdapter(PlatformId platform, const QString& accessToken);

    struct PlatformRuntimeState {
        QString phase;
        QString lastErrorCode;
        QString lastErrorMessage;
        QDateTime updatedAtUtc;
    };

    QString m_configDir;
    AppSettings m_settings;
    TokenManager m_tokenManager;
    QNetworkAccessManager m_networkAccessManager;
    AppSettingsSnapshot m_snapshot;
    ActionExecutor m_actionExecutor;
    QHash<PlatformId, PlatformRuntimeState> m_platformRuntimeStates;
    QHash<PlatformId, LiveBroadcastState> m_liveStates;
    QHash<PlatformId, QString> m_liveStateDetails;
    QTimer* m_liveProbeTimer = nullptr;
    QTimer* m_youtubeViewerCountTimer = nullptr;
    bool m_awaitingYouTubeViewerCount = false;
    int m_youtubeViewerCount = -1;
    int m_chzzkViewerCount = -1;
    // YouTube viewer flicker 완화 (Constants.h Viewers 참조)
    int m_youtubeViewerMissStreak = 0;          // 연속 결측 횟수 (≥grace 일 때 리셋)
    QDateTime m_youtubeViewerLastFreshAt;       // 마지막 유효값 수신 시각 (tooltip용)
    qint64 m_youtubeViewerMissTotal = 0;        // 누적 결측 tick
    qint64 m_youtubeViewerTotalTicks = 0;       // 누적 polling tick (결측·성공 합)
    QTimer* m_apiStatusReconcileTimer = nullptr;
    QDateTime m_nextPeriodicChzzkProbeAtUtc;
    bool m_awaitingChzzkLiveProbe = false;
    bool m_awaitingYouTubeProfileSync = false;
    bool m_awaitingChzzkProfileSync = false;
    bool m_awaitingYouTubeAuthorLookup = false;

    ConnectionCoordinator m_connectionCoordinator;
    YouTubeAdapter m_youtubeAdapter;
    ChzzkAdapter m_chzzkAdapter;

    ConfigurationDialog* m_configurationDialog = nullptr;
    ChatterListDialog* m_chatterListDialog = nullptr;
    BroadcastChatWindow* m_broadcastWindow = nullptr;
    EmojiImageCache* m_emojiCache = nullptr;
    ChatMessageModel* m_chatModel = nullptr;
    ChatBubbleDelegate* m_chatDelegate = nullptr;
    ChatDisplayController* m_chatController = nullptr;
#ifdef ONIONMIXERCHATMANAGER_BROADCHAT_SERVER_ENABLED
    BroadChatServer* m_broadChatServer = nullptr;
    // §20 UI 상태 표시 — 상태바 영구 라벨
    QLabel* m_broadChatStatusLabel = nullptr;
    QLabel* m_broadChatClientCountLabel = nullptr;
#endif

    QPushButton* m_btnConnectToggle = nullptr;
    QPushButton* m_btnToggleChatView = nullptr;
    QPushButton* m_btnOpenBroadcast = nullptr;
    QPushButton* m_btnOpenChatterList = nullptr;
    QPushButton* m_btnOpenConfiguration = nullptr;
    QLabel* m_lblStateCaption = nullptr;
    QLabel* m_lblConnectionState = nullptr;
    QLabel* m_lblYouTubeStatus = nullptr;
    QLabel* m_lblChzzkStatus = nullptr;
    QGroupBox* m_grpActionPanel = nullptr;
    QFrame* m_boxYouTubeRuntime = nullptr;
    QFrame* m_boxChzzkRuntime = nullptr;
    QFrame* m_boxYouTubeLive = nullptr;
    QFrame* m_boxChzzkLive = nullptr;
    QLabel* m_lblYouTubeLive = nullptr;
    QLabel* m_lblChzzkLive = nullptr;
    QLabel* m_lblYouTubeViewers = nullptr;
    QLabel* m_lblChzzkViewers = nullptr;
    QLabel* m_lblTotalViewers = nullptr;
    QSplitter* m_mainSplitter = nullptr;
    QSplitter* m_upperSplitter = nullptr;
    QStackedWidget* m_chatStack = nullptr;
    QTableWidget* m_tblChat = nullptr;
    QListView* m_chatListView = nullptr;
    QLabel* m_lblSelectedPlatformCaption = nullptr;
    QLabel* m_lblSelectedAuthorCaption = nullptr;
    QLabel* m_lblSelectedMessageCaption = nullptr;
    QLabel* m_lblSelectedPlatform = nullptr;
    QLabel* m_lblSelectedAuthor = nullptr;
    QLabel* m_lblSelectedMessage = nullptr;
    QPushButton* m_btnActionSendMessage = nullptr;
    QPushButton* m_btnActionRestrictUser = nullptr;
    QPushButton* m_btnActionYoutubeDeleteMessage = nullptr;
    QPushButton* m_btnActionYoutubeTimeout = nullptr;
    QPushButton* m_btnActionChzzkRestrict = nullptr;
    QPlainTextEdit* m_edtComposer = nullptr;
    QPushButton* m_btnComposerSend = nullptr;
    QTextEdit* m_txtEventLog = nullptr;

    ChatterStatsManager* m_chatterStatsManager = nullptr;
    QHash<PlatformId, QString> m_platformStatusCodes;
    QHash<QString, QString> m_youtubeAuthorHandleCache;
    QSet<QString> m_youtubeAuthorHandlePending;
    QStringList m_youtubeAuthorHandleLookupQueue;
    QTimer* m_youtubeAuthorLookupTimer = nullptr;
    QStringList m_sendHistory;
    int m_sendHistoryIndex = 0;
    QString m_sendHistoryDraft;
    bool m_composerApplyingHistory = false;
    bool m_detailLogEnabled = false;
};

#endif // MAIN_WINDOW_H
