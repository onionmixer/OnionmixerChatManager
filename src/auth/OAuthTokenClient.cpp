#include "auth/OAuthTokenClient.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace {
QString tokenClientText(const char* sourceText)
{
    return QCoreApplication::translate("OAuthTokenClient", sourceText);
}

const QString kFlowAuthorizationCode = QStringLiteral("authorization_code");
const QString kFlowRefreshToken = QStringLiteral("refresh_token");
const QString kFlowTokenRevoke = QStringLiteral("token_revoke");

int parseIntOrDefault(const QJsonObject& obj, const QString& key, int fallback)
{
    const QJsonValue v = obj.value(key);
    if (v.isDouble()) {
        return v.toInt();
    }
    if (v.isString()) {
        bool ok = false;
        const int parsed = v.toString().toInt(&ok);
        return ok ? parsed : fallback;
    }
    return fallback;
}

bool parseIntValue(const QJsonValue& v, int* out)
{
    if (!out) {
        return false;
    }
    if (v.isDouble()) {
        *out = v.toInt();
        return true;
    }
    if (v.isString()) {
        bool ok = false;
        const int parsed = v.toString().toInt(&ok);
        if (ok) {
            *out = parsed;
            return true;
        }
    }
    return false;
}

QString readStringByKeys(const QJsonObject& primary, const QJsonObject& fallback, const QStringList& keys)
{
    for (const QString& key : keys) {
        const QString fromPrimary = primary.value(key).toString().trimmed();
        if (!fromPrimary.isEmpty()) {
            return fromPrimary;
        }
        const QString fromFallback = fallback.value(key).toString().trimmed();
        if (!fromFallback.isEmpty()) {
            return fromFallback;
        }
    }
    return QString();
}

int readIntByKeys(const QJsonObject& primary, const QJsonObject& fallback, const QStringList& keys, int defaultValue)
{
    int parsed = defaultValue;
    for (const QString& key : keys) {
        if (parseIntValue(primary.value(key), &parsed)) {
            return parsed;
        }
        if (parseIntValue(fallback.value(key), &parsed)) {
            return parsed;
        }
    }
    return defaultValue;
}

QUrl resolveChzzkRevokeEndpoint(const PlatformSettings& settings)
{
    QUrl endpoint(settings.tokenEndpoint.trimmed());
    if (!endpoint.isValid() || endpoint.scheme() != QStringLiteral("https") || endpoint.host().trimmed().isEmpty()) {
        return QUrl(QStringLiteral("https://openapi.chzzk.naver.com/auth/v1/token/revoke"));
    }

    QString path = endpoint.path();
    if (path.endsWith(QStringLiteral("/token/revoke"))) {
        return endpoint;
    }
    if (path.endsWith(QStringLiteral("/token"))) {
        endpoint.setPath(path + QStringLiteral("/revoke"));
        return endpoint;
    }

    endpoint.setPath(QStringLiteral("/auth/v1/token/revoke"));
    return endpoint;
}
} // namespace

OAuthTokenClient::OAuthTokenClient(QNetworkAccessManager* network, QObject* parent)
    : QObject(parent)
    , m_network(network)
{
}

bool OAuthTokenClient::requestAuthorizationCodeToken(PlatformId platform,
    const PlatformSettings& settings,
    const QString& code,
    const QString& codeVerifier,
    const QString& authState,
    QString* errorMessage)
{
    if (code.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = tokenClientText("Authorization code is empty.");
        }
        return false;
    }

    QList<QPair<QString, QString>> form;
    if (platform == PlatformId::Chzzk) {
        if (settings.clientId.trimmed().isEmpty() || settings.clientSecret.trimmed().isEmpty()) {
            if (errorMessage) {
                *errorMessage = tokenClientText("CHZZK authorization_code requires clientId and clientSecret.");
            }
            return false;
        }
        if (authState.trimmed().isEmpty()) {
            if (errorMessage) {
                *errorMessage = tokenClientText("CHZZK authorization_code requires state.");
            }
            return false;
        }
        form.append({ QStringLiteral("grantType"), QStringLiteral("authorization_code") });
        form.append({ QStringLiteral("clientId"), settings.clientId.trimmed() });
        form.append({ QStringLiteral("clientSecret"), settings.clientSecret.trimmed() });
        form.append({ QStringLiteral("code"), code.trimmed() });
        form.append({ QStringLiteral("state"), authState.trimmed() });
    } else {
        form.append({ QStringLiteral("grant_type"), QStringLiteral("authorization_code") });
        form.append({ QStringLiteral("code"), code });
        form.append({ QStringLiteral("client_id"), settings.clientId });
        form.append({ QStringLiteral("redirect_uri"), settings.redirectUri });
        if (!settings.clientSecret.trimmed().isEmpty()) {
            form.append({ QStringLiteral("client_secret"), settings.clientSecret });
        }
        if (!codeVerifier.trimmed().isEmpty()) {
            form.append({ QStringLiteral("code_verifier"), codeVerifier });
        }
    }

    return postTokenRequest(platform, kFlowAuthorizationCode, settings, form, errorMessage);
}

bool OAuthTokenClient::requestRefreshToken(PlatformId platform,
    const PlatformSettings& settings,
    const QString& refreshToken,
    QString* errorMessage)
{
    if (refreshToken.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = tokenClientText("Refresh token is empty.");
        }
        return false;
    }

    QList<QPair<QString, QString>> form;
    if (platform == PlatformId::Chzzk) {
        if (settings.clientId.trimmed().isEmpty() || settings.clientSecret.trimmed().isEmpty()) {
            if (errorMessage) {
                *errorMessage = tokenClientText("CHZZK refresh_token requires clientId and clientSecret.");
            }
            return false;
        }
        form.append({ QStringLiteral("grantType"), QStringLiteral("refresh_token") });
        form.append({ QStringLiteral("refreshToken"), refreshToken.trimmed() });
        form.append({ QStringLiteral("clientId"), settings.clientId.trimmed() });
        form.append({ QStringLiteral("clientSecret"), settings.clientSecret.trimmed() });
    } else {
        form.append({ QStringLiteral("grant_type"), QStringLiteral("refresh_token") });
        form.append({ QStringLiteral("refresh_token"), refreshToken });
        form.append({ QStringLiteral("client_id"), settings.clientId });
        if (!settings.clientSecret.trimmed().isEmpty()) {
            form.append({ QStringLiteral("client_secret"), settings.clientSecret });
        }
    }

    return postTokenRequest(platform, kFlowRefreshToken, settings, form, errorMessage);
}

bool OAuthTokenClient::requestRevokeToken(PlatformId platform,
    const PlatformSettings& settings,
    const TokenRecord& record,
    QString* errorMessage)
{
    if (!m_network) {
        if (errorMessage) {
            *errorMessage = tokenClientText("Network manager is null.");
        }
        return false;
    }

    const QString token = !record.refreshToken.trimmed().isEmpty()
        ? record.refreshToken.trimmed()
        : record.accessToken.trimmed();
    if (token.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tokenClientText("No token available to revoke.");
        }
        return false;
    }

    QUrl endpoint;
    QList<QPair<QString, QString>> form;
    if (platform == PlatformId::YouTube) {
        endpoint = QUrl(QStringLiteral("https://oauth2.googleapis.com/revoke"));
        form.append({ QStringLiteral("token"), token });
    } else {
        if (settings.clientId.trimmed().isEmpty() || settings.clientSecret.trimmed().isEmpty()) {
            if (errorMessage) {
                *errorMessage = tokenClientText("CHZZK revoke requires client_id and client_secret.");
            }
            return false;
        }
        endpoint = resolveChzzkRevokeEndpoint(settings);
        form.append({ QStringLiteral("clientId"), settings.clientId.trimmed() });
        form.append({ QStringLiteral("clientSecret"), settings.clientSecret.trimmed() });
        form.append({ QStringLiteral("token"), token });
        form.append({ QStringLiteral("tokenTypeHint"),
            !record.refreshToken.trimmed().isEmpty() ? QStringLiteral("refresh_token") : QStringLiteral("access_token") });
    }

    if (!endpoint.isValid() || endpoint.scheme() != QStringLiteral("https") || endpoint.host().trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = tokenClientText("revoke endpoint must be a valid https URL.");
        }
        return false;
    }

    QNetworkRequest request(endpoint);
    QByteArray requestBody;
    if (platform == PlatformId::Chzzk) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setRawHeader("Client-Id", settings.clientId.trimmed().toUtf8());
        request.setRawHeader("Client-Secret", settings.clientSecret.trimmed().toUtf8());

        QJsonObject payload;
        for (const auto& pair : form) {
            payload.insert(pair.first, pair.second);
        }
        requestBody = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    } else {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
        QUrlQuery query;
        for (const auto& pair : form) {
            query.addQueryItem(pair.first, pair.second);
        }
        requestBody = query.query(QUrl::FullyEncoded).toUtf8();
    }

    QNetworkReply* reply = m_network->post(request, requestBody);
    if (!reply) {
        if (errorMessage) {
            *errorMessage = tokenClientText("Failed to create network reply.");
        }
        return false;
    }

    reply->setProperty("platform", static_cast<int>(platform));
    reply->setProperty("flow", kFlowTokenRevoke);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const PlatformId platform = static_cast<PlatformId>(reply->property("platform").toInt());
        const QString flow = reply->property("flow").toString();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();

        QJsonObject obj;
        const QJsonDocument json = QJsonDocument::fromJson(body);
        if (json.isObject()) {
            obj = json.object();
        }

        QString errCode = obj.value(QStringLiteral("error")).toString();
        QString errMsg = obj.value(QStringLiteral("error_description")).toString();
        if (errMsg.trimmed().isEmpty()) {
            errMsg = obj.value(QStringLiteral("message")).toString();
        }
        if (errMsg.trimmed().isEmpty()) {
            errMsg = reply->errorString();
        }

        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        if (reply->error() != QNetworkReply::NoError || !httpOk) {
            if (errCode.trimmed().isEmpty()) {
                errCode = QStringLiteral("TOKEN_REVOKE_FAILED");
            }
            emit tokenRevokeFailed(platform, flow, errCode, errMsg, httpStatus);
            reply->deleteLater();
            return;
        }

        if (platform == PlatformId::Chzzk && obj.value(QStringLiteral("code")).isDouble() && obj.value(QStringLiteral("code")).toInt() != 200) {
            const QString chzzkMsg = obj.value(QStringLiteral("message")).toString();
            emit tokenRevokeFailed(platform,
                flow,
                QStringLiteral("CHZZK_TOKEN_REVOKE_FAILED"),
                chzzkMsg.trimmed().isEmpty() ? tokenClientText("CHZZK token revoke failed") : chzzkMsg,
                httpStatus);
            reply->deleteLater();
            return;
        }

        emit tokenRevoked(platform, flow);
        reply->deleteLater();
    });

    return true;
}

bool OAuthTokenClient::postTokenRequest(PlatformId platform,
    const QString& flow,
    const PlatformSettings& settings,
    const QList<QPair<QString, QString>>& formItems,
    QString* errorMessage)
{
    if (!m_network) {
        if (errorMessage) {
            *errorMessage = tokenClientText("Network manager is null.");
        }
        return false;
    }

    if (settings.clientId.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = tokenClientText("client id is empty.");
        }
        return false;
    }

    const QUrl tokenEndpoint(settings.tokenEndpoint);
    if (!tokenEndpoint.isValid() || tokenEndpoint.scheme() != QStringLiteral("https") || tokenEndpoint.host().trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = tokenClientText("token_endpoint must be a valid https URL.");
        }
        return false;
    }

    QNetworkRequest request(tokenEndpoint);
    QByteArray requestBody;
    if (platform == PlatformId::Chzzk) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        if (!settings.clientSecret.trimmed().isEmpty()) {
            request.setRawHeader("Client-Id", settings.clientId.trimmed().toUtf8());
            request.setRawHeader("Client-Secret", settings.clientSecret.trimmed().toUtf8());
        }

        QJsonObject payload;
        for (const auto& pair : formItems) {
            payload.insert(pair.first, pair.second);
        }
        requestBody = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    } else {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
        QUrlQuery query;
        for (const auto& pair : formItems) {
            query.addQueryItem(pair.first, pair.second);
        }
        requestBody = query.query(QUrl::FullyEncoded).toUtf8();
    }

    QNetworkReply* reply = m_network->post(request, requestBody);
    if (!reply) {
        if (errorMessage) {
            *errorMessage = tokenClientText("Failed to create network reply.");
        }
        return false;
    }

    reply->setProperty("platform", static_cast<int>(platform));
    reply->setProperty("flow", flow);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const PlatformId platform = static_cast<PlatformId>(reply->property("platform").toInt());
        const QString flow = reply->property("flow").toString();

        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();

        QJsonObject obj;
        const QJsonDocument json = QJsonDocument::fromJson(body);
        if (json.isObject()) {
            obj = json.object();
        }

        const QJsonObject contentObj = obj.value(QStringLiteral("content")).toObject();
        const QJsonObject payload = contentObj.isEmpty() ? obj : contentObj;

        const QString accessToken = readStringByKeys(payload, obj, { QStringLiteral("access_token"), QStringLiteral("accessToken") });
        const QString refreshToken = readStringByKeys(payload, obj, { QStringLiteral("refresh_token"), QStringLiteral("refreshToken") });
        const int expiresInSec = readIntByKeys(payload, obj, { QStringLiteral("expires_in"), QStringLiteral("expiresIn") }, 3600);
        const int refreshExpiresInSec = readIntByKeys(payload, obj, { QStringLiteral("refresh_token_expires_in"), QStringLiteral("refreshTokenExpiresIn") }, 0);

        QString errCode = readStringByKeys(payload, obj, { QStringLiteral("error"), QStringLiteral("code") });
        QString errMsg = readStringByKeys(payload, obj, { QStringLiteral("error_description"), QStringLiteral("message") });
        if (errMsg.trimmed().isEmpty()) {
            errMsg = reply->errorString();
        }
        if (errMsg.trimmed().isEmpty() && !body.trimmed().isEmpty()) {
            errMsg = QString::fromUtf8(body).trimmed();
        }

        if (platform == PlatformId::Chzzk && obj.contains(QStringLiteral("code"))) {
            const int apiCode = readIntByKeys(obj, QJsonObject(), { QStringLiteral("code") }, 0);
            if (apiCode != 200) {
                if (errCode.trimmed().isEmpty()) {
                    errCode = QString::number(apiCode);
                }
                if (errMsg.trimmed().isEmpty()) {
                    errMsg = tokenClientText("CHZZK token exchange failed");
                }
                emit tokenFailed(platform, flow, errCode, errMsg, httpStatus);
                reply->deleteLater();
                return;
            }
        }

        const bool httpOk = httpStatus >= 200 && httpStatus < 300;
        const bool hasAccessToken = !accessToken.trimmed().isEmpty();
        if (reply->error() != QNetworkReply::NoError || !httpOk || !hasAccessToken) {
            emit tokenFailed(platform,
                flow,
                errCode.trimmed().isEmpty() ? QStringLiteral("TOKEN_EXCHANGE_FAILED") : errCode,
                errMsg,
                httpStatus);
            reply->deleteLater();
            return;
        }

        emit tokenGranted(platform, flow, accessToken, refreshToken, expiresInSec, refreshExpiresInSec);
        reply->deleteLater();
    });

    return true;
}
