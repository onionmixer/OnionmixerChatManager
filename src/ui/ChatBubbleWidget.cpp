#include "ui/ChatBubbleWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

QWidget* buildChatBubble(const ChatBubbleParams& p, QWidget* parent)
{
    auto* wrap = new QWidget(parent);
    wrap->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    wrap->setFocusPolicy(Qt::NoFocus);

    auto* layout = new QVBoxLayout(wrap);
    layout->setContentsMargins(8, p.lineSpacing, 8, p.lineSpacing);
    layout->setSpacing(1);
    layout->setAlignment(Qt::AlignTop);

    // Badge
    auto* badge = new QLabel(wrap);
    badge->setFixedSize(p.badgeSize, p.badgeSize);
    badge->setAlignment(Qt::AlignCenter);
    badge->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    badge->setFocusPolicy(Qt::NoFocus);
    badge->setStyleSheet(p.badgeStyle);
    badge->setText(p.badgeText);

    auto* badgeWrap = new QWidget(wrap);
    auto* badgeWrapLayout = new QVBoxLayout(badgeWrap);
    badgeWrapLayout->setContentsMargins(0, 1, 0, 0);
    badgeWrapLayout->setSpacing(0);
    badgeWrapLayout->addWidget(badge, 0, Qt::AlignTop);

    // Author
    auto* lblAuthor = new QLabel(p.authorText, wrap);
    lblAuthor->setTextFormat(Qt::RichText);
    lblAuthor->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    lblAuthor->setFocusPolicy(Qt::NoFocus);
    lblAuthor->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    lblAuthor->setStyleSheet(p.authorStyle);

    // Timestamp
    auto* lblTimestamp = new QLabel(p.timestampText, wrap);
    lblTimestamp->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    lblTimestamp->setFocusPolicy(Qt::NoFocus);
    lblTimestamp->setStyleSheet(p.timestampStyle);

    // Head layout
    auto* headLayout = new QHBoxLayout;
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(8);
    headLayout->addWidget(badgeWrap, 0, Qt::AlignVCenter);
    headLayout->addWidget(lblAuthor, 0, Qt::AlignVCenter);
    headLayout->addWidget(lblTimestamp, 0, Qt::AlignVCenter);
    headLayout->addStretch();

    // Message
    auto* lblMessage = new QLabel(wrap);
    lblMessage->setTextFormat(Qt::RichText);
    lblMessage->setTextInteractionFlags(Qt::NoTextInteraction);
    lblMessage->setWordWrap(true);
    lblMessage->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    lblMessage->setFocusPolicy(Qt::NoFocus);
    lblMessage->setStyleSheet(p.messageStyle);
    lblMessage->setText(p.messageHtml);

    // Body layout
    auto* bodyLayout = new QHBoxLayout;
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addSpacing(p.badgeSize + 8);
    bodyLayout->addWidget(lblMessage, 1, Qt::AlignTop);

    layout->addLayout(headLayout);
    layout->addLayout(bodyLayout);
    return wrap;
}
