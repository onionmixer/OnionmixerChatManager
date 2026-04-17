#ifndef EMOJI_TEXT_DOCUMENT_H
#define EMOJI_TEXT_DOCUMENT_H

#include <QTextDocument>

class EmojiImageCache;

// QTextDocument 서브클래스 — <img src='emoji://{id}'/> placeholder를
// EmojiImageCache에서 찾은 QPixmap으로 해석.
class EmojiTextDocument : public QTextDocument {
    Q_OBJECT
public:
    explicit EmojiTextDocument(EmojiImageCache* cache, QObject* parent = nullptr);

protected:
    QVariant loadResource(int type, const QUrl& name) override;

private:
    EmojiImageCache* m_cache = nullptr;
};

#endif // EMOJI_TEXT_DOCUMENT_H
