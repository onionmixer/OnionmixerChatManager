#include "auth/TokenManager.h"
#include "auth/PkceUtil.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

namespace {

QString tmText(const char* sourceText)
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
        return tmText("YouTube client_id format invalid. Parsed value is empty.");
    }
    if (!parsedClientId.contains(QStringLiteral("googleusercontent.com"), Qt::CaseInsensitive)
        && parsedClientId.contains('.')) {
        return tmText("YouTube client_id format invalid. parsed=%1 (looks like bundle/package id). "
                      "Use OAuth client_id ending with *.googleusercontent.com")
            .arg(parsedClientId);
    }
    return tmText("YouTube client_id format invalid. expected=*.googleusercontent.com parsed=%1")
        .arg(parsedClientId);
}

bool looksLikeYouTubeClientSecretMismatch(const QString& errorCode, const QString& message)
{
    const QString normalizedCode = errorCode.trimmed().toLower();
    const QString normalizedMessage = message.trimmed().toLower();
    if (normalizedCode == QStringLiteral("invalid_client")) return true;
    if (normalizedMessage.contains(QStringLiteral("client_secret"))) return true;
    if (normalizedMessage.contains(QStringLiteral("unauthorized_client"))) return true;
    return false;
}

QString buildTokenFailureDetailWithGuidance(PlatformId platform, const QString& flow,
    const QString& errorCode, const QString& message, int httpStatus)
{
    const QString base = tmText("%1 (http=%2)").arg(message).arg(httpStatus);
    if (platform != PlatformId::YouTube || flow != QStringLiteral("authorization_code")) {
        return base;
    }
    if (!looksLikeYouTubeClientSecretMismatch(errorCode, message)) {
        return base;
    }
    return base + tmText(
                      "\nHint: YouTube AUTH_CODE_GRANT failed at token exchange."
                      "\n- Use OAuth client type: Desktop app"
                      "\n- Use client_id ending with .apps.googleusercontent.com"
                      "\n- If error says 'client_secret is missing', set YouTube Client Secret in Configuration"
                      "\n- Ensure the same Google project is used for consent screen + test user + client_id");
}

} // namespace

TokenManager::TokenManager(const AppSettingsSnapshot* snapshotRef, const QString& configDir,
                           QNetworkAccessManager* network, QObject* parent)
    : QObject(parent)
    , m_snapshot(snapshotRef)
    , m_tokenVault(configDir)
    , m_oauthTokenClient(network, this)
{
    connect(&m_oauthLocalServer, &OAuthLocalServer::callbackReceived,
        this, &TokenManager::onOAuthCallbackReceived);
    connect(&m_oauthLocalServer, &OAuthLocalServer::sessionFailed,
        this, &TokenManager::onOAuthSessionFailed);
    connect(&m_oauthTokenClient, &OAuthTokenClient::tokenGranted,
        this, &TokenManager::onTokenGranted);
    connect(&m_oauthTokenClient, &OAuthTokenClient::tokenFailed,
        this, &TokenManager::onTokenFailed);
    connect(&m_oauthTokenClient, &OAuthTokenClient::tokenRevoked,
        this, &TokenManager::onTokenRevoked);
    connect(&m_oauthTokenClient, &OAuthTokenClient::tokenRevokeFailed,
        this, &TokenManager::onTokenRevokeFailed);
}

PlatformSettings TokenManager::settingsFor(PlatformId platform) const
{
    if (!m_snapshot) return PlatformSettings();
    return platform == PlatformId::YouTube ? m_snapshot->youtube : m_snapshot->chzzk;
}

TokenState TokenManager::inferTokenState(const TokenRecord* record) const
{
    if (!record) return TokenState::NO_TOKEN;
    if (record->accessToken.trimmed().isEmpty() && record->refreshToken.trimmed().isEmpty()) {
        return TokenState::NO_TOKEN;
    }
    if (!record->accessExpireAtUtc.isValid()) {
        return record->accessToken.trimmed().isEmpty() ? TokenState::EXPIRED : TokenState::VALID;
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (now >= record->accessExpireAtUtc) return TokenState::EXPIRED;
    if (now.secsTo(record->accessExpireAtUtc) <= 600) return TokenState::EXPIRING_SOON;
    return TokenState::VALID;
}

bool TokenManager::readToken(PlatformId platform, TokenRecord* record) const
{
    return m_tokenVault.read(platform, record);
}

bool TokenManager::isAuthInProgress(PlatformId platform) const
{
    return m_authInProgress.value(platform, false);
}

void TokenManager::refreshTokenUi(PlatformId platform)
{
    TokenRecord record;
    if (!m_tokenVault.read(platform, &record)) {
        TokenRecord empty;
        emit tokenRecordUpdated(platform, TokenState::NO_TOKEN, empty, tmText("No token"));
        return;
    }
    const TokenState state = inferTokenState(&record);
    emit tokenRecordUpdated(platform, state, record, tmText("Loaded from vault"));
}

void TokenManager::refreshAllTokenUi()
{
    refreshTokenUi(PlatformId::YouTube);
    refreshTokenUi(PlatformId::Chzzk);
}

void TokenManager::appendTokenAudit(PlatformId platform, const QString& action, bool ok, const QString& detail)
{
    emit tokenAuditEntry(platform, action, ok, detail);
}

QUrl TokenManager::buildAuthorizationUrl(PlatformId platform, const PlatformSettings& settings,
    const QString& state, const QString& codeChallenge) const
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

QString TokenManager::createOAuthState(PlatformId platform) const
{
    return QStringLiteral("%1_%2").arg(platformKey(platform), QUuid::createUuid().toString(QUuid::WithoutBraces));
}

bool TokenManager::startTokenRefreshFlow(PlatformId platform, const PlatformSettings& settings, const TokenRecord& currentRecord)
{
    PendingTokenFlowContext ctx;
    ctx.flow = QStringLiteral("refresh_token");
    ctx.settings = settings;
    ctx.previousRecord = currentRecord;
    m_pendingTokenFlows.insert(platform, ctx);
    return m_oauthTokenClient.requestRefreshToken(platform, settings, currentRecord.refreshToken);
}

bool TokenManager::startAuthCodeExchangeFlow(PlatformId platform, const PlatformSettings& settings,
    const QString& code, const QString& codeVerifier, const QString& authState)
{
    TokenRecord currentRecord;
    m_tokenVault.read(platform, &currentRecord);
    PendingTokenFlowContext ctx;
    ctx.flow = QStringLiteral("authorization_code");
    ctx.settings = settings;
    ctx.previousRecord = currentRecord;
    m_pendingTokenFlows.insert(platform, ctx);
    return m_oauthTokenClient.requestAuthorizationCodeToken(platform, settings, code, codeVerifier, authState);
}

void TokenManager::tryStartupTokenRefresh()
{
    tryStartupTokenRefreshForPlatform(PlatformId::YouTube);
    tryStartupTokenRefreshForPlatform(PlatformId::Chzzk);
}

void TokenManager::tryStartupTokenRefreshForPlatform(PlatformId platform)
{
    const PlatformSettings settings = settingsFor(platform);
    if (!settings.enabled) return;
    if (m_pendingTokenFlows.contains(platform) || m_pendingTokenRevokes.value(platform, false)) return;

    TokenRecord record;
    if (!m_tokenVault.read(platform, &record)) return;

    const TokenState state = inferTokenState(&record);
    if (state != TokenState::EXPIRED && state != TokenState::EXPIRING_SOON) return;

    if (record.refreshToken.trimmed().isEmpty()) {
        const QString detail = tmText("Startup auto refresh skipped: refresh token missing.");
        emit logMessage(QStringLiteral("[TOKEN-AUTO-REFRESH-SKIP] %1 %2").arg(platformKey(platform), detail));
        appendTokenAudit(platform, QStringLiteral("token_refresh"), false, detail);
        return;
    }

    if (settings.clientId.trimmed().isEmpty() || settings.tokenEndpoint.trimmed().isEmpty()) {
        const QString detail = tmText("Startup auto refresh skipped: OAuth config incomplete.");
        emit logMessage(QStringLiteral("[TOKEN-AUTO-REFRESH-SKIP] %1 %2").arg(platformKey(platform), detail));
        appendTokenAudit(platform, QStringLiteral("token_refresh"), false, detail);
        return;
    }
    if (platform == PlatformId::Chzzk && settings.clientSecret.trimmed().isEmpty()) {
        const QString detail = tmText("Startup auto refresh skipped: Chzzk client_secret missing.");
        emit logMessage(QStringLiteral("[TOKEN-AUTO-REFRESH-SKIP] %1 %2").arg(platformKey(platform), detail));
        appendTokenAudit(platform, QStringLiteral("token_refresh"), false, detail);
        return;
    }

    emit tokenOperationStarted(platform, QStringLiteral("token_refresh"));
    emit tokenStateChanged(platform, TokenState::REFRESHING, tmText("Startup auto refresh..."));
    if (!startTokenRefreshFlow(platform, settings, record)) {
        const QString detail = tmText("Failed to start refresh flow.");
        emit tokenStateChanged(platform, TokenState::ERROR, detail);
        emit tokenActionFinished(platform, false, tmText("Silent refresh failed"));
        appendTokenAudit(platform, QStringLiteral("token_refresh"), false, detail);
    }
}

void TokenManager::onTokenRefreshRequested(PlatformId platform, const PlatformSettings& settings)
{
    if (m_pendingTokenFlows.contains(platform)) {
        emit tokenActionFinished(platform, false, tmText("Another token operation is in progress"));
        appendTokenAudit(platform, QStringLiteral("token_refresh"), false, tmText("Another token operation is in progress"));
        return;
    }
    TokenRecord record;
    if (!m_tokenVault.read(platform, &record) || record.refreshToken.trimmed().isEmpty()) {
        emit tokenStateChanged(platform, TokenState::AUTH_REQUIRED, tmText("No refresh token. Browser re-auth required."));
        emit tokenActionFinished(platform, false, tmText("Refresh token not found"));
        appendTokenAudit(platform, QStringLiteral("token_refresh"), false, tmText("Refresh token not found"));
        return;
    }
    emit tokenOperationStarted(platform, QStringLiteral("refresh_token"));
    emit tokenStateChanged(platform, TokenState::REFRESHING, tmText("Refreshing token..."));
    if (!startTokenRefreshFlow(platform, settings, record)) {
        emit tokenActionFinished(platform, false, tmText("Failed to start refresh"));
        appendTokenAudit(platform, QStringLiteral("token_refresh"), false, tmText("Failed to start refresh"));
    }
}

void TokenManager::onTokenDeleteRequested(PlatformId platform)
{
    if (m_pendingTokenFlows.contains(platform) || m_pendingTokenRevokes.value(platform, false)) {
        emit tokenActionFinished(platform, false, tmText("Another token operation is in progress"));
        return;
    }

    TokenRecord record;
    const bool hadToken = m_tokenVault.read(platform, &record) && !record.accessToken.trimmed().isEmpty();
    const PlatformSettings settings = settingsFor(platform);

    if (hadToken && !record.refreshToken.trimmed().isEmpty()
        && !settings.tokenEndpoint.trimmed().isEmpty() && !settings.clientId.trimmed().isEmpty()) {
        m_pendingTokenRevokes.insert(platform, true);
        emit tokenOperationStarted(platform, QStringLiteral("token_revoke"));
        emit tokenStateChanged(platform, TokenState::REFRESHING, tmText("Revoking token..."));
        m_oauthTokenClient.requestRevokeToken(platform, settings, record);
    } else {
        const bool cleared = m_tokenVault.clear(platform);
        m_authInProgress.insert(platform, false);
        refreshTokenUi(platform);
        emit tokenUpdated(platform, QString());
        emit runtimePhaseChanged(platform, QStringLiteral("IDLE"));
        emit runtimeErrorCleared(platform);
        emit tokenActionFinished(platform, cleared,
            cleared ? tmText("Token cleared locally.") : tmText("Token clear failed."));
        appendTokenAudit(platform, QStringLiteral("token_delete"), cleared, tmText("Local clear only"));
        emit liveStateResetNeeded(platform);
    }
}

void TokenManager::onInteractiveAuthRequested(PlatformId platform, const PlatformSettings& settings)
{
    if (m_pendingTokenFlows.contains(platform) || m_oauthLocalServer.hasActiveSession(platform)) {
        emit tokenActionFinished(platform, false, tmText("Another token operation is in progress"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, tmText("Another token operation is in progress"));
        return;
    }
    if (settings.clientId.trimmed().isEmpty() || settings.redirectUri.trimmed().isEmpty() || settings.scope.trimmed().isEmpty()
        || settings.authEndpoint.trimmed().isEmpty() || settings.tokenEndpoint.trimmed().isEmpty()) {
        emit tokenStateChanged(platform, TokenState::AUTH_REQUIRED, tmText("Missing required OAuth fields"));
        emit tokenActionFinished(platform, false, tmText("Interactive re-auth blocked by invalid config"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, tmText("Invalid OAuth configuration"));
        return;
    }
    if (platform == PlatformId::YouTube && !isLikelyGoogleOAuthClientId(settings.clientId)) {
        const QString normalized = normalizeGoogleOAuthClientId(settings.clientId);
        const QString validationMessage = googleClientIdValidationMessage(normalized);
        emit tokenStateChanged(platform, TokenState::AUTH_REQUIRED, validationMessage);
        emit tokenActionFinished(platform, false, tmText("Interactive re-auth blocked by invalid client_id"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, validationMessage);
        return;
    }
    const QUrl redirectUri(settings.redirectUri);
    if (!redirectUri.isValid() || redirectUri.scheme() != QStringLiteral("http") || redirectUri.host() != QStringLiteral("127.0.0.1") || redirectUri.port() <= 0) {
        emit tokenStateChanged(platform, TokenState::AUTH_REQUIRED, tmText("redirect_uri must be http://127.0.0.1:{port}/..."));
        emit tokenActionFinished(platform, false, tmText("Interactive re-auth blocked by invalid redirect_uri"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, tmText("Invalid redirect_uri"));
        return;
    }
    const QString state = createOAuthState(platform);
    QString codeVerifier;
    QString codeChallenge;
    if (platform == PlatformId::YouTube) {
        codeVerifier = pkce::generateCodeVerifier();
        codeChallenge = pkce::makeCodeChallengeS256(codeVerifier);
        if (codeVerifier.trimmed().isEmpty() || codeChallenge.trimmed().isEmpty()) {
            emit tokenStateChanged(platform, TokenState::ERROR, tmText("Failed to prepare PKCE"));
            emit tokenActionFinished(platform, false, tmText("Interactive re-auth failed"));
            appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, tmText("Failed to prepare PKCE"));
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
        emit tokenStateChanged(platform, TokenState::ERROR, startError);
        emit tokenActionFinished(platform, false, tmText("Failed to start OAuth callback server"));
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
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, tmText("Invalid authorize URL"));
        return;
    }
    if (platform == PlatformId::Chzzk && authUrl.host().contains(QStringLiteral("example.com"))) {
        emit logMessage(QStringLiteral("[WARN] CHZZK OAuth endpoint is placeholder. Replace auth/token endpoint in Configuration."));
    }
    if (!QDesktopServices::openUrl(authUrl)) {
        m_oauthLocalServer.cancelSession(platform, QStringLiteral("OAUTH_BROWSER_OPEN_FAILED"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, tmText("Failed to open system browser"));
        return;
    }
    m_authInProgress.insert(platform, true);
    emit runtimePhaseChanged(platform, QStringLiteral("STARTING"));
    emit runtimeErrorCleared(platform);
    emit tokenOperationStarted(platform, QStringLiteral("interactive_auth"));
    emit tokenStateChanged(platform, TokenState::REFRESHING, tmText("Browser opened. Waiting callback..."));
    emit logMessage(QStringLiteral("[OAUTH] %1 browser auth started").arg(platformKey(platform)));
}

void TokenManager::onOAuthCallbackReceived(PlatformId platform, const QString& code, const QString& state,
    const QString& errorCode, const QString& errorDescription)
{
    const QString expectedState = m_pendingOAuthState.value(platform);
    const PlatformSettings settings = m_pendingOAuthSettings.value(platform);
    const QString codeVerifier = m_pendingPkceVerifier.value(platform);
    m_pendingOAuthState.remove(platform);
    m_pendingOAuthSettings.remove(platform);
    m_pendingPkceVerifier.remove(platform);

    if (!expectedState.isEmpty() && state != expectedState) {
        emit tokenStateChanged(platform, TokenState::ERROR, tmText("OAuth state mismatch"));
        emit tokenActionFinished(platform, false, tmText("Interactive re-auth failed"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, tmText("OAuth state mismatch"));
        emit runtimeErrorChanged(platform, QStringLiteral("OAUTH_STATE_MISMATCH"), tmText("OAuth state mismatch"));
        return;
    }
    if (!errorCode.isEmpty()) {
        const QString detail = QStringLiteral("%1: %2").arg(errorCode, errorDescription);
        emit tokenStateChanged(platform, TokenState::AUTH_REQUIRED, detail);
        emit tokenActionFinished(platform, false, tmText("Interactive re-auth canceled/failed"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, detail);
        emit runtimeErrorChanged(platform, QStringLiteral("OAUTH_CALLBACK_ERROR"), detail);
        return;
    }
    if (code.trimmed().isEmpty()) {
        emit tokenStateChanged(platform, TokenState::ERROR, tmText("OAuth code missing"));
        emit tokenActionFinished(platform, false, tmText("Interactive re-auth failed"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, tmText("OAuth code missing"));
        return;
    }
    if (platform == PlatformId::YouTube && codeVerifier.isEmpty()) {
        emit tokenStateChanged(platform, TokenState::ERROR, tmText("PKCE verifier missing"));
        emit tokenActionFinished(platform, false, tmText("Interactive re-auth failed"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, tmText("PKCE verifier missing"));
        return;
    }
    emit tokenStateChanged(platform, TokenState::REFRESHING, tmText("Exchanging token..."));
    if (!startAuthCodeExchangeFlow(platform, settings, code, codeVerifier, state)) {
        emit tokenStateChanged(platform, TokenState::ERROR, tmText("Failed to start token exchange"));
        emit tokenActionFinished(platform, false, tmText("Interactive re-auth failed"));
        appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, tmText("Failed to start token exchange"));
    }
}

void TokenManager::onOAuthSessionFailed(PlatformId platform, const QString& reason)
{
    m_authInProgress.insert(platform, false);
    m_pendingOAuthState.remove(platform);
    m_pendingOAuthSettings.remove(platform);
    m_pendingPkceVerifier.remove(platform);
    emit tokenStateChanged(platform, TokenState::AUTH_REQUIRED, reason);
    emit tokenActionFinished(platform, false, tmText("Interactive re-auth failed"));
    appendTokenAudit(platform, QStringLiteral("interactive_auth"), false, reason);
    emit runtimeErrorChanged(platform, QStringLiteral("OAUTH_SESSION_FAILED"), reason);
}

void TokenManager::onTokenGranted(PlatformId platform, const QString& flow,
    const QString& accessToken, const QString& refreshToken,
    int expiresInSec, int refreshExpiresInSec)
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
        emit runtimeErrorChanged(platform, QStringLiteral("TOKEN_REFRESH_MISSING"), tmText("Refresh token missing in response"));
        emit tokenStateChanged(platform, TokenState::AUTH_REQUIRED, tmText("Refresh token missing in response"));
        emit tokenActionFinished(platform, false, tmText("Token update failed"));
        appendTokenAudit(platform, flow, false, tmText("Refresh token missing in response"));
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
        emit runtimeErrorChanged(platform, QStringLiteral("TOKEN_VAULT_WRITE_FAILED"), tmText("Token vault write failed"));
        emit tokenStateChanged(platform, TokenState::ERROR, tmText("Token vault write failed"));
        emit tokenActionFinished(platform, false, tmText("Token update failed"));
        appendTokenAudit(platform, flow, false, tmText("Token vault write failed"));
        return;
    }
    emit runtimeErrorCleared(platform);
    refreshTokenUi(platform);
    emit tokenUpdated(platform, accessToken);
    const bool isRefresh = flow == QStringLiteral("refresh_token");
    const QString message = isRefresh ? tmText("Silent refresh success") : tmText("Interactive re-auth success");
    emit tokenActionFinished(platform, true, message);
    appendTokenAudit(platform, flow, true, message);
    emit logMessage(QStringLiteral("[TOKEN-OK] %1 flow=%2").arg(platformKey(platform), flow));
    emit profileSyncNeeded(platform, accessToken);
    emit liveProbeNeeded();
}

void TokenManager::onTokenFailed(PlatformId platform, const QString& flow,
    const QString& errorCode, const QString& message, int httpStatus)
{
    m_pendingTokenFlows.remove(platform);
    if (flow == QStringLiteral("authorization_code")) {
        m_authInProgress.insert(platform, false);
    }
    const QString detail = buildTokenFailureDetailWithGuidance(platform, flow, errorCode, message, httpStatus);
    emit runtimeErrorChanged(platform, QStringLiteral("TOKEN_FAILED"), detail);
    emit tokenStateChanged(platform,
        flow == QStringLiteral("refresh_token") ? TokenState::EXPIRED : TokenState::AUTH_REQUIRED,
        detail);
    emit tokenActionFinished(platform, false,
        flow == QStringLiteral("refresh_token") ? tmText("Silent refresh failed") : tmText("Interactive re-auth failed"));
    appendTokenAudit(platform, flow, false, detail);
    emit liveStateResetNeeded(platform);
}

void TokenManager::onTokenRevoked(PlatformId platform, const QString& flow)
{
    m_pendingTokenRevokes.remove(platform);
    const bool localCleared = m_tokenVault.clear(platform);
    refreshTokenUi(platform);
    emit tokenUpdated(platform, QString());
    emit runtimePhaseChanged(platform, QStringLiteral("IDLE"));
    emit runtimeErrorCleared(platform);
    emit tokenActionFinished(platform, localCleared,
        localCleared ? tmText("Token revoked and cleared.") : tmText("Revoked but local clear failed."));
    appendTokenAudit(platform, flow, true, tmText("Token revoked"));
    emit liveStateResetNeeded(platform);
}

void TokenManager::onTokenRevokeFailed(PlatformId platform, const QString& flow,
    const QString& errorCode, const QString& message, int httpStatus)
{
    m_pendingTokenRevokes.remove(platform);
    const bool localCleared = m_tokenVault.clear(platform);
    emit tokenUpdated(platform, QString());
    emit runtimeErrorChanged(platform, QStringLiteral("TOKEN_REVOKE_FAILED"),
        QStringLiteral("%1 (http=%2)").arg(message).arg(httpStatus));
    emit tokenActionFinished(platform, localCleared,
        localCleared ? tmText("Revoke failed but token cleared locally.") : tmText("Revoke failed."));
    appendTokenAudit(platform, flow, false, QStringLiteral("%1 (http=%2)").arg(message).arg(httpStatus));
    emit liveStateResetNeeded(platform);
}
