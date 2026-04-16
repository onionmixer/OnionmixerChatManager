#ifndef JSON_HELPER_H
#define JSON_HELPER_H

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

class QNetworkReply;

namespace JsonHelper {

QString readStringByKeys(const QJsonObject& primary, const QJsonObject& fallback, const QStringList& keys);
QJsonObject parseJsonObjectString(const QString& text);
QJsonObject jsonObjectFromValue(const QJsonValue& value);
QDateTime parseEventTime(const QString& raw);

struct HttpResponse {
    int httpStatus = 0;
    bool networkOk = false;
    bool httpOk = false;
    QByteArray body;
    QJsonObject json;
    QString errorString;
};

HttpResponse parseHttpResponse(QNetworkReply* reply);

} // namespace JsonHelper

#endif // JSON_HELPER_H
