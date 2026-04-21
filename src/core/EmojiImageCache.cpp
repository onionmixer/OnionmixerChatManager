#include "core/EmojiImageCache.h"

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
    if (m_cache.contains(emojiId)) {
        touchLru(emojiId); // v24 D4: access 시 move-to-front
    }
    return m_cache.value(emojiId);
}

bool EmojiImageCache::contains(const QString& emojiId) const
{
    const bool hit = m_cache.contains(emojiId);
    if (hit) touchLru(emojiId);
    return hit;
}

// v24 D4 LRU helpers
void EmojiImageCache::touchLru(const QString& emojiId) const
{
    m_lruOrder.removeOne(emojiId);
    m_lruOrder.prepend(emojiId);
}

void EmojiImageCache::evictIfNeeded()
{
    while (m_cache.size() > kMaxCacheSize && !m_lruOrder.isEmpty()) {
        const QString victim = m_lruOrder.takeLast();
        m_cache.remove(victim);
        m_rawBytes.remove(victim);
        m_mime.remove(victim);
        // m_urlMap·m_pending은 유지 — URL 메타·진행 중 요청은 별개
    }
}

void EmojiImageCache::registerUrl(const QString& emojiId, const QString& imageUrl)
{
    if (emojiId.isEmpty() || imageUrl.isEmpty()) return;
    // v12-3 idempotent: 첫 등록 유지. 다른 url은 warn하되 덮어쓰지 않음.
    const auto it = m_urlMap.constFind(emojiId);
    if (it != m_urlMap.constEnd() && it.value() != imageUrl) {
        // 실무상 발생 드문 케이스 — 로그 억제 (폭주 방지)
        return;
    }
    if (it == m_urlMap.constEnd()) {
        m_urlMap.insert(emojiId, imageUrl);
    }
}

QString EmojiImageCache::getUrl(const QString& emojiId) const
{
    return m_urlMap.value(emojiId);
}

QByteArray EmojiImageCache::getRawBytes(const QString& emojiId) const
{
    return m_rawBytes.value(emojiId);
}

QString EmojiImageCache::getMime(const QString& emojiId) const
{
    return m_mime.value(emojiId);
}

void EmojiImageCache::setImage(const QString& emojiId, const QByteArray& bytes,
                                const QString& mime)
{
    if (emojiId.isEmpty() || bytes.isEmpty()) return;

    QPixmap pix;
    if (!pix.loadFromData(bytes) || pix.isNull()) {
        return;
    }

    m_cache.insert(emojiId,
                   pix.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_rawBytes.insert(emojiId, bytes);
    m_mime.insert(emojiId,
                  mime.isEmpty() ? QStringLiteral("image/png") : mime);
    touchLru(emojiId);
    evictIfNeeded(); // v24 D4: 상한 초과 시 tail eviction
    m_pending.remove(emojiId);
    emit imageReady(emojiId);
}

void EmojiImageCache::ensureLoaded(const QString& emojiId, const QString& imageUrl)
{
    if (emojiId.isEmpty() || imageUrl.isEmpty()) {
        return;
    }
    // v11-7: URL 메타도 등록 (ensureLoaded 경유 진입도 커버)
    registerUrl(emojiId, imageUrl);

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
            const QString contentType =
                reply->header(QNetworkRequest::ContentTypeHeader).toString();

            QPixmap pix;
            if (pix.loadFromData(data) && !pix.isNull()) {
                m_cache.insert(emojiId,
                               pix.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                m_rawBytes.insert(emojiId, data);
                m_mime.insert(emojiId,
                              contentType.isEmpty() ? QStringLiteral("image/png")
                                                    : contentType.split(';').first().trimmed());
                touchLru(emojiId);
                evictIfNeeded(); // v24 D4
                emit imageReady(emojiId);
            }
        }

        reply->deleteLater();
    });
}
