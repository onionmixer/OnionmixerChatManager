#ifndef TOKEN_MANAGER_H
#define TOKEN_MANAGER_H

#include "auth/OAuthLocalServer.h"
#include "auth/OAuthTokenClient.h"
#include "auth/TokenVault.h"
#include "core/AppTypes.h"

#include <QHash>
#include <QObject>
#include <QString>

class TokenManager : public QObject {
    Q_OBJECT
public:
    explicit TokenManager(const AppSettingsSnapshot* snapshotRef, const QString& configDir,
                          QNetworkAccessManager* network, QObject* parent = nullptr);

    void tryStartupTokenRefresh();
    void refreshAllTokenUi();

    PlatformSettings settingsFor(PlatformId platform) const;
    TokenState inferTokenState(const TokenRecord* record) const;
    bool readToken(PlatformId platform, TokenRecord* record) const;
    bool isAuthInProgress(PlatformId platform) const;

public slots:
    void onTokenRefreshRequested(PlatformId platform, const PlatformSettings& settings);
    void onInteractiveAuthRequested(PlatformId platform, const PlatformSettings& settings);
    void onTokenDeleteRequested(PlatformId platform);
    void onOAuthCallbackReceived(PlatformId platform, const QString& code, const QString& state,
        const QString& errorCode, const QString& errorDescription);
    void onOAuthSessionFailed(PlatformId platform, const QString& reason);

signals:
    void tokenOperationStarted(PlatformId platform, const QString& operation);
    void tokenStateChanged(PlatformId platform, TokenState state, const QString& detail);
    void tokenActionFinished(PlatformId platform, bool ok, const QString& message);
    void tokenAuditEntry(PlatformId platform, const QString& action, bool ok, const QString& detail);
    void tokenRecordUpdated(PlatformId platform, TokenState state, const TokenRecord& record, const QString& detail);
    void tokenUpdated(PlatformId platform, const QString& accessToken);
    void profileSyncNeeded(PlatformId platform, const QString& accessToken);
    void liveProbeNeeded();
    void runtimePhaseChanged(PlatformId platform, const QString& phase);
    void runtimeErrorChanged(PlatformId platform, const QString& code, const QString& message);
    void runtimeErrorCleared(PlatformId platform);
    void liveStateResetNeeded(PlatformId platform);
    void logMessage(const QString& text);

private:
    struct PendingTokenFlowContext {
        QString flow;
        PlatformSettings settings;
        TokenRecord previousRecord;
    };

    void onTokenGranted(PlatformId platform, const QString& flow,
        const QString& accessToken, const QString& refreshToken,
        int expiresInSec, int refreshExpiresInSec);
    void onTokenFailed(PlatformId platform, const QString& flow,
        const QString& errorCode, const QString& message, int httpStatus);
    void onTokenRevoked(PlatformId platform, const QString& flow);
    void onTokenRevokeFailed(PlatformId platform, const QString& flow,
        const QString& errorCode, const QString& message, int httpStatus);

    bool startTokenRefreshFlow(PlatformId platform, const PlatformSettings& settings, const TokenRecord& currentRecord);
    bool startAuthCodeExchangeFlow(PlatformId platform, const PlatformSettings& settings,
        const QString& code, const QString& codeVerifier, const QString& authState);
    void tryStartupTokenRefreshForPlatform(PlatformId platform);
    void refreshTokenUi(PlatformId platform);
    void appendTokenAudit(PlatformId platform, const QString& action, bool ok, const QString& detail);
    QUrl buildAuthorizationUrl(PlatformId platform, const PlatformSettings& settings,
        const QString& state, const QString& codeChallenge) const;
    QString createOAuthState(PlatformId platform) const;

    const AppSettingsSnapshot* m_snapshot = nullptr;
    FileTokenVault m_tokenVault;
    OAuthLocalServer m_oauthLocalServer;
    OAuthTokenClient m_oauthTokenClient;
    QHash<PlatformId, QString> m_pendingOAuthState;
    QHash<PlatformId, PlatformSettings> m_pendingOAuthSettings;
    QHash<PlatformId, QString> m_pendingPkceVerifier;
    QHash<PlatformId, PendingTokenFlowContext> m_pendingTokenFlows;
    QHash<PlatformId, bool> m_pendingTokenRevokes;
    QHash<PlatformId, bool> m_authInProgress;
};

#endif // TOKEN_MANAGER_H
