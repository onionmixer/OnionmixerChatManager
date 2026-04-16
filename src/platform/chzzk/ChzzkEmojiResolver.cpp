#include "platform/chzzk/ChzzkEmojiResolver.h"
#include "platform/chzzk/ChzzkEndpoints.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

ChzzkEmojiResolver::ChzzkEmojiResolver(QNetworkAccessManager* network, QObject* parent)
    : QObject(parent)
    , m_network(network)
{
}

void ChzzkEmojiResolver::loadEmojiPacks(const QString& channelId)
{
    if (channelId.trimmed().isEmpty() || m_loading) {
        return;
    }
    m_loading = true;

    const QUrl url(Chzzk::ServiceApi::emojiPacks(channelId.trimmed()));
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_network->get(req);
    if (!reply) {
        m_loading = false;
        return;
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_loading = false;

        if (reply->error() != QNetworkReply::NoError) {
            return;
        }

        const QByteArray body = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject()) {
            return;
        }

        const QJsonObject content = doc.object().value(QStringLiteral("content")).toObject();

        // Parse all three pack types: emojiPacks, cheatKeyEmojiPacks, subscriptionEmojiPacks
        const QStringList packKeys = {
            QStringLiteral("emojiPacks"),
            QStringLiteral("cheatKeyEmojiPacks"),
            QStringLiteral("subscriptionEmojiPacks"),
        };

        for (const QString& key : packKeys) {
            const QJsonArray packs = content.value(key).toArray();
            for (const QJsonValue& packVal : packs) {
                const QJsonObject pack = packVal.toObject();
                if (pack.value(QStringLiteral("emojiPackLocked")).toBool(false)) {
                    continue;
                }
                const QJsonArray emojis = pack.value(QStringLiteral("emojis")).toArray();
                for (const QJsonValue& emojiVal : emojis) {
                    const QJsonObject emoji = emojiVal.toObject();
                    const QString emojiId = emoji.value(QStringLiteral("emojiId")).toString().trimmed();
                    const QString imageUrl = emoji.value(QStringLiteral("imageUrl")).toString().trimmed();
                    if (!emojiId.isEmpty() && !imageUrl.isEmpty()) {
                        m_emojiMap.insert(emojiId, imageUrl);
                    }
                }
            }
        }

        m_loaded = true;
    });
}

QString ChzzkEmojiResolver::imageUrlForId(const QString& emojiId) const
{
    return m_emojiMap.value(emojiId);
}

bool ChzzkEmojiResolver::isLoaded() const
{
    return m_loaded;
}
