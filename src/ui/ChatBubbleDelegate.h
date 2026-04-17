#ifndef CHAT_BUBBLE_DELEGATE_H
#define CHAT_BUBBLE_DELEGATE_H

#include "core/AppTypes.h"

#include <QColor>
#include <QStyledItemDelegate>

class EmojiImageCache;

class ChatBubbleDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit ChatBubbleDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

    void setFontFamily(const QString& family);
    void setFontSize(int size);
    void setFontBold(bool bold);
    void setFontItalic(bool italic);
    void setLineSpacing(int spacing);
    void setEmojiCache(EmojiImageCache* cache);

    // 방송창 전용 스타일 확장 (invalid QColor() = off, 기본값)
    void setBodyOverrideColor(const QColor& c);
    void setTextOutlineColor(const QColor& c);
    QColor bodyOverrideColor() const { return m_bodyOverrideColor; }
    QColor textOutlineColor() const { return m_textOutlineColor; }

private:
    int badgeSize() const;
    int badgeFontSize() const;
    int timestampFontSize() const;
    QFont messageFont() const;
    QFont authorFont() const;
    QFont timestampFont() const;

    QString m_fontFamily;
    int m_fontSize = 11;
    bool m_fontBold = false;
    bool m_fontItalic = false;
    int m_lineSpacing = 3;
    EmojiImageCache* m_emojiCache = nullptr;
    QColor m_bodyOverrideColor;  // invalid = off
    QColor m_textOutlineColor;   // invalid = off
};

#endif // CHAT_BUBBLE_DELEGATE_H
