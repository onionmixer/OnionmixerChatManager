#ifndef CHAT_BUBBLE_WIDGET_H
#define CHAT_BUBBLE_WIDGET_H

#include <QString>

class QWidget;

struct ChatBubbleParams {
    QString badgeText;        // "▶" or "Z"
    QString badgeStyle;       // "background:#E53935; ..."
    QString authorText;       // HTML-escaped nickname
    QString authorStyle;      // "color:#6A3FA0; ..."
    QString messageHtml;      // richText HTML or text.toHtmlEscaped()
    QString messageStyle;     // "color:#111111; ..."
    QString timestampText;    // "2026-04-16 15:30:00" or empty
    QString timestampStyle;   // "color:#999999; ..."
    int lineSpacing = 3;
    int badgeSize = 13;
};

QWidget* buildChatBubble(const ChatBubbleParams& params, QWidget* parent);

#endif // CHAT_BUBBLE_WIDGET_H
