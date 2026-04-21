#ifndef EMOJI_IMAGE_CACHE_H
#define EMOJI_IMAGE_CACHE_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>

class QNetworkAccessManager;

class EmojiImageCache : public QObject {
    Q_OBJECT
public:
    explicit EmojiImageCache(QNetworkAccessManager* network, QObject* parent = nullptr);

    // v24 D4 LRU: get·contains 접근 시 move-to-front (const 유지 위해 mutable 내부).
    QPixmap get(const QString& emojiId) const;
    bool contains(const QString& emojiId) const;
    void ensureLoaded(const QString& emojiId, const QString& imageUrl);

    // PLAN §11.2 v11-7: 서버가 broadcastChat 시 id → url 메타만 등록.
    // 이미지 bytes 로드는 request_emoji 수신 시 ensureLoaded로 진행.
    void registerUrl(const QString& emojiId, const QString& imageUrl);
    QString getUrl(const QString& emojiId) const;

    // 서버가 emoji_image 응답 구성 시 원본 bytes·mime 조회.
    QByteArray getRawBytes(const QString& emojiId) const;
    QString getMime(const QString& emojiId) const;

    // PLAN_DEV_BROADCHATCLIENT §5.4 v2-11: 클라가 서버에서 받은 bytes 주입.
    // 성공 시 imageReady(emojiId) signal 발행. 디코드 실패 시 no-op.
    void setImage(const QString& emojiId, const QByteArray& bytes,
                  const QString& mime);

signals:
    void imageReady(const QString& emojiId);

private:
    // v24 D4 helpers
    void touchLru(const QString& emojiId) const; // move-to-front
    void evictIfNeeded();                        // 초과 시 tail eviction

    QNetworkAccessManager* m_network = nullptr;
    QHash<QString, QPixmap> m_cache;
    QHash<QString, QByteArray> m_rawBytes;
    QHash<QString, QString> m_mime;
    QHash<QString, QString> m_urlMap; // v11-7
    QSet<QString> m_pending;
    mutable QList<QString> m_lruOrder;  // front=most-recent, back=LRU eviction target
    static const int kMaxCacheSize = 500;
};

#endif // EMOJI_IMAGE_CACHE_H
