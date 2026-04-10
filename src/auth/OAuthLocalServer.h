#ifndef OAUTH_LOCAL_SERVER_H
#define OAUTH_LOCAL_SERVER_H

#include "core/AppTypes.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>

class QTcpServer;
class QTcpSocket;
class QTimer;

struct OAuthSessionConfig {
    PlatformId platform = PlatformId::YouTube;
    QUrl redirectUri;
    QString expectedState;
    int timeoutMs = 240000;
};

class OAuthLocalServer : public QObject {
    Q_OBJECT
public:
    explicit OAuthLocalServer(QObject* parent = nullptr);
    ~OAuthLocalServer() override;

    bool startSession(const OAuthSessionConfig& config, QString* errorMessage = nullptr);
    void cancelSession(PlatformId platform, const QString& reason = QString());
    bool hasActiveSession(PlatformId platform) const;

signals:
    void callbackReceived(PlatformId platform,
        const QString& code,
        const QString& state,
        const QString& errorCode,
        const QString& errorDescription);
    void sessionFailed(PlatformId platform, const QString& reason);

private:
    struct Session {
        QTcpServer* server = nullptr;
        QTimer* timeoutTimer = nullptr;
        QString expectedState;
        QString expectedPath;
    };

    void clearSession(PlatformId platform);
    void onNewConnection(PlatformId platform);
    void onSocketReadyRead(QTcpSocket* socket);
    void sendHttpResponse(QTcpSocket* socket, int status, const QByteArray& body) const;
    bool validateRedirectUri(const QUrl& uri, QString* reason) const;

    QHash<PlatformId, Session> m_sessions;
    QHash<QTcpSocket*, PlatformId> m_socketPlatforms;
};

#endif // OAUTH_LOCAL_SERVER_H
