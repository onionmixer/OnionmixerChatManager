#include "ui/MainWindow.h"

#include "auth/PkceUtil.h"
#include "ui/ConfigurationDialog.h"

#include <QAction>
#include <QAbstractItemView>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequence>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QUuid>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWidget>
#include <QHeaderView>

namespace {
QString normalizeGoogleOAuthClientId(const QString& raw)
{
    QString value = raw.trimmed();
    if (value.isEmpty()) {
        return value;
    }

    static const QRegularExpression reExtract(QStringLiteral("([A-Za-z0-9._-]+\\.googleusercontent\\.com)"));
    const QRegularExpressionMatch m = reExtract.match(value);
    if (m.hasMatch()) {
        value = m.captured(1);
    }

    if ((value.startsWith('"') && value.endsWith('"')) || (value.startsWith('\'') && value.endsWith('\''))) {
        value = value.mid(1, value.size() - 2).trimmed();
    }
    return value;
}

bool isLikelyGoogleOAuthClientId(const QString& clientId)
{
    const QString normalized = normalizeGoogleOAuthClientId(clientId);
    return !normalized.isEmpty()
        && !normalized.contains(' ')
        && normalized.endsWith(QStringLiteral(".googleusercontent.com"), Qt::CaseInsensitive);
}

QString googleClientIdValidationMessage(const QString& parsedClientId)
{
    if (parsedClientId.isEmpty()) {
        return QStringLiteral("YouTube client_id format invalid. Parsed value is empty.");
    }
    if (!parsedClientId.contains(QStringLiteral("googleusercontent.com"), Qt::CaseInsensitive)
        && parsedClientId.contains('.')) {
        return QStringLiteral(
            "YouTube client_id format invalid. parsed=%1 (looks like bundle/package id). "
            "Use OAuth client_id ending with *.googleusercontent.com")
            .arg(parsedClientId);
    }
    return QStringLiteral("YouTube client_id format invalid. expected=*.googleusercontent.com parsed=%1")
        .arg(parsedClientId);
}

bool looksLikeYouTubeClientSecretMismatch(const QString& errorCode, const QString& message)
{
    const QString normalizedCode = errorCode.trimmed().toLower();
    const QString normalizedMessage = message.trimmed().toLower();

    if (normalizedCode == QStringLiteral("invalid_client")) {
        return true;
    }
    if (normalizedMessage.contains(QStringLiteral("client_secret"))) {
        return true;
    }
    if (normalizedMessage.contains(QStringLiteral("unauthorized_client"))) {
        return true;
    }
    return false;
}

QString buildTokenFailureDetailWithGuidance(PlatformId platform, const QString& flow, const QString& errorCode, const QString& message, int httpStatus)
{
    const QString base = QStringLiteral("%1 (http=%2)").arg(message).arg(httpStatus);
    if (platform != PlatformId::YouTube || flow != QStringLiteral("authorization_code")) {
        return base;
    }
    if (!looksLikeYouTubeClientSecretMismatch(errorCode, message)) {
        return base;
    }

    return base + QStringLiteral(
                      "\nHint: YouTube AUTH_CODE_GRANT failed at token exchange."
                      "\n- Use OAuth client type: Desktop app"
                      "\n- Use client_id ending with .apps.googleusercontent.com"
                      "\n- If error says 'client_secret is missing', set YouTube Client Secret in Configuration"
                      "\n- Ensure the same Google project is used for consent screen + test user + client_id");
}
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_settings(QStringLiteral("config/app.ini"))
    , m_tokenVault(QStringLiteral("config/tokens.ini"))
    , m_oauthTokenClient(&m_networkAccessManager, this)
    , m_connectionCoordinator(this)
    , m_youtubeAdapter(this)
    , m_chzzkAdapter(this)
{
    setupUi();

    m_snapshot = m_settings.load();

    m_connectionCoordinator.bindAdapters({
        { PlatformId::YouTube, &m_youtubeAdapter },
        { PlatformId::Chzzk, &m_chzzkAdapter },
    });

    connect(&m_connectionCoordinator, &ConnectionCoordinator::stateChanged,
        this, &MainWindow::onConnectionStateChanged);
    connect(&m_connectionCoordinator, &ConnectionCoordinator::connectProgress,
        this, &MainWindow::onConnectProgress);
    connect(&m_connectionCoordinator, &ConnectionCoordinator::connectFinished,
        this, &MainWindow::onConnectFinished);
    connect(&m_connectionCoordinator, &ConnectionCoordinator::disconnectFinished,
        this, &MainWindow::onDisconnectFinished);
    connect(&m_connectionCoordinator, &ConnectionCoordinator::warningRaised,
        this, &MainWindow::onWarningRaised);

    connect(&m_youtubeAdapter, &IChatPlatformAdapter::chatReceived,
        this, &MainWindow::onChatReceived);
    connect(&m_chzzkAdapter, &IChatPlatformAdapter::chatReceived,
        this, &MainWindow::onChatReceived);

    m_configurationDialog = new ConfigurationDialog(this);
    m_configurationDialog->setSnapshot(m_snapshot);

    connect(m_configurationDialog, &ConfigurationDialog::configApplyRequested,
        this, &MainWindow::onConfigApplyRequested);
    connect(m_configurationDialog, &ConfigurationDialog::tokenRefreshRequested,
        this, &MainWindow::onTokenRefreshRequested);
    connect(m_configurationDialog, &ConfigurationDialog::interactiveAuthRequested,
        this, &MainWindow::onInteractiveAuthRequested);
    connect(m_configurationDialog, &ConfigurationDialog::tokenDeleteRequested,
        this, &MainWindow::onTokenDeleteRequested);
    connect(m_configurationDialog, &ConfigurationDialog::platformConfigValidationRequested,
        this, &MainWindow::onPlatformConfigValidationRequested);

    connect(&m_oauthLocalServer, &OAuthLocalServer::callbackReceived,
        this, &MainWindow::onOAuthCallbackReceived);
    connect(&m_oauthLocalServer, &OAuthLocalServer::sessionFailed,
        this, &MainWindow::onOAuthSessionFailed);
    connect(&m_oauthTokenClient, &OAuthTokenClient::tokenGranted,
        this, &MainWindow::onTokenGranted);
    connect(&m_oauthTokenClient, &OAuthTokenClient::tokenFailed,
        this, &MainWindow::onTokenFailed);
    connect(&m_oauthTokenClient, &OAuthTokenClient::tokenRevoked,
        this, &MainWindow::onTokenRevoked);
    connect(&m_oauthTokenClient, &OAuthTokenClient::tokenRevokeFailed,
        this, &MainWindow::onTokenRevokeFailed);

    setPlatformStatus(PlatformId::YouTube, QStringLiteral("IDLE"));
    setPlatformStatus(PlatformId::Chzzk, QStringLiteral("IDLE"));
    refreshConnectButton();
    refreshAllTokenUi();
    tryStartupTokenRefresh();
    updateActionPanel();
}

void MainWindow::onConnectToggleClicked()
{
    switch (m_connectionCoordinator.state()) {
    case ConnectionState::IDLE:
    case ConnectionState::ERROR:
        m_snapshot = m_settings.load();
        m_connectionCoordinator.connectAll(m_snapshot);
        break;
    case ConnectionState::CONNECTED:
    case ConnectionState::PARTIALLY_CONNECTED:
        m_connectionCoordinator.disconnectAll();
        break;
    case ConnectionState::CONNECTING:
    case ConnectionState::DISCONNECTING:
        break;
    }
}

void MainWindow::onToggleChatViewClicked()
{
    m_chatViewMode = (m_chatViewMode == ChatViewMode::Messenger)
        ? ChatViewMode::Table
        : ChatViewMode::Messenger;
    refreshChatViewToggleButton();
    rebuildChatTable();
    statusBar()->showMessage(
        m_chatViewMode == ChatViewMode::Messenger
            ? QStringLiteral("Chat view: Messenger")
            : QStringLiteral("Chat view: Table"),
        2000);
}

void MainWindow::onOpenConfiguration()
{
    m_configurationDialog->setSnapshot(m_snapshot);
    m_configurationDialog->show();
    m_configurationDialog->raise();
    m_configurationDialog->activateWindow();
}

void MainWindow::onConfigApplyRequested(const AppSettingsSnapshot& snapshot)
{
    if (!m_settings.save(snapshot)) {
        QMessageBox::warning(this, QStringLiteral("Configuration"), QStringLiteral("Failed to save config/app.ini"));
        return;
    }

    m_snapshot = snapshot;
    m_txtEventLog->append(QStringLiteral("[CONFIG] Applied and saved."));
    statusBar()->showMessage(QStringLiteral("Configuration updated. Reconnect to apply running session."), 4000);
}

void MainWindow::onTokenRefreshRequested(PlatformId platform, const PlatformSettings& settings)
{
    if (m_pendingTokenFlows.contains(platform)) {
        m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Another token operation is in progress"));
        appendTokenAudit(platform, QStringLiteral("token_refresh"), false, QStringLiteral("Another token operation is in progress"));
        return;
    }

    TokenRecord record;
    if (!m_tokenVault.read(platform, &record) || record.refreshToken.trimmed().isEmpty()) {
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::AUTH_REQUIRED, QStringLiteral("No refresh token. Browser re-auth required."));
        m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Refresh token not found"));
        appendTokenAudit(platform, QStringLiteral("token_refresh"), false, QStringLiteral("Refresh token not found"));
        return;
    }

    m_configurationDialog->onTokenOperationStarted(platform, QStringLiteral("refresh_token"));
    m_configurationDialog->onTokenStateUpdated(platform, TokenState::REFRESHING, QStringLiteral("Refreshing token..."));
    if (!startTokenRefreshFlow(platform, settings, record)) {
        return;
    }
}

void MainWindow::onInteractiveAuthRequested(PlatformId platform, const PlatformSettings& settings)
{
    if (m_pendingTokenFlows.contains(platform) || m_oauthLocalServer.hasActiveSession(platform)) {
        m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Another token operation is in progress"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, QStringLiteral("Another token operation is in progress"));
        return;
    }

    if (settings.clientId.trimmed().isEmpty() || settings.redirectUri.trimmed().isEmpty() || settings.scope.trimmed().isEmpty()
        || settings.authEndpoint.trimmed().isEmpty() || settings.tokenEndpoint.trimmed().isEmpty()) {
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::AUTH_REQUIRED, QStringLiteral("Missing required OAuth fields"));
        m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Interactive re-auth blocked by invalid config"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, QStringLiteral("Invalid OAuth configuration"));
        return;
    }
    if (platform == PlatformId::YouTube && !isLikelyGoogleOAuthClientId(settings.clientId)) {
        const QString normalized = normalizeGoogleOAuthClientId(settings.clientId);
        const QString validationMessage = googleClientIdValidationMessage(normalized);
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::AUTH_REQUIRED,
            validationMessage);
        m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Interactive re-auth blocked by invalid client_id"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, validationMessage);
        return;
    }

    const QUrl redirectUri(settings.redirectUri);
    if (!redirectUri.isValid() || redirectUri.scheme() != QStringLiteral("http") || redirectUri.host() != QStringLiteral("127.0.0.1") || redirectUri.port() <= 0) {
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::AUTH_REQUIRED, QStringLiteral("redirect_uri must be http://127.0.0.1:{port}/..."));
        m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Interactive re-auth blocked by invalid redirect_uri"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, QStringLiteral("Invalid redirect_uri"));
        return;
    }

    const QString state = createOAuthState(platform);
    QString codeVerifier;
    QString codeChallenge;
    if (platform == PlatformId::YouTube) {
        codeVerifier = pkce::generateCodeVerifier();
        codeChallenge = pkce::makeCodeChallengeS256(codeVerifier);
        if (codeVerifier.trimmed().isEmpty() || codeChallenge.trimmed().isEmpty()) {
            m_configurationDialog->onTokenStateUpdated(platform, TokenState::ERROR, QStringLiteral("Failed to prepare PKCE"));
            m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Interactive re-auth failed"));
            appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, QStringLiteral("Failed to prepare PKCE"));
            return;
        }
    }

    OAuthSessionConfig session;
    session.platform = platform;
    session.redirectUri = redirectUri;
    session.expectedState = state;
    session.timeoutMs = 240000;

    QString startError;
    if (!m_oauthLocalServer.startSession(session, &startError)) {
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::ERROR, startError);
        m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Failed to start OAuth callback server"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, startError);
        return;
    }

    m_pendingOAuthState.insert(platform, state);
    m_pendingOAuthSettings.insert(platform, settings);
    if (!codeVerifier.isEmpty()) {
        m_pendingPkceVerifier.insert(platform, codeVerifier);
    } else {
        m_pendingPkceVerifier.remove(platform);
    }

    const QUrl authUrl = buildAuthorizationUrl(platform, settings, state, codeChallenge);
    if (!authUrl.isValid()) {
        m_oauthLocalServer.cancelSession(platform, QStringLiteral("OAUTH_AUTH_URL_INVALID"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, QStringLiteral("Invalid authorize URL"));
        return;
    }
    if (platform == PlatformId::Chzzk && authUrl.host().contains(QStringLiteral("example.com"))) {
        m_txtEventLog->append(QStringLiteral("[WARN] CHZZK OAuth endpoint is placeholder. Replace auth/token endpoint in Configuration."));
    }

    if (!QDesktopServices::openUrl(authUrl)) {
        m_oauthLocalServer.cancelSession(platform, QStringLiteral("OAUTH_BROWSER_OPEN_FAILED"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, QStringLiteral("Failed to open system browser"));
        return;
    }

    m_configurationDialog->onTokenOperationStarted(platform, QStringLiteral("interactive_auth"));
    m_configurationDialog->onTokenStateUpdated(platform, TokenState::REFRESHING, QStringLiteral("Browser opened. Waiting callback..."));
    m_txtEventLog->append(QStringLiteral("[OAUTH] %1 browser auth started").arg(platformKey(platform)));
}

void MainWindow::onTokenDeleteRequested(PlatformId platform)
{
    if (m_pendingTokenFlows.contains(platform) || m_pendingTokenRevokes.value(platform, false)) {
        const QString detail = QStringLiteral("Another token operation is in progress");
        m_configurationDialog->onTokenActionFinished(platform, false, detail);
        appendTokenAudit(platform, QStringLiteral("token_revoke"), false, detail);
        return;
    }

    TokenRecord record;
    if (!m_tokenVault.read(platform, &record)) {
        const bool cleared = m_tokenVault.clear(platform);
        refreshTokenUi(platform);
        const QString detail = cleared ? QStringLiteral("No token to revoke. Local token state cleared.")
                                       : QStringLiteral("No token to revoke. Local clear failed.");
        m_configurationDialog->onTokenActionFinished(platform, cleared, detail);
        appendTokenAudit(platform, QStringLiteral("token_revoke"), cleared, detail);
        return;
    }

    if (record.accessToken.trimmed().isEmpty() && record.refreshToken.trimmed().isEmpty()) {
        const bool cleared = m_tokenVault.clear(platform);
        refreshTokenUi(platform);
        const QString detail = cleared ? QStringLiteral("Token already empty. Local token state cleared.")
                                       : QStringLiteral("Token already empty. Local clear failed.");
        m_configurationDialog->onTokenActionFinished(platform, cleared, detail);
        appendTokenAudit(platform, QStringLiteral("token_revoke"), cleared, detail);
        return;
    }

    const PlatformSettings settings = settingsFor(platform);
    m_configurationDialog->onTokenOperationStarted(platform, QStringLiteral("token_revoke"));
    m_configurationDialog->onTokenStateUpdated(platform, TokenState::REFRESHING, QStringLiteral("Revoking token..."));
    m_pendingTokenRevokes.insert(platform, true);

    QString errorMessage;
    if (!m_oauthTokenClient.requestRevokeToken(platform, settings, record, &errorMessage)) {
        m_pendingTokenRevokes.remove(platform);
        const bool localCleared = m_tokenVault.clear(platform);
        refreshTokenUi(platform);
        const QString detail = QStringLiteral("Remote revoke request failed: %1. Local token %2.")
                                   .arg(errorMessage, localCleared ? QStringLiteral("deleted") : QStringLiteral("delete failed"));
        m_configurationDialog->onTokenActionFinished(platform, localCleared,
            localCleared ? QStringLiteral("Local token deleted (remote revoke skipped)")
                         : QStringLiteral("Token delete failed"));
        appendTokenAudit(platform, QStringLiteral("token_revoke"), false, detail);
    }
}

void MainWindow::onPlatformConfigValidationRequested(PlatformId platform, const PlatformSettings& settings)
{
    bool ok = !settings.clientId.trimmed().isEmpty() && !settings.redirectUri.trimmed().isEmpty() && !settings.scope.trimmed().isEmpty();
    if (platform == PlatformId::Chzzk) {
        ok = ok && !settings.clientSecret.trimmed().isEmpty();
    }
    ok = ok && !settings.authEndpoint.trimmed().isEmpty() && !settings.tokenEndpoint.trimmed().isEmpty();

    const QString target = platform == PlatformId::YouTube ? QStringLiteral("YouTube") : QStringLiteral("CHZZK");
    m_txtEventLog->append(QStringLiteral("[CONFIG-TEST] %1 -> %2").arg(target, ok ? QStringLiteral("OK") : QStringLiteral("FAIL")));
    statusBar()->showMessage(QStringLiteral("%1 config test: %2").arg(target, ok ? QStringLiteral("OK") : QStringLiteral("FAIL")), 3000);
}

void MainWindow::onOAuthCallbackReceived(PlatformId platform, const QString& code, const QString& state, const QString& errorCode, const QString& errorDescription)
{
    const QString expectedState = m_pendingOAuthState.value(platform);
    m_pendingOAuthState.remove(platform);
    const PlatformSettings settings = m_pendingOAuthSettings.value(platform, settingsFor(platform));
    m_pendingOAuthSettings.remove(platform);
    const QString codeVerifier = m_pendingPkceVerifier.value(platform);
    m_pendingPkceVerifier.remove(platform);

    if (expectedState.isEmpty() || state != expectedState) {
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::ERROR, QStringLiteral("OAuth state mismatch"));
        m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Interactive re-auth failed"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, QStringLiteral("OAuth state mismatch"));
        return;
    }

    if (!errorCode.trimmed().isEmpty()) {
        const QString detail = errorDescription.trimmed().isEmpty() ? errorCode : (errorCode + QStringLiteral(": ") + errorDescription);
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::AUTH_REQUIRED, detail);
        m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Interactive re-auth canceled/failed"));
        m_txtEventLog->append(QStringLiteral("[OAUTH-FAIL] %1 %2").arg(platformKey(platform), detail));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, detail);
        return;
    }

    if (code.trimmed().isEmpty()) {
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::ERROR, QStringLiteral("OAuth code missing"));
        m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Interactive re-auth failed"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, QStringLiteral("OAuth code missing"));
        return;
    }
    if (platform == PlatformId::YouTube && codeVerifier.trimmed().isEmpty()) {
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::ERROR, QStringLiteral("PKCE verifier missing"));
        m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Interactive re-auth failed"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, QStringLiteral("PKCE verifier missing"));
        return;
    }

    m_configurationDialog->onTokenStateUpdated(platform, TokenState::REFRESHING, QStringLiteral("Exchanging token..."));
    if (!startAuthCodeExchangeFlow(platform, settings, code, codeVerifier, state)) {
        return;
    }
}

void MainWindow::onOAuthSessionFailed(PlatformId platform, const QString& reason)
{
    m_pendingOAuthState.remove(platform);
    m_pendingOAuthSettings.remove(platform);
    m_pendingPkceVerifier.remove(platform);
    m_configurationDialog->onTokenStateUpdated(platform, TokenState::AUTH_REQUIRED, reason);
    m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Interactive re-auth failed"));
    m_txtEventLog->append(QStringLiteral("[OAUTH-FAIL] %1 %2").arg(platformKey(platform), reason));
    appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, reason);
}

void MainWindow::onTokenGranted(PlatformId platform,
    const QString& flow,
    const QString& accessToken,
    const QString& refreshToken,
    int expiresInSec,
    int refreshExpiresInSec)
{
    const PendingTokenFlowContext ctx = m_pendingTokenFlows.value(platform);
    m_pendingTokenFlows.remove(platform);

    TokenRecord record = ctx.previousRecord;
    record.accessToken = accessToken;
    if (!refreshToken.trimmed().isEmpty()) {
        record.refreshToken = refreshToken;
    }

    if (record.refreshToken.trimmed().isEmpty()) {
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::AUTH_REQUIRED, QStringLiteral("Refresh token missing in response"));
        m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Token update failed"));
        appendTokenAudit(platform, flow, false, QStringLiteral("Refresh token missing in response"));
        return;
    }

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    record.accessExpireAtUtc = nowUtc.addSecs(expiresInSec > 0 ? expiresInSec : 3600);
    if (refreshExpiresInSec > 0) {
        record.refreshExpireAtUtc = nowUtc.addSecs(refreshExpiresInSec);
    } else if (!record.refreshExpireAtUtc.isValid()) {
        record.refreshExpireAtUtc = nowUtc.addDays(platform == PlatformId::Chzzk ? 30 : 90);
    }
    record.updatedAtUtc = nowUtc;

    if (!m_tokenVault.write(platform, record)) {
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::ERROR, QStringLiteral("Token vault write failed"));
        m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Token update failed"));
        appendTokenAudit(platform, flow, false, QStringLiteral("Token vault write failed"));
        return;
    }

    refreshTokenUi(platform);
    const bool isRefresh = flow == QStringLiteral("refresh_token");
    const QString message = isRefresh ? QStringLiteral("Silent refresh success") : QStringLiteral("Interactive re-auth success");
    m_configurationDialog->onTokenActionFinished(platform, true, message);
    appendTokenAudit(platform, flow, true, message);
    m_txtEventLog->append(QStringLiteral("[TOKEN-OK] %1 flow=%2").arg(platformKey(platform), flow));
}

void MainWindow::onTokenFailed(PlatformId platform, const QString& flow, const QString& errorCode, const QString& message, int httpStatus)
{
    m_pendingTokenFlows.remove(platform);

    const bool authRequired = errorCode == QStringLiteral("invalid_grant");
    const QString detailWithGuidance = buildTokenFailureDetailWithGuidance(platform, flow, errorCode, message, httpStatus);
    m_configurationDialog->onTokenStateUpdated(platform,
        authRequired ? TokenState::AUTH_REQUIRED : TokenState::ERROR,
        detailWithGuidance);
    m_configurationDialog->onTokenActionFinished(platform, false, flow == QStringLiteral("refresh_token")
            ? QStringLiteral("Silent refresh failed")
            : QStringLiteral("Interactive re-auth failed"));
    appendTokenAudit(platform, flow, false, detailWithGuidance);
    m_txtEventLog->append(QStringLiteral("[TOKEN-FAIL] %1 flow=%2 code=%3 http=%4 msg=%5")
                              .arg(platformKey(platform), flow, errorCode)
                              .arg(httpStatus)
                              .arg(message));
}

void MainWindow::onTokenRevoked(PlatformId platform, const QString& flow)
{
    Q_UNUSED(flow)
    m_pendingTokenRevokes.remove(platform);

    const bool localCleared = m_tokenVault.clear(platform);
    refreshTokenUi(platform);
    const QString detail = localCleared
        ? QStringLiteral("Remote token revoke succeeded. Local token deleted.")
        : QStringLiteral("Remote token revoke succeeded, but local token delete failed.");
    m_configurationDialog->onTokenActionFinished(platform, localCleared,
        localCleared ? QStringLiteral("Token revoked and deleted")
                     : QStringLiteral("Token revoked but local delete failed"));
    appendTokenAudit(platform, QStringLiteral("token_revoke"), localCleared, detail);
    m_txtEventLog->append(QStringLiteral("[TOKEN-REVOKE-OK] %1").arg(platformKey(platform)));
}

void MainWindow::onTokenRevokeFailed(PlatformId platform, const QString& flow, const QString& errorCode, const QString& message, int httpStatus)
{
    Q_UNUSED(flow)
    m_pendingTokenRevokes.remove(platform);

    const bool localCleared = m_tokenVault.clear(platform);
    refreshTokenUi(platform);
    const QString detail = QStringLiteral("%1 (http=%2, code=%3). Local token %4.")
                               .arg(message)
                               .arg(httpStatus)
                               .arg(errorCode)
                               .arg(localCleared ? QStringLiteral("deleted") : QStringLiteral("delete failed"));
    m_configurationDialog->onTokenActionFinished(platform, localCleared,
        localCleared ? QStringLiteral("Remote revoke failed, local token deleted")
                     : QStringLiteral("Remote revoke failed, local delete failed"));
    appendTokenAudit(platform, QStringLiteral("token_revoke"), false, detail);
    m_txtEventLog->append(QStringLiteral("[TOKEN-REVOKE-FAIL] %1 code=%2 http=%3 msg=%4")
                              .arg(platformKey(platform), errorCode)
                              .arg(httpStatus)
                              .arg(message));
}

void MainWindow::onConnectionStateChanged(ConnectionState state)
{
    m_lblConnectionState->setText(connectionStateText(state));
    refreshConnectButton();
    updateActionPanel();
}

void MainWindow::onConnectProgress(PlatformId platform, const QString& phase)
{
    setPlatformStatus(platform, phase);
    m_txtEventLog->append(QStringLiteral("[CONNECT] %1: %2").arg(platformKey(platform), phase));
}

void MainWindow::onConnectFinished(const ConnectSessionResult& result)
{
    for (const QString& p : result.connectedPlatforms) {
        m_txtEventLog->append(QStringLiteral("[CONNECTED] %1").arg(p));
    }

    for (auto it = result.failedPlatforms.cbegin(); it != result.failedPlatforms.cend(); ++it) {
        m_txtEventLog->append(QStringLiteral("[FAILED] %1: %2").arg(it.key(), it.value()));
    }

    refreshConnectButton();
    updateActionPanel();
}

void MainWindow::onDisconnectFinished()
{
    setPlatformStatus(PlatformId::YouTube, QStringLiteral("IDLE"));
    setPlatformStatus(PlatformId::Chzzk, QStringLiteral("IDLE"));
    m_txtEventLog->append(QStringLiteral("[DISCONNECT] completed"));
    refreshConnectButton();
    updateActionPanel();
}

void MainWindow::onWarningRaised(const QString& code, const QString& message)
{
    m_txtEventLog->append(QStringLiteral("[WARN] %1: %2").arg(code, message));
    statusBar()->showMessage(QStringLiteral("%1: %2").arg(code, message), 4000);
}

void MainWindow::setupUi()
{
    auto* root = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(root);

    auto* topLayout = new QHBoxLayout;
    m_btnConnectToggle = new QPushButton(QStringLiteral("Connect"), root);
    m_btnConnectToggle->setObjectName(QStringLiteral("btnConnectToggle"));
    m_btnToggleChatView = new QPushButton(root);
    m_btnToggleChatView->setObjectName(QStringLiteral("btnToggleChatView"));
    refreshChatViewToggleButton();

    m_lblConnectionState = new QLabel(QStringLiteral("IDLE"), root);
    m_lblConnectionState->setObjectName(QStringLiteral("lblConnectionState"));

    m_btnOpenConfiguration = new QPushButton(QStringLiteral("Configuration"), root);
    m_btnOpenConfiguration->setObjectName(QStringLiteral("btnOpenConfiguration"));

    topLayout->addWidget(m_btnConnectToggle);
    topLayout->addWidget(m_btnToggleChatView);
    topLayout->addWidget(new QLabel(QStringLiteral("State:"), root));
    topLayout->addWidget(m_lblConnectionState);
    topLayout->addStretch();
    topLayout->addWidget(m_btnOpenConfiguration);

    auto* statusLayout = new QHBoxLayout;
    m_lblYouTubeStatus = new QLabel(QStringLiteral("YouTube: IDLE"), root);
    m_lblChzzkStatus = new QLabel(QStringLiteral("CHZZK: IDLE"), root);
    statusLayout->addWidget(m_lblYouTubeStatus);
    statusLayout->addWidget(m_lblChzzkStatus);
    statusLayout->addStretch();

    m_tblChat = new QTableWidget(root);
    m_tblChat->setObjectName(QStringLiteral("tblUnifiedChat"));
    m_tblChat->verticalHeader()->setVisible(false);
    m_tblChat->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tblChat->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tblChat->setSelectionMode(QAbstractItemView::SingleSelection);
    configureChatTableForCurrentView();
    connect(m_tblChat, &QTableWidget::itemSelectionChanged, this, &MainWindow::onChatSelectionChanged);
    auto* copyShortcut = new QShortcut(QKeySequence::Copy, m_tblChat);
    connect(copyShortcut, &QShortcut::activated, this, &MainWindow::onCopySelectedChat);
    auto* copyAction = new QAction(QStringLiteral("Copy Selected Chat"), m_tblChat);
    connect(copyAction, &QAction::triggered, this, &MainWindow::onCopySelectedChat);
    m_tblChat->setContextMenuPolicy(Qt::ActionsContextMenu);
    m_tblChat->addAction(copyAction);

    auto* actionPanel = new QGroupBox(QStringLiteral("Actions"), root);
    actionPanel->setObjectName(QStringLiteral("grpActionPanel"));
    auto* actionPanelLayout = new QVBoxLayout(actionPanel);

    auto* selectedInfoLayout = new QFormLayout;
    m_lblSelectedPlatform = new QLabel(QStringLiteral("-"), actionPanel);
    m_lblSelectedAuthor = new QLabel(QStringLiteral("-"), actionPanel);
    m_lblSelectedMessage = new QLabel(QStringLiteral("-"), actionPanel);
    m_lblSelectedMessage->setWordWrap(true);
    selectedInfoLayout->addRow(QStringLiteral("Platform"), m_lblSelectedPlatform);
    selectedInfoLayout->addRow(QStringLiteral("Author"), m_lblSelectedAuthor);
    selectedInfoLayout->addRow(QStringLiteral("Message"), m_lblSelectedMessage);
    actionPanelLayout->addLayout(selectedInfoLayout);

    m_btnActionSendMessage = new QPushButton(QStringLiteral("Send Message"), actionPanel);
    m_btnActionSendMessage->setObjectName(QStringLiteral("btnActionSendMessage"));
    m_btnActionRestrictUser = new QPushButton(QStringLiteral("Restrict User"), actionPanel);
    m_btnActionRestrictUser->setObjectName(QStringLiteral("btnActionRestrictUser"));
    m_btnActionYoutubeDeleteMessage = new QPushButton(QStringLiteral("YouTube Delete Message"), actionPanel);
    m_btnActionYoutubeDeleteMessage->setObjectName(QStringLiteral("btnActionYoutubeDeleteMessage"));
    m_btnActionYoutubeTimeout = new QPushButton(QStringLiteral("YouTube Timeout 5m"), actionPanel);
    m_btnActionYoutubeTimeout->setObjectName(QStringLiteral("btnActionYoutubeTimeout"));
    m_btnActionChzzkRestrict = new QPushButton(QStringLiteral("CHZZK Add Restriction"), actionPanel);
    m_btnActionChzzkRestrict->setObjectName(QStringLiteral("btnActionChzzkRestrict"));

    actionPanelLayout->addWidget(m_btnActionSendMessage);
    actionPanelLayout->addWidget(m_btnActionRestrictUser);
    actionPanelLayout->addWidget(m_btnActionYoutubeDeleteMessage);
    actionPanelLayout->addWidget(m_btnActionYoutubeTimeout);
    actionPanelLayout->addWidget(m_btnActionChzzkRestrict);
    actionPanelLayout->addStretch();

    connect(m_btnActionSendMessage, &QPushButton::clicked, this, &MainWindow::onActionSendMessage);
    connect(m_btnActionRestrictUser, &QPushButton::clicked, this, &MainWindow::onActionRestrictUser);
    connect(m_btnActionYoutubeDeleteMessage, &QPushButton::clicked, this, &MainWindow::onActionYoutubeDeleteMessage);
    connect(m_btnActionYoutubeTimeout, &QPushButton::clicked, this, &MainWindow::onActionYoutubeTimeout);
    connect(m_btnActionChzzkRestrict, &QPushButton::clicked, this, &MainWindow::onActionChzzkRestrict);

    auto* centerLayout = new QHBoxLayout;
    centerLayout->addWidget(m_tblChat, 3);
    centerLayout->addWidget(actionPanel, 2);

    m_txtEventLog = new QTextEdit(root);
    m_txtEventLog->setReadOnly(true);
    m_txtEventLog->setObjectName(QStringLiteral("txtEventLog"));
    m_txtEventLog->setMaximumHeight(180);

    rootLayout->addLayout(topLayout);
    rootLayout->addLayout(statusLayout);
    rootLayout->addLayout(centerLayout);
    rootLayout->addWidget(m_txtEventLog);

    setCentralWidget(root);
    setWindowTitle(QStringLiteral("BotManager Qt5"));
    resize(1000, 700);

    connect(m_btnConnectToggle, &QPushButton::clicked, this, &MainWindow::onConnectToggleClicked);
    connect(m_btnToggleChatView, &QPushButton::clicked, this, &MainWindow::onToggleChatViewClicked);
    connect(m_btnOpenConfiguration, &QPushButton::clicked, this, &MainWindow::onOpenConfiguration);
}

void MainWindow::refreshConnectButton()
{
    const ConnectionState state = m_connectionCoordinator.state();

    switch (state) {
    case ConnectionState::IDLE:
    case ConnectionState::ERROR:
        m_btnConnectToggle->setText(QStringLiteral("Connect"));
        m_btnConnectToggle->setEnabled(true);
        break;
    case ConnectionState::CONNECTING:
        m_btnConnectToggle->setText(QStringLiteral("Connecting..."));
        m_btnConnectToggle->setEnabled(false);
        break;
    case ConnectionState::CONNECTED:
    case ConnectionState::PARTIALLY_CONNECTED:
        m_btnConnectToggle->setText(QStringLiteral("Disconnect"));
        m_btnConnectToggle->setEnabled(true);
        break;
    case ConnectionState::DISCONNECTING:
        m_btnConnectToggle->setText(QStringLiteral("Disconnecting..."));
        m_btnConnectToggle->setEnabled(false);
        break;
    }
}

void MainWindow::refreshChatViewToggleButton()
{
    if (!m_btnToggleChatView) {
        return;
    }
    m_btnToggleChatView->setText(
        m_chatViewMode == ChatViewMode::Messenger
            ? QStringLiteral("View: Messenger")
            : QStringLiteral("View: Table"));
}

void MainWindow::configureChatTableForCurrentView()
{
    if (!m_tblChat) {
        return;
    }

    if (m_chatViewMode == ChatViewMode::Messenger) {
        m_tblChat->setColumnCount(1);
        m_tblChat->setHorizontalHeaderLabels({ QStringLiteral("Chat") });
        m_tblChat->horizontalHeader()->setVisible(false);
        m_tblChat->horizontalHeader()->setStretchLastSection(true);
        m_tblChat->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_tblChat->setWordWrap(true);
        return;
    }

    m_tblChat->setColumnCount(4);
    m_tblChat->setHorizontalHeaderLabels({ QStringLiteral("Time"), QStringLiteral("Platform"), QStringLiteral("Author"), QStringLiteral("Message") });
    m_tblChat->horizontalHeader()->setVisible(true);
    m_tblChat->horizontalHeader()->setStretchLastSection(true);
    m_tblChat->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tblChat->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tblChat->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tblChat->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
}

void MainWindow::setPlatformStatus(PlatformId platform, const QString& statusText)
{
    if (platform == PlatformId::YouTube) {
        m_lblYouTubeStatus->setText(QStringLiteral("YouTube: %1").arg(statusText));
        return;
    }
    m_lblChzzkStatus->setText(QStringLiteral("CHZZK: %1").arg(statusText));
}

QString MainWindow::connectionStateText(ConnectionState state) const
{
    switch (state) {
    case ConnectionState::IDLE:
        return QStringLiteral("IDLE");
    case ConnectionState::CONNECTING:
        return QStringLiteral("CONNECTING");
    case ConnectionState::PARTIALLY_CONNECTED:
        return QStringLiteral("PARTIALLY_CONNECTED");
    case ConnectionState::CONNECTED:
        return QStringLiteral("CONNECTED");
    case ConnectionState::DISCONNECTING:
        return QStringLiteral("DISCONNECTING");
    case ConnectionState::ERROR:
        return QStringLiteral("ERROR");
    }
    return QStringLiteral("UNKNOWN");
}

void MainWindow::onChatReceived(const UnifiedChatMessage& message)
{
    appendChatMessage(message);
}

void MainWindow::appendChatMessage(const UnifiedChatMessage& message)
{
    m_chatMessages.push_back(message);

    const int row = m_tblChat->rowCount();
    m_tblChat->insertRow(row);
    appendChatRow(row, message);

    m_tblChat->scrollToBottom();
    updateActionPanel();
}

void MainWindow::appendChatRow(int row, const UnifiedChatMessage& message)
{
    const QString platform = platformKey(message.platform);
    const QString timeText = message.timestamp.isValid()
        ? message.timestamp.toString(QStringLiteral("HH:mm:ss"))
        : QStringLiteral("-");

    if (m_chatViewMode == ChatViewMode::Messenger) {
        const QString sender = messengerAuthorLabel(message);
        auto* item = new QTableWidgetItem(QStringLiteral("%1\n%2").arg(sender, message.text));
        item->setToolTip(QStringLiteral("%1 | %2 | %3")
                             .arg(timeText, platform, message.authorName));
        m_tblChat->setItem(row, 0, item);
        m_tblChat->resizeRowToContents(row);
        return;
    }

    m_tblChat->setItem(row, 0, new QTableWidgetItem(timeText));
    m_tblChat->setItem(row, 1, new QTableWidgetItem(platform));
    m_tblChat->setItem(row, 2, new QTableWidgetItem(message.authorName));
    m_tblChat->setItem(row, 3, new QTableWidgetItem(message.text));
}

void MainWindow::rebuildChatTable()
{
    if (!m_tblChat) {
        return;
    }

    const int previousRow = selectedChatRow();
    QSignalBlocker blocker(m_tblChat);
    m_tblChat->setRowCount(0);
    configureChatTableForCurrentView();
    for (int i = 0; i < m_chatMessages.size(); ++i) {
        m_tblChat->insertRow(i);
        appendChatRow(i, m_chatMessages.at(i));
    }
    if (previousRow >= 0 && previousRow < m_tblChat->rowCount()) {
        m_tblChat->selectRow(previousRow);
    }
    updateActionPanel();
}

QString MainWindow::messengerAuthorLabel(const UnifiedChatMessage& message) const
{
    const QString authorId = message.authorId.trimmed();
    if (!authorId.isEmpty()) {
        return authorId;
    }
    const QString authorName = message.authorName.trimmed();
    if (!authorName.isEmpty()) {
        return authorName;
    }
    return QStringLiteral("-");
}

void MainWindow::onChatSelectionChanged()
{
    updateActionPanel();
}

void MainWindow::onCopySelectedChat()
{
    if (!m_tblChat || !QGuiApplication::clipboard()) {
        return;
    }

    const int row = selectedChatRow();
    if (row < 0) {
        statusBar()->showMessage(QStringLiteral("No chat selected."), 1500);
        return;
    }

    QStringList cells;
    cells.reserve(m_tblChat->columnCount());
    for (int col = 0; col < m_tblChat->columnCount(); ++col) {
        const QTableWidgetItem* item = m_tblChat->item(row, col);
        cells.push_back(item ? item->text() : QString());
    }
    QGuiApplication::clipboard()->setText(cells.join(QStringLiteral("\t")));
    statusBar()->showMessage(QStringLiteral("Selected chat copied."), 1500);
}

void MainWindow::onActionSendMessage()
{
    executeAction(QStringLiteral("common.send_message"));
}

void MainWindow::onActionRestrictUser()
{
    executeAction(QStringLiteral("common.restrict_user"));
}

void MainWindow::onActionYoutubeDeleteMessage()
{
    executeAction(QStringLiteral("youtube.delete_message"));
}

void MainWindow::onActionYoutubeTimeout()
{
    executeAction(QStringLiteral("youtube.timeout_5m"));
}

void MainWindow::onActionChzzkRestrict()
{
    executeAction(QStringLiteral("chzzk.add_restriction"));
}

void MainWindow::updateActionPanel()
{
    const UnifiedChatMessage* msg = selectedChatMessage();
    const QMap<PlatformId, bool> connections = currentConnections();
    if (!msg) {
        m_lblSelectedPlatform->setText(QStringLiteral("-"));
        m_lblSelectedAuthor->setText(QStringLiteral("-"));
        m_lblSelectedMessage->setText(QStringLiteral("-"));

        const QString reason = QStringLiteral("Select a chat message.");
        setActionButtonState(m_btnActionSendMessage, false, reason);
        setActionButtonState(m_btnActionRestrictUser, false, reason);
        setActionButtonState(m_btnActionYoutubeDeleteMessage, false, reason);
        setActionButtonState(m_btnActionYoutubeTimeout, false, reason);
        setActionButtonState(m_btnActionChzzkRestrict, false, reason);
        return;
    }

    m_lblSelectedPlatform->setText(platformKey(msg->platform));
    m_lblSelectedAuthor->setText(msg->authorName + QStringLiteral(" (") + msg->authorId + QStringLiteral(")"));
    m_lblSelectedMessage->setText(msg->text);

    setActionButtonState(m_btnActionSendMessage, connections.value(msg->platform, false), QStringLiteral("Platform not connected."));
    setActionButtonState(m_btnActionRestrictUser, !msg->authorId.isEmpty() && connections.value(msg->platform, false), QStringLiteral("Missing author id or disconnected."));

    if (msg->platform == PlatformId::YouTube) {
        setActionButtonState(m_btnActionYoutubeDeleteMessage, !msg->messageId.isEmpty() && connections.value(PlatformId::YouTube, false), QStringLiteral("Missing message id or YouTube disconnected."));
        setActionButtonState(m_btnActionYoutubeTimeout, !msg->authorId.isEmpty() && connections.value(PlatformId::YouTube, false), QStringLiteral("Missing author id or YouTube disconnected."));
        setActionButtonState(m_btnActionChzzkRestrict, false, QStringLiteral("Not a CHZZK message."));
    } else {
        setActionButtonState(m_btnActionYoutubeDeleteMessage, false, QStringLiteral("Not a YouTube message."));
        setActionButtonState(m_btnActionYoutubeTimeout, false, QStringLiteral("Not a YouTube message."));
        setActionButtonState(m_btnActionChzzkRestrict, !msg->authorId.isEmpty() && connections.value(PlatformId::Chzzk, false), QStringLiteral("Missing author id or CHZZK disconnected."));
    }
}

void MainWindow::setActionButtonState(QPushButton* button, bool enabled, const QString& reason)
{
    if (!button) {
        return;
    }
    button->setEnabled(enabled);
    button->setToolTip(enabled ? QString() : reason);
}

const UnifiedChatMessage* MainWindow::selectedChatMessage() const
{
    const int row = selectedChatRow();
    if (row < 0 || row >= m_chatMessages.size()) {
        return nullptr;
    }
    return &m_chatMessages.at(row);
}

int MainWindow::selectedChatRow() const
{
    if (!m_tblChat || !m_tblChat->selectionModel()) {
        return -1;
    }
    const QModelIndexList rows = m_tblChat->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return -1;
    }
    return rows.first().row();
}

void MainWindow::executeAction(const QString& actionId)
{
    const UnifiedChatMessage* msg = selectedChatMessage();
    if (!msg) {
        statusBar()->showMessage(QStringLiteral("No chat selected."), 2500);
        return;
    }

    const ActionExecutionResult result = m_actionExecutor.execute(actionId, *msg, currentConnections());
    if (result.ok) {
        m_txtEventLog->append(QStringLiteral("[ACTION-OK] %1 platform=%2 author=%3 messageId=%4 msg=%5")
                                  .arg(actionId, platformKey(msg->platform), msg->authorId, msg->messageId, result.message));
        statusBar()->showMessage(QStringLiteral("Action executed: %1").arg(actionId), 2500);
        return;
    }

    m_txtEventLog->append(QStringLiteral("[ACTION-FAIL] %1 code=%2 message=%3")
                              .arg(actionId, result.errorCode, result.message));
    statusBar()->showMessage(QStringLiteral("Action failed: %1 (%2)").arg(actionId, result.errorCode), 3000);
}

void MainWindow::refreshTokenUi(PlatformId platform)
{
    TokenRecord record;
    if (!m_tokenVault.read(platform, &record)) {
        TokenRecord empty;
        m_configurationDialog->onTokenRecordUpdated(platform, TokenState::NO_TOKEN, empty, QStringLiteral("No token"));
        return;
    }

    const TokenState state = inferTokenState(&record);
    m_configurationDialog->onTokenRecordUpdated(platform, state, record, QStringLiteral("Loaded from vault"));
}

void MainWindow::refreshAllTokenUi()
{
    refreshTokenUi(PlatformId::YouTube);
    refreshTokenUi(PlatformId::Chzzk);
}

void MainWindow::tryStartupTokenRefresh()
{
    tryStartupTokenRefreshForPlatform(PlatformId::YouTube);
    tryStartupTokenRefreshForPlatform(PlatformId::Chzzk);
}

void MainWindow::tryStartupTokenRefreshForPlatform(PlatformId platform)
{
    const PlatformSettings settings = settingsFor(platform);
    if (!settings.enabled) {
        return;
    }
    if (m_pendingTokenFlows.contains(platform) || m_pendingTokenRevokes.value(platform, false)) {
        return;
    }

    TokenRecord record;
    if (!m_tokenVault.read(platform, &record)) {
        return;
    }

    const TokenState state = inferTokenState(&record);
    if (state != TokenState::EXPIRED && state != TokenState::EXPIRING_SOON) {
        return;
    }

    if (record.refreshToken.trimmed().isEmpty()) {
        const QString detail = QStringLiteral("Startup auto refresh skipped: refresh token missing.");
        m_txtEventLog->append(QStringLiteral("[TOKEN-AUTO-REFRESH-SKIP] %1 %2").arg(platformKey(platform), detail));
        appendTokenAudit(platform, QStringLiteral("token_refresh"), false, detail);
        return;
    }

    if (settings.clientId.trimmed().isEmpty() || settings.tokenEndpoint.trimmed().isEmpty()) {
        const QString detail = QStringLiteral("Startup auto refresh skipped: OAuth config incomplete.");
        m_txtEventLog->append(QStringLiteral("[TOKEN-AUTO-REFRESH-SKIP] %1 %2").arg(platformKey(platform), detail));
        appendTokenAudit(platform, QStringLiteral("token_refresh"), false, detail);
        return;
    }
    if (platform == PlatformId::Chzzk && settings.clientSecret.trimmed().isEmpty()) {
        const QString detail = QStringLiteral("Startup auto refresh skipped: CHZZK client_secret missing.");
        m_txtEventLog->append(QStringLiteral("[TOKEN-AUTO-REFRESH-SKIP] %1 %2").arg(platformKey(platform), detail));
        appendTokenAudit(platform, QStringLiteral("token_refresh"), false, detail);
        return;
    }

    m_configurationDialog->onTokenOperationStarted(platform, QStringLiteral("token_refresh"));
    m_configurationDialog->onTokenStateUpdated(platform, TokenState::REFRESHING, QStringLiteral("Startup auto refresh..."));
    if (!startTokenRefreshFlow(platform, settings, record)) {
        m_txtEventLog->append(QStringLiteral("[TOKEN-AUTO-REFRESH-FAIL] %1 start failed").arg(platformKey(platform)));
        return;
    }

    m_txtEventLog->append(QStringLiteral("[TOKEN-AUTO-REFRESH] %1 state=%2")
                              .arg(platformKey(platform),
                                  state == TokenState::EXPIRED ? QStringLiteral("EXPIRED") : QStringLiteral("EXPIRING_SOON")));
}

QMap<PlatformId, bool> MainWindow::currentConnections() const
{
    return {
        { PlatformId::YouTube, m_youtubeAdapter.isConnected() },
        { PlatformId::Chzzk, m_chzzkAdapter.isConnected() },
    };
}

PlatformSettings MainWindow::settingsFor(PlatformId platform) const
{
    return platform == PlatformId::YouTube ? m_snapshot.youtube : m_snapshot.chzzk;
}

bool MainWindow::startTokenRefreshFlow(PlatformId platform, const PlatformSettings& settings, const TokenRecord& currentRecord)
{
    PendingTokenFlowContext ctx;
    ctx.flow = QStringLiteral("refresh_token");
    ctx.settings = settings;
    ctx.previousRecord = currentRecord;
    m_pendingTokenFlows.insert(platform, ctx);

    QString errorMessage;
    if (!m_oauthTokenClient.requestRefreshToken(platform, settings, currentRecord.refreshToken, &errorMessage)) {
        m_pendingTokenFlows.remove(platform);
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::ERROR, errorMessage);
        m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Silent refresh failed"));
        return false;
    }

    return true;
}

bool MainWindow::startAuthCodeExchangeFlow(PlatformId platform,
    const PlatformSettings& settings,
    const QString& code,
    const QString& codeVerifier,
    const QString& authState)
{
    TokenRecord existing;
    m_tokenVault.read(platform, &existing);

    PendingTokenFlowContext ctx;
    ctx.flow = QStringLiteral("authorization_code");
    ctx.settings = settings;
    ctx.previousRecord = existing;
    m_pendingTokenFlows.insert(platform, ctx);

    QString errorMessage;
    if (!m_oauthTokenClient.requestAuthorizationCodeToken(platform, settings, code, codeVerifier, authState, &errorMessage)) {
        m_pendingTokenFlows.remove(platform);
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::ERROR, errorMessage);
        m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Interactive re-auth failed"));
        return false;
    }

    return true;
}

QUrl MainWindow::buildAuthorizationUrl(PlatformId platform, const PlatformSettings& settings, const QString& state, const QString& codeChallenge) const
{
    QUrl endpoint(settings.authEndpoint);
    QUrlQuery query;
    const QString normalizedClientId = platform == PlatformId::YouTube
        ? normalizeGoogleOAuthClientId(settings.clientId)
        : settings.clientId.trimmed();

    if (platform == PlatformId::YouTube) {
        query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
        query.addQueryItem(QStringLiteral("client_id"), normalizedClientId);
        query.addQueryItem(QStringLiteral("redirect_uri"), settings.redirectUri);
        query.addQueryItem(QStringLiteral("scope"), settings.scope);
        query.addQueryItem(QStringLiteral("access_type"), QStringLiteral("offline"));
        query.addQueryItem(QStringLiteral("include_granted_scopes"), QStringLiteral("true"));
        query.addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent"));
        query.addQueryItem(QStringLiteral("state"), state);
        if (!codeChallenge.trimmed().isEmpty()) {
            query.addQueryItem(QStringLiteral("code_challenge"), codeChallenge);
            query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
        }
    } else {
        query.addQueryItem(QStringLiteral("clientId"), normalizedClientId);
        query.addQueryItem(QStringLiteral("redirectUri"), settings.redirectUri);
        query.addQueryItem(QStringLiteral("state"), state);
    }

    endpoint.setQuery(query);
    return endpoint;
}

QString MainWindow::createOAuthState(PlatformId platform) const
{
    return QStringLiteral("%1_%2").arg(platformKey(platform), QUuid::createUuid().toString(QUuid::WithoutBraces));
}

void MainWindow::appendTokenAudit(PlatformId platform, const QString& action, bool ok, const QString& detail)
{
    if (!m_configurationDialog) {
        return;
    }
    m_configurationDialog->onTokenAuditAppended(platform, action, ok, detail);
}

TokenState MainWindow::inferTokenState(const TokenRecord* record) const
{
    if (!record) {
        return TokenState::NO_TOKEN;
    }
    if (record->accessToken.trimmed().isEmpty() && record->refreshToken.trimmed().isEmpty()) {
        return TokenState::NO_TOKEN;
    }
    if (!record->accessExpireAtUtc.isValid()) {
        return record->accessToken.trimmed().isEmpty() ? TokenState::EXPIRED : TokenState::VALID;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (now >= record->accessExpireAtUtc) {
        return TokenState::EXPIRED;
    }
    if (now.secsTo(record->accessExpireAtUtc) <= 600) {
        return TokenState::EXPIRING_SOON;
    }
    return TokenState::VALID;
}
