#include "auth/OAuthLocalServer.h"

#include <QByteArray>
#include <QHostAddress>
#include <QStringList>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>

namespace {
QByteArray htmlBody(const QString& title, const QString& message)
{
    return QStringLiteral("<!doctype html><html><head><meta charset=\"utf-8\"><title>%1</title></head>"
                          "<body><h3>%1</h3><p>%2</p><p>You can close this tab and return to the app.</p></body></html>")
        .arg(title, message)
        .toUtf8();
}

QString bindAddressLabel(const QHostAddress& address)
{
    if (address == QHostAddress::AnyIPv4) {
        return QStringLiteral("0.0.0.0");
    }
    if (address == QHostAddress::LocalHost) {
        return QStringLiteral("127.0.0.1");
    }
    return address.toString();
}
}

OAuthLocalServer::OAuthLocalServer(QObject* parent)
    : QObject(parent)
{
}

OAuthLocalServer::~OAuthLocalServer()
{
    const QList<PlatformId> keys = m_sessions.keys();
    for (PlatformId platform : keys) {
        clearSession(platform);
    }
}

bool OAuthLocalServer::startSession(const OAuthSessionConfig& config, QString* errorMessage)
{
    QString reason;
    if (!validateRedirectUri(config.redirectUri, &reason)) {
        if (errorMessage) {
            *errorMessage = reason;
        }
        return false;
    }

    if (config.expectedState.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("expectedState is empty");
        }
        return false;
    }

    if (m_sessions.contains(config.platform)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("OAuth callback session is already active");
        }
        return false;
    }

    auto* server = new QTcpServer(this);
    const quint16 port = static_cast<quint16>(config.redirectUri.port());
    const QList<QHostAddress> bindCandidates = { QHostAddress::AnyIPv4, QHostAddress::LocalHost };
    QStringList bindErrors;
    bool bound = false;
    for (const QHostAddress& bindAddress : bindCandidates) {
        if (server->listen(bindAddress, port)) {
            bound = true;
            break;
        }
        bindErrors.push_back(QStringLiteral("%1: %2").arg(bindAddressLabel(bindAddress), server->errorString()));
    }
    if (!bound) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("listen failed on port %1 (%2)")
                                .arg(config.redirectUri.port())
                                .arg(bindErrors.join(QStringLiteral(", ")));
        }
        server->deleteLater();
        return false;
    }

    auto* timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);

    Session session;
    session.server = server;
    session.timeoutTimer = timeoutTimer;
    session.expectedState = config.expectedState;
    session.expectedPath = config.redirectUri.path().isEmpty() ? QStringLiteral("/") : config.redirectUri.path();
    m_sessions.insert(config.platform, session);

    connect(server, &QTcpServer::newConnection, this, [this, platform = config.platform]() {
        onNewConnection(platform);
    });

    connect(timeoutTimer, &QTimer::timeout, this, [this, platform = config.platform]() {
        if (!m_sessions.contains(platform)) {
            return;
        }
        emit sessionFailed(platform, QStringLiteral("OAUTH_CALLBACK_TIMEOUT"));
        clearSession(platform);
    });

    timeoutTimer->start(config.timeoutMs > 0 ? config.timeoutMs : 240000);
    return true;
}

void OAuthLocalServer::cancelSession(PlatformId platform, const QString& reason)
{
    if (!m_sessions.contains(platform)) {
        return;
    }

    if (!reason.trimmed().isEmpty()) {
        emit sessionFailed(platform, reason);
    }
    clearSession(platform);
}

bool OAuthLocalServer::hasActiveSession(PlatformId platform) const
{
    return m_sessions.contains(platform);
}

void OAuthLocalServer::clearSession(PlatformId platform)
{
    auto it = m_sessions.find(platform);
    if (it == m_sessions.end()) {
        return;
    }

    if (it->timeoutTimer) {
        it->timeoutTimer->stop();
        it->timeoutTimer->deleteLater();
    }

    if (it->server) {
        it->server->close();
        it->server->deleteLater();
    }

    m_sessions.erase(it);
}

void OAuthLocalServer::onNewConnection(PlatformId platform)
{
    auto it = m_sessions.find(platform);
    if (it == m_sessions.end() || !it->server) {
        return;
    }

    QTcpServer* server = it->server;
    while (server->hasPendingConnections()) {
        QTcpSocket* socket = server->nextPendingConnection();
        if (!socket) {
            continue;
        }

        m_socketPlatforms.insert(socket, platform);
        socket->setProperty("requestBuffer", QByteArray());

        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            onSocketReadyRead(socket);
        });

        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QObject::destroyed, this, [this, socket]() {
            m_socketPlatforms.remove(socket);
        });
    }
}

void OAuthLocalServer::onSocketReadyRead(QTcpSocket* socket)
{
    if (!socket) {
        return;
    }

    if (!m_socketPlatforms.contains(socket)) {
        return;
    }
    const PlatformId platform = m_socketPlatforms.value(socket);
    const Session session = m_sessions.value(platform);
    if (!session.server) {
        return;
    }

    QByteArray buffer = socket->property("requestBuffer").toByteArray();
    buffer += socket->readAll();
    socket->setProperty("requestBuffer", buffer);

    if (buffer.size() > 8192) {
        sendHttpResponse(socket, 413, htmlBody(QStringLiteral("OAuth Failed"), QStringLiteral("Request is too large.")));
        socket->disconnectFromHost();
        emit sessionFailed(platform, QStringLiteral("OAUTH_CALLBACK_REQUEST_TOO_LARGE"));
        clearSession(platform);
        return;
    }

    const int lineEnd = buffer.indexOf("\r\n");
    if (lineEnd < 0) {
        return;
    }

    const QByteArray requestLine = buffer.left(lineEnd).trimmed();
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2 || parts.first() != QByteArrayLiteral("GET")) {
        sendHttpResponse(socket, 400, htmlBody(QStringLiteral("OAuth Failed"), QStringLiteral("Invalid callback request.")));
        socket->disconnectFromHost();
        emit sessionFailed(platform, QStringLiteral("OAUTH_INVALID_REQUEST_LINE"));
        clearSession(platform);
        return;
    }

    const QUrl relativeUrl = QUrl::fromEncoded(parts.at(1));
    const QString path = relativeUrl.path().isEmpty() ? QStringLiteral("/") : relativeUrl.path();
    if (path != session.expectedPath) {
        sendHttpResponse(socket, 404, htmlBody(QStringLiteral("OAuth Callback"), QStringLiteral("Path mismatch.")));
        socket->disconnectFromHost();
        return;
    }

    const QUrlQuery query(relativeUrl);
    const QString state = query.queryItemValue(QStringLiteral("state"));
    const QString code = query.queryItemValue(QStringLiteral("code"));
    const QString errorCode = query.queryItemValue(QStringLiteral("error"));
    const QString errorDescription = query.queryItemValue(QStringLiteral("error_description"));

    if (state.trimmed().isEmpty() || state != session.expectedState) {
        sendHttpResponse(socket, 400, htmlBody(QStringLiteral("OAuth Failed"), QStringLiteral("State verification failed.")));
        socket->disconnectFromHost();
        emit sessionFailed(platform, QStringLiteral("OAUTH_STATE_MISMATCH"));
        clearSession(platform);
        return;
    }

    if (errorCode.trimmed().isEmpty() && code.trimmed().isEmpty()) {
        sendHttpResponse(socket, 400, htmlBody(QStringLiteral("OAuth Failed"), QStringLiteral("Missing authorization code.")));
        socket->disconnectFromHost();
        emit sessionFailed(platform, QStringLiteral("OAUTH_CODE_MISSING"));
        clearSession(platform);
        return;
    }

    if (!errorCode.trimmed().isEmpty()) {
        sendHttpResponse(socket, 200, htmlBody(QStringLiteral("OAuth Failed"), QStringLiteral("Authorization was not completed.")));
    } else {
        sendHttpResponse(socket, 200, htmlBody(QStringLiteral("OAuth Complete"), QStringLiteral("Authorization completed successfully.")));
    }

    socket->disconnectFromHost();
    emit callbackReceived(platform, code, state, errorCode, errorDescription);
    clearSession(platform);
}

void OAuthLocalServer::sendHttpResponse(QTcpSocket* socket, int status, const QByteArray& body) const
{
    if (!socket) {
        return;
    }

    QByteArray reason;
    switch (status) {
    case 200:
        reason = QByteArrayLiteral("OK");
        break;
    case 400:
        reason = QByteArrayLiteral("Bad Request");
        break;
    case 404:
        reason = QByteArrayLiteral("Not Found");
        break;
    case 413:
        reason = QByteArrayLiteral("Payload Too Large");
        break;
    default:
        reason = QByteArrayLiteral("Error");
        break;
    }

    QByteArray response;
    response += QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status) + QByteArrayLiteral(" ") + reason + QByteArrayLiteral("\r\n");
    response += QByteArrayLiteral("Content-Type: text/html; charset=utf-8\r\n");
    response += QByteArrayLiteral("Content-Length: ") + QByteArray::number(body.size()) + QByteArrayLiteral("\r\n");
    response += QByteArrayLiteral("Connection: close\r\n\r\n");
    response += body;

    socket->write(response);
    socket->flush();
}

bool OAuthLocalServer::validateRedirectUri(const QUrl& uri, QString* reason) const
{
    if (!uri.isValid()) {
        if (reason) {
            *reason = QStringLiteral("redirect_uri is invalid");
        }
        return false;
    }

    if (uri.scheme() != QStringLiteral("http")) {
        if (reason) {
            *reason = QStringLiteral("redirect_uri scheme must be http");
        }
        return false;
    }

    if (uri.host() != QStringLiteral("127.0.0.1") && uri.host() != QStringLiteral("localhost")) {
        if (reason) {
            *reason = QStringLiteral("redirect_uri host must be 127.0.0.1 or localhost");
        }
        return false;
    }

    if (uri.port() <= 0) {
        if (reason) {
            *reason = QStringLiteral("redirect_uri must include a valid port");
        }
        return false;
    }

    if (uri.path().trimmed().isEmpty()) {
        if (reason) {
            *reason = QStringLiteral("redirect_uri path is empty");
        }
        return false;
    }

    return true;
}
