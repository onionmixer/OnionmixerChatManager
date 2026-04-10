#ifndef OAUTH_TOKEN_CLIENT_H
#define OAUTH_TOKEN_CLIENT_H

#include "core/AppTypes.h"

#include <QObject>

class QNetworkAccessManager;

class OAuthTokenClient : public QObject {
    Q_OBJECT
public:
    explicit OAuthTokenClient(QNetworkAccessManager* network, QObject* parent = nullptr);

    bool requestAuthorizationCodeToken(PlatformId platform,
        const PlatformSettings& settings,
        const QString& code,
        const QString& codeVerifier,
        const QString& authState = QString(),
        QString* errorMessage = nullptr);

    bool requestRefreshToken(PlatformId platform,
        const PlatformSettings& settings,
        const QString& refreshToken,
        QString* errorMessage = nullptr);

    bool requestRevokeToken(PlatformId platform,
        const PlatformSettings& settings,
        const TokenRecord& record,
        QString* errorMessage = nullptr);

signals:
    void tokenGranted(PlatformId platform,
        const QString& flow,
        const QString& accessToken,
        const QString& refreshToken,
        int expiresInSec,
        int refreshExpiresInSec);

    void tokenFailed(PlatformId platform,
        const QString& flow,
        const QString& errorCode,
        const QString& message,
        int httpStatus);

    void tokenRevoked(PlatformId platform,
        const QString& flow);

    void tokenRevokeFailed(PlatformId platform,
        const QString& flow,
        const QString& errorCode,
        const QString& message,
        int httpStatus);

private:
    bool postTokenRequest(PlatformId platform,
        const QString& flow,
        const PlatformSettings& settings,
        const QList<QPair<QString, QString>>& formItems,
        QString* errorMessage);

    QNetworkAccessManager* m_network = nullptr;
};

#endif // OAUTH_TOKEN_CLIENT_H
