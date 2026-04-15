#ifndef EMOJI_IMAGE_CACHE_H
#define EMOJI_IMAGE_CACHE_H

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>

class QNetworkAccessManager;

class EmojiImageCache : public QObject {
    Q_OBJECT
public:
    explicit EmojiImageCache(QNetworkAccessManager* network, QObject* parent = nullptr);

    QPixmap get(const QString& emojiId) const;
    bool contains(const QString& emojiId) const;
    void ensureLoaded(const QString& emojiId, const QString& imageUrl);

signals:
    void imageReady(const QString& emojiId);

private:
    QNetworkAccessManager* m_network = nullptr;
    QHash<QString, QPixmap> m_cache;
    QSet<QString> m_pending;
    static const int kMaxCacheSize = 500;
};

#endif // EMOJI_IMAGE_CACHE_H
