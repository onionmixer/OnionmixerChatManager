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

#include <QHash>
#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QVector>

class ConfigurationDialog;
class QLabel;
class QPushButton;
class QTableWidget;
class QTextEdit;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onConnectToggleClicked();
    void onToggleChatViewClicked();
    void onOpenConfiguration();

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

private:
    void setupUi();
    void refreshConnectButton();
    void refreshChatViewToggleButton();
    void configureChatTableForCurrentView();
    void setPlatformStatus(PlatformId platform, const QString& statusText);
    QString connectionStateText(ConnectionState state) const;
    void appendChatMessage(const UnifiedChatMessage& message);
    void appendChatRow(int row, const UnifiedChatMessage& message);
    void rebuildChatTable();
    QString messengerAuthorLabel(const UnifiedChatMessage& message) const;
    void updateActionPanel();
    void setActionButtonState(QPushButton* button, bool enabled, const QString& reason);
    int selectedChatRow() const;
    const UnifiedChatMessage* selectedChatMessage() const;
    void executeAction(const QString& actionId);
    void refreshTokenUi(PlatformId platform);
    void refreshAllTokenUi();
    void tryStartupTokenRefresh();
    void tryStartupTokenRefreshForPlatform(PlatformId platform);
    QMap<PlatformId, bool> currentConnections() const;
    TokenState inferTokenState(const TokenRecord* record) const;
    QUrl buildAuthorizationUrl(PlatformId platform, const PlatformSettings& settings, const QString& state, const QString& codeChallenge) const;
    QString createOAuthState(PlatformId platform) const;
    PlatformSettings settingsFor(PlatformId platform) const;
    bool startTokenRefreshFlow(PlatformId platform, const PlatformSettings& settings, const TokenRecord& currentRecord);
    bool startAuthCodeExchangeFlow(PlatformId platform, const PlatformSettings& settings, const QString& code, const QString& codeVerifier, const QString& authState);
    void appendTokenAudit(PlatformId platform, const QString& action, bool ok, const QString& detail);

    struct PendingTokenFlowContext {
        QString flow;
        PlatformSettings settings;
        TokenRecord previousRecord;
    };

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

    ConnectionCoordinator m_connectionCoordinator;
    YouTubeAdapter m_youtubeAdapter;
    ChzzkAdapter m_chzzkAdapter;

    ConfigurationDialog* m_configurationDialog = nullptr;

    enum class ChatViewMode {
        Messenger,
        Table,
    };

    QPushButton* m_btnConnectToggle = nullptr;
    QPushButton* m_btnToggleChatView = nullptr;
    QPushButton* m_btnOpenConfiguration = nullptr;
    QLabel* m_lblConnectionState = nullptr;
    QLabel* m_lblYouTubeStatus = nullptr;
    QLabel* m_lblChzzkStatus = nullptr;
    QTableWidget* m_tblChat = nullptr;
    QLabel* m_lblSelectedPlatform = nullptr;
    QLabel* m_lblSelectedAuthor = nullptr;
    QLabel* m_lblSelectedMessage = nullptr;
    QPushButton* m_btnActionSendMessage = nullptr;
    QPushButton* m_btnActionRestrictUser = nullptr;
    QPushButton* m_btnActionYoutubeDeleteMessage = nullptr;
    QPushButton* m_btnActionYoutubeTimeout = nullptr;
    QPushButton* m_btnActionChzzkRestrict = nullptr;
    QTextEdit* m_txtEventLog = nullptr;

    QVector<UnifiedChatMessage> m_chatMessages;
    ChatViewMode m_chatViewMode = ChatViewMode::Messenger;
};

#endif // MAIN_WINDOW_H
