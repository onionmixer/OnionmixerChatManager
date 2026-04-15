#include "platform/youtube/EmojiImageCache.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>

EmojiImageCache::EmojiImageCache(QNetworkAccessManager* network, QObject* parent)
    : QObject(parent)
    , m_network(network)
{
}

QPixmap EmojiImageCache::get(const QString& emojiId) const
{
    return m_cache.value(emojiId);
}

bool EmojiImageCache::contains(const QString& emojiId) const
{
    return m_cache.contains(emojiId);
}

void EmojiImageCache::ensureLoaded(const QString& emojiId, const QString& imageUrl)
{
    if (emojiId.isEmpty() || imageUrl.isEmpty()) {
        return;
    }
    if (m_cache.contains(emojiId) || m_pending.contains(emojiId)) {
        return;
    }

    m_pending.insert(emojiId);

    const QUrl url(imageUrl);
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_network->get(req);
    if (!reply) {
        m_pending.remove(emojiId);
        return;
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply, emojiId]() {
        m_pending.remove(emojiId);

        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray data = reply->readAll();
            QPixmap pix;
            if (pix.loadFromData(data) && !pix.isNull()) {
                if (m_cache.size() >= kMaxCacheSize) {
                    m_cache.clear();
                }
                m_cache.insert(emojiId, pix.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                emit imageReady(emojiId);
            }
        }

        reply->deleteLater();
    });
}
