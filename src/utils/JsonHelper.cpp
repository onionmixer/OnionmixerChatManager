#include "utils/JsonHelper.h"

#include <QJsonDocument>
#include <QNetworkReply>

namespace JsonHelper {

QString readStringByKeys(const QJsonObject& primary, const QJsonObject& fallback, const QStringList& keys)
{
    for (const QString& key : keys) {
        const QString a = primary.value(key).toString().trimmed();
        if (!a.isEmpty()) {
            return a;
        }
        const QString b = fallback.value(key).toString().trimmed();
        if (!b.isEmpty()) {
            return b;
        }
    }
    return QString();
}

QJsonObject parseJsonObjectString(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return QJsonObject();
    }
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8());
    return doc.isObject() ? doc.object() : QJsonObject();
}

QJsonObject jsonObjectFromValue(const QJsonValue& value)
{
    if (value.isObject()) {
        return value.toObject();
    }
    if (value.isString()) {
        return parseJsonObjectString(value.toString());
    }
    return QJsonObject();
}

QDateTime parseEventTime(const QString& raw)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) {
        return QDateTime();
    }

    QDateTime parsed = QDateTime::fromString(trimmed, Qt::ISODate);
    if (parsed.isValid()) {
        return parsed;
    }
    parsed = QDateTime::fromString(trimmed, Qt::ISODateWithMs);
    if (parsed.isValid()) {
        return parsed;
    }

    bool ok = false;
    const qlonglong n = trimmed.toLongLong(&ok);
    if (!ok || n <= 0) {
        return QDateTime();
    }

    if (trimmed.size() >= 13) {
        return QDateTime::fromMSecsSinceEpoch(n);
    }
    return QDateTime::fromSecsSinceEpoch(n);
}

HttpResponse parseHttpResponse(QNetworkReply* reply)
{
    HttpResponse r;
    if (!reply) {
        return r;
    }
    r.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    r.networkOk = (reply->error() == QNetworkReply::NoError);
    r.httpOk = (r.httpStatus >= 200 && r.httpStatus < 300);
    r.errorString = reply->errorString();
    r.body = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(r.body);
    if (doc.isObject()) {
        r.json = doc.object();
    }
    return r;
}

} // namespace JsonHelper
