#include "ui/MainWindow.h"

#include "auth/PkceUtil.h"
#include "ui/ConfigurationDialog.h"

#include <algorithm>
#include <QAction>
#include <QAbstractItemView>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QFrame>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequence>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QTimer>
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

QString readJsonStringByKeys(const QJsonObject& primary, const QJsonObject& fallback, const QStringList& keys)
{
    for (const QString& key : keys) {
        const QString v1 = primary.value(key).toString().trimmed();
        if (!v1.isEmpty()) {
            return v1;
        }
        const QString v2 = fallback.value(key).toString().trimmed();
        if (!v2.isEmpty()) {
            return v2;
        }
    }
    return QString();
}

bool isQuotaExceededMessage(const QString& message)
{
    const QString normalized = message.trimmed().toLower();
    return normalized.contains(QStringLiteral("quota"))
        || normalized.contains(QStringLiteral("rate limit"))
        || normalized.contains(QStringLiteral("userrate"))
        || normalized.contains(QStringLiteral("daily limit"));
}

bool tryPlatformFromCodePrefix(const QString& code, PlatformId* outPlatform, QString* outInnerCode)
{
    const int sep = code.indexOf(QLatin1Char(':'));
    if (sep <= 0) {
        return false;
    }
    const QString head = code.left(sep).trimmed().toLower();
    const QString inner = code.mid(sep + 1).trimmed();
    if (head == QStringLiteral("youtube")) {
        if (outPlatform) {
            *outPlatform = PlatformId::YouTube;
        }
        if (outInnerCode) {
            *outInnerCode = inner;
        }
        return true;
    }
    if (head == QStringLiteral("chzzk")) {
        if (outPlatform) {
            *outPlatform = PlatformId::Chzzk;
        }
        if (outInnerCode) {
            *outInnerCode = inner;
        }
        return true;
    }
    return false;
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
    m_chatterListDialog = new ChatterListDialog(this);
    connect(m_chatterListDialog, &ChatterListDialog::resetRequested, this, &MainWindow::onResetChatterList);

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
    setPlatformRuntimePhase(PlatformId::YouTube, QStringLiteral("IDLE"));
    setPlatformRuntimePhase(PlatformId::Chzzk, QStringLiteral("IDLE"));
    refreshConnectButton();
    refreshAllTokenUi();
    tryStartupTokenRefresh();
    refreshPlatformIndicators();
    updateActionPanel();
    initializeLiveProbe();
    m_apiStatusReconcileTimer = new QTimer(this);
    m_apiStatusReconcileTimer->setInterval(1000);
    connect(m_apiStatusReconcileTimer, &QTimer::timeout, this, &MainWindow::reconcileApiStatus);
    m_apiStatusReconcileTimer->start();
    reconcileApiStatus();
}

void MainWindow::onConnectToggleClicked()
{
    switch (m_connectionCoordinator.state()) {
    case ConnectionState::IDLE:
    case ConnectionState::ERROR:
        m_snapshot = m_settings.load();
        m_connectionCoordinator.connectAll(buildRuntimeConnectSnapshot(m_snapshot));
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

void MainWindow::onOpenChatterList()
{
    refreshChatterListDialog();
    m_chatterListDialog->show();
    m_chatterListDialog->raise();
    m_chatterListDialog->activateWindow();
}

void MainWindow::onResetChatterList()
{
    m_chatterStats.clear();
    refreshChatterListDialog();
    statusBar()->showMessage(QStringLiteral("Chatter list reset."), 2000);
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
    onLiveProbeTimeout();
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

    m_authInProgress.insert(platform, true);
    setPlatformRuntimePhase(platform, QStringLiteral("STARTING"));
    clearPlatformRuntimeError(platform);
    m_configurationDialog->onTokenOperationStarted(platform, QStringLiteral("interactive_auth"));
    m_configurationDialog->onTokenStateUpdated(platform, TokenState::REFRESHING, QStringLiteral("Browser opened. Waiting callback..."));
    m_txtEventLog->append(QStringLiteral("[OAUTH] %1 browser auth started").arg(platformKey(platform)));
    reconcileApiStatus();
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
        setLiveBroadcastState(platform, LiveBroadcastState::UNKNOWN, QStringLiteral("Token missing"));
        return;
    }

    if (record.accessToken.trimmed().isEmpty() && record.refreshToken.trimmed().isEmpty()) {
        const bool cleared = m_tokenVault.clear(platform);
        refreshTokenUi(platform);
        const QString detail = cleared ? QStringLiteral("Token already empty. Local token state cleared.")
                                       : QStringLiteral("Token already empty. Local clear failed.");
        m_configurationDialog->onTokenActionFinished(platform, cleared, detail);
        appendTokenAudit(platform, QStringLiteral("token_revoke"), cleared, detail);
        setLiveBroadcastState(platform, LiveBroadcastState::UNKNOWN, QStringLiteral("Token empty"));
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
        if (localCleared) {
            setLiveBroadcastState(platform, LiveBroadcastState::UNKNOWN, QStringLiteral("Token deleted locally"));
        }
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
        m_authInProgress.insert(platform, false);
        setPlatformRuntimeError(platform, QStringLiteral("OAUTH_STATE_MISMATCH"), QStringLiteral("OAuth state mismatch"));
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::ERROR, QStringLiteral("OAuth state mismatch"));
        m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Interactive re-auth failed"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, QStringLiteral("OAuth state mismatch"));
        reconcileApiStatus();
        return;
    }

    if (!errorCode.trimmed().isEmpty()) {
        m_authInProgress.insert(platform, false);
        setPlatformRuntimeError(platform, errorCode, errorDescription);
        const QString detail = errorDescription.trimmed().isEmpty() ? errorCode : (errorCode + QStringLiteral(": ") + errorDescription);
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::AUTH_REQUIRED, detail);
        m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Interactive re-auth canceled/failed"));
        m_txtEventLog->append(QStringLiteral("[OAUTH-FAIL] %1 %2").arg(platformKey(platform), detail));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, detail);
        reconcileApiStatus();
        return;
    }

    if (code.trimmed().isEmpty()) {
        m_authInProgress.insert(platform, false);
        setPlatformRuntimeError(platform, QStringLiteral("OAUTH_CODE_MISSING"), QStringLiteral("OAuth code missing"));
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::ERROR, QStringLiteral("OAuth code missing"));
        m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Interactive re-auth failed"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, QStringLiteral("OAuth code missing"));
        reconcileApiStatus();
        return;
    }
    if (platform == PlatformId::YouTube && codeVerifier.trimmed().isEmpty()) {
        m_authInProgress.insert(platform, false);
        setPlatformRuntimeError(platform, QStringLiteral("PKCE_MISSING"), QStringLiteral("PKCE verifier missing"));
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::ERROR, QStringLiteral("PKCE verifier missing"));
        m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Interactive re-auth failed"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, QStringLiteral("PKCE verifier missing"));
        reconcileApiStatus();
        return;
    }

    m_configurationDialog->onTokenStateUpdated(platform, TokenState::REFRESHING, QStringLiteral("Exchanging token..."));
    if (!startAuthCodeExchangeFlow(platform, settings, code, codeVerifier, state)) {
        m_authInProgress.insert(platform, false);
        setPlatformRuntimeError(platform, QStringLiteral("AUTH_CODE_EXCHANGE_FAILED"), QStringLiteral("Failed to request token exchange"));
        reconcileApiStatus();
        return;
    }
}

void MainWindow::onOAuthSessionFailed(PlatformId platform, const QString& reason)
{
    m_authInProgress.insert(platform, false);
    setPlatformRuntimeError(platform, QStringLiteral("OAUTH_SESSION_FAILED"), reason);
    m_pendingOAuthState.remove(platform);
    m_pendingOAuthSettings.remove(platform);
    m_pendingPkceVerifier.remove(platform);
    m_configurationDialog->onTokenStateUpdated(platform, TokenState::AUTH_REQUIRED, reason);
    m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Interactive re-auth failed"));
    m_txtEventLog->append(QStringLiteral("[OAUTH-FAIL] %1 %2").arg(platformKey(platform), reason));
    appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, reason);
    reconcileApiStatus();
}

void MainWindow::onTokenGranted(PlatformId platform,
    const QString& flow,
    const QString& accessToken,
    const QString& refreshToken,
    int expiresInSec,
    int refreshExpiresInSec)
{
    if (flow == QStringLiteral("authorization_code")) {
        m_authInProgress.insert(platform, false);
    }
    const PendingTokenFlowContext ctx = m_pendingTokenFlows.value(platform);
    m_pendingTokenFlows.remove(platform);

    TokenRecord record = ctx.previousRecord;
    record.accessToken = accessToken;
    if (!refreshToken.trimmed().isEmpty()) {
        record.refreshToken = refreshToken;
    }

    if (record.refreshToken.trimmed().isEmpty()) {
        setPlatformRuntimeError(platform, QStringLiteral("TOKEN_REFRESH_MISSING"), QStringLiteral("Refresh token missing in response"));
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
        setPlatformRuntimeError(platform, QStringLiteral("TOKEN_VAULT_WRITE_FAILED"), QStringLiteral("Token vault write failed"));
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::ERROR, QStringLiteral("Token vault write failed"));
        m_configurationDialog->onTokenActionFinished(platform, false, QStringLiteral("Token update failed"));
        appendTokenAudit(platform, flow, false, QStringLiteral("Token vault write failed"));
        return;
    }

    clearPlatformRuntimeError(platform);
    refreshTokenUi(platform);
    const bool isRefresh = flow == QStringLiteral("refresh_token");
    const QString message = isRefresh ? QStringLiteral("Silent refresh success") : QStringLiteral("Interactive re-auth success");
    m_configurationDialog->onTokenActionFinished(platform, true, message);
    appendTokenAudit(platform, flow, true, message);
    m_txtEventLog->append(QStringLiteral("[TOKEN-OK] %1 flow=%2").arg(platformKey(platform), flow));
    reconcileApiStatus();
    if (platform == PlatformId::YouTube) {
        m_nextYouTubeLiveProbeAllowedAtUtc = QDateTime();
        m_nextPeriodicYouTubeProbeAtUtc = QDateTime();
    }
    if (platform == PlatformId::YouTube
        && (flow == QStringLiteral("authorization_code") || flow == QStringLiteral("refresh_token"))) {
        syncYouTubeProfileFromAccessToken(accessToken);
    }
    if (platform == PlatformId::Chzzk
        && (flow == QStringLiteral("authorization_code") || flow == QStringLiteral("refresh_token"))) {
        syncChzzkProfileFromAccessToken(accessToken);
    }
    onLiveProbeTimeout();
}

void MainWindow::onTokenFailed(PlatformId platform, const QString& flow, const QString& errorCode, const QString& message, int httpStatus)
{
    m_pendingTokenFlows.remove(platform);
    if (flow == QStringLiteral("authorization_code")) {
        m_authInProgress.insert(platform, false);
    }

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
    setPlatformRuntimeError(platform, errorCode, detailWithGuidance);
    reconcileApiStatus();
    if (errorCode == QStringLiteral("invalid_grant")) {
        setLiveBroadcastState(platform, LiveBroadcastState::ERROR, QStringLiteral("Token invalid"));
    }
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
    setPlatformRuntimePhase(platform, QStringLiteral("IDLE"));
    clearPlatformRuntimeError(platform);
    reconcileApiStatus();
    setLiveBroadcastState(platform, LiveBroadcastState::UNKNOWN, QStringLiteral("Token revoked"));
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
    setPlatformRuntimeError(platform, errorCode, message);
    reconcileApiStatus();
    if (localCleared) {
        setLiveBroadcastState(platform, LiveBroadcastState::UNKNOWN, QStringLiteral("Token deleted locally"));
    }
}

void MainWindow::onConnectionStateChanged(ConnectionState state)
{
    m_lblConnectionState->setText(connectionStateText(state));
    refreshConnectButton();
    reconcileApiStatus();
    updateActionPanel();
}

void MainWindow::onConnectProgress(PlatformId platform, const QString& phase)
{
    setPlatformStatus(platform, phase);
    setPlatformRuntimePhase(platform, phase);
    const QString normalized = phase.trimmed().toUpper();
    if (normalized == QStringLiteral("STARTING")) {
        clearPlatformRuntimeError(platform);
    } else if (normalized == QStringLiteral("CONNECTED")) {
        clearPlatformRuntimeError(platform);
    } else if (normalized == QStringLiteral("FAILED")) {
        setPlatformRuntimeError(platform, QStringLiteral("CONNECT_FAILED"), QStringLiteral("Connect failed"));
    }
    m_txtEventLog->append(QStringLiteral("[CONNECT] %1: %2").arg(platformKey(platform), phase));
    reconcileApiStatus();
}

void MainWindow::onConnectFinished(const ConnectSessionResult& result)
{
    for (const QString& p : result.connectedPlatforms) {
        m_txtEventLog->append(QStringLiteral("[CONNECTED] %1").arg(p));
        if (p == QStringLiteral("youtube")) {
            setPlatformRuntimePhase(PlatformId::YouTube, QStringLiteral("CONNECTED"));
            clearPlatformRuntimeError(PlatformId::YouTube);
        } else if (p == QStringLiteral("chzzk")) {
            setPlatformRuntimePhase(PlatformId::Chzzk, QStringLiteral("CONNECTED"));
            clearPlatformRuntimeError(PlatformId::Chzzk);
        }
    }

    for (auto it = result.failedPlatforms.cbegin(); it != result.failedPlatforms.cend(); ++it) {
        m_txtEventLog->append(QStringLiteral("[FAILED] %1: %2").arg(it.key(), it.value()));
        if (it.key() == QStringLiteral("youtube")) {
            setPlatformRuntimePhase(PlatformId::YouTube, QStringLiteral("FAILED"));
            setPlatformRuntimeError(PlatformId::YouTube, QStringLiteral("CONNECT_FAILED"), it.value());
        } else if (it.key() == QStringLiteral("chzzk")) {
            setPlatformRuntimePhase(PlatformId::Chzzk, QStringLiteral("FAILED"));
            setPlatformRuntimeError(PlatformId::Chzzk, QStringLiteral("CONNECT_FAILED"), it.value());
        }
    }

    refreshConnectButton();
    reconcileApiStatus();
    updateActionPanel();
}

void MainWindow::onDisconnectFinished()
{
    setPlatformStatus(PlatformId::YouTube, QStringLiteral("IDLE"));
    setPlatformStatus(PlatformId::Chzzk, QStringLiteral("IDLE"));
    setPlatformRuntimePhase(PlatformId::YouTube, QStringLiteral("IDLE"));
    setPlatformRuntimePhase(PlatformId::Chzzk, QStringLiteral("IDLE"));
    clearPlatformRuntimeError(PlatformId::YouTube);
    clearPlatformRuntimeError(PlatformId::Chzzk);
    m_txtEventLog->append(QStringLiteral("[DISCONNECT] completed"));
    refreshConnectButton();
    reconcileApiStatus();
    updateActionPanel();
}

void MainWindow::onWarningRaised(const QString& code, const QString& message)
{
    m_txtEventLog->append(QStringLiteral("[WARN] %1: %2").arg(code, message));
    PlatformId platform = PlatformId::YouTube;
    QString innerCode;
    if (tryPlatformFromCodePrefix(code, &platform, &innerCode)) {
        if (!innerCode.startsWith(QStringLiteral("TRACE_")) && !innerCode.startsWith(QStringLiteral("INFO_"))) {
            setPlatformRuntimeError(platform, innerCode, message);
            reconcileApiStatus();
        }
    }
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

    m_btnOpenChatterList = new QPushButton(QStringLiteral("ChatterList"), root);
    m_btnOpenChatterList->setObjectName(QStringLiteral("btnOpenChatterList"));
    m_btnOpenConfiguration = new QPushButton(QStringLiteral("Configuration"), root);
    m_btnOpenConfiguration->setObjectName(QStringLiteral("btnOpenConfiguration"));

    topLayout->addWidget(m_btnConnectToggle);
    topLayout->addWidget(m_btnToggleChatView);
    topLayout->addWidget(new QLabel(QStringLiteral("State:"), root));
    topLayout->addWidget(m_lblConnectionState);
    topLayout->addStretch();
    topLayout->addWidget(m_btnOpenChatterList);
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

    auto* platformIndicatorLayout = new QHBoxLayout;
    m_boxYouTubeRuntime = new QFrame(actionPanel);
    m_boxYouTubeRuntime->setObjectName(QStringLiteral("ytStatusBox"));
    m_boxYouTubeRuntime->setFixedSize(18, 18);
    auto* lblYouTubeRuntime = new QLabel(QStringLiteral("YouTube"), actionPanel);
    m_boxChzzkRuntime = new QFrame(actionPanel);
    m_boxChzzkRuntime->setObjectName(QStringLiteral("chzStatusBox"));
    m_boxChzzkRuntime->setFixedSize(18, 18);
    auto* lblChzzkRuntime = new QLabel(QStringLiteral("CHZZK"), actionPanel);
    platformIndicatorLayout->addWidget(m_boxYouTubeRuntime);
    platformIndicatorLayout->addWidget(lblYouTubeRuntime);
    platformIndicatorLayout->addSpacing(12);
    platformIndicatorLayout->addWidget(m_boxChzzkRuntime);
    platformIndicatorLayout->addWidget(lblChzzkRuntime);
    platformIndicatorLayout->addStretch();
    actionPanelLayout->addLayout(platformIndicatorLayout);

    auto* liveIndicatorLayout = new QHBoxLayout;
    m_boxYouTubeLive = new QFrame(actionPanel);
    m_boxYouTubeLive->setObjectName(QStringLiteral("ytLiveBox"));
    m_boxYouTubeLive->setFixedSize(18, 18);
    m_lblYouTubeLive = new QLabel(QStringLiteral("YouTube Live: UNKNOWN"), actionPanel);
    m_boxChzzkLive = new QFrame(actionPanel);
    m_boxChzzkLive->setObjectName(QStringLiteral("chzLiveBox"));
    m_boxChzzkLive->setFixedSize(18, 18);
    m_lblChzzkLive = new QLabel(QStringLiteral("CHZZK Live: UNKNOWN"), actionPanel);
    liveIndicatorLayout->addWidget(m_boxYouTubeLive);
    liveIndicatorLayout->addWidget(m_lblYouTubeLive);
    liveIndicatorLayout->addSpacing(12);
    liveIndicatorLayout->addWidget(m_boxChzzkLive);
    liveIndicatorLayout->addWidget(m_lblChzzkLive);
    liveIndicatorLayout->addStretch();
    actionPanelLayout->addLayout(liveIndicatorLayout);

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

    auto* legendWrap = new QWidget(actionPanel);
    legendWrap->setObjectName(QStringLiteral("statusLegend"));
    auto* legendGrid = new QGridLayout(legendWrap);
    legendGrid->setContentsMargins(0, 0, 0, 0);
    legendGrid->setHorizontalSpacing(10);
    legendGrid->setVerticalSpacing(4);

    auto addLegendItem = [legendWrap, legendGrid](int row, int col, const QString& color, const QString& label) {
        auto* chip = new QFrame(legendWrap);
        chip->setFixedSize(12, 12);
        chip->setStyleSheet(QStringLiteral("background-color:%1; border:1px solid #666666; border-radius:2px;").arg(color));
        auto* text = new QLabel(label, legendWrap);
        const int base = col * 2;
        legendGrid->addWidget(chip, row, base);
        legendGrid->addWidget(text, row, base + 1);
    };

    addLegendItem(0, 0, QStringLiteral("#FFB74D"), QStringLiteral("인증중"));
    addLegendItem(0, 1, QStringLiteral("#81D4FA"), QStringLiteral("온라인"));
    addLegendItem(1, 0, QStringLiteral("#66BB6A"), QStringLiteral("토큰정상"));
    addLegendItem(1, 1, QStringLiteral("#EF5350"), QStringLiteral("토큰비정상"));

    actionPanelLayout->addWidget(legendWrap);

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
    connect(m_btnOpenChatterList, &QPushButton::clicked, this, &MainWindow::onOpenChatterList);
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
        m_tblChat->setShowGrid(false);
        m_tblChat->setFrameShape(QFrame::NoFrame);
        m_tblChat->setStyleSheet(QStringLiteral(
            "QTableWidget#tblUnifiedChat {"
            " border: none;"
            " gridline-color: transparent;"
            "}"
            "QTableWidget#tblUnifiedChat::item {"
            " border: none;"
            "}"));
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
    m_tblChat->setShowGrid(true);
    m_tblChat->setFrameShape(QFrame::StyledPanel);
    m_tblChat->setStyleSheet(QString());
}

void MainWindow::setPlatformStatus(PlatformId platform, const QString& statusText)
{
    if (platform == PlatformId::YouTube) {
        m_lblYouTubeStatus->setText(QStringLiteral("YouTube: %1").arg(statusText));
        return;
    }
    m_lblChzzkStatus->setText(QStringLiteral("CHZZK: %1").arg(statusText));
}

void MainWindow::setPlatformRuntimePhase(PlatformId platform, const QString& phase)
{
    PlatformRuntimeState state = m_platformRuntimeStates.value(platform);
    state.phase = phase.trimmed();
    state.updatedAtUtc = QDateTime::currentDateTimeUtc();
    m_platformRuntimeStates.insert(platform, state);
}

void MainWindow::setPlatformRuntimeError(PlatformId platform, const QString& code, const QString& message)
{
    PlatformRuntimeState state = m_platformRuntimeStates.value(platform);
    state.lastErrorCode = code.trimmed();
    state.lastErrorMessage = message.trimmed();
    state.updatedAtUtc = QDateTime::currentDateTimeUtc();
    if (state.phase.isEmpty()) {
        state.phase = QStringLiteral("FAILED");
    }
    m_platformRuntimeStates.insert(platform, state);
}

void MainWindow::clearPlatformRuntimeError(PlatformId platform)
{
    PlatformRuntimeState state = m_platformRuntimeStates.value(platform);
    state.lastErrorCode.clear();
    state.lastErrorMessage.clear();
    state.updatedAtUtc = QDateTime::currentDateTimeUtc();
    m_platformRuntimeStates.insert(platform, state);
}

void MainWindow::reconcileApiStatus()
{
    const ConnectionState connState = m_connectionCoordinator.state();
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();

    for (PlatformId platform : { PlatformId::YouTube, PlatformId::Chzzk }) {
        const bool connected = platform == PlatformId::YouTube
            ? m_youtubeAdapter.isConnected()
            : m_chzzkAdapter.isConnected();
        const bool authBusy = m_authInProgress.value(platform, false);

        PlatformRuntimeState runtime = m_platformRuntimeStates.value(platform);
        const QString phase = runtime.phase.trimmed().toUpper();

        if (connected) {
            if (phase != QStringLiteral("CONNECTED")) {
                setPlatformRuntimePhase(platform, QStringLiteral("CONNECTED"));
            }
            if (!runtime.lastErrorCode.trimmed().isEmpty() || !runtime.lastErrorMessage.trimmed().isEmpty()) {
                clearPlatformRuntimeError(platform);
            }
            continue;
        }

        if (authBusy) {
            if (phase != QStringLiteral("STARTING")) {
                setPlatformRuntimePhase(platform, QStringLiteral("STARTING"));
            }
            continue;
        }

        if (connState == ConnectionState::CONNECTING) {
            if (phase.isEmpty() || phase == QStringLiteral("IDLE")) {
                setPlatformRuntimePhase(platform, QStringLiteral("STARTING"));
            }
        } else if (connState == ConnectionState::DISCONNECTING || connState == ConnectionState::IDLE) {
            if (phase == QStringLiteral("STARTING") || phase == QStringLiteral("CONNECTING") || phase == QStringLiteral("CONNECTED")) {
                setPlatformRuntimePhase(platform, QStringLiteral("IDLE"));
                clearPlatformRuntimeError(platform);
            }
        }

        // transient warning/error fallback: auto-clear stale runtime errors after 30s if not FAILED phase.
        runtime = m_platformRuntimeStates.value(platform);
        const qint64 ageSec = runtime.updatedAtUtc.isValid() ? runtime.updatedAtUtc.secsTo(nowUtc) : 0;
        if ((!runtime.lastErrorCode.trimmed().isEmpty() || !runtime.lastErrorMessage.trimmed().isEmpty())
            && runtime.phase.trimmed().toUpper() != QStringLiteral("FAILED")
            && ageSec >= 30) {
            clearPlatformRuntimeError(platform);
        }
    }

    refreshPlatformIndicators();
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
    recordChatter(message);
    appendChatMessage(message);
    m_txtEventLog->append(QStringLiteral("[CHAT] %1 author=%2 text=%3")
                              .arg(platformKey(message.platform),
                                  message.authorName.isEmpty() ? message.authorId : message.authorName,
                                  message.text.left(120)));
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

void MainWindow::recordChatter(const UnifiedChatMessage& message)
{
    QString nickname = message.authorName.trimmed();
    if (nickname.isEmpty()) {
        nickname = message.authorId.trimmed();
    }
    if (nickname.isEmpty()) {
        nickname = QStringLiteral("-");
    }

    const QString key = QStringLiteral("%1|%2").arg(platformKey(message.platform), nickname);
    ChatterListEntry entry = m_chatterStats.value(key);
    if (entry.count == 0) {
        entry.platform = message.platform;
        entry.nickname = nickname;
    }
    ++entry.count;
    entry.lastSeen = message.timestamp.isValid() ? message.timestamp : QDateTime::currentDateTime();
    m_chatterStats.insert(key, entry);

    if (m_chatterListDialog && m_chatterListDialog->isVisible()) {
        refreshChatterListDialog();
    }
}

void MainWindow::refreshChatterListDialog()
{
    if (!m_chatterListDialog) {
        return;
    }

    QVector<ChatterListEntry> rows;
    rows.reserve(m_chatterStats.size());
    for (auto it = m_chatterStats.cbegin(); it != m_chatterStats.cend(); ++it) {
        rows.push_back(it.value());
    }

    std::sort(rows.begin(), rows.end(), [](const ChatterListEntry& a, const ChatterListEntry& b) {
        if (a.count != b.count) {
            return a.count > b.count;
        }
        if (a.lastSeen != b.lastSeen) {
            return a.lastSeen > b.lastSeen;
        }
        if (a.platform != b.platform) {
            return platformKey(a.platform) < platformKey(b.platform);
        }
        return a.nickname < b.nickname;
    });

    m_chatterListDialog->setEntries(rows);
}

void MainWindow::appendChatRow(int row, const UnifiedChatMessage& message)
{
    const QString platform = platformKey(message.platform);
    const QString timeText = message.timestamp.isValid()
        ? message.timestamp.toString(QStringLiteral("HH:mm:ss"))
        : QStringLiteral("-");
    const QString authorDisplay = messengerAuthorLabel(message);

    if (m_chatViewMode == ChatViewMode::Messenger) {
        const QString copyText = QStringLiteral("%1\n%2").arg(authorDisplay, message.text);
        auto* item = new QTableWidgetItem(QString());
        item->setData(Qt::UserRole, copyText);
        item->setToolTip(QStringLiteral("%1 | %2 | %3")
                             .arg(timeText, platform, authorDisplay));
        m_tblChat->setItem(row, 0, item);
        QWidget* widget = buildMessengerCellWidget(message, authorDisplay);
        m_tblChat->setCellWidget(row, 0, widget);
        m_tblChat->setRowHeight(row, qMax(42, widget->sizeHint().height() + 4));
        return;
    }

    m_tblChat->setItem(row, 0, new QTableWidgetItem(timeText));
    m_tblChat->setItem(row, 1, new QTableWidgetItem(platform));
    m_tblChat->setItem(row, 2, new QTableWidgetItem(authorDisplay));
    m_tblChat->setItem(row, 3, new QTableWidgetItem(message.text));
}

QWidget* MainWindow::buildMessengerCellWidget(const UnifiedChatMessage& message, const QString& authorDisplay) const
{
    auto* wrap = new QWidget(m_tblChat);
    wrap->setObjectName(QStringLiteral("chatBubbleWrap"));
    wrap->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    wrap->setFocusPolicy(Qt::NoFocus);

    auto* layout = new QVBoxLayout(wrap);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(1);
    layout->setAlignment(Qt::AlignTop);

    auto* badge = new QLabel(wrap);
    const int badgeSize = message.platform == PlatformId::Chzzk ? 18 : 22;
    badge->setFixedSize(badgeSize, badgeSize);
    badge->setAlignment(Qt::AlignCenter);
    badge->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    badge->setFocusPolicy(Qt::NoFocus);
    badge->setStyleSheet(message.platform == PlatformId::YouTube
            ? QStringLiteral("background:#E53935; color:#ffffff; border-radius:%1px; font-weight:700; font-size:12px;")
                  .arg(badgeSize / 2)
            : QStringLiteral("background:#16C784; color:#101010; border-radius:%1px; font-weight:700; font-size:10px;")
                  .arg(badgeSize / 2));
    badge->setText(message.platform == PlatformId::YouTube ? QStringLiteral("▶") : QStringLiteral("Z"));
    auto* badgeWrap = new QWidget(wrap);
    auto* badgeWrapLayout = new QVBoxLayout(badgeWrap);
    badgeWrapLayout->setContentsMargins(0, 1, 0, 0); // icon 1px down
    badgeWrapLayout->setSpacing(0);
    badgeWrapLayout->addWidget(badge, 0, Qt::AlignTop);

    auto* lblAuthor = new QLabel(authorDisplay.toHtmlEscaped(), wrap);
    lblAuthor->setTextFormat(Qt::RichText);
    lblAuthor->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    lblAuthor->setFocusPolicy(Qt::NoFocus);
    lblAuthor->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    lblAuthor->setStyleSheet(message.platform == PlatformId::YouTube
            ? QStringLiteral("color:#6A3FA0; font-weight:700; font-size:19px;")
            : QStringLiteral("color:#D17A00; font-weight:700; font-size:19px;"));

    auto* lblMessage = new QLabel(message.text.toHtmlEscaped(), wrap);
    lblMessage->setWordWrap(true);
    lblMessage->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    lblMessage->setFocusPolicy(Qt::NoFocus);
    lblMessage->setStyleSheet(QStringLiteral("color:#111111; font-size:18px; font-weight:600;"));

    auto* headLayout = new QHBoxLayout;
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(8);
    headLayout->addWidget(badgeWrap, 0, Qt::AlignVCenter);
    headLayout->addWidget(lblAuthor, 0, Qt::AlignVCenter);
    headLayout->addStretch();

    auto* bodyLayout = new QHBoxLayout;
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addSpacing(badgeSize + 8);
    bodyLayout->addWidget(lblMessage, 1, Qt::AlignTop);

    layout->addLayout(headLayout);
    layout->addLayout(bodyLayout);
    return wrap;
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
    const QString authorName = message.authorName.trimmed();
    if (!authorName.isEmpty()) {
        return authorName;
    }
    const QString authorId = message.authorId.trimmed();
    if (!authorId.isEmpty()) {
        return authorId;
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
        QString value = item ? item->text() : QString();
        if (value.isEmpty() && item) {
            value = item->data(Qt::UserRole).toString();
        }
        cells.push_back(value);
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
    const QString authorDisplay = messengerAuthorLabel(*msg);
    if (!msg->authorId.trimmed().isEmpty() && authorDisplay != msg->authorId.trimmed()) {
        m_lblSelectedAuthor->setText(QStringLiteral("%1 (%2)").arg(authorDisplay, msg->authorId));
    } else {
        m_lblSelectedAuthor->setText(authorDisplay);
    }
    m_lblSelectedMessage->setText(msg->text);

    const bool platformReady = connections.value(msg->platform, false) && isPlatformLiveOnline(msg->platform);
    setActionButtonState(m_btnActionSendMessage, platformReady, QStringLiteral("Platform disconnected or live is offline."));
    setActionButtonState(m_btnActionRestrictUser, !msg->authorId.isEmpty() && platformReady, QStringLiteral("Missing author id or platform not ready."));

    if (msg->platform == PlatformId::YouTube) {
        const bool youtubeReady = connections.value(PlatformId::YouTube, false) && isPlatformLiveOnline(PlatformId::YouTube);
        setActionButtonState(m_btnActionYoutubeDeleteMessage, !msg->messageId.isEmpty() && youtubeReady, QStringLiteral("Missing message id or YouTube not ready."));
        setActionButtonState(m_btnActionYoutubeTimeout, !msg->authorId.isEmpty() && youtubeReady, QStringLiteral("Missing author id or YouTube not ready."));
        setActionButtonState(m_btnActionChzzkRestrict, false, QStringLiteral("Not a CHZZK message."));
    } else {
        setActionButtonState(m_btnActionYoutubeDeleteMessage, false, QStringLiteral("Not a YouTube message."));
        setActionButtonState(m_btnActionYoutubeTimeout, false, QStringLiteral("Not a YouTube message."));
        const bool chzzkReady = connections.value(PlatformId::Chzzk, false) && isPlatformLiveOnline(PlatformId::Chzzk);
        setActionButtonState(m_btnActionChzzkRestrict, !msg->authorId.isEmpty() && chzzkReady, QStringLiteral("Missing author id or CHZZK not ready."));
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
        refreshPlatformIndicator(platform);
        return;
    }

    const TokenState state = inferTokenState(&record);
    m_configurationDialog->onTokenRecordUpdated(platform, state, record, QStringLiteral("Loaded from vault"));
    refreshPlatformIndicator(platform);
}

void MainWindow::refreshAllTokenUi()
{
    refreshTokenUi(PlatformId::YouTube);
    refreshTokenUi(PlatformId::Chzzk);
}

void MainWindow::refreshPlatformIndicators()
{
    refreshPlatformIndicator(PlatformId::YouTube);
    refreshPlatformIndicator(PlatformId::Chzzk);
}

void MainWindow::refreshPlatformIndicator(PlatformId platform)
{
    QFrame* indicator = platform == PlatformId::YouTube ? m_boxYouTubeRuntime : m_boxChzzkRuntime;
    if (!indicator) {
        return;
    }
    applyPlatformIndicatorStyle(indicator, platform, resolvePlatformVisualState(platform));
}

MainWindow::PlatformVisualState MainWindow::resolvePlatformVisualState(PlatformId platform) const
{
    if (m_authInProgress.value(platform, false)) {
        return PlatformVisualState::AUTH_IN_PROGRESS;
    }

    const PlatformRuntimeState runtime = m_platformRuntimeStates.value(platform);
    const QString phase = runtime.phase.trimmed().toUpper();
    if (phase == QStringLiteral("STARTING") || phase == QStringLiteral("CONNECTING")) {
        return PlatformVisualState::AUTH_IN_PROGRESS;
    }

    const bool online = platform == PlatformId::YouTube
        ? m_youtubeAdapter.isConnected()
        : m_chzzkAdapter.isConnected();
    if (online) {
        return PlatformVisualState::ONLINE;
    }
    if (phase == QStringLiteral("FAILED")) {
        return PlatformVisualState::TOKEN_BAD;
    }
    if (!runtime.lastErrorCode.trimmed().isEmpty() || !runtime.lastErrorMessage.trimmed().isEmpty()) {
        return PlatformVisualState::TOKEN_BAD;
    }

    TokenRecord record;
    if (!m_tokenVault.read(platform, &record)) {
        return PlatformVisualState::TOKEN_BAD;
    }

    const TokenState state = inferTokenState(&record);
    if (state == TokenState::VALID || state == TokenState::EXPIRING_SOON) {
        return PlatformVisualState::TOKEN_OK;
    }
    return PlatformVisualState::TOKEN_BAD;
}

void MainWindow::applyPlatformIndicatorStyle(QFrame* indicator, PlatformId platform, PlatformVisualState state)
{
    if (!indicator) {
        return;
    }

    const PlatformRuntimeState runtime = m_platformRuntimeStates.value(platform);

    QString color = QStringLiteral("#EF5350");
    QString stateText = QStringLiteral("TOKEN_BAD");
    switch (state) {
    case PlatformVisualState::AUTH_IN_PROGRESS:
        color = QStringLiteral("#FFB74D");
        stateText = QStringLiteral("AUTH_IN_PROGRESS");
        break;
    case PlatformVisualState::ONLINE:
        color = QStringLiteral("#81D4FA");
        stateText = QStringLiteral("ONLINE");
        break;
    case PlatformVisualState::TOKEN_OK:
        color = QStringLiteral("#66BB6A");
        stateText = QStringLiteral("TOKEN_OK");
        break;
    case PlatformVisualState::TOKEN_BAD:
        break;
    }

    indicator->setStyleSheet(QStringLiteral("background-color:%1; border:1px solid #666666; border-radius:2px;").arg(color));
    QString tooltip = QStringLiteral("%1: %2").arg(platform == PlatformId::YouTube ? QStringLiteral("YouTube") : QStringLiteral("CHZZK"), stateText);
    if (!runtime.phase.trimmed().isEmpty()) {
        tooltip += QStringLiteral("\nphase=%1").arg(runtime.phase);
    }
    if (!runtime.lastErrorCode.trimmed().isEmpty() || !runtime.lastErrorMessage.trimmed().isEmpty()) {
        tooltip += QStringLiteral("\nerror=%1 %2").arg(runtime.lastErrorCode, runtime.lastErrorMessage);
    }
    indicator->setToolTip(tooltip);
}

void MainWindow::initializeLiveProbe()
{
    m_liveStates.insert(PlatformId::YouTube, LiveBroadcastState::UNKNOWN);
    m_liveStates.insert(PlatformId::Chzzk, LiveBroadcastState::UNKNOWN);
    refreshLiveBroadcastIndicators();

    m_liveProbeTimer = new QTimer(this);
    m_liveProbeTimer->setInterval(3000);
    connect(m_liveProbeTimer, &QTimer::timeout, this, &MainWindow::onLiveProbeTimeout);
    m_liveProbeTimer->start();
    onLiveProbeTimeout();
}

void MainWindow::onLiveProbeTimeout()
{
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    if (!m_nextPeriodicYouTubeProbeAtUtc.isValid() || nowUtc >= m_nextPeriodicYouTubeProbeAtUtc) {
        probeLiveStatus(PlatformId::YouTube);
        m_nextPeriodicYouTubeProbeAtUtc = nowUtc.addSecs(60);
    }
    if (!m_nextPeriodicChzzkProbeAtUtc.isValid() || nowUtc >= m_nextPeriodicChzzkProbeAtUtc) {
        probeLiveStatus(PlatformId::Chzzk);
        m_nextPeriodicChzzkProbeAtUtc = nowUtc.addSecs(10);
    }
}

void MainWindow::probeLiveStatus(PlatformId platform)
{
    if (platform == PlatformId::YouTube
        && m_nextYouTubeLiveProbeAllowedAtUtc.isValid()
        && QDateTime::currentDateTimeUtc() < m_nextYouTubeLiveProbeAllowedAtUtc) {
        return;
    }

    const PlatformSettings settings = settingsFor(platform);
    if (!settings.enabled) {
        setLiveBroadcastState(platform, LiveBroadcastState::UNKNOWN, QStringLiteral("Disabled"));
        return;
    }

    TokenRecord record;
    if (!m_tokenVault.read(platform, &record) || record.accessToken.trimmed().isEmpty()) {
        setLiveBroadcastState(platform, LiveBroadcastState::UNKNOWN, QStringLiteral("No access token"));
        return;
    }

    const TokenState tokenState = inferTokenState(&record);
    if (tokenState == TokenState::EXPIRED || tokenState == TokenState::NO_TOKEN || tokenState == TokenState::AUTH_REQUIRED) {
        setLiveBroadcastState(platform, LiveBroadcastState::ERROR, QStringLiteral("Token unavailable"));
        return;
    }

    if (m_liveStates.value(platform, LiveBroadcastState::UNKNOWN) == LiveBroadcastState::UNKNOWN) {
        setLiveBroadcastState(platform, LiveBroadcastState::CHECKING, QStringLiteral("Checking live status"));
    }

    if (platform == PlatformId::YouTube) {
        m_nextPeriodicYouTubeProbeAtUtc = QDateTime::currentDateTimeUtc().addSecs(60);
        if (!m_pendingYouTubeLiveProbe) {
            probeYouTubeLiveStatus(record.accessToken);
        }
        return;
    }

    m_nextPeriodicChzzkProbeAtUtc = QDateTime::currentDateTimeUtc().addSecs(10);
    if (!m_pendingChzzkLiveProbe) {
        probeChzzkLiveStatus(record.accessToken);
    }
}

void MainWindow::probeYouTubeLiveStatus(const QString& accessToken)
{
    const QString token = accessToken.trimmed();
    if (token.isEmpty()) {
        setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::UNKNOWN, QStringLiteral("No access token"));
        return;
    }

    QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/liveBroadcasts"));
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("id,snippet,status"));
    query.addQueryItem(QStringLiteral("mine"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("50"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(token).toUtf8());

    QNetworkReply* reply = m_networkAccessManager.get(req);
    if (!reply) {
        setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::ERROR, QStringLiteral("Failed to create liveBroadcasts request"));
        return;
    }
    m_pendingYouTubeLiveProbe = true;

    connect(reply, &QNetworkReply::finished, this, [this, reply, token]() {
        m_pendingYouTubeLiveProbe = false;

        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();

        QJsonObject obj;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            obj = doc.object();
        }

        const QJsonObject error = obj.value(QStringLiteral("error")).toObject();
        const QString apiMessage = error.value(QStringLiteral("message")).toString().trimmed();
        const int apiCode = error.value(QStringLiteral("code")).toInt();
        const bool hasApiCode = error.value(QStringLiteral("code")).isDouble();

        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        if (reply->error() != QNetworkReply::NoError || !httpOk) {
            const QString detail = QStringLiteral("http=%1 apiCode=%2 msg=%3")
                                       .arg(httpStatus)
                                       .arg(hasApiCode ? QString::number(apiCode) : QStringLiteral("-"))
                                       .arg(apiMessage.isEmpty() ? reply->errorString() : apiMessage);
            if (isQuotaExceededMessage(detail)) {
                m_nextYouTubeLiveProbeAllowedAtUtc = QDateTime::currentDateTimeUtc().addSecs(300);
                setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::ERROR,
                    QStringLiteral("%1 (cooldown 300s)").arg(detail));
                reply->deleteLater();
                return;
            }
            const QString channelId = m_snapshot.youtube.channelId.trimmed();
            if (!token.isEmpty() && !channelId.isEmpty()) {
                probeYouTubeLiveStatusBySearch(token);
            } else if (!token.isEmpty()) {
                syncYouTubeProfileFromAccessToken(token);
                setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::CHECKING, QStringLiteral("channel_id missing (syncing profile)"));
            } else {
                setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::ERROR, detail);
            }
            reply->deleteLater();
            return;
        }

        const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
        QString liveTitle;
        bool isLive = false;
        for (const QJsonValue& v : items) {
            const QJsonObject item = v.toObject();
            const QJsonObject status = item.value(QStringLiteral("status")).toObject();
            const QString lifeCycle = status.value(QStringLiteral("lifeCycleStatus")).toString().trimmed().toLower();
            if (lifeCycle != QStringLiteral("live") && lifeCycle != QStringLiteral("live_starting")) {
                continue;
            }
            const QJsonObject snippet = item.value(QStringLiteral("snippet")).toObject();
            liveTitle = snippet.value(QStringLiteral("title")).toString().trimmed();
            isLive = true;
            break;
        }

        if (!isLive) {
            m_nextYouTubeLiveProbeAllowedAtUtc = QDateTime();
            setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::OFFLINE, QStringLiteral("No active broadcast"));
            reply->deleteLater();
            return;
        }
        m_nextYouTubeLiveProbeAllowedAtUtc = QDateTime();
        setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::ONLINE,
            liveTitle.isEmpty() ? QStringLiteral("Active broadcast detected") : liveTitle);
        reply->deleteLater();
    });
}

void MainWindow::probeYouTubeLiveStatusBySearch(const QString& accessToken)
{
    const QString token = accessToken.trimmed();
    const QString channelId = m_snapshot.youtube.channelId.trimmed();
    if (token.isEmpty() || channelId.isEmpty()) {
        setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::UNKNOWN, QStringLiteral("channel_id missing"));
        return;
    }

    QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/search"));
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("id,snippet"));
    query.addQueryItem(QStringLiteral("channelId"), channelId);
    query.addQueryItem(QStringLiteral("eventType"), QStringLiteral("live"));
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("video"));
    query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("1"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(token).toUtf8());
    QNetworkReply* reply = m_networkAccessManager.get(req);
    if (!reply) {
        setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::ERROR, QStringLiteral("Failed to create search request"));
        return;
    }
    m_pendingYouTubeLiveProbe = true;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_pendingYouTubeLiveProbe = false;
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();

        QJsonObject obj;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            obj = doc.object();
        }

        const QJsonObject error = obj.value(QStringLiteral("error")).toObject();
        const QString apiMessage = error.value(QStringLiteral("message")).toString().trimmed();
        const int apiCode = error.value(QStringLiteral("code")).toInt();
        const bool hasApiCode = error.value(QStringLiteral("code")).isDouble();

        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        if (reply->error() != QNetworkReply::NoError || !httpOk) {
            const QString detail = QStringLiteral("fallback http=%1 apiCode=%2 msg=%3")
                                       .arg(httpStatus)
                                       .arg(hasApiCode ? QString::number(apiCode) : QStringLiteral("-"))
                                       .arg(apiMessage.isEmpty() ? reply->errorString() : apiMessage);
            if (isQuotaExceededMessage(detail)) {
                m_nextYouTubeLiveProbeAllowedAtUtc = QDateTime::currentDateTimeUtc().addSecs(300);
                setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::ERROR,
                    QStringLiteral("%1 (cooldown 300s)").arg(detail));
                reply->deleteLater();
                return;
            }
            setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::ERROR, detail);
            reply->deleteLater();
            return;
        }

        const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
        if (items.isEmpty()) {
            m_nextYouTubeLiveProbeAllowedAtUtc = QDateTime();
            setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::OFFLINE, QStringLiteral("No live search result"));
            reply->deleteLater();
            return;
        }

        const QJsonObject first = items.first().toObject();
        const QJsonObject snippet = first.value(QStringLiteral("snippet")).toObject();
        const QString title = snippet.value(QStringLiteral("title")).toString().trimmed();
        m_nextYouTubeLiveProbeAllowedAtUtc = QDateTime();
        setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::ONLINE,
            title.isEmpty() ? QStringLiteral("Live search result found") : title);
        reply->deleteLater();
    });
}

void MainWindow::probeChzzkLiveStatus(const QString& accessToken)
{
    const QString channelId = m_snapshot.chzzk.channelId.trimmed();
    if (channelId.isEmpty()) {
        const QString token = accessToken.trimmed();
        if (!token.isEmpty()) {
            syncChzzkProfileFromAccessToken(token);
        }
        setLiveBroadcastState(PlatformId::Chzzk, LiveBroadcastState::UNKNOWN, QStringLiteral("channel_id missing (syncing profile)"));
        return;
    }

    QUrl url(QStringLiteral("https://api.chzzk.naver.com/service/v3/channels/%1/live-detail")
                 .arg(QString::fromUtf8(QUrl::toPercentEncoding(channelId))));
    QNetworkRequest req(url);

    QNetworkReply* reply = m_networkAccessManager.get(req);
    if (!reply) {
        setLiveBroadcastState(PlatformId::Chzzk, LiveBroadcastState::ERROR, QStringLiteral("Failed to create live-detail request"));
        return;
    }
    m_pendingChzzkLiveProbe = true;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_pendingChzzkLiveProbe = false;

        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();

        QJsonObject obj;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            obj = doc.object();
        }

        const QJsonObject content = obj.value(QStringLiteral("content")).toObject();
        const QJsonObject payload = content.isEmpty() ? obj : content;
        const QString status = readJsonStringByKeys(payload, obj, { QStringLiteral("status"), QStringLiteral("liveStatus"), QStringLiteral("broadcastStatus") });
        const QString liveTitle = readJsonStringByKeys(payload, obj, { QStringLiteral("liveTitle"), QStringLiteral("title") });
        const QString apiMessage = readJsonStringByKeys(payload, obj, { QStringLiteral("message"), QStringLiteral("error") });

        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        if (reply->error() != QNetworkReply::NoError || !httpOk) {
            const QString detail = QStringLiteral("http=%1 msg=%2")
                                       .arg(httpStatus)
                                       .arg(apiMessage.isEmpty() ? reply->errorString() : apiMessage);
            setLiveBroadcastState(PlatformId::Chzzk, LiveBroadcastState::ERROR, detail);
            reply->deleteLater();
            return;
        }

        bool isLive = false;
        const QString normalized = status.trimmed().toUpper();
        if (!normalized.isEmpty()) {
            isLive = normalized.contains(QStringLiteral("OPEN"))
                || normalized.contains(QStringLiteral("LIVE"))
                || normalized.contains(QStringLiteral("ONAIR"))
                || normalized == QStringLiteral("ON");
        }
        if (!isLive) {
            isLive = payload.value(QStringLiteral("openLive")).toBool()
                || payload.value(QStringLiteral("open_live")).toBool()
                || payload.value(QStringLiteral("isLive")).toBool()
                || payload.value(QStringLiteral("live")).toBool();
        }

        if (isLive) {
            setLiveBroadcastState(PlatformId::Chzzk, LiveBroadcastState::ONLINE,
                liveTitle.isEmpty() ? QStringLiteral("Active broadcast detected") : liveTitle);
        } else {
            setLiveBroadcastState(PlatformId::Chzzk, LiveBroadcastState::OFFLINE,
                status.isEmpty() ? QStringLiteral("No active broadcast") : status);
        }
        reply->deleteLater();
    });
}

void MainWindow::setLiveBroadcastState(PlatformId platform, LiveBroadcastState state, const QString& detail)
{
    const LiveBroadcastState previous = m_liveStates.value(platform, LiveBroadcastState::UNKNOWN);
    const QString previousDetail = m_liveStateDetails.value(platform);
    m_liveStates.insert(platform, state);
    m_liveStateDetails.insert(platform, detail);
    refreshLiveBroadcastIndicator(platform);
    updateActionPanel();

    if (previous != state || previousDetail != detail) {
        m_txtEventLog->append(QStringLiteral("[LIVE] %1 state=%2 detail=%3")
                                  .arg(platformKey(platform), liveBroadcastStateText(state), detail));
    }
}

QString MainWindow::liveBroadcastStateText(LiveBroadcastState state) const
{
    switch (state) {
    case LiveBroadcastState::UNKNOWN:
        return QStringLiteral("UNKNOWN");
    case LiveBroadcastState::CHECKING:
        return QStringLiteral("CHECKING");
    case LiveBroadcastState::ONLINE:
        return QStringLiteral("ONLINE");
    case LiveBroadcastState::OFFLINE:
        return QStringLiteral("OFFLINE");
    case LiveBroadcastState::ERROR:
        return QStringLiteral("ERROR");
    }
    return QStringLiteral("UNKNOWN");
}

void MainWindow::refreshLiveBroadcastIndicators()
{
    refreshLiveBroadcastIndicator(PlatformId::YouTube);
    refreshLiveBroadcastIndicator(PlatformId::Chzzk);
}

void MainWindow::refreshLiveBroadcastIndicator(PlatformId platform)
{
    QFrame* box = platform == PlatformId::YouTube ? m_boxYouTubeLive : m_boxChzzkLive;
    QLabel* label = platform == PlatformId::YouTube ? m_lblYouTubeLive : m_lblChzzkLive;
    applyLiveBroadcastIndicatorStyle(box, label, platform);
}

void MainWindow::applyLiveBroadcastIndicatorStyle(QFrame* indicator, QLabel* label, PlatformId platform)
{
    if (!indicator || !label) {
        return;
    }

    const LiveBroadcastState state = m_liveStates.value(platform, LiveBroadcastState::UNKNOWN);
    const QString detail = m_liveStateDetails.value(platform).trimmed();

    QString color = QStringLiteral("#9E9E9E");
    switch (state) {
    case LiveBroadcastState::UNKNOWN:
        color = QStringLiteral("#9E9E9E");
        break;
    case LiveBroadcastState::CHECKING:
        color = QStringLiteral("#FFB74D");
        break;
    case LiveBroadcastState::ONLINE:
        color = QStringLiteral("#4FC3F7");
        break;
    case LiveBroadcastState::OFFLINE:
        color = QStringLiteral("#EF5350");
        break;
    case LiveBroadcastState::ERROR:
        color = QStringLiteral("#8E24AA");
        break;
    }

    indicator->setStyleSheet(QStringLiteral("background-color:%1; border:1px solid #666666; border-radius:2px;").arg(color));

    const QString stateText = liveBroadcastStateText(state);
    const QString platformText = platform == PlatformId::YouTube ? QStringLiteral("YouTube") : QStringLiteral("CHZZK");
    label->setText(QStringLiteral("%1 Live: %2").arg(platformText, stateText));
    const QString tooltip = detail.isEmpty()
        ? QStringLiteral("%1 live state: %2").arg(platformText, stateText)
        : QStringLiteral("%1 live state: %2\n%3").arg(platformText, stateText, detail);
    label->setToolTip(tooltip);
    indicator->setToolTip(tooltip);
}

bool MainWindow::isPlatformLiveOnline(PlatformId platform) const
{
    return m_liveStates.value(platform, LiveBroadcastState::UNKNOWN) == LiveBroadcastState::ONLINE;
}

void MainWindow::syncYouTubeProfileFromAccessToken(const QString& accessToken)
{
    const QString token = accessToken.trimmed();
    if (token.isEmpty()) {
        return;
    }
    if (m_pendingYouTubeProfileSync) {
        return;
    }

    QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/channels"));
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("id,snippet"));
    query.addQueryItem(QStringLiteral("mine"), QStringLiteral("true"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(token).toUtf8());

    QNetworkReply* reply = m_networkAccessManager.get(req);
    if (!reply) {
        m_txtEventLog->append(QStringLiteral("[YOUTUBE-PROFILE-FAIL] Failed to create channels request"));
        return;
    }
    m_pendingYouTubeProfileSync = true;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_pendingYouTubeProfileSync = false;

        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();

        QJsonObject obj;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            obj = doc.object();
        }

        const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
        const QJsonObject first = items.isEmpty() ? QJsonObject() : items.first().toObject();
        const QString channelId = first.value(QStringLiteral("id")).toString().trimmed();
        const QJsonObject snippet = first.value(QStringLiteral("snippet")).toObject();
        const QString channelName = snippet.value(QStringLiteral("title")).toString().trimmed();
        const QString channelHandle = snippet.value(QStringLiteral("customUrl")).toString().trimmed();

        QString apiMessage;
        int apiCode = 0;
        bool hasApiCode = false;
        const QJsonObject error = obj.value(QStringLiteral("error")).toObject();
        if (!error.isEmpty()) {
            apiMessage = error.value(QStringLiteral("message")).toString().trimmed();
            if (error.value(QStringLiteral("code")).isDouble()) {
                apiCode = error.value(QStringLiteral("code")).toInt();
                hasApiCode = true;
            }
        }

        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        if (reply->error() != QNetworkReply::NoError || !httpOk || channelId.isEmpty()) {
            const QString detail = QStringLiteral("http=%1 apiCode=%2 msg=%3")
                                       .arg(httpStatus)
                                       .arg(hasApiCode ? QString::number(apiCode) : QStringLiteral("-"))
                                       .arg(apiMessage.isEmpty() ? reply->errorString() : apiMessage);
            m_txtEventLog->append(QStringLiteral("[YOUTUBE-PROFILE-FAIL] %1").arg(detail));
            reply->deleteLater();
            return;
        }

        AppSettingsSnapshot updated = m_snapshot;
        bool changed = false;
        if (updated.youtube.channelId != channelId) {
            updated.youtube.channelId = channelId;
            changed = true;
        }
        if (!channelName.isEmpty() && updated.youtube.channelName != channelName) {
            updated.youtube.channelName = channelName;
            changed = true;
        }
        if (!channelHandle.isEmpty() && updated.youtube.accountLabel != channelHandle) {
            updated.youtube.accountLabel = channelHandle;
            changed = true;
        } else if (!channelName.isEmpty() && updated.youtube.accountLabel != channelName) {
            updated.youtube.accountLabel = channelName;
            changed = true;
        }

        if (!changed) {
            m_txtEventLog->append(QStringLiteral("[YOUTUBE-PROFILE] channels.mine synchronized (no changes)"));
            reply->deleteLater();
            return;
        }

        if (!m_settings.save(updated)) {
            m_txtEventLog->append(QStringLiteral("[YOUTUBE-PROFILE-FAIL] Failed to persist channel profile to config/app.ini"));
            reply->deleteLater();
            return;
        }

        m_snapshot = updated;
        if (m_configurationDialog) {
            m_configurationDialog->setSnapshot(m_snapshot);
        }

        const QString synced = QStringLiteral("channelId=%1 handle=%2 channelName=%3")
                                   .arg(updated.youtube.channelId,
                                       updated.youtube.accountLabel.isEmpty() ? QStringLiteral("-") : updated.youtube.accountLabel,
                                       updated.youtube.channelName.isEmpty() ? QStringLiteral("-") : updated.youtube.channelName);
        m_txtEventLog->append(QStringLiteral("[YOUTUBE-PROFILE] channels.mine synchronized %1").arg(synced));
        statusBar()->showMessage(QStringLiteral("YouTube profile synchronized from channels.mine"), 3000);
        reply->deleteLater();
    });
}

void MainWindow::syncChzzkProfileFromAccessToken(const QString& accessToken)
{
    const QString token = accessToken.trimmed();
    if (token.isEmpty()) {
        return;
    }
    if (m_pendingChzzkProfileSync) {
        return;
    }

    QNetworkRequest req(QUrl(QStringLiteral("https://openapi.chzzk.naver.com/open/v1/users/me")));
    req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(token).toUtf8());
    if (!m_snapshot.chzzk.clientId.trimmed().isEmpty()) {
        req.setRawHeader("Client-Id", m_snapshot.chzzk.clientId.trimmed().toUtf8());
    }
    if (!m_snapshot.chzzk.clientSecret.trimmed().isEmpty()) {
        req.setRawHeader("Client-Secret", m_snapshot.chzzk.clientSecret.trimmed().toUtf8());
    }

    QNetworkReply* reply = m_networkAccessManager.get(req);
    if (!reply) {
        m_txtEventLog->append(QStringLiteral("[CHZZK-PROFILE-FAIL] Failed to create users/me request"));
        return;
    }
    m_pendingChzzkProfileSync = true;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_pendingChzzkProfileSync = false;

        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();

        QJsonObject obj;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            obj = doc.object();
        }
        const QJsonObject content = obj.value(QStringLiteral("content")).toObject();
        const QJsonObject payload = content.isEmpty() ? obj : content;

        const QString channelId = readJsonStringByKeys(payload, obj, { QStringLiteral("channelId"), QStringLiteral("channel_id") });
        const QString channelName = readJsonStringByKeys(payload, obj, { QStringLiteral("channelName"), QStringLiteral("channel_name") });
        const QString apiMessage = readJsonStringByKeys(payload, obj, { QStringLiteral("message"), QStringLiteral("error") });

        int apiCode = 0;
        bool hasApiCode = false;
        if (obj.value(QStringLiteral("code")).isDouble()) {
            apiCode = obj.value(QStringLiteral("code")).toInt();
            hasApiCode = true;
        } else if (obj.value(QStringLiteral("code")).isString()) {
            bool ok = false;
            const int parsed = obj.value(QStringLiteral("code")).toString().toInt(&ok);
            if (ok) {
                apiCode = parsed;
                hasApiCode = true;
            }
        }

        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        const bool apiOk = !hasApiCode || apiCode == 200;
        if (reply->error() != QNetworkReply::NoError || !httpOk || !apiOk || channelId.isEmpty()) {
            const QString detail = QStringLiteral("http=%1 apiCode=%2 msg=%3")
                                       .arg(httpStatus)
                                       .arg(hasApiCode ? QString::number(apiCode) : QStringLiteral("-"))
                                       .arg(apiMessage.isEmpty() ? reply->errorString() : apiMessage);
            m_txtEventLog->append(QStringLiteral("[CHZZK-PROFILE-FAIL] %1").arg(detail));
            reply->deleteLater();
            return;
        }

        AppSettingsSnapshot updated = m_snapshot;
        bool changed = false;
        if (updated.chzzk.channelId != channelId) {
            updated.chzzk.channelId = channelId;
            changed = true;
        }
        if (!channelName.isEmpty() && updated.chzzk.channelName != channelName) {
            updated.chzzk.channelName = channelName;
            changed = true;
        }
        if (!channelName.isEmpty() && updated.chzzk.accountLabel != channelName) {
            updated.chzzk.accountLabel = channelName;
            changed = true;
        }

        if (!changed) {
            m_txtEventLog->append(QStringLiteral("[CHZZK-PROFILE] users/me synchronized (no changes)"));
            reply->deleteLater();
            return;
        }

        if (!m_settings.save(updated)) {
            m_txtEventLog->append(QStringLiteral("[CHZZK-PROFILE-FAIL] Failed to persist channel profile to config/app.ini"));
            reply->deleteLater();
            return;
        }

        m_snapshot = updated;
        if (m_configurationDialog) {
            m_configurationDialog->setSnapshot(m_snapshot);
        }

        const QString synced = QStringLiteral("channelId=%1 channelName=%2")
                                   .arg(updated.chzzk.channelId,
                                       updated.chzzk.channelName.isEmpty() ? QStringLiteral("-") : updated.chzzk.channelName);
        m_txtEventLog->append(QStringLiteral("[CHZZK-PROFILE] users/me synchronized %1").arg(synced));
        statusBar()->showMessage(QStringLiteral("CHZZK profile synchronized from users/me"), 3000);
        reply->deleteLater();
    });
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

AppSettingsSnapshot MainWindow::buildRuntimeConnectSnapshot(const AppSettingsSnapshot& base) const
{
    AppSettingsSnapshot snapshot = base;
    TokenRecord ytRecord;
    if (m_tokenVault.read(PlatformId::YouTube, &ytRecord)) {
        snapshot.youtube.runtimeAccessToken = ytRecord.accessToken.trimmed();
    } else {
        snapshot.youtube.runtimeAccessToken.clear();
    }

    TokenRecord chzRecord;
    if (m_tokenVault.read(PlatformId::Chzzk, &chzRecord)) {
        snapshot.chzzk.runtimeAccessToken = chzRecord.accessToken.trimmed();
    } else {
        snapshot.chzzk.runtimeAccessToken.clear();
    }
    return snapshot;
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
