#include "BroadChatProtocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>

namespace BroadChatProtocol {

QByteArray encodeEnvelope(const QString& type,
                          const QJsonObject& data,
                          const QString& id,
                          qint64 timestampMs)
{
    if (type.isEmpty()) return {};
    QJsonObject env;
    env.insert(QStringLiteral("v"), kProtocolVersion);
    env.insert(QStringLiteral("type"), type);
    if (!id.isEmpty()) env.insert(QStringLiteral("id"), id);
    if (timestampMs >= 0) env.insert(QStringLiteral("t"), timestampMs);
    if (!data.isEmpty()) env.insert(QStringLiteral("data"), data);

    QByteArray out = QJsonDocument(env).toJson(QJsonDocument::Compact);
    out.append('\n');
    return out;
}

Envelope parseEnvelope(const QByteArray& line)
{
    Envelope env;
    if (line.isEmpty()) {
        env.parseError = QStringLiteral("empty line");
        return env;
    }

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        env.parseError = perr.errorString();
        return env;
    }
    const QJsonObject obj = doc.object();
    const QJsonValue vVal = obj.value(QStringLiteral("v"));
    const QJsonValue typeVal = obj.value(QStringLiteral("type"));
    if (!vVal.isDouble() || !typeVal.isString()) {
        env.parseError = QStringLiteral("missing v or type");
        return env;
    }
    env.v = vVal.toInt();
    env.type = typeVal.toString();
    env.id = obj.value(QStringLiteral("id")).toString();
    const QJsonValue tVal = obj.value(QStringLiteral("t"));
    env.t = tVal.isDouble() ? static_cast<qint64>(tVal.toDouble()) : -1;
    env.data = obj.value(QStringLiteral("data")).toObject();
    env.valid = true;
    return env;
}

QJsonObject buildChatData(const UnifiedChatMessage& msg)
{
    QJsonObject o;
    o.insert(QStringLiteral("platform"), platformKey(msg.platform));
    o.insert(QStringLiteral("messageId"), msg.messageId);
    o.insert(QStringLiteral("channelId"), msg.channelId);
    o.insert(QStringLiteral("channelName"), msg.channelName);
    o.insert(QStringLiteral("authorId"), msg.authorId);
    o.insert(QStringLiteral("authorName"), msg.authorName);
    o.insert(QStringLiteral("rawAuthorDisplayName"), msg.rawAuthorDisplayName);
    o.insert(QStringLiteral("rawAuthorChannelId"), msg.rawAuthorChannelId);
    o.insert(QStringLiteral("authorIsChatOwner"), msg.authorIsChatOwner);
    o.insert(QStringLiteral("authorIsChatModerator"), msg.authorIsChatModerator);
    o.insert(QStringLiteral("authorIsChatSponsor"), msg.authorIsChatSponsor);
    o.insert(QStringLiteral("authorIsVerified"), msg.authorIsVerified);
    o.insert(QStringLiteral("text"), msg.text);
    o.insert(QStringLiteral("richText"), msg.richText);

    QJsonArray emojis;
    for (const ChatEmojiInfo& e : msg.emojis) {
        QJsonObject ej;
        ej.insert(QStringLiteral("emojiId"), e.emojiId);
        ej.insert(QStringLiteral("imageUrl"), e.imageUrl);
        if (!e.fallbackText.isEmpty()) {
            ej.insert(QStringLiteral("fallbackText"), e.fallbackText);
        }
        emojis.append(ej);
    }
    o.insert(QStringLiteral("emojis"), emojis);

    if (msg.timestamp.isValid()) {
        o.insert(QStringLiteral("timestamp"),
                 msg.timestamp.toUTC().toString(Qt::ISODateWithMs));
    }
    return o;
}

QJsonObject buildViewersData(int youtube, int chzzk)
{
    QJsonObject o;
    o.insert(QStringLiteral("youtube"), youtube);
    o.insert(QStringLiteral("chzzk"), chzzk);
    // §6.4.4 v2-12: total은 서버 계산
    const int y = qMax(0, youtube);
    const int c = qMax(0, chzzk);
    o.insert(QStringLiteral("total"), y + c);
    return o;
}

QJsonObject buildPlatformStatusData(PlatformId platform, const QString& state,
                                    bool live, const QString& runtimePhase)
{
    QJsonObject o;
    o.insert(QStringLiteral("platform"), platformKey(platform));
    o.insert(QStringLiteral("connection"), state);
    o.insert(QStringLiteral("live"), live);
    if (!runtimePhase.isEmpty()) {
        o.insert(QStringLiteral("runtimePhase"), runtimePhase);
    }
    return o;
}

} // namespace BroadChatProtocol
