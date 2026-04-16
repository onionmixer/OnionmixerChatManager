#ifndef CHZZK_EMOJI_RESOLVER_H
#define CHZZK_EMOJI_RESOLVER_H

#include <QHash>
#include <QObject>
#include <QString>

class QNetworkAccessManager;

class ChzzkEmojiResolver : public QObject {
    Q_OBJECT
public:
    explicit ChzzkEmojiResolver(QNetworkAccessManager* network, QObject* parent = nullptr);

    void loadEmojiPacks(const QString& channelId);
    QString imageUrlForId(const QString& emojiId) const;
    bool isLoaded() const;

private:
    QNetworkAccessManager* m_network = nullptr;
    QHash<QString, QString> m_emojiMap; // emojiId -> imageUrl
    bool m_loaded = false;
    bool m_loading = false;
};

#endif // CHZZK_EMOJI_RESOLVER_H
