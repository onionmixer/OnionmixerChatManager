#include "ui/ChatBubbleDelegate.h"
#include "core/EmojiImageCache.h"
#include "core/PlatformTraits.h"
#include "ui/ChatMessageModel.h"

#include <QApplication>
#include <QBuffer>
#include <QPainter>
#include <QPainterPath>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>

namespace {
constexpr qreal kOutlineWidthPx = 1.5;

// 텍스트 한 문자열에 대해 stroke outline을 그린다 (drawText 이전에 호출)
void strokeTextAt(QPainter* p, const QColor& color, const QString& text,
                  const QFont& font, const QPointF& baseline)
{
    if (!color.isValid() || text.isEmpty()) return;
    QPainterPath path;
    path.addText(baseline, font, text);
    const QPen pen(color, kOutlineWidthPx, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p->setBrush(Qt::NoBrush);
    p->strokePath(path, pen);
}

// QTextDocument 본문 중 텍스트 fragment만 찾아 각 line별로 outline을 그린다.
// QTextLayout::glyphRuns 기반으로 이모지 이미지는 skip.
void strokeBodyTextFragments(QTextDocument* doc, QPainter* p, const QColor& outlineColor)
{
    if (!doc || !outlineColor.isValid()) return;
    const QPen pen(outlineColor, kOutlineWidthPx, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p->setBrush(Qt::NoBrush);

    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        QTextLayout* layout = block.layout();
        if (!layout) continue;
        for (int i = 0, n = layout->lineCount(); i < n; ++i) {
            const QTextLine line = layout->lineAt(i);
            const QList<QGlyphRun> runs = line.glyphRuns();
            for (const QGlyphRun& run : runs) {
                QPainterPath path;
                const QRawFont& rawFont = run.rawFont();
                const QVector<quint32> indexes = run.glyphIndexes();
                const QVector<QPointF> positions = run.positions();
                const QPointF runPos = layout->position();
                for (int g = 0; g < indexes.size() && g < positions.size(); ++g) {
                    const QPainterPath glyphPath = rawFont.pathForGlyph(indexes[g]);
                    path.addPath(glyphPath.translated(runPos + positions[g]));
                }
                p->strokePath(path, pen);
            }
        }
    }
}
} // namespace

ChatBubbleDelegate::ChatBubbleDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

int ChatBubbleDelegate::badgeSize() const { return m_fontSize + 2; }
int ChatBubbleDelegate::badgeFontSize() const { return qMax(m_fontSize - 4, 6); }
int ChatBubbleDelegate::timestampFontSize() const { return qMax(m_fontSize - 2, 7); }

QFont ChatBubbleDelegate::messageFont() const
{
    QFont f;
    if (!m_fontFamily.isEmpty()) f.setFamily(m_fontFamily);
    f.setPixelSize(m_fontSize);
    f.setBold(m_fontBold);
    f.setItalic(m_fontItalic);
    f.setWeight(m_fontBold ? QFont::Bold : static_cast<QFont::Weight>(60));
    return f;
}

QFont ChatBubbleDelegate::authorFont() const
{
    QFont f;
    if (!m_fontFamily.isEmpty()) f.setFamily(m_fontFamily);
    f.setPixelSize(m_fontSize);
    f.setBold(true);
    f.setItalic(m_fontItalic);
    return f;
}

QFont ChatBubbleDelegate::timestampFont() const
{
    QFont f;
    if (!m_fontFamily.isEmpty()) f.setFamily(m_fontFamily);
    f.setPixelSize(timestampFontSize());
    f.setItalic(m_fontItalic);
    return f;
}

void ChatBubbleDelegate::setFontFamily(const QString& family) { m_fontFamily = family; }
void ChatBubbleDelegate::setFontSize(int size) { m_fontSize = qBound(8, size, 24); }
void ChatBubbleDelegate::setFontBold(bool bold) { m_fontBold = bold; }
void ChatBubbleDelegate::setFontItalic(bool italic) { m_fontItalic = italic; }
void ChatBubbleDelegate::setLineSpacing(int spacing) { m_lineSpacing = qBound(0, spacing, 20); }
void ChatBubbleDelegate::setEmojiCache(EmojiImageCache* cache) { m_emojiCache = cache; }
void ChatBubbleDelegate::setBodyOverrideColor(const QColor& c) { m_bodyOverrideColor = c; }
void ChatBubbleDelegate::setTextOutlineColor(const QColor& c) { m_textOutlineColor = c; }

void ChatBubbleDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const
{
    painter->save();

    // Selection highlight
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, option.palette.highlight());
    }

    const int padding = 8;
    const int bs = badgeSize();
    const QRect contentRect = option.rect.adjusted(padding, m_lineSpacing, -padding, -m_lineSpacing);

    // Platform info
    const auto platform = static_cast<PlatformId>(index.data(ChatMessageModel::PlatformRole).toInt());
    const QString authorName = index.data(ChatMessageModel::AuthorNameRole).toString();
    const QString messageText = index.data(ChatMessageModel::MessageTextRole).toString();
    const QString richText = index.data(ChatMessageModel::RichTextRole).toString();
    const QDateTime timestamp = index.data(ChatMessageModel::TimestampRole).toDateTime();

    // ── Badge ──
    const int badgeY = contentRect.top() + 1;
    const QRect badgeRect(contentRect.left(), badgeY, bs, bs);
    const QColor bgColor(PlatformTraits::badgeBgColor(platform));
    const QColor fgColor(PlatformTraits::badgeFgColor(platform));

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(bgColor);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(badgeRect, bs / 2, bs / 2);

    QFont bf;
    bf.setPixelSize(badgeFontSize());
    bf.setBold(true);
    painter->setFont(bf);
    painter->setPen(fgColor);
    painter->drawText(badgeRect, Qt::AlignCenter, PlatformTraits::badgeSymbol(platform));
    painter->setRenderHint(QPainter::Antialiasing, false);

    // ── Author name ──
    const int headerLeft = contentRect.left() + bs + 8;
    const QFont af = authorFont();
    painter->setFont(af);
    const QFontMetrics afm(af);
    const QRect authorRect(headerLeft, contentRect.top(), afm.horizontalAdvance(authorName) + 4, afm.height());
    // Outline (stroke) first, then fill — 순서를 지켜야 fill이 inside를 덮음 (§2)
    if (m_textOutlineColor.isValid()) {
        painter->setRenderHint(QPainter::Antialiasing, true);
        const qreal baseY = authorRect.top() + (authorRect.height() + afm.ascent() - afm.descent()) / 2.0;
        strokeTextAt(painter, m_textOutlineColor, authorName, af,
                     QPointF(authorRect.left(), baseY));
    }
    painter->setPen(QColor(PlatformTraits::authorColor(platform)));
    painter->drawText(authorRect, Qt::AlignVCenter, authorName);

    // ── Timestamp ──
    const QFont tf = timestampFont();
    painter->setFont(tf);
    const QString tsText = timestamp.isValid() ? timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QString();
    if (!tsText.isEmpty()) {
        const QFontMetrics tfm(tf);
        const QRect tsRect(authorRect.right() + 8, contentRect.top(), tfm.horizontalAdvance(tsText) + 4, tfm.height());
        if (m_textOutlineColor.isValid()) {
            painter->setRenderHint(QPainter::Antialiasing, true);
            const qreal baseY = tsRect.top() + (tsRect.height() + tfm.ascent() - tfm.descent()) / 2.0;
            strokeTextAt(painter, m_textOutlineColor, tsText, tf,
                         QPointF(tsRect.left(), baseY));
        }
        painter->setPen(QColor(QStringLiteral("#999999")));
        painter->drawText(tsRect, Qt::AlignVCenter, tsText);
    }

    // ── Message body ──
    const int bodyLeft = contentRect.left() + bs + 8;
    const int bodyTop = contentRect.top() + afm.height() + 2;
    const int bodyWidth = contentRect.width() - bs - 8;

    const QFont mf = messageFont();
    const QString html = richText.isEmpty() ? messageText.toHtmlEscaped() : richText;

    // Use QTextDocument for RichText rendering (supports word wrap + inline images)
    QTextDocument doc;
    doc.setDefaultFont(mf);
    doc.setTextWidth(bodyWidth);
    doc.setHtml(html);

    painter->translate(bodyLeft, bodyTop);
    // 본문 outline (stroke first, fill after — §2)
    if (m_textOutlineColor.isValid()) {
        painter->setRenderHint(QPainter::Antialiasing, true);
        strokeBodyTextFragments(&doc, painter, m_textOutlineColor);
    }
    // 본문 색: override 우선, 없으면 기본 #111111
    const QColor bodyColor = m_bodyOverrideColor.isValid()
                           ? m_bodyOverrideColor
                           : QColor(QStringLiteral("#111111"));
    painter->setPen(bodyColor);
    doc.drawContents(painter);
    painter->translate(-bodyLeft, -bodyTop);

    painter->restore();
}

QSize ChatBubbleDelegate::sizeHint(const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const
{
    const int padding = 8;
    const int bs = badgeSize();
    const int availableWidth = option.rect.width() > 0 ? option.rect.width() : 400;
    const int bodyWidth = availableWidth - padding * 2 - bs - 8;

    const QFont af = authorFont();
    const QFontMetrics afm(af);
    const int headerHeight = afm.height();

    const QString richText = index.data(ChatMessageModel::RichTextRole).toString();
    const QString messageText = index.data(ChatMessageModel::MessageTextRole).toString();
    const QString html = richText.isEmpty() ? messageText.toHtmlEscaped() : richText;

    QTextDocument doc;
    doc.setDefaultFont(messageFont());
    doc.setTextWidth(qMax(bodyWidth, 100));
    doc.setHtml(html);

    const int bodyHeight = static_cast<int>(doc.size().height());
    const int totalHeight = m_lineSpacing * 2 + headerHeight + 2 + bodyHeight;

    return QSize(availableWidth, qMax(totalHeight, bs + m_lineSpacing * 2));
}
