#include "ui/MainWindow.h"

#include "auth/PkceUtil.h"
#include "i18n/AppLanguage.h"
#include "ui/ConfigurationDialog.h"

#include <algorithm>
#include <QAction>
#include <QAbstractItemView>
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QEvent>
#include <QFrame>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QKeySequence>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStringList>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWidget>
#include <QHeaderView>

namespace {
QString mainWindowText(const char* sourceText)
{
    return QCoreApplication::translate("MainWindow", sourceText);
}

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
        return mainWindowText("YouTube client_id format invalid. Parsed value is empty.");
    }
    if (!parsedClientId.contains(QStringLiteral("googleusercontent.com"), Qt::CaseInsensitive)
        && parsedClientId.contains('.')) {
        return mainWindowText(
            "YouTube client_id format invalid. parsed=%1 (looks like bundle/package id). "
            "Use OAuth client_id ending with *.googleusercontent.com")
            .arg(parsedClientId);
    }
    return mainWindowText("YouTube client_id format invalid. expected=*.googleusercontent.com parsed=%1")
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
    const QString base = mainWindowText("%1 (http=%2)").arg(message).arg(httpStatus);
    if (platform != PlatformId::YouTube || flow != QStringLiteral("authorization_code")) {
        return base;
    }
    if (!looksLikeYouTubeClientSecretMismatch(errorCode, message)) {
        return base;
    }

    return base + mainWindowText(
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

QString extractYouTubeErrorReason(const QJsonObject& root)
{
    const QJsonObject error = root.value(QStringLiteral("error")).toObject();
    const QJsonArray errors = error.value(QStringLiteral("errors")).toArray();
    if (!errors.isEmpty() && errors.first().isObject()) {
        return errors.first().toObject().value(QStringLiteral("reason")).toString().trimmed();
    }
    return QString();
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

bool isDetailWarningCode(const QString& code)
{
    const int sep = code.indexOf(QLatin1Char(':'));
    const QString innerCode = (sep >= 0 ? code.mid(sep + 1) : code).trimmed().toUpper();
    return innerCode.startsWith(QStringLiteral("TRACE_"))
        || innerCode.startsWith(QStringLiteral("INFO_"));
}
} // namespace

MainWindow::MainWindow(const QString& configDir, QWidget* parent)
    : QMainWindow(parent)
    , m_settings(configDir + QStringLiteral("/app.ini"))
    , m_tokenVault(configDir + QStringLiteral("/tokens.ini"))
    , m_oauthTokenClient(&m_networkAccessManager, this)
    , m_connectionCoordinator(this)
    , m_youtubeAdapter(this)
    , m_chzzkAdapter(this)
{
    setupUi();

    m_snapshot = m_settings.load();
    m_detailLogEnabled = m_snapshot.detailLogEnabled;
    m_youtubeAuthorLookupTimer = new QTimer(this);
    m_youtubeAuthorLookupTimer->setSingleShot(true);
    connect(m_youtubeAuthorLookupTimer, &QTimer::timeout,
        this, &MainWindow::flushYouTubeAuthorHandleLookupQueue);

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

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_edtComposer && event && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        const Qt::KeyboardModifiers mods = keyEvent->modifiers();
        const int key = keyEvent->key();

        if ((key == Qt::Key_Return || key == Qt::Key_Enter) && mods == Qt::NoModifier) {
            onActionSendMessage();
            return true;
        }
        if (key == Qt::Key_Space && mods == Qt::ShiftModifier) {
            if (m_edtComposer) {
                m_edtComposer->insertPlainText(QStringLiteral("\n"));
            }
            return true;
        }
        if (key == Qt::Key_Up && mods == Qt::ControlModifier) {
            onComposerHistoryPrevRequested();
            return true;
        }
        if (key == Qt::Key_Down && mods == Qt::ControlModifier) {
            onComposerHistoryNextRequested();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event && event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::onConnectToggleClicked()
{
    switch (m_connectionCoordinator.state()) {
    case ConnectionState::IDLE:
    case ConnectionState::ERROR:
        m_snapshot = m_settings.load();
        m_detailLogEnabled = m_snapshot.detailLogEnabled;
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
            ? tr("Chat view: Messenger")
            : tr("Chat view: Table"),
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
    statusBar()->showMessage(tr("Chatter list reset."), 2000);
}

void MainWindow::onConfigApplyRequested(const AppSettingsSnapshot& snapshot)
{
    if (!m_settings.save(snapshot)) {
        QMessageBox::warning(this, tr("Configuration"), tr("Failed to save config/app.ini"));
        return;
    }

    const bool languageChanged = AppLanguage::normalizeLanguage(m_snapshot.language) != AppLanguage::normalizeLanguage(snapshot.language);
    const bool reopenConfiguration = m_configurationDialog && m_configurationDialog->isVisible();
    const bool reopenChatterList = m_chatterListDialog && m_chatterListDialog->isVisible();

    const bool chatFontChanged = m_snapshot.chatFontFamily != snapshot.chatFontFamily
        || m_snapshot.chatFontSize != snapshot.chatFontSize
        || m_snapshot.chatFontBold != snapshot.chatFontBold
        || m_snapshot.chatFontItalic != snapshot.chatFontItalic
        || m_snapshot.chatLineSpacing != snapshot.chatLineSpacing;
    m_snapshot = snapshot;
    m_detailLogEnabled = snapshot.detailLogEnabled;
    if (chatFontChanged) {
        rebuildChatTable();
    }
    m_txtEventLog->append(QStringLiteral("[CONFIG] Applied and saved."));
    if (languageChanged) {
        QString languageError;
        AppLanguage::applyLanguage(*qApp, snapshot.language, &languageError);
        retranslateUi();
        recreateAuxiliaryDialogs(reopenConfiguration, reopenChatterList);
        const QString message = languageError.isEmpty()
            ? tr("Configuration updated. Language applied to the UI.")
            : tr("Configuration updated, but translation load failed: %1").arg(languageError);
        statusBar()->showMessage(message, 5000);
    } else {
        statusBar()->showMessage(tr("Configuration updated. Reconnect to apply running session."), 4000);
    }
    onLiveProbeTimeout();
}

void MainWindow::onTokenRefreshRequested(PlatformId platform, const PlatformSettings& settings)
{
    if (m_pendingTokenFlows.contains(platform)) {
        m_configurationDialog->onTokenActionFinished(platform, false, tr("Another token operation is in progress"));
        appendTokenAudit(platform, QStringLiteral("token_refresh"), false, tr("Another token operation is in progress"));
        return;
    }

    TokenRecord record;
    if (!m_tokenVault.read(platform, &record) || record.refreshToken.trimmed().isEmpty()) {
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::AUTH_REQUIRED, tr("No refresh token. Browser re-auth required."));
        m_configurationDialog->onTokenActionFinished(platform, false, tr("Refresh token not found"));
        appendTokenAudit(platform, QStringLiteral("token_refresh"), false, tr("Refresh token not found"));
        return;
    }

    m_configurationDialog->onTokenOperationStarted(platform, QStringLiteral("refresh_token"));
    m_configurationDialog->onTokenStateUpdated(platform, TokenState::REFRESHING, tr("Refreshing token..."));
    if (!startTokenRefreshFlow(platform, settings, record)) {
        return;
    }
}

void MainWindow::onInteractiveAuthRequested(PlatformId platform, const PlatformSettings& settings)
{
    if (m_pendingTokenFlows.contains(platform) || m_oauthLocalServer.hasActiveSession(platform)) {
        m_configurationDialog->onTokenActionFinished(platform, false, tr("Another token operation is in progress"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, tr("Another token operation is in progress"));
        return;
    }

    if (settings.clientId.trimmed().isEmpty() || settings.redirectUri.trimmed().isEmpty() || settings.scope.trimmed().isEmpty()
        || settings.authEndpoint.trimmed().isEmpty() || settings.tokenEndpoint.trimmed().isEmpty()) {
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::AUTH_REQUIRED, tr("Missing required OAuth fields"));
        m_configurationDialog->onTokenActionFinished(platform, false, tr("Interactive re-auth blocked by invalid config"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, tr("Invalid OAuth configuration"));
        return;
    }
    if (platform == PlatformId::YouTube && !isLikelyGoogleOAuthClientId(settings.clientId)) {
        const QString normalized = normalizeGoogleOAuthClientId(settings.clientId);
        const QString validationMessage = googleClientIdValidationMessage(normalized);
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::AUTH_REQUIRED,
            validationMessage);
        m_configurationDialog->onTokenActionFinished(platform, false, tr("Interactive re-auth blocked by invalid client_id"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, validationMessage);
        return;
    }

    const QUrl redirectUri(settings.redirectUri);
    if (!redirectUri.isValid() || redirectUri.scheme() != QStringLiteral("http") || redirectUri.host() != QStringLiteral("127.0.0.1") || redirectUri.port() <= 0) {
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::AUTH_REQUIRED, tr("redirect_uri must be http://127.0.0.1:{port}/..."));
        m_configurationDialog->onTokenActionFinished(platform, false, tr("Interactive re-auth blocked by invalid redirect_uri"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, tr("Invalid redirect_uri"));
        return;
    }

    const QString state = createOAuthState(platform);
    QString codeVerifier;
    QString codeChallenge;
    if (platform == PlatformId::YouTube) {
        codeVerifier = pkce::generateCodeVerifier();
        codeChallenge = pkce::makeCodeChallengeS256(codeVerifier);
        if (codeVerifier.trimmed().isEmpty() || codeChallenge.trimmed().isEmpty()) {
            m_configurationDialog->onTokenStateUpdated(platform, TokenState::ERROR, tr("Failed to prepare PKCE"));
            m_configurationDialog->onTokenActionFinished(platform, false, tr("Interactive re-auth failed"));
            appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, tr("Failed to prepare PKCE"));
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
        m_configurationDialog->onTokenActionFinished(platform, false, tr("Failed to start OAuth callback server"));
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
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, tr("Invalid authorize URL"));
        return;
    }
    if (platform == PlatformId::Chzzk && authUrl.host().contains(QStringLiteral("example.com"))) {
        m_txtEventLog->append(QStringLiteral("[WARN] CHZZK OAuth endpoint is placeholder. Replace auth/token endpoint in Configuration."));
    }

    if (!QDesktopServices::openUrl(authUrl)) {
        m_oauthLocalServer.cancelSession(platform, QStringLiteral("OAUTH_BROWSER_OPEN_FAILED"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, tr("Failed to open system browser"));
        return;
    }

    m_authInProgress.insert(platform, true);
    setPlatformRuntimePhase(platform, QStringLiteral("STARTING"));
    clearPlatformRuntimeError(platform);
    m_configurationDialog->onTokenOperationStarted(platform, QStringLiteral("interactive_auth"));
    m_configurationDialog->onTokenStateUpdated(platform, TokenState::REFRESHING, tr("Browser opened. Waiting callback..."));
    m_txtEventLog->append(QStringLiteral("[OAUTH] %1 browser auth started").arg(platformKey(platform)));
    reconcileApiStatus();
}

void MainWindow::onTokenDeleteRequested(PlatformId platform)
{
    if (m_pendingTokenFlows.contains(platform) || m_pendingTokenRevokes.value(platform, false)) {
        const QString detail = tr("Another token operation is in progress");
        m_configurationDialog->onTokenActionFinished(platform, false, detail);
        appendTokenAudit(platform, QStringLiteral("token_revoke"), false, detail);
        return;
    }

    TokenRecord record;
    if (!m_tokenVault.read(platform, &record)) {
        const bool cleared = m_tokenVault.clear(platform);
        refreshTokenUi(platform);
        if (cleared) {
            applyRuntimeAccessTokenToAdapter(platform, QString());
        }
        const QString detail = cleared ? tr("No token to revoke. Local token state cleared.")
                                       : tr("No token to revoke. Local clear failed.");
        m_configurationDialog->onTokenActionFinished(platform, cleared, detail);
        appendTokenAudit(platform, QStringLiteral("token_revoke"), cleared, detail);
        setLiveBroadcastState(platform, LiveBroadcastState::UNKNOWN, tr("Token missing"));
        return;
    }

    if (record.accessToken.trimmed().isEmpty() && record.refreshToken.trimmed().isEmpty()) {
        const bool cleared = m_tokenVault.clear(platform);
        refreshTokenUi(platform);
        if (cleared) {
            applyRuntimeAccessTokenToAdapter(platform, QString());
        }
        const QString detail = cleared ? tr("Token already empty. Local token state cleared.")
                                       : tr("Token already empty. Local clear failed.");
        m_configurationDialog->onTokenActionFinished(platform, cleared, detail);
        appendTokenAudit(platform, QStringLiteral("token_revoke"), cleared, detail);
        setLiveBroadcastState(platform, LiveBroadcastState::UNKNOWN, tr("Token empty"));
        return;
    }

    const PlatformSettings settings = settingsFor(platform);
    m_configurationDialog->onTokenOperationStarted(platform, QStringLiteral("token_revoke"));
    m_configurationDialog->onTokenStateUpdated(platform, TokenState::REFRESHING, tr("Revoking token..."));
    m_pendingTokenRevokes.insert(platform, true);

    QString errorMessage;
    if (!m_oauthTokenClient.requestRevokeToken(platform, settings, record, &errorMessage)) {
        m_pendingTokenRevokes.remove(platform);
        const bool localCleared = m_tokenVault.clear(platform);
        refreshTokenUi(platform);
        if (localCleared) {
            applyRuntimeAccessTokenToAdapter(platform, QString());
        }
        const QString detail = tr("Remote revoke request failed: %1. Local token %2.")
                                   .arg(errorMessage, localCleared ? tr("deleted") : tr("delete failed"));
        m_configurationDialog->onTokenActionFinished(platform, localCleared,
            localCleared ? tr("Local token deleted (remote revoke skipped)")
                         : tr("Token delete failed"));
        appendTokenAudit(platform, QStringLiteral("token_revoke"), false, detail);
        if (localCleared) {
            setLiveBroadcastState(platform, LiveBroadcastState::UNKNOWN, tr("Token deleted locally"));
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
    statusBar()->showMessage(tr("%1 config test: %2").arg(target, ok ? tr("OK") : tr("FAIL")), 3000);
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
        setPlatformRuntimeError(platform, QStringLiteral("OAUTH_STATE_MISMATCH"), tr("OAuth state mismatch"));
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::ERROR, tr("OAuth state mismatch"));
        m_configurationDialog->onTokenActionFinished(platform, false, tr("Interactive re-auth failed"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, tr("OAuth state mismatch"));
        reconcileApiStatus();
        return;
    }

    if (!errorCode.trimmed().isEmpty()) {
        m_authInProgress.insert(platform, false);
        setPlatformRuntimeError(platform, errorCode, errorDescription);
        const QString detail = errorDescription.trimmed().isEmpty() ? errorCode : (errorCode + QStringLiteral(": ") + errorDescription);
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::AUTH_REQUIRED, detail);
        m_configurationDialog->onTokenActionFinished(platform, false, tr("Interactive re-auth canceled/failed"));
        m_txtEventLog->append(QStringLiteral("[OAUTH-FAIL] %1 %2").arg(platformKey(platform), detail));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, detail);
        reconcileApiStatus();
        return;
    }

    if (code.trimmed().isEmpty()) {
        m_authInProgress.insert(platform, false);
        setPlatformRuntimeError(platform, QStringLiteral("OAUTH_CODE_MISSING"), tr("OAuth code missing"));
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::ERROR, tr("OAuth code missing"));
        m_configurationDialog->onTokenActionFinished(platform, false, tr("Interactive re-auth failed"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, tr("OAuth code missing"));
        reconcileApiStatus();
        return;
    }
    if (platform == PlatformId::YouTube && codeVerifier.trimmed().isEmpty()) {
        m_authInProgress.insert(platform, false);
        setPlatformRuntimeError(platform, QStringLiteral("PKCE_MISSING"), tr("PKCE verifier missing"));
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::ERROR, tr("PKCE verifier missing"));
        m_configurationDialog->onTokenActionFinished(platform, false, tr("Interactive re-auth failed"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, tr("PKCE verifier missing"));
        reconcileApiStatus();
        return;
    }

    m_configurationDialog->onTokenStateUpdated(platform, TokenState::REFRESHING, tr("Exchanging token..."));
    if (!startAuthCodeExchangeFlow(platform, settings, code, codeVerifier, state)) {
        m_authInProgress.insert(platform, false);
        setPlatformRuntimeError(platform, QStringLiteral("AUTH_CODE_EXCHANGE_FAILED"), tr("Failed to request token exchange"));
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
    m_configurationDialog->onTokenActionFinished(platform, false, tr("Interactive re-auth failed"));
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
        setPlatformRuntimeError(platform, QStringLiteral("TOKEN_REFRESH_MISSING"), tr("Refresh token missing in response"));
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::AUTH_REQUIRED, tr("Refresh token missing in response"));
        m_configurationDialog->onTokenActionFinished(platform, false, tr("Token update failed"));
        appendTokenAudit(platform, flow, false, tr("Refresh token missing in response"));
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
        setPlatformRuntimeError(platform, QStringLiteral("TOKEN_VAULT_WRITE_FAILED"), tr("Token vault write failed"));
        m_configurationDialog->onTokenStateUpdated(platform, TokenState::ERROR, tr("Token vault write failed"));
        m_configurationDialog->onTokenActionFinished(platform, false, tr("Token update failed"));
        appendTokenAudit(platform, flow, false, tr("Token vault write failed"));
        return;
    }

    clearPlatformRuntimeError(platform);
    refreshTokenUi(platform);
    applyRuntimeAccessTokenToAdapter(platform, accessToken);
    const bool isRefresh = flow == QStringLiteral("refresh_token");
    const QString message = isRefresh ? tr("Silent refresh success") : tr("Interactive re-auth success");
    m_configurationDialog->onTokenActionFinished(platform, true, message);
    appendTokenAudit(platform, flow, true, message);
    m_txtEventLog->append(QStringLiteral("[TOKEN-OK] %1 flow=%2").arg(platformKey(platform), flow));
    reconcileApiStatus();
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
            ? tr("Silent refresh failed")
            : tr("Interactive re-auth failed"));
    appendTokenAudit(platform, flow, false, detailWithGuidance);
    m_txtEventLog->append(QStringLiteral("[TOKEN-FAIL] %1 flow=%2 code=%3 http=%4 msg=%5")
                              .arg(platformKey(platform), flow, errorCode)
                              .arg(httpStatus)
                              .arg(message));
    setPlatformRuntimeError(platform, errorCode, detailWithGuidance);
    reconcileApiStatus();
    if (errorCode == QStringLiteral("invalid_grant")) {
        setLiveBroadcastState(platform, LiveBroadcastState::UNKNOWN, tr("Live check skipped: token invalid"));
    }
}

void MainWindow::onTokenRevoked(PlatformId platform, const QString& flow)
{
    Q_UNUSED(flow)
    m_pendingTokenRevokes.remove(platform);

    const bool localCleared = m_tokenVault.clear(platform);
    refreshTokenUi(platform);
    if (localCleared) {
        applyRuntimeAccessTokenToAdapter(platform, QString());
    }
    const QString detail = localCleared
        ? tr("Remote token revoke succeeded. Local token deleted.")
        : tr("Remote token revoke succeeded, but local token delete failed.");
    m_configurationDialog->onTokenActionFinished(platform, localCleared,
        localCleared ? tr("Token revoked and deleted")
                     : tr("Token revoked but local delete failed"));
    appendTokenAudit(platform, QStringLiteral("token_revoke"), localCleared, detail);
    m_txtEventLog->append(QStringLiteral("[TOKEN-REVOKE-OK] %1").arg(platformKey(platform)));
    setPlatformRuntimePhase(platform, QStringLiteral("IDLE"));
    clearPlatformRuntimeError(platform);
    reconcileApiStatus();
    setLiveBroadcastState(platform, LiveBroadcastState::UNKNOWN, tr("Token revoked"));
}

void MainWindow::onTokenRevokeFailed(PlatformId platform, const QString& flow, const QString& errorCode, const QString& message, int httpStatus)
{
    Q_UNUSED(flow)
    m_pendingTokenRevokes.remove(platform);

    const bool localCleared = m_tokenVault.clear(platform);
    refreshTokenUi(platform);
    if (localCleared) {
        applyRuntimeAccessTokenToAdapter(platform, QString());
    }
    const QString detail = QStringLiteral("%1 (http=%2, code=%3). Local token %4.")
                               .arg(message)
                               .arg(httpStatus)
                               .arg(errorCode)
                               .arg(localCleared ? tr("deleted") : tr("delete failed"));
    m_configurationDialog->onTokenActionFinished(platform, localCleared,
        localCleared ? tr("Remote revoke failed, local token deleted")
                     : tr("Remote revoke failed, local delete failed"));
    appendTokenAudit(platform, QStringLiteral("token_revoke"), false, detail);
    m_txtEventLog->append(QStringLiteral("[TOKEN-REVOKE-FAIL] %1 code=%2 http=%3 msg=%4")
                              .arg(platformKey(platform), errorCode)
                              .arg(httpStatus)
                              .arg(message));
    setPlatformRuntimeError(platform, errorCode, message);
    reconcileApiStatus();
    if (localCleared) {
        setLiveBroadcastState(platform, LiveBroadcastState::UNKNOWN, tr("Token deleted locally"));
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
        setPlatformRuntimeError(platform, QStringLiteral("CONNECT_FAILED"), tr("Connect failed"));
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
    setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::UNKNOWN, tr("Disconnected"));
    setLiveBroadcastState(PlatformId::Chzzk, LiveBroadcastState::UNKNOWN, tr("Disconnected"));
    m_txtEventLog->append(QStringLiteral("[DISCONNECT] completed"));
    refreshConnectButton();
    reconcileApiStatus();
    updateActionPanel();
}

void MainWindow::onWarningRaised(const QString& code, const QString& message)
{
    const bool isDetail = isDetailWarningCode(code);
    if (m_detailLogEnabled || !isDetail) {
        m_txtEventLog->append(QStringLiteral("[WARN] %1: %2").arg(code, message));
    }
    PlatformId platform = PlatformId::YouTube;
    QString innerCode;
    if (tryPlatformFromCodePrefix(code, &platform, &innerCode)) {
        const QString normalizedInnerCode = innerCode.trimmed().toUpper();
        if (platform == PlatformId::YouTube && normalizedInnerCode == QStringLiteral("INFO_LIVE_STATE_UNKNOWN")) {
            setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::UNKNOWN, message);
        } else if (platform == PlatformId::YouTube && normalizedInnerCode == QStringLiteral("INFO_LIVE_STATE_CHECKING")) {
            setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::CHECKING, message);
        } else if (platform == PlatformId::YouTube && normalizedInnerCode == QStringLiteral("INFO_LIVE_STATE_ONLINE")) {
            setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::ONLINE, message);
        } else if (platform == PlatformId::YouTube && normalizedInnerCode == QStringLiteral("INFO_LIVE_STATE_OFFLINE")) {
            setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::OFFLINE, message);
        } else if (platform == PlatformId::YouTube && normalizedInnerCode == QStringLiteral("INFO_LIVECHAT_ID_PENDING")) {
            setPlatformStatus(PlatformId::YouTube, QStringLiteral("CONNECTED_NO_LIVECHAT"));
            setPlatformRuntimePhase(PlatformId::YouTube, QStringLiteral("CONNECTED_NO_LIVECHAT"));
            clearPlatformRuntimeError(PlatformId::YouTube);
            reconcileApiStatus();
        } else if (platform == PlatformId::YouTube && normalizedInnerCode == QStringLiteral("INFO_LIVECHAT_ID_READY")) {
            setPlatformStatus(PlatformId::YouTube, QStringLiteral("CONNECTED"));
            setPlatformRuntimePhase(PlatformId::YouTube, QStringLiteral("CONNECTED"));
            clearPlatformRuntimeError(PlatformId::YouTube);
            reconcileApiStatus();
        } else if (platform == PlatformId::YouTube && normalizedInnerCode == QStringLiteral("INFO_LIVE_DISCOVERY_FALLBACK_SEARCH")) {
            setPlatformStatus(PlatformId::YouTube, QStringLiteral("CONNECTED_NO_LIVECHAT"));
            setPlatformRuntimePhase(PlatformId::YouTube, QStringLiteral("CONNECTED_NO_LIVECHAT"));
            clearPlatformRuntimeError(PlatformId::YouTube);
            reconcileApiStatus();
        } else if (platform == PlatformId::Chzzk && normalizedInnerCode == QStringLiteral("INFO_CHZZK_SESSION_PENDING")) {
            setPlatformStatus(PlatformId::Chzzk, QStringLiteral("CONNECTED_NO_SESSIONKEY"));
            setPlatformRuntimePhase(PlatformId::Chzzk, QStringLiteral("CONNECTED_NO_SESSIONKEY"));
            clearPlatformRuntimeError(PlatformId::Chzzk);
            reconcileApiStatus();
        } else if (platform == PlatformId::Chzzk && normalizedInnerCode == QStringLiteral("INFO_CHZZK_SUBSCRIBE_PENDING")) {
            setPlatformStatus(PlatformId::Chzzk, QStringLiteral("CONNECTED_NO_SUBSCRIBE"));
            setPlatformRuntimePhase(PlatformId::Chzzk, QStringLiteral("CONNECTED_NO_SUBSCRIBE"));
            clearPlatformRuntimeError(PlatformId::Chzzk);
            reconcileApiStatus();
        } else if (platform == PlatformId::Chzzk && normalizedInnerCode == QStringLiteral("INFO_CHZZK_CHAT_READY")) {
            setPlatformStatus(PlatformId::Chzzk, QStringLiteral("CONNECTED"));
            setPlatformRuntimePhase(PlatformId::Chzzk, QStringLiteral("CONNECTED"));
            clearPlatformRuntimeError(PlatformId::Chzzk);
            reconcileApiStatus();
        }
        if (platform == PlatformId::YouTube) {
            if (normalizedInnerCode == QStringLiteral("LIVE_DISCOVERY_FAILED")
                || normalizedInnerCode == QStringLiteral("LIVE_CHAT_UNAVAILABLE")
                || normalizedInnerCode == QStringLiteral("CHAT_RATE_LIMIT")
                || normalizedInnerCode == QStringLiteral("CHAT_POLL_FAILED")
                || normalizedInnerCode == QStringLiteral("YT_STREAM_RESOURCE_EXHAUSTED")
                || normalizedInnerCode == QStringLiteral("YT_STREAM_QUOTA_BACKOFF")
                || normalizedInnerCode == QStringLiteral("YT_STREAM_PERMISSION_DENIED")
                || normalizedInnerCode == QStringLiteral("YT_STREAM_INTERNAL")
                || normalizedInnerCode == QStringLiteral("YT_STREAM_UNAVAILABLE")) {
                setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::ERROR, message);
            } else if (normalizedInnerCode == QStringLiteral("INVALID_CONFIG")
                       || normalizedInnerCode == QStringLiteral("TOKEN_MISSING")
                       || normalizedInnerCode == QStringLiteral("YT_STREAM_UNAUTHENTICATED")
                       || normalizedInnerCode == QStringLiteral("YT_STREAM_TOKEN_MISSING")
                       || normalizedInnerCode == QStringLiteral("YT_STREAM_LIVECHAT_ID_MISSING")) {
                setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::UNKNOWN, message);
            }
        }
        if (!normalizedInnerCode.startsWith(QStringLiteral("TRACE_")) && !normalizedInnerCode.startsWith(QStringLiteral("INFO_"))) {
            setPlatformRuntimeError(platform, innerCode, message);
            reconcileApiStatus();
        }
    }
    if (m_detailLogEnabled || !isDetail) {
        statusBar()->showMessage(QStringLiteral("%1: %2").arg(code, message), 4000);
    }
}

void MainWindow::setupUi()
{
    auto* root = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(root);

    auto* topLayout = new QHBoxLayout;
    m_btnConnectToggle = new QPushButton(root);
    m_btnConnectToggle->setObjectName(QStringLiteral("btnConnectToggle"));
    m_btnToggleChatView = new QPushButton(root);
    m_btnToggleChatView->setObjectName(QStringLiteral("btnToggleChatView"));
    m_lblStateCaption = new QLabel(root);
    m_lblStateCaption->setObjectName(QStringLiteral("lblStateCaption"));
    m_lblConnectionState = new QLabel(root);
    m_lblConnectionState->setObjectName(QStringLiteral("lblConnectionState"));

    m_btnOpenChatterList = new QPushButton(root);
    m_btnOpenChatterList->setObjectName(QStringLiteral("btnOpenChatterList"));
    m_btnOpenConfiguration = new QPushButton(root);
    m_btnOpenConfiguration->setObjectName(QStringLiteral("btnOpenConfiguration"));

    topLayout->addWidget(m_btnConnectToggle);
    topLayout->addWidget(m_btnToggleChatView);
    topLayout->addWidget(m_lblStateCaption);
    topLayout->addWidget(m_lblConnectionState);
    topLayout->addStretch();
    topLayout->addWidget(m_btnOpenChatterList);
    topLayout->addWidget(m_btnOpenConfiguration);

    auto* statusLayout = new QHBoxLayout;
    m_lblYouTubeStatus = new QLabel(root);
    m_lblChzzkStatus = new QLabel(root);
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
    auto* copyAction = new QAction(m_tblChat);
    copyAction->setObjectName(QStringLiteral("actCopySelectedChat"));
    connect(copyAction, &QAction::triggered, this, &MainWindow::onCopySelectedChat);
    m_tblChat->setContextMenuPolicy(Qt::ActionsContextMenu);
    m_tblChat->addAction(copyAction);

    m_grpActionPanel = new QGroupBox(root);
    m_grpActionPanel->setObjectName(QStringLiteral("grpActionPanel"));
    auto* actionPanelLayout = new QVBoxLayout(m_grpActionPanel);

    auto* platformIndicatorLayout = new QHBoxLayout;
    m_boxYouTubeRuntime = new QFrame(m_grpActionPanel);
    m_boxYouTubeRuntime->setObjectName(QStringLiteral("ytStatusBox"));
    m_boxYouTubeRuntime->setFixedSize(18, 18);
    auto* lblYouTubeRuntime = new QLabel(QStringLiteral("YouTube"), m_grpActionPanel);
    m_boxChzzkRuntime = new QFrame(m_grpActionPanel);
    m_boxChzzkRuntime->setObjectName(QStringLiteral("chzStatusBox"));
    m_boxChzzkRuntime->setFixedSize(18, 18);
    auto* lblChzzkRuntime = new QLabel(QStringLiteral("CHZZK"), m_grpActionPanel);
    platformIndicatorLayout->addWidget(m_boxYouTubeRuntime);
    platformIndicatorLayout->addWidget(lblYouTubeRuntime);
    platformIndicatorLayout->addSpacing(12);
    platformIndicatorLayout->addWidget(m_boxChzzkRuntime);
    platformIndicatorLayout->addWidget(lblChzzkRuntime);
    platformIndicatorLayout->addStretch();
    actionPanelLayout->addLayout(platformIndicatorLayout);

    auto* liveIndicatorLayout = new QHBoxLayout;
    m_boxYouTubeLive = new QFrame(m_grpActionPanel);
    m_boxYouTubeLive->setObjectName(QStringLiteral("ytLiveBox"));
    m_boxYouTubeLive->setFixedSize(18, 18);
    m_lblYouTubeLive = new QLabel(m_grpActionPanel);
    m_boxChzzkLive = new QFrame(m_grpActionPanel);
    m_boxChzzkLive->setObjectName(QStringLiteral("chzLiveBox"));
    m_boxChzzkLive->setFixedSize(18, 18);
    m_lblChzzkLive = new QLabel(m_grpActionPanel);
    liveIndicatorLayout->addWidget(m_boxYouTubeLive);
    liveIndicatorLayout->addWidget(m_lblYouTubeLive);
    liveIndicatorLayout->addSpacing(12);
    liveIndicatorLayout->addWidget(m_boxChzzkLive);
    liveIndicatorLayout->addWidget(m_lblChzzkLive);
    liveIndicatorLayout->addStretch();
    actionPanelLayout->addLayout(liveIndicatorLayout);

    auto* selectedInfoLayout = new QFormLayout;
    m_lblSelectedPlatformCaption = new QLabel(m_grpActionPanel);
    m_lblSelectedAuthorCaption = new QLabel(m_grpActionPanel);
    m_lblSelectedMessageCaption = new QLabel(m_grpActionPanel);
    m_lblSelectedPlatform = new QLabel(QStringLiteral("-"), m_grpActionPanel);
    m_lblSelectedAuthor = new QLabel(QStringLiteral("-"), m_grpActionPanel);
    m_lblSelectedMessage = new QLabel(QStringLiteral("-"), m_grpActionPanel);
    m_lblSelectedMessage->setWordWrap(true);
    selectedInfoLayout->addRow(m_lblSelectedPlatformCaption, m_lblSelectedPlatform);
    selectedInfoLayout->addRow(m_lblSelectedAuthorCaption, m_lblSelectedAuthor);
    selectedInfoLayout->addRow(m_lblSelectedMessageCaption, m_lblSelectedMessage);
    actionPanelLayout->addLayout(selectedInfoLayout);

    m_btnActionSendMessage = new QPushButton(m_grpActionPanel);
    m_btnActionSendMessage->setObjectName(QStringLiteral("btnActionSendMessage"));
    m_btnActionRestrictUser = new QPushButton(m_grpActionPanel);
    m_btnActionRestrictUser->setObjectName(QStringLiteral("btnActionRestrictUser"));
    m_btnActionYoutubeDeleteMessage = new QPushButton(m_grpActionPanel);
    m_btnActionYoutubeDeleteMessage->setObjectName(QStringLiteral("btnActionYoutubeDeleteMessage"));
    m_btnActionYoutubeTimeout = new QPushButton(m_grpActionPanel);
    m_btnActionYoutubeTimeout->setObjectName(QStringLiteral("btnActionYoutubeTimeout"));
    m_btnActionChzzkRestrict = new QPushButton(m_grpActionPanel);
    m_btnActionChzzkRestrict->setObjectName(QStringLiteral("btnActionChzzkRestrict"));

    actionPanelLayout->addWidget(m_btnActionSendMessage);
    actionPanelLayout->addWidget(m_btnActionRestrictUser);
    actionPanelLayout->addWidget(m_btnActionYoutubeDeleteMessage);
    actionPanelLayout->addWidget(m_btnActionYoutubeTimeout);
    actionPanelLayout->addWidget(m_btnActionChzzkRestrict);
    actionPanelLayout->addStretch();

    auto* legendWrap = new QWidget(m_grpActionPanel);
    legendWrap->setObjectName(QStringLiteral("statusLegend"));
    auto* legendGrid = new QGridLayout(legendWrap);
    legendGrid->setContentsMargins(0, 0, 0, 0);
    legendGrid->setHorizontalSpacing(10);
    legendGrid->setVerticalSpacing(4);

    auto addLegendItem = [legendWrap, legendGrid](int row, int col, const QString& color, const QString& objectName, const QString& label) {
        auto* chip = new QFrame(legendWrap);
        chip->setFixedSize(12, 12);
        chip->setStyleSheet(QStringLiteral("background-color:%1; border:1px solid #666666; border-radius:2px;").arg(color));
        auto* text = new QLabel(label, legendWrap);
        text->setObjectName(objectName);
        const int base = col * 2;
        legendGrid->addWidget(chip, row, base);
        legendGrid->addWidget(text, row, base + 1);
    };

    addLegendItem(0, 0, QStringLiteral("#FFB74D"), QStringLiteral("lblLegendAuthInProgress"), tr("Auth In Progress"));
    addLegendItem(0, 1, QStringLiteral("#81D4FA"), QStringLiteral("lblLegendOnline"), tr("Connected"));
    addLegendItem(1, 0, QStringLiteral("#66BB6A"), QStringLiteral("lblLegendTokenOk"), tr("Token OK"));
    addLegendItem(1, 1, QStringLiteral("#EF5350"), QStringLiteral("lblLegendTokenBad"), tr("Token Invalid"));

    actionPanelLayout->addWidget(legendWrap);

    auto* liveLegendWrap = new QWidget(m_grpActionPanel);
    liveLegendWrap->setObjectName(QStringLiteral("liveStatusLegend"));
    auto* liveLegendGrid = new QGridLayout(liveLegendWrap);
    liveLegendGrid->setContentsMargins(0, 0, 0, 0);
    liveLegendGrid->setHorizontalSpacing(10);
    liveLegendGrid->setVerticalSpacing(4);

    auto addLiveLegendItem = [liveLegendWrap, liveLegendGrid](int row, int col, const QString& color, const QString& objectName, const QString& label) {
        auto* chip = new QFrame(liveLegendWrap);
        chip->setFixedSize(12, 12);
        chip->setStyleSheet(QStringLiteral("background-color:%1; border:1px solid #666666; border-radius:2px;").arg(color));
        auto* text = new QLabel(label, liveLegendWrap);
        text->setObjectName(objectName);
        const int base = col * 2;
        liveLegendGrid->addWidget(chip, row, base);
        liveLegendGrid->addWidget(text, row, base + 1);
    };

    addLiveLegendItem(0, 0, QStringLiteral("#90A4AE"), QStringLiteral("lblLegendLiveUnknown"), tr("Live Unknown"));
    addLiveLegendItem(0, 1, QStringLiteral("#5C6BC0"), QStringLiteral("lblLegendLiveChecking"), tr("Live Checking"));
    addLiveLegendItem(1, 0, QStringLiteral("#26A69A"), QStringLiteral("lblLegendLiveOnline"), tr("Live Online"));
    addLiveLegendItem(1, 1, QStringLiteral("#BCAAA4"), QStringLiteral("lblLegendLiveOffline"), tr("Live Offline"));
    addLiveLegendItem(2, 0, QStringLiteral("#8E24AA"), QStringLiteral("lblLegendLiveError"), tr("Live Error"));

    actionPanelLayout->addWidget(liveLegendWrap);

    connect(m_btnActionSendMessage, &QPushButton::clicked, this, &MainWindow::onActionSendMessage);
    connect(m_btnActionRestrictUser, &QPushButton::clicked, this, &MainWindow::onActionRestrictUser);
    connect(m_btnActionYoutubeDeleteMessage, &QPushButton::clicked, this, &MainWindow::onActionYoutubeDeleteMessage);
    connect(m_btnActionYoutubeTimeout, &QPushButton::clicked, this, &MainWindow::onActionYoutubeTimeout);
    connect(m_btnActionChzzkRestrict, &QPushButton::clicked, this, &MainWindow::onActionChzzkRestrict);

    m_upperSplitter = new QSplitter(Qt::Horizontal, root);
    m_upperSplitter->addWidget(m_tblChat);
    m_upperSplitter->addWidget(m_grpActionPanel);
    m_upperSplitter->setStretchFactor(0, 3);
    m_upperSplitter->setStretchFactor(1, 2);

    auto* composerWrap = new QWidget(root);
    composerWrap->setObjectName(QStringLiteral("composerWrap"));
    auto* composerLayout = new QHBoxLayout(composerWrap);
    composerLayout->setContentsMargins(0, 0, 0, 0);
    composerLayout->setSpacing(8);
    m_edtComposer = new QPlainTextEdit(composerWrap);
    m_edtComposer->setObjectName(QStringLiteral("edtComposer"));
    m_edtComposer->setFixedHeight(68);
    m_edtComposer->installEventFilter(this);
    connect(m_edtComposer, &QPlainTextEdit::textChanged, this, [this]() {
        if (!m_composerApplyingHistory) {
            m_sendHistoryIndex = m_sendHistory.size();
            if (m_edtComposer) {
                m_sendHistoryDraft = m_edtComposer->toPlainText();
            }
        }
        updateComposerUiState();
    });
    m_btnComposerSend = new QPushButton(composerWrap);
    m_btnComposerSend->setObjectName(QStringLiteral("btnComposerSend"));
    connect(m_btnComposerSend, &QPushButton::clicked, this, &MainWindow::onActionSendMessage);
    composerLayout->addWidget(m_edtComposer, 1);
    composerLayout->addWidget(m_btnComposerSend, 0, Qt::AlignBottom);

    m_txtEventLog = new QTextEdit(root);
    m_txtEventLog->setReadOnly(true);
    m_txtEventLog->setObjectName(QStringLiteral("txtEventLog"));

    auto* bottomWrap = new QWidget(root);
    auto* bottomLayout = new QVBoxLayout(bottomWrap);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(4);
    bottomLayout->addWidget(composerWrap, 0);
    bottomLayout->addWidget(m_txtEventLog, 1);

    m_mainSplitter = new QSplitter(Qt::Vertical, root);
    m_mainSplitter->addWidget(m_upperSplitter);
    m_mainSplitter->addWidget(bottomWrap);
    m_mainSplitter->setStretchFactor(0, 4);
    m_mainSplitter->setStretchFactor(1, 1);

    rootLayout->addLayout(topLayout);
    rootLayout->addLayout(statusLayout);
    rootLayout->addWidget(m_mainSplitter, 1);

    setCentralWidget(root);
    resize(1000, 700);

    connect(m_btnConnectToggle, &QPushButton::clicked, this, &MainWindow::onConnectToggleClicked);
    connect(m_btnToggleChatView, &QPushButton::clicked, this, &MainWindow::onToggleChatViewClicked);
    connect(m_btnOpenChatterList, &QPushButton::clicked, this, &MainWindow::onOpenChatterList);
    connect(m_btnOpenConfiguration, &QPushButton::clicked, this, &MainWindow::onOpenConfiguration);
    retranslateUi();
    updateComposerUiState();
}

void MainWindow::retranslateUi()
{
    setWindowTitle(tr("BotManager Qt5"));
    if (m_lblStateCaption) {
        m_lblStateCaption->setText(tr("State:"));
    }
    if (m_btnOpenChatterList) {
        m_btnOpenChatterList->setText(tr("ChatterList"));
    }
    if (m_btnOpenConfiguration) {
        m_btnOpenConfiguration->setText(tr("Configuration"));
    }
    if (m_grpActionPanel) {
        m_grpActionPanel->setTitle(tr("Actions"));
    }
    if (m_lblSelectedPlatformCaption) {
        m_lblSelectedPlatformCaption->setText(tr("Platform"));
    }
    if (m_lblSelectedAuthorCaption) {
        m_lblSelectedAuthorCaption->setText(tr("Author"));
    }
    if (m_lblSelectedMessageCaption) {
        m_lblSelectedMessageCaption->setText(tr("Message"));
    }
    if (m_btnActionSendMessage) {
        m_btnActionSendMessage->setText(tr("Send Message"));
    }
    if (m_btnActionRestrictUser) {
        m_btnActionRestrictUser->setText(tr("Restrict User"));
    }
    if (m_btnActionYoutubeDeleteMessage) {
        m_btnActionYoutubeDeleteMessage->setText(tr("YouTube Delete Message"));
    }
    if (m_btnActionYoutubeTimeout) {
        m_btnActionYoutubeTimeout->setText(tr("YouTube Timeout 5m"));
    }
    if (m_btnActionChzzkRestrict) {
        m_btnActionChzzkRestrict->setText(tr("CHZZK Add Restriction"));
    }
    if (m_edtComposer) {
        m_edtComposer->setPlaceholderText(tr("Type message here... (Enter=Send, Shift+Space=New line, Ctrl+Up/Down=History)"));
    }
    if (m_btnComposerSend) {
        m_btnComposerSend->setText(tr("Send"));
    }
    if (QAction* copyAction = m_tblChat ? m_tblChat->findChild<QAction*>(QStringLiteral("actCopySelectedChat")) : nullptr) {
        copyAction->setText(tr("Copy Selected Chat"));
    }
    if (QLabel* label = findChild<QLabel*>(QStringLiteral("lblLegendAuthInProgress"))) {
        label->setText(tr("Auth In Progress"));
    }
    if (QLabel* label = findChild<QLabel*>(QStringLiteral("lblLegendOnline"))) {
        label->setText(tr("Connected"));
    }
    if (QLabel* label = findChild<QLabel*>(QStringLiteral("lblLegendTokenOk"))) {
        label->setText(tr("Token OK"));
    }
    if (QLabel* label = findChild<QLabel*>(QStringLiteral("lblLegendTokenBad"))) {
        label->setText(tr("Token Invalid"));
    }
    if (QLabel* label = findChild<QLabel*>(QStringLiteral("lblLegendLiveUnknown"))) {
        label->setText(tr("Live Unknown"));
    }
    if (QLabel* label = findChild<QLabel*>(QStringLiteral("lblLegendLiveChecking"))) {
        label->setText(tr("Live Checking"));
    }
    if (QLabel* label = findChild<QLabel*>(QStringLiteral("lblLegendLiveOnline"))) {
        label->setText(tr("Live Online"));
    }
    if (QLabel* label = findChild<QLabel*>(QStringLiteral("lblLegendLiveOffline"))) {
        label->setText(tr("Live Offline"));
    }
    if (QLabel* label = findChild<QLabel*>(QStringLiteral("lblLegendLiveError"))) {
        label->setText(tr("Live Error"));
    }
    refreshConnectButton();
    refreshChatViewToggleButton();
    configureChatTableForCurrentView();
    m_lblConnectionState->setText(connectionStateText(m_connectionCoordinator.state()));
    setPlatformStatus(PlatformId::YouTube, m_platformStatusCodes.value(PlatformId::YouTube, QStringLiteral("IDLE")));
    setPlatformStatus(PlatformId::Chzzk, m_platformStatusCodes.value(PlatformId::Chzzk, QStringLiteral("IDLE")));
    refreshLiveBroadcastIndicators();
    refreshPlatformIndicators();
    updateActionPanel();
}

void MainWindow::recreateAuxiliaryDialogs(bool reopenConfiguration, bool reopenChatterList)
{
    if (m_configurationDialog) {
        disconnect(m_configurationDialog, nullptr, this, nullptr);
        m_configurationDialog->hide();
        m_configurationDialog->deleteLater();
        m_configurationDialog = nullptr;
    }
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
    refreshAllTokenUi();
    if (reopenConfiguration) {
        m_configurationDialog->show();
        m_configurationDialog->raise();
        m_configurationDialog->activateWindow();
    }

    if (m_chatterListDialog) {
        disconnect(m_chatterListDialog, nullptr, this, nullptr);
        m_chatterListDialog->hide();
        m_chatterListDialog->deleteLater();
        m_chatterListDialog = nullptr;
    }
    m_chatterListDialog = new ChatterListDialog(this);
    connect(m_chatterListDialog, &ChatterListDialog::resetRequested, this, &MainWindow::onResetChatterList);
    refreshChatterListDialog();
    if (reopenChatterList) {
        m_chatterListDialog->show();
        m_chatterListDialog->raise();
        m_chatterListDialog->activateWindow();
    }
}

void MainWindow::refreshConnectButton()
{
    const ConnectionState state = m_connectionCoordinator.state();

    switch (state) {
    case ConnectionState::IDLE:
    case ConnectionState::ERROR:
        m_btnConnectToggle->setText(tr("Connect"));
        m_btnConnectToggle->setEnabled(true);
        break;
    case ConnectionState::CONNECTING:
        m_btnConnectToggle->setText(tr("Connecting..."));
        m_btnConnectToggle->setEnabled(false);
        break;
    case ConnectionState::CONNECTED:
    case ConnectionState::PARTIALLY_CONNECTED:
        m_btnConnectToggle->setText(tr("Disconnect"));
        m_btnConnectToggle->setEnabled(true);
        break;
    case ConnectionState::DISCONNECTING:
        m_btnConnectToggle->setText(tr("Disconnecting..."));
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
            ? tr("View: Messenger")
            : tr("View: Table"));
}

void MainWindow::configureChatTableForCurrentView()
{
    if (!m_tblChat) {
        return;
    }

    if (m_chatViewMode == ChatViewMode::Messenger) {
        m_tblChat->setColumnCount(1);
        m_tblChat->setHorizontalHeaderLabels({ tr("Chat") });
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
    m_tblChat->setHorizontalHeaderLabels({ tr("Time"), tr("Platform"), tr("Author"), tr("Message") });
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
    m_platformStatusCodes.insert(platform, statusText);
    const QString display = displayPlatformPhase(statusText);
    if (platform == PlatformId::YouTube) {
        m_lblYouTubeStatus->setText(tr("YouTube: %1").arg(display));
        return;
    }
    m_lblChzzkStatus->setText(tr("CHZZK: %1").arg(display));
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
            const bool keepConnectedSpecial
                = (platform == PlatformId::YouTube
                      && phase == QStringLiteral("CONNECTED_NO_LIVECHAT"))
                || (platform == PlatformId::Chzzk
                    && (phase == QStringLiteral("CONNECTED_NO_SESSIONKEY")
                        || phase == QStringLiteral("CONNECTED_NO_SUBSCRIBE")));
            if (phase != QStringLiteral("CONNECTED") && !keepConnectedSpecial) {
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
            if (phase == QStringLiteral("STARTING")
                || phase == QStringLiteral("CONNECTING")
                || phase == QStringLiteral("CONNECTED")
                || phase == QStringLiteral("CONNECTED_NO_LIVECHAT")
                || phase == QStringLiteral("CONNECTED_NO_SESSIONKEY")
                || phase == QStringLiteral("CONNECTED_NO_SUBSCRIBE")) {
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
        return tr("IDLE");
    case ConnectionState::CONNECTING:
        return tr("CONNECTING");
    case ConnectionState::PARTIALLY_CONNECTED:
        return tr("PARTIALLY_CONNECTED");
    case ConnectionState::CONNECTED:
        return tr("CONNECTED");
    case ConnectionState::DISCONNECTING:
        return tr("DISCONNECTING");
    case ConnectionState::ERROR:
        return tr("ERROR");
    }
    return tr("UNKNOWN");
}

QString MainWindow::displayPlatformPhase(const QString& phase) const
{
    const QString normalized = phase.trimmed().toUpper();
    if (normalized == QStringLiteral("IDLE")) {
        return tr("IDLE");
    }
    if (normalized == QStringLiteral("STARTING")) {
        return tr("STARTING");
    }
    if (normalized == QStringLiteral("CONNECTED")) {
        return tr("CONNECTED");
    }
    if (normalized == QStringLiteral("FAILED")) {
        return tr("FAILED");
    }
    if (normalized == QStringLiteral("CONNECTED_NO_LIVECHAT")) {
        return tr("CONNECTED_NO_LIVECHAT");
    }
    if (normalized == QStringLiteral("CONNECTED_NO_SESSIONKEY")) {
        return tr("CONNECTED_NO_SESSIONKEY");
    }
    if (normalized == QStringLiteral("CONNECTED_NO_SUBSCRIBE")) {
        return tr("CONNECTED_NO_SUBSCRIBE");
    }
    return phase;
}

void MainWindow::onChatReceived(const UnifiedChatMessage& message)
{
    maybeQueueYouTubeAuthorHandleLookup(message);
    recordChatter(message);
    appendChatMessage(message);
    m_txtEventLog->append(QStringLiteral("[CHAT] %1 author=%2 text=%3")
                              .arg(platformKey(message.platform),
                                  displayAuthorLabel(message),
                                  message.text.left(120)));
    if (m_detailLogEnabled && message.platform == PlatformId::YouTube) {
        m_txtEventLog->append(QStringLiteral("[CHAT-TRACE] youtube rawDisplayName=%1 rawChannelId=%2 authorId=%3 owner=%4 moderator=%5 sponsor=%6 verified=%7")
                                  .arg(message.rawAuthorDisplayName.isEmpty() ? QStringLiteral("-") : message.rawAuthorDisplayName,
                                      message.rawAuthorChannelId.isEmpty() ? QStringLiteral("-") : message.rawAuthorChannelId,
                                      message.authorId.isEmpty() ? QStringLiteral("-") : message.authorId,
                                      message.authorIsChatOwner ? QStringLiteral("true") : QStringLiteral("false"),
                                      message.authorIsChatModerator ? QStringLiteral("true") : QStringLiteral("false"),
                                      message.authorIsChatSponsor ? QStringLiteral("true") : QStringLiteral("false"),
                                      message.authorIsVerified ? QStringLiteral("true") : QStringLiteral("false")));
    }
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
    QString nickname = displayAuthorLabel(message).trimmed();
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

void MainWindow::rebuildChatterStatsFromMessages()
{
    m_chatterStats.clear();
    for (const UnifiedChatMessage& message : m_chatMessages) {
        QString nickname = displayAuthorLabel(message).trimmed();
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
    const QString platform = message.platform == PlatformId::YouTube ? tr("YouTube") : tr("CHZZK");
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
        const int minRowHeight = qBound(8, m_snapshot.chatFontSize, 24) * 3 + qBound(0, m_snapshot.chatLineSpacing, 20) * 2;
        m_tblChat->setRowHeight(row, qMax(minRowHeight, widget->sizeHint().height()));
        return;
    }

    m_tblChat->setItem(row, 0, new QTableWidgetItem(timeText));
    m_tblChat->setItem(row, 1, new QTableWidgetItem(platform));
    m_tblChat->setItem(row, 2, new QTableWidgetItem(authorDisplay));
    m_tblChat->setItem(row, 3, new QTableWidgetItem(message.text));
}

QWidget* MainWindow::buildMessengerCellWidget(const UnifiedChatMessage& message, const QString& authorDisplay) const
{
    const int fontSize = qBound(8, m_snapshot.chatFontSize, 24);
    const int lineSpacing = qBound(0, m_snapshot.chatLineSpacing, 20);
    const bool isBold = m_snapshot.chatFontBold;
    const bool isItalic = m_snapshot.chatFontItalic;
    const int badgeSize = fontSize + 2;
    const int badgeFontSize = qMax(fontSize - 4, 6);
    const int timestampFontSize = qMax(fontSize - 2, 7);
    const QString fontFamily = m_snapshot.chatFontFamily.trimmed();
    QString fontExtraStyle;
    if (!fontFamily.isEmpty()) {
        fontExtraStyle += QStringLiteral("font-family:'%1';").arg(fontFamily);
    }
    if (isBold) {
        fontExtraStyle += QStringLiteral("font-weight:bold;");
    }
    if (isItalic) {
        fontExtraStyle += QStringLiteral("font-style:italic;");
    }

    auto* wrap = new QWidget(m_tblChat);
    wrap->setObjectName(QStringLiteral("chatBubbleWrap"));
    wrap->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    wrap->setFocusPolicy(Qt::NoFocus);

    auto* layout = new QVBoxLayout(wrap);
    layout->setContentsMargins(8, lineSpacing, 8, lineSpacing);
    layout->setSpacing(1);
    layout->setAlignment(Qt::AlignTop);

    auto* badge = new QLabel(wrap);
    badge->setFixedSize(badgeSize, badgeSize);
    badge->setAlignment(Qt::AlignCenter);
    badge->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    badge->setFocusPolicy(Qt::NoFocus);
    badge->setStyleSheet(message.platform == PlatformId::YouTube
            ? QStringLiteral("background:#E53935; color:#ffffff; border-radius:%1px; font-weight:700; font-size:%2px;")
                  .arg(badgeSize / 2).arg(badgeFontSize)
            : QStringLiteral("background:#16C784; color:#101010; border-radius:%1px; font-weight:700; font-size:%2px;")
                  .arg(badgeSize / 2).arg(badgeFontSize));
    badge->setText(message.platform == PlatformId::YouTube ? QStringLiteral("▶") : QStringLiteral("Z"));
    auto* badgeWrap = new QWidget(wrap);
    auto* badgeWrapLayout = new QVBoxLayout(badgeWrap);
    badgeWrapLayout->setContentsMargins(0, 1, 0, 0);
    badgeWrapLayout->setSpacing(0);
    badgeWrapLayout->addWidget(badge, 0, Qt::AlignTop);

    auto* lblAuthor = new QLabel(authorDisplay.toHtmlEscaped(), wrap);
    lblAuthor->setTextFormat(Qt::RichText);
    lblAuthor->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    lblAuthor->setFocusPolicy(Qt::NoFocus);
    lblAuthor->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    lblAuthor->setStyleSheet(message.platform == PlatformId::YouTube
            ? QStringLiteral("color:#6A3FA0; font-weight:700; font-size:%1px;%2").arg(fontSize).arg(fontExtraStyle)
            : QStringLiteral("color:#D17A00; font-weight:700; font-size:%1px;%2").arg(fontSize).arg(fontExtraStyle));

    auto* lblMessage = new QLabel(message.text.toHtmlEscaped(), wrap);
    lblMessage->setWordWrap(true);
    lblMessage->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    lblMessage->setFocusPolicy(Qt::NoFocus);
    lblMessage->setStyleSheet(QStringLiteral("color:#111111; font-size:%1px; font-weight:600;%2").arg(fontSize).arg(fontExtraStyle));

    const QString timestampText = message.timestamp.isValid()
        ? message.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : QString();
    auto* lblTimestamp = new QLabel(timestampText, wrap);
    lblTimestamp->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    lblTimestamp->setFocusPolicy(Qt::NoFocus);
    lblTimestamp->setStyleSheet(QStringLiteral("color:#999999; font-size:%1px;%2").arg(timestampFontSize).arg(fontExtraStyle));

    auto* headLayout = new QHBoxLayout;
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(8);
    headLayout->addWidget(badgeWrap, 0, Qt::AlignVCenter);
    headLayout->addWidget(lblAuthor, 0, Qt::AlignVCenter);
    headLayout->addWidget(lblTimestamp, 0, Qt::AlignVCenter);
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
    return displayAuthorLabel(message);
}

QString MainWindow::displayAuthorLabel(const UnifiedChatMessage& message) const
{
    if (message.platform == PlatformId::YouTube) {
        const QString selfHandle = normalizeYouTubeHandle(m_snapshot.youtube.accountLabel);
        if (!selfHandle.isEmpty() && !m_snapshot.youtube.channelId.trimmed().isEmpty()
            && message.authorId.trimmed() == m_snapshot.youtube.channelId.trimmed()) {
            return selfHandle;
        }

        const QString authorId = message.authorId.trimmed();
        if (!authorId.isEmpty()) {
            const QString cachedHandle = normalizeYouTubeHandle(m_youtubeAuthorHandleCache.value(authorId));
            if (!cachedHandle.isEmpty()) {
                return cachedHandle;
            }
        }

        const QString rawHandle = normalizeYouTubeHandle(message.rawAuthorDisplayName);
        if (!rawHandle.isEmpty()) {
            return rawHandle;
        }

        const QString normalizedAuthorName = normalizeYouTubeHandle(message.authorName);
        if (!normalizedAuthorName.isEmpty()) {
            return normalizedAuthorName;
        }
    }

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

QString MainWindow::normalizeYouTubeHandle(const QString& value) const
{
    QString normalized = value.trimmed();
    if (normalized.isEmpty()) {
        return QString();
    }
    if (normalized.startsWith(QStringLiteral("@"))) {
        return normalized;
    }
    if (normalized.contains(QLatin1Char(' '))) {
        return QString();
    }
    return QStringLiteral("@%1").arg(normalized);
}

void MainWindow::maybeQueueYouTubeAuthorHandleLookup(const UnifiedChatMessage& message)
{
    if (message.platform != PlatformId::YouTube) {
        return;
    }

    const QString authorId = message.authorId.trimmed();
    if (authorId.isEmpty()) {
        return;
    }

    const QString rawHandle = normalizeYouTubeHandle(message.rawAuthorDisplayName);
    if (!rawHandle.isEmpty()) {
        m_youtubeAuthorHandleCache.insert(authorId, rawHandle);
        return;
    }

    if (m_youtubeAuthorHandleCache.contains(authorId) || m_youtubeAuthorHandlePending.contains(authorId)) {
        return;
    }

    m_youtubeAuthorHandlePending.insert(authorId);
    m_youtubeAuthorHandleLookupQueue.append(authorId);
    if (m_youtubeAuthorLookupTimer && !m_youtubeAuthorLookupTimer->isActive()) {
        m_youtubeAuthorLookupTimer->start(150);
    }
}

void MainWindow::flushYouTubeAuthorHandleLookupQueue()
{
    if (m_pendingYouTubeAuthorLookup || m_youtubeAuthorHandleLookupQueue.isEmpty()) {
        return;
    }

    TokenRecord record;
    if (!m_tokenVault.read(PlatformId::YouTube, &record) || record.accessToken.trimmed().isEmpty()) {
        return;
    }

    QStringList authorIds;
    while (!m_youtubeAuthorHandleLookupQueue.isEmpty() && authorIds.size() < 50) {
        const QString id = m_youtubeAuthorHandleLookupQueue.takeFirst().trimmed();
        if (!id.isEmpty() && !authorIds.contains(id)) {
            authorIds.append(id);
        }
    }
    if (authorIds.isEmpty()) {
        return;
    }

    QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/channels"));
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("snippet"));
    query.addQueryItem(QStringLiteral("id"), authorIds.join(QLatin1Char(',')));
    query.addQueryItem(QStringLiteral("maxResults"), QString::number(authorIds.size()));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(record.accessToken.trimmed()).toUtf8());

    QNetworkReply* reply = m_networkAccessManager.get(req);
    if (!reply) {
        for (const QString& id : authorIds) {
            m_youtubeAuthorHandlePending.remove(id);
        }
        return;
    }
    m_pendingYouTubeAuthorLookup = true;

    connect(reply, &QNetworkReply::finished, this, [this, reply, authorIds]() {
        m_pendingYouTubeAuthorLookup = false;

        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        QJsonObject obj;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            obj = doc.object();
        }

        QSet<QString> unresolved = QSet<QString>(authorIds.begin(), authorIds.end());
        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        if (reply->error() == QNetworkReply::NoError && httpOk) {
            const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
            for (const QJsonValue& value : items) {
                const QJsonObject item = value.toObject();
                const QString channelId = item.value(QStringLiteral("id")).toString().trimmed();
                const QString customUrl = item.value(QStringLiteral("snippet")).toObject().value(QStringLiteral("customUrl")).toString().trimmed();
                if (!channelId.isEmpty()) {
                    unresolved.remove(channelId);
                }
                const QString normalized = normalizeYouTubeHandle(customUrl);
                if (!channelId.isEmpty() && !normalized.isEmpty()) {
                    m_youtubeAuthorHandleCache.insert(channelId, normalized);
                }
            }

            bool updated = false;
            for (UnifiedChatMessage& message : m_chatMessages) {
                if (message.platform != PlatformId::YouTube) {
                    continue;
                }
                const QString cachedHandle = m_youtubeAuthorHandleCache.value(message.authorId.trimmed());
                if (!cachedHandle.isEmpty() && message.rawAuthorDisplayName != cachedHandle) {
                    message.rawAuthorDisplayName = cachedHandle;
                    updated = true;
                }
            }
            if (updated) {
                rebuildChatterStatsFromMessages();
                rebuildChatTable();
                refreshChatterListDialog();
                updateActionPanel();
            }
        }

        for (const QString& id : authorIds) {
            m_youtubeAuthorHandlePending.remove(id);
        }
        if (!m_youtubeAuthorHandleLookupQueue.isEmpty() && m_youtubeAuthorLookupTimer) {
            m_youtubeAuthorLookupTimer->start(150);
        }
        reply->deleteLater();
    });
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
        statusBar()->showMessage(tr("No chat selected."), 1500);
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
    statusBar()->showMessage(tr("Selected chat copied."), 1500);
}

void MainWindow::onActionSendMessage()
{
    sendComposedMessage();
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

void MainWindow::onComposerHistoryPrevRequested()
{
    if (!m_edtComposer || m_sendHistory.isEmpty()) {
        return;
    }
    if (m_sendHistoryIndex >= m_sendHistory.size()) {
        m_sendHistoryDraft = m_edtComposer->toPlainText();
        m_sendHistoryIndex = m_sendHistory.size() - 1;
        applyComposerHistoryText(m_sendHistory.at(m_sendHistoryIndex));
        return;
    }
    if (m_sendHistoryIndex > 0) {
        --m_sendHistoryIndex;
        applyComposerHistoryText(m_sendHistory.at(m_sendHistoryIndex));
    }
}

void MainWindow::onComposerHistoryNextRequested()
{
    if (!m_edtComposer || m_sendHistory.isEmpty()) {
        return;
    }
    if (m_sendHistoryIndex < 0) {
        m_sendHistoryIndex = m_sendHistory.size();
    }
    if (m_sendHistoryIndex < m_sendHistory.size() - 1) {
        ++m_sendHistoryIndex;
        applyComposerHistoryText(m_sendHistory.at(m_sendHistoryIndex));
        return;
    }
    if (m_sendHistoryIndex == m_sendHistory.size() - 1) {
        m_sendHistoryIndex = m_sendHistory.size();
        applyComposerHistoryText(m_sendHistoryDraft);
    }
}

void MainWindow::updateActionPanel()
{
    const UnifiedChatMessage* msg = selectedChatMessage();
    const QMap<PlatformId, bool> connections = currentConnections();
    if (!msg) {
        m_lblSelectedPlatform->setText(QStringLiteral("-"));
        m_lblSelectedAuthor->setText(QStringLiteral("-"));
        m_lblSelectedMessage->setText(QStringLiteral("-"));

        const QString reason = tr("Select a chat message.");
        setActionButtonState(m_btnActionRestrictUser, false, reason);
        setActionButtonState(m_btnActionYoutubeDeleteMessage, false, reason);
        setActionButtonState(m_btnActionYoutubeTimeout, false, reason);
        setActionButtonState(m_btnActionChzzkRestrict, false, reason);
        updateComposerUiState();
        return;
    }

    m_lblSelectedPlatform->setText(msg->platform == PlatformId::YouTube ? tr("YouTube") : tr("CHZZK"));
    const QString authorDisplay = messengerAuthorLabel(*msg);
    if (!msg->authorId.trimmed().isEmpty() && authorDisplay != msg->authorId.trimmed()) {
        m_lblSelectedAuthor->setText(QStringLiteral("%1 (%2)").arg(authorDisplay, msg->authorId));
    } else {
        m_lblSelectedAuthor->setText(authorDisplay);
    }
    m_lblSelectedMessage->setText(msg->text);

    const bool platformReady = connections.value(msg->platform, false) && isPlatformLiveOnline(msg->platform);
    setActionButtonState(m_btnActionRestrictUser, !msg->authorId.isEmpty() && platformReady, tr("Missing author id or platform not ready."));

    if (msg->platform == PlatformId::YouTube) {
        const bool youtubeReady = connections.value(PlatformId::YouTube, false) && isPlatformLiveOnline(PlatformId::YouTube);
        setActionButtonState(m_btnActionYoutubeDeleteMessage, !msg->messageId.isEmpty() && youtubeReady, tr("Missing message id or YouTube not ready."));
        setActionButtonState(m_btnActionYoutubeTimeout, !msg->authorId.isEmpty() && youtubeReady, tr("Missing author id or YouTube not ready."));
        setActionButtonState(m_btnActionChzzkRestrict, false, tr("Not a CHZZK message."));
    } else {
        setActionButtonState(m_btnActionYoutubeDeleteMessage, false, tr("Not a YouTube message."));
        setActionButtonState(m_btnActionYoutubeTimeout, false, tr("Not a YouTube message."));
        const bool chzzkReady = connections.value(PlatformId::Chzzk, false) && isPlatformLiveOnline(PlatformId::Chzzk);
        setActionButtonState(m_btnActionChzzkRestrict, !msg->authorId.isEmpty() && chzzkReady, tr("Missing author id or CHZZK not ready."));
    }
    updateComposerUiState();
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
        statusBar()->showMessage(tr("No chat selected."), 2500);
        return;
    }

    const ActionExecutionResult result = m_actionExecutor.execute(actionId, *msg, currentConnections());
    if (result.ok) {
        m_txtEventLog->append(QStringLiteral("[ACTION-OK] %1 platform=%2 author=%3 messageId=%4 msg=%5")
                                  .arg(actionId, platformKey(msg->platform), displayAuthorLabel(*msg), msg->messageId, result.message));
        statusBar()->showMessage(tr("Action executed: %1").arg(actionId), 2500);
        return;
    }

    m_txtEventLog->append(QStringLiteral("[ACTION-FAIL] %1 code=%2 message=%3")
                              .arg(actionId, result.errorCode, result.message));
    statusBar()->showMessage(tr("Action failed: %1 (%2)").arg(actionId, result.errorCode), 3000);
}

void MainWindow::updateComposerUiState()
{
    const bool ytReady = m_youtubeAdapter.isConnected() && isPlatformLiveOnline(PlatformId::YouTube);
    const bool chzReady = m_chzzkAdapter.isConnected() && isPlatformLiveOnline(PlatformId::Chzzk);
    const bool anyPlatformReady = ytReady || chzReady;
    const QString text = m_edtComposer ? m_edtComposer->toPlainText() : QString();
    const bool hasText = !text.trimmed().isEmpty();
    const bool enabled = hasText && anyPlatformReady;

    QString reason;
    if (!hasText) {
        reason = tr("Input message first.");
    } else if (!anyPlatformReady) {
        reason = tr("Both platforms are disconnected or live is offline.");
    }

    setActionButtonState(m_btnActionSendMessage, enabled, reason);
    setActionButtonState(m_btnComposerSend, enabled, reason);
}

void MainWindow::sendComposedMessage()
{
    if (!m_edtComposer) {
        return;
    }
    const QString text = m_edtComposer->toPlainText();
    if (text.trimmed().isEmpty()) {
        statusBar()->showMessage(tr("Input message first."), 2000);
        return;
    }

    int started = 0;
    if (dispatchSendToYouTube(text)) {
        ++started;
    }
    if (dispatchSendToChzzk(text)) {
        ++started;
    }

    if (started <= 0) {
        statusBar()->showMessage(tr("No platform is ready to send."), 2500);
        return;
    }

    pushComposerHistory(text);
    m_composerApplyingHistory = true;
    m_edtComposer->clear();
    m_composerApplyingHistory = false;
    m_sendHistoryDraft.clear();
    m_sendHistoryIndex = m_sendHistory.size();
    updateComposerUiState();
    m_txtEventLog->append(QStringLiteral("[SEND-SUMMARY] dispatched=%1").arg(started));
}

bool MainWindow::dispatchSendToYouTube(const QString& text)
{
    if (!m_youtubeAdapter.isConnected() || !isPlatformLiveOnline(PlatformId::YouTube)) {
        return false;
    }

    TokenRecord record;
    if (!m_tokenVault.read(PlatformId::YouTube, &record) || record.accessToken.trimmed().isEmpty()) {
        m_txtEventLog->append(QStringLiteral("[SEND-FAIL] youtube code=TOKEN_MISSING msg=No access token"));
        return false;
    }

    const QString liveChatId = m_youtubeAdapter.currentLiveChatId().trimmed();
    if (liveChatId.isEmpty()) {
        m_txtEventLog->append(QStringLiteral("[SEND-FAIL] youtube code=LIVECHAT_ID_MISSING msg=liveChatId unavailable"));
        return false;
    }

    QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/liveChat/messages"));
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("snippet"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(record.accessToken.trimmed()).toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject payload;
    QJsonObject snippet;
    QJsonObject details;
    details.insert(QStringLiteral("messageText"), text);
    snippet.insert(QStringLiteral("liveChatId"), liveChatId);
    snippet.insert(QStringLiteral("type"), QStringLiteral("textMessageEvent"));
    snippet.insert(QStringLiteral("textMessageDetails"), details);
    payload.insert(QStringLiteral("snippet"), snippet);

    QNetworkReply* reply = m_networkAccessManager.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    if (!reply) {
        m_txtEventLog->append(QStringLiteral("[SEND-FAIL] youtube code=REQUEST_FAILED msg=Failed to create send request"));
        return false;
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        QJsonObject obj;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            obj = doc.object();
        }

        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        if (reply->error() != QNetworkReply::NoError || !httpOk) {
            const QString apiMessage = obj.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString().trimmed();
            const QString message = apiMessage.isEmpty() ? reply->errorString() : apiMessage;
            m_txtEventLog->append(QStringLiteral("[SEND-FAIL] youtube code=HTTP_%1 msg=%2")
                                      .arg(httpStatus)
                                      .arg(message));
            reply->deleteLater();
            return;
        }

        const QString messageId = obj.value(QStringLiteral("id")).toString().trimmed();
        m_txtEventLog->append(QStringLiteral("[SEND-OK] youtube messageId=%1")
                                  .arg(messageId.isEmpty() ? QStringLiteral("-") : messageId));
        reply->deleteLater();
    });
    return true;
}

bool MainWindow::dispatchSendToChzzk(const QString& text)
{
    if (!m_chzzkAdapter.isConnected() || !isPlatformLiveOnline(PlatformId::Chzzk)) {
        return false;
    }

    TokenRecord record;
    if (!m_tokenVault.read(PlatformId::Chzzk, &record) || record.accessToken.trimmed().isEmpty()) {
        m_txtEventLog->append(QStringLiteral("[SEND-FAIL] chzzk code=TOKEN_MISSING msg=No access token"));
        return false;
    }

    QNetworkRequest req(QUrl(QStringLiteral("https://openapi.chzzk.naver.com/open/v1/chats/send")));
    req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(record.accessToken.trimmed()).toUtf8());
    if (!m_snapshot.chzzk.clientId.trimmed().isEmpty()) {
        req.setRawHeader("Client-Id", m_snapshot.chzzk.clientId.trimmed().toUtf8());
    }
    if (!m_snapshot.chzzk.clientSecret.trimmed().isEmpty()) {
        req.setRawHeader("Client-Secret", m_snapshot.chzzk.clientSecret.trimmed().toUtf8());
    }
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject payload;
    payload.insert(QStringLiteral("message"), text);
    QNetworkReply* reply = m_networkAccessManager.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    if (!reply) {
        m_txtEventLog->append(QStringLiteral("[SEND-FAIL] chzzk code=REQUEST_FAILED msg=Failed to create send request"));
        return false;
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        QJsonObject obj;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            obj = doc.object();
        }

        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        const int apiCode = obj.value(QStringLiteral("code")).toInt(200);
        if (reply->error() != QNetworkReply::NoError || !httpOk || apiCode != 200) {
            const QString apiMessage = obj.value(QStringLiteral("message")).toString().trimmed();
            const QString message = apiMessage.isEmpty() ? reply->errorString() : apiMessage;
            m_txtEventLog->append(QStringLiteral("[SEND-FAIL] chzzk code=HTTP_%1 msg=%2")
                                      .arg(httpStatus)
                                      .arg(message));
            reply->deleteLater();
            return;
        }

        m_txtEventLog->append(QStringLiteral("[SEND-OK] chzzk"));
        reply->deleteLater();
    });
    return true;
}

void MainWindow::pushComposerHistory(const QString& text)
{
    const QString normalized = text;
    if (normalized.trimmed().isEmpty()) {
        return;
    }
    if (!m_sendHistory.isEmpty() && m_sendHistory.back() == normalized) {
        m_sendHistoryIndex = m_sendHistory.size();
        return;
    }
    m_sendHistory.push_back(normalized);
    while (m_sendHistory.size() > 100) {
        m_sendHistory.removeFirst();
    }
    m_sendHistoryIndex = m_sendHistory.size();
}

void MainWindow::applyComposerHistoryText(const QString& text)
{
    if (!m_edtComposer) {
        return;
    }
    m_composerApplyingHistory = true;
    m_edtComposer->setPlainText(text);
    auto cursor = m_edtComposer->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_edtComposer->setTextCursor(cursor);
    m_composerApplyingHistory = false;
    updateComposerUiState();
}

void MainWindow::refreshTokenUi(PlatformId platform)
{
    TokenRecord record;
    if (!m_tokenVault.read(platform, &record)) {
        TokenRecord empty;
        m_configurationDialog->onTokenRecordUpdated(platform, TokenState::NO_TOKEN, empty, tr("No token"));
        refreshPlatformIndicator(platform);
        return;
    }

    const TokenState state = inferTokenState(&record);
    m_configurationDialog->onTokenRecordUpdated(platform, state, record, tr("Loaded from vault"));
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
    // Token indicator must be independent from live probe/runtime warnings.
    // Live/runtime problems are shown by platform status text + live indicator.
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
    QString stateText = tr("TOKEN_BAD");
    switch (state) {
    case PlatformVisualState::AUTH_IN_PROGRESS:
        color = QStringLiteral("#FFB74D");
        stateText = tr("AUTH_IN_PROGRESS");
        break;
    case PlatformVisualState::ONLINE:
        color = QStringLiteral("#81D4FA");
        stateText = tr("ONLINE");
        break;
    case PlatformVisualState::TOKEN_OK:
        color = QStringLiteral("#66BB6A");
        stateText = tr("TOKEN_OK");
        break;
    case PlatformVisualState::TOKEN_BAD:
        break;
    }

    indicator->setStyleSheet(QStringLiteral("background-color:%1; border:1px solid #666666; border-radius:2px;").arg(color));
    QString tooltip = tr("%1: %2").arg(platform == PlatformId::YouTube ? QStringLiteral("YouTube") : QStringLiteral("CHZZK"), stateText);
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
    probeLiveStatus(PlatformId::YouTube);
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    if (!m_nextPeriodicChzzkProbeAtUtc.isValid() || nowUtc >= m_nextPeriodicChzzkProbeAtUtc) {
        probeLiveStatus(PlatformId::Chzzk);
        m_nextPeriodicChzzkProbeAtUtc = nowUtc.addSecs(10);
    }
}

void MainWindow::probeLiveStatus(PlatformId platform)
{
    const PlatformSettings settings = settingsFor(platform);
    if (!settings.enabled) {
        setLiveBroadcastState(platform, LiveBroadcastState::UNKNOWN, tr("Disabled"));
        return;
    }

    TokenRecord record;
    if (!m_tokenVault.read(platform, &record) || record.accessToken.trimmed().isEmpty()) {
        setLiveBroadcastState(platform, LiveBroadcastState::UNKNOWN, tr("No access token"));
        return;
    }

    const TokenState tokenState = inferTokenState(&record);
    if (tokenState == TokenState::EXPIRED || tokenState == TokenState::NO_TOKEN || tokenState == TokenState::AUTH_REQUIRED) {
        setLiveBroadcastState(platform, LiveBroadcastState::UNKNOWN, tr("Live check skipped: token unavailable"));
        return;
    }

    if (m_liveStates.value(platform, LiveBroadcastState::UNKNOWN) == LiveBroadcastState::UNKNOWN) {
        setLiveBroadcastState(platform, LiveBroadcastState::CHECKING, tr("Checking live status"));
    }

    if (platform == PlatformId::YouTube) {
        const QString phase = m_platformRuntimeStates.value(PlatformId::YouTube).phase.trimmed().toUpper();
        const bool adapterManaged = m_youtubeAdapter.isConnected()
            || phase == QStringLiteral("STARTING")
            || phase == QStringLiteral("CONNECTED")
            || phase == QStringLiteral("CONNECTED_NO_LIVECHAT");
        if (!adapterManaged) {
            setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::UNKNOWN, tr("Connect required for YouTube live check"));
        } else if (m_liveStates.value(PlatformId::YouTube, LiveBroadcastState::UNKNOWN) == LiveBroadcastState::UNKNOWN) {
            setLiveBroadcastState(PlatformId::YouTube, LiveBroadcastState::CHECKING, tr("Waiting for YouTube adapter live state"));
        }
        return;
    }

    m_nextPeriodicChzzkProbeAtUtc = QDateTime::currentDateTimeUtc().addSecs(10);
    if (!m_pendingChzzkLiveProbe) {
        probeChzzkLiveStatus(record.accessToken);
    }
}

void MainWindow::probeChzzkLiveStatus(const QString& accessToken)
{
    const QString channelId = m_snapshot.chzzk.channelId.trimmed();
    if (channelId.isEmpty()) {
        const QString token = accessToken.trimmed();
        if (!token.isEmpty()) {
            syncChzzkProfileFromAccessToken(token);
        }
        setLiveBroadcastState(PlatformId::Chzzk, LiveBroadcastState::UNKNOWN, tr("channel_id missing (syncing profile)"));
        return;
    }

    QUrl url(QStringLiteral("https://api.chzzk.naver.com/service/v3/channels/%1/live-detail")
                 .arg(QString::fromUtf8(QUrl::toPercentEncoding(channelId))));
    QNetworkRequest req(url);

    QNetworkReply* reply = m_networkAccessManager.get(req);
    if (!reply) {
        setLiveBroadcastState(PlatformId::Chzzk, LiveBroadcastState::ERROR, tr("Failed to create live-detail request"));
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
                liveTitle.isEmpty() ? tr("Active broadcast detected") : liveTitle);
        } else {
            setLiveBroadcastState(PlatformId::Chzzk, LiveBroadcastState::OFFLINE,
                status.isEmpty() ? tr("No active broadcast") : status);
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
        return tr("UNKNOWN");
    case LiveBroadcastState::CHECKING:
        return tr("CHECKING");
    case LiveBroadcastState::ONLINE:
        return tr("ONLINE");
    case LiveBroadcastState::OFFLINE:
        return tr("OFFLINE");
    case LiveBroadcastState::ERROR:
        return tr("ERROR");
    }
    return tr("UNKNOWN");
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

    // Live status uses a palette independent from token/runtime palette.
    QString color = QStringLiteral("#90A4AE");
    switch (state) {
    case LiveBroadcastState::UNKNOWN:
        color = QStringLiteral("#90A4AE");
        break;
    case LiveBroadcastState::CHECKING:
        color = QStringLiteral("#5C6BC0");
        break;
    case LiveBroadcastState::ONLINE:
        color = QStringLiteral("#26A69A");
        break;
    case LiveBroadcastState::OFFLINE:
        color = QStringLiteral("#BCAAA4");
        break;
    case LiveBroadcastState::ERROR:
        color = QStringLiteral("#8E24AA");
        break;
    }

    indicator->setStyleSheet(QStringLiteral("background-color:%1; border:1px solid #666666; border-radius:2px;").arg(color));

    const QString stateText = liveBroadcastStateText(state);
    const QString platformText = platform == PlatformId::YouTube ? QStringLiteral("YouTube") : QStringLiteral("CHZZK");
    label->setText(tr("%1 Live: %2").arg(platformText, stateText));
    const QString tooltip = detail.isEmpty()
        ? tr("%1 live state: %2").arg(platformText, stateText)
        : tr("%1 live state: %2\n%3").arg(platformText, stateText, detail);
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

        // Runtime-only: cache the bot's own profile for author display (never persist to INI)
        const QString selfHandle = !channelHandle.isEmpty() ? channelHandle : channelName;
        const QString normalizedSelfHandle = normalizeYouTubeHandle(selfHandle);
        if (!channelId.isEmpty() && !normalizedSelfHandle.isEmpty()) {
            m_youtubeAuthorHandleCache.insert(channelId, normalizedSelfHandle);
        }

        const QString synced = QStringLiteral("channelId=%1 handle=%2 channelName=%3")
                                   .arg(channelId,
                                       selfHandle.isEmpty() ? QStringLiteral("-") : selfHandle,
                                       channelName.isEmpty() ? QStringLiteral("-") : channelName);
        m_txtEventLog->append(QStringLiteral("[YOUTUBE-PROFILE] channels.mine synchronized %1").arg(synced));
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

        // Runtime-only: log the profile for diagnostics (never persist to INI)
        const QString synced = QStringLiteral("channelId=%1 channelName=%2")
                                   .arg(channelId,
                                       channelName.isEmpty() ? QStringLiteral("-") : channelName);
        m_txtEventLog->append(QStringLiteral("[CHZZK-PROFILE] users/me synchronized %1").arg(synced));
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
        const QString detail = tr("Startup auto refresh skipped: refresh token missing.");
        m_txtEventLog->append(QStringLiteral("[TOKEN-AUTO-REFRESH-SKIP] %1 %2").arg(platformKey(platform), detail));
        appendTokenAudit(platform, QStringLiteral("token_refresh"), false, detail);
        return;
    }

    if (settings.clientId.trimmed().isEmpty() || settings.tokenEndpoint.trimmed().isEmpty()) {
        const QString detail = tr("Startup auto refresh skipped: OAuth config incomplete.");
        m_txtEventLog->append(QStringLiteral("[TOKEN-AUTO-REFRESH-SKIP] %1 %2").arg(platformKey(platform), detail));
        appendTokenAudit(platform, QStringLiteral("token_refresh"), false, detail);
        return;
    }
    if (platform == PlatformId::Chzzk && settings.clientSecret.trimmed().isEmpty()) {
        const QString detail = tr("Startup auto refresh skipped: CHZZK client_secret missing.");
        m_txtEventLog->append(QStringLiteral("[TOKEN-AUTO-REFRESH-SKIP] %1 %2").arg(platformKey(platform), detail));
        appendTokenAudit(platform, QStringLiteral("token_refresh"), false, detail);
        return;
    }

    m_configurationDialog->onTokenOperationStarted(platform, QStringLiteral("token_refresh"));
    m_configurationDialog->onTokenStateUpdated(platform, TokenState::REFRESHING, tr("Startup auto refresh..."));
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

void MainWindow::applyRuntimeAccessTokenToAdapter(PlatformId platform, const QString& accessToken)
{
    if (platform == PlatformId::YouTube) {
        m_youtubeAdapter.applyRuntimeAccessToken(accessToken);
        return;
    }
    m_chzzkAdapter.applyRuntimeAccessToken(accessToken);
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
        m_configurationDialog->onTokenActionFinished(platform, false, tr("Silent refresh failed"));
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
        m_configurationDialog->onTokenActionFinished(platform, false, tr("Interactive re-auth failed"));
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
