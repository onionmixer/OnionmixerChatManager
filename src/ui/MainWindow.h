#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include "auth/OAuthLocalServer.h"
#include "auth/OAuthTokenClient.h"
#include "auth/TokenVault.h"
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

class ConfigurationDialog;
class ChatterListDialog;
class QEvent;
class QFrame;
class QGroupBox;
class QLabel;
class QNetworkReply;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
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
    void onTokenRefreshRequested(PlatformId platform, const PlatformSettings& settings);
    void onInteractiveAuthRequested(PlatformId platform, const PlatformSettings& settings);
    void onTokenDeleteRequested(PlatformId platform);
    void onPlatformConfigValidationRequested(PlatformId platform, const PlatformSettings& settings);
    void onOAuthCallbackReceived(PlatformId platform, const QString& code, const QString& state, const QString& errorCode, const QString& errorDescription);
    void onOAuthSessionFailed(PlatformId platform, const QString& reason);
    void onTokenGranted(PlatformId platform, const QString& flow, const QString& accessToken, const QString& refreshToken, int expiresInSec, int refreshExpiresInSec);
    void onTokenFailed(PlatformId platform, const QString& flow, const QString& errorCode, const QString& message, int httpStatus);
    void onTokenRevoked(PlatformId platform, const QString& flow);
    void onTokenRevokeFailed(PlatformId platform, const QString& flow, const QString& errorCode, const QString& message, int httpStatus);

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
    void retranslateUi();
    void recreateAuxiliaryDialogs(bool reopenConfiguration, bool reopenChatterList);
    void refreshConnectButton();
    void refreshChatViewToggleButton();
    void configureChatTableForCurrentView();
    void setPlatformStatus(PlatformId platform, const QString& statusText);
    void setPlatformRuntimePhase(PlatformId platform, const QString& phase);
    void setPlatformRuntimeError(PlatformId platform, const QString& code, const QString& message);
    void clearPlatformRuntimeError(PlatformId platform);
    void reconcileApiStatus();
    QString connectionStateText(ConnectionState state) const;
    void appendChatMessage(const UnifiedChatMessage& message, const QString& authorLabel = QString());
    void appendChatRow(int row, const UnifiedChatMessage& message, const QString& authorLabel = QString());
    void rebuildChatTable();
    QString messengerAuthorLabel(const UnifiedChatMessage& message) const;
    QString displayAuthorLabel(const UnifiedChatMessage& message) const;
    QString normalizeYouTubeHandle(const QString& value) const;
    void maybeQueueYouTubeAuthorHandleLookup(const UnifiedChatMessage& message);
    void flushYouTubeAuthorHandleLookupQueue();
    QWidget* buildMessengerCellWidget(const UnifiedChatMessage& message, const QString& authorDisplay) const;
    void recordChatter(const UnifiedChatMessage& message, const QString& authorLabel = QString());
    void rebuildChatterStatsFromMessages();
    void refreshChatterListDialog();
    void updateActionPanel();
    void updateComposerUiState();
    void setActionButtonState(QPushButton* button, bool enabled, const QString& reason);
    int selectedChatRow() const;
    const UnifiedChatMessage* selectedChatMessage() const;
    void executeAction(const QString& actionId);
    void sendComposedMessage();
    bool dispatchSendToYouTube(const QString& text);
    bool dispatchSendToChzzk(const QString& text);
    void pushComposerHistory(const QString& text);
    void applyComposerHistoryText(const QString& text);
    void refreshTokenUi(PlatformId platform);
    void refreshAllTokenUi();
    void tryStartupTokenRefresh();
    void tryStartupTokenRefreshForPlatform(PlatformId platform);
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
    void syncYouTubeProfileFromAccessToken(const QString& accessToken);
    void syncChzzkProfileFromAccessToken(const QString& accessToken);
    QMap<PlatformId, bool> currentConnections() const;
    TokenState inferTokenState(const TokenRecord* record) const;
    QUrl buildAuthorizationUrl(PlatformId platform, const PlatformSettings& settings, const QString& state, const QString& codeChallenge) const;
    QString createOAuthState(PlatformId platform) const;
    PlatformSettings settingsFor(PlatformId platform) const;
    bool startTokenRefreshFlow(PlatformId platform, const PlatformSettings& settings, const TokenRecord& currentRecord);
    bool startAuthCodeExchangeFlow(PlatformId platform, const PlatformSettings& settings, const QString& code, const QString& codeVerifier, const QString& authState);
    void appendTokenAudit(PlatformId platform, const QString& action, bool ok, const QString& detail);
    AppSettingsSnapshot buildRuntimeConnectSnapshot(const AppSettingsSnapshot& base) const;
    void applyRuntimeAccessTokenToAdapter(PlatformId platform, const QString& accessToken);

    struct PendingTokenFlowContext {
        QString flow;
        PlatformSettings settings;
        TokenRecord previousRecord;
    };
    struct PlatformRuntimeState {
        QString phase;
        QString lastErrorCode;
        QString lastErrorMessage;
        QDateTime updatedAtUtc;
    };

    QString m_configDir;
    AppSettings m_settings;
    FileTokenVault m_tokenVault;
    OAuthLocalServer m_oauthLocalServer;
    QNetworkAccessManager m_networkAccessManager;
    OAuthTokenClient m_oauthTokenClient;
    AppSettingsSnapshot m_snapshot;
    ActionExecutor m_actionExecutor;
    QHash<PlatformId, QString> m_pendingOAuthState;
    QHash<PlatformId, PlatformSettings> m_pendingOAuthSettings;
    QHash<PlatformId, QString> m_pendingPkceVerifier;
    QHash<PlatformId, PendingTokenFlowContext> m_pendingTokenFlows;
    QHash<PlatformId, bool> m_pendingTokenRevokes;
    QHash<PlatformId, bool> m_authInProgress;
    QHash<PlatformId, PlatformRuntimeState> m_platformRuntimeStates;
    QHash<PlatformId, LiveBroadcastState> m_liveStates;
    QHash<PlatformId, QString> m_liveStateDetails;
    QTimer* m_liveProbeTimer = nullptr;
    QTimer* m_apiStatusReconcileTimer = nullptr;
    QDateTime m_nextPeriodicChzzkProbeAtUtc;
    bool m_pendingChzzkLiveProbe = false;
    bool m_pendingYouTubeProfileSync = false;
    bool m_pendingChzzkProfileSync = false;
    bool m_pendingYouTubeAuthorLookup = false;

    ConnectionCoordinator m_connectionCoordinator;
    YouTubeAdapter m_youtubeAdapter;
    ChzzkAdapter m_chzzkAdapter;

    ConfigurationDialog* m_configurationDialog = nullptr;
    ChatterListDialog* m_chatterListDialog = nullptr;

    enum class ChatViewMode {
        Messenger,
        Table,
    };

    QPushButton* m_btnConnectToggle = nullptr;
    QPushButton* m_btnToggleChatView = nullptr;
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
    QSplitter* m_mainSplitter = nullptr;
    QSplitter* m_upperSplitter = nullptr;
    QTableWidget* m_tblChat = nullptr;
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

    QVector<UnifiedChatMessage> m_chatMessages;
    QHash<QString, ChatterListEntry> m_chatterStats;
    QHash<PlatformId, QString> m_platformStatusCodes;
    QHash<QString, QString> m_youtubeAuthorHandleCache;
    QSet<QString> m_youtubeAuthorHandlePending;
    QStringList m_youtubeAuthorHandleLookupQueue;
    QTimer* m_youtubeAuthorLookupTimer = nullptr;
    QStringList m_sendHistory;
    int m_sendHistoryIndex = 0;
    QString m_sendHistoryDraft;
    bool m_composerApplyingHistory = false;
    ChatViewMode m_chatViewMode = ChatViewMode::Messenger;
    bool m_detailLogEnabled = false;
    bool m_chatterRefreshPending = false;
    QSet<QString> m_recentMessageIds;
};

#endif // MAIN_WINDOW_H
