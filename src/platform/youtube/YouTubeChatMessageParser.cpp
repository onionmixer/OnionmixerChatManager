#include "platform/youtube/YouTubeChatMessageParser.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

namespace {
QString firstNonEmpty(const QStringList& values)
{
    for (const QString& value : values) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty()) {
            return trimmed;
        }
    }
    return QString();
}

struct ParsedYouTubeSnippet {
    QString liveChatId;
    QString authorChannelId;
    QString publishedAt;
    QString type;
    bool hasDisplayContent = true;
    QString displayMessage;
    QString textMessage;
    QString deletedMessageId;
    QString bannedDisplayName;
    QString banType;
    int banDurationSeconds = 0;
    QString superChatAmount;
    QString superChatComment;
    QString superStickerAmount;
    QString superStickerAlt;
    int milestoneMonths = 0;
    QString milestoneComment;
    QString sponsorLevel;
    bool sponsorUpgrade = false;
    int giftingCount = 0;
    QString giftingLevel;
    QString giftReceivedLevel;
    QString pollQuestion;
    QString giftName;
    int giftJewels = 0;
};

QString summarizeParsedSnippet(const ParsedYouTubeSnippet& snippet)
{
    const QString type = snippet.type.trimmed();
    if (type == QStringLiteral("textMessageEvent")) {
        return firstNonEmpty({snippet.displayMessage, snippet.textMessage});
    }
    if (type == QStringLiteral("messageDeletedEvent")) {
        return snippet.deletedMessageId.trimmed().isEmpty()
            ? QStringLiteral("[Message deleted]")
            : QStringLiteral("[Message deleted] id=%1").arg(snippet.deletedMessageId.trimmed());
    }
    if (type == QStringLiteral("userBannedEvent")) {
        const QString name = snippet.bannedDisplayName.trimmed().isEmpty()
            ? QStringLiteral("-")
            : snippet.bannedDisplayName.trimmed();
        if (snippet.banType.compare(QStringLiteral("temporary"), Qt::CaseInsensitive) == 0) {
            return QStringLiteral("[User banned] %1 (%2s)").arg(name).arg(snippet.banDurationSeconds);
        }
        return QStringLiteral("[User banned] %1").arg(name);
    }
    if (type == QStringLiteral("superChatEvent")) {
        return snippet.superChatComment.trimmed().isEmpty()
            ? QStringLiteral("[Super Chat] %1").arg(snippet.superChatAmount.trimmed().isEmpty() ? QStringLiteral("-") : snippet.superChatAmount.trimmed())
            : QStringLiteral("[Super Chat] %1 %2")
                  .arg(snippet.superChatAmount.trimmed().isEmpty() ? QStringLiteral("-") : snippet.superChatAmount.trimmed(),
                      snippet.superChatComment.trimmed());
    }
    if (type == QStringLiteral("superStickerEvent")) {
        return snippet.superStickerAlt.trimmed().isEmpty()
            ? QStringLiteral("[Super Sticker] %1").arg(snippet.superStickerAmount.trimmed().isEmpty() ? QStringLiteral("-") : snippet.superStickerAmount.trimmed())
            : QStringLiteral("[Super Sticker] %1 %2")
                  .arg(snippet.superStickerAmount.trimmed().isEmpty() ? QStringLiteral("-") : snippet.superStickerAmount.trimmed(),
                      snippet.superStickerAlt.trimmed());
    }
    if (type == QStringLiteral("memberMilestoneChatEvent")) {
        return snippet.milestoneComment.trimmed().isEmpty()
            ? QStringLiteral("[Member Milestone] %1 months").arg(snippet.milestoneMonths)
            : QStringLiteral("[Member Milestone] %1 months %2").arg(snippet.milestoneMonths).arg(snippet.milestoneComment.trimmed());
    }
    if (type == QStringLiteral("newSponsorEvent")) {
        const QString level = snippet.sponsorLevel.trimmed().isEmpty() ? QStringLiteral("-") : snippet.sponsorLevel.trimmed();
        return snippet.sponsorUpgrade
            ? QStringLiteral("[Membership Upgrade] %1").arg(level)
            : QStringLiteral("[New Member] %1").arg(level);
    }
    if (type == QStringLiteral("membershipGiftingEvent")) {
        return QStringLiteral("[Membership Gifting] count=%1 level=%2")
            .arg(snippet.giftingCount)
            .arg(snippet.giftingLevel.trimmed().isEmpty() ? QStringLiteral("-") : snippet.giftingLevel.trimmed());
    }
    if (type == QStringLiteral("giftMembershipReceivedEvent")) {
        return QStringLiteral("[Gift Membership Received] %1")
            .arg(snippet.giftReceivedLevel.trimmed().isEmpty() ? QStringLiteral("-") : snippet.giftReceivedLevel.trimmed());
    }
    if (type == QStringLiteral("pollEvent") || type == QStringLiteral("pollDetails")) {
        return snippet.pollQuestion.trimmed().isEmpty()
            ? QStringLiteral("[Poll]")
            : QStringLiteral("[Poll] %1").arg(snippet.pollQuestion.trimmed());
    }
    if (type == QStringLiteral("giftEvent")) {
        return QStringLiteral("[Gift] %1 jewels=%2")
            .arg(snippet.giftName.trimmed().isEmpty() ? QStringLiteral("-") : snippet.giftName.trimmed())
            .arg(snippet.giftJewels);
    }
    if (type == QStringLiteral("chatEndedEvent")) {
        return QStringLiteral("[Chat ended]");
    }
    if (type == QStringLiteral("sponsorOnlyModeStartedEvent")) {
        return QStringLiteral("[Sponsors-only mode started]");
    }
    if (type == QStringLiteral("sponsorOnlyModeEndedEvent")) {
        return QStringLiteral("[Sponsors-only mode ended]");
    }
    if (type == QStringLiteral("tombstone")) {
        return QStringLiteral("[Deleted message placeholder]");
    }
    return type.isEmpty() ? QString() : QStringLiteral("[%1]").arg(type);
}

UnifiedChatMessage buildUnifiedChatMessage(const QString& messageId,
                                          const QString& authorId,
                                          const QString& authorName,
                                          const QString& rawAuthorDisplayName,
                                          const QString& rawAuthorChannelId,
                                          bool authorIsChatOwner,
                                          bool authorIsChatModerator,
                                          bool authorIsChatSponsor,
                                          bool authorIsVerified,
                                          const ParsedYouTubeSnippet& snippet)
{
    UnifiedChatMessage msg;
    msg.platform = PlatformId::YouTube;
    msg.messageId = messageId.trimmed();
    msg.authorId = authorId.trimmed().isEmpty() ? snippet.authorChannelId.trimmed() : authorId.trimmed();
    msg.authorName = authorName.trimmed();
    msg.rawAuthorDisplayName = rawAuthorDisplayName.trimmed();
    msg.rawAuthorChannelId = rawAuthorChannelId.trimmed().isEmpty() ? msg.authorId : rawAuthorChannelId.trimmed();
    msg.authorIsChatOwner = authorIsChatOwner;
    msg.authorIsChatModerator = authorIsChatModerator;
    msg.authorIsChatSponsor = authorIsChatSponsor;
    msg.authorIsVerified = authorIsVerified;
    msg.text = firstNonEmpty({snippet.displayMessage, snippet.textMessage});
    if (msg.text.trimmed().isEmpty()) {
        msg.text = summarizeParsedSnippet(snippet);
    }
    if (msg.authorName.trimmed().isEmpty() && snippet.type == QStringLiteral("userBannedEvent")) {
        msg.authorName = snippet.bannedDisplayName.trimmed();
    }
    if (msg.text.trimmed().isEmpty() && !snippet.hasDisplayContent) {
        msg.text = QStringLiteral("[Event] %1").arg(snippet.type.trimmed().isEmpty() ? QStringLiteral("unknown") : snippet.type.trimmed());
    }
    msg.timestamp = QDateTime::fromString(snippet.publishedAt.trimmed(), Qt::ISODate);
    if (!msg.timestamp.isValid()) {
        msg.timestamp = QDateTime::currentDateTime();
    }
    return msg;
}

ParsedYouTubeSnippet parseJsonSnippet(const QJsonObject& snippet)
{
    ParsedYouTubeSnippet parsed;
    parsed.liveChatId = snippet.value(QStringLiteral("liveChatId")).toString();
    parsed.authorChannelId = snippet.value(QStringLiteral("authorChannelId")).toString();
    parsed.publishedAt = snippet.value(QStringLiteral("publishedAt")).toString();
    parsed.type = snippet.value(QStringLiteral("type")).toString();
    parsed.hasDisplayContent = !snippet.contains(QStringLiteral("hasDisplayContent"))
        || snippet.value(QStringLiteral("hasDisplayContent")).toBool();
    parsed.displayMessage = snippet.value(QStringLiteral("displayMessage")).toString();
    parsed.textMessage = snippet.value(QStringLiteral("textMessageDetails")).toObject().value(QStringLiteral("messageText")).toString();
    parsed.deletedMessageId = snippet.value(QStringLiteral("messageDeletedDetails")).toObject().value(QStringLiteral("deletedMessageId")).toString();
    const QJsonObject banned = snippet.value(QStringLiteral("userBannedDetails")).toObject();
    parsed.bannedDisplayName = banned.value(QStringLiteral("bannedUserDetails")).toObject().value(QStringLiteral("displayName")).toString();
    parsed.banType = banned.value(QStringLiteral("banType")).toString();
    parsed.banDurationSeconds = banned.value(QStringLiteral("banDurationSeconds")).toInt();
    const QJsonObject superChat = snippet.value(QStringLiteral("superChatDetails")).toObject();
    parsed.superChatAmount = superChat.value(QStringLiteral("amountDisplayString")).toString();
    parsed.superChatComment = superChat.value(QStringLiteral("userComment")).toString();
    const QJsonObject superSticker = snippet.value(QStringLiteral("superStickerDetails")).toObject();
    parsed.superStickerAmount = superSticker.value(QStringLiteral("amountDisplayString")).toString();
    parsed.superStickerAlt = superSticker.value(QStringLiteral("superStickerMetadata")).toObject().value(QStringLiteral("altText")).toString();
    const QJsonObject milestone = snippet.value(QStringLiteral("memberMilestoneChatDetails")).toObject();
    parsed.milestoneMonths = milestone.value(QStringLiteral("memberMonth")).toInt();
    parsed.milestoneComment = milestone.value(QStringLiteral("userComment")).toString();
    const QJsonObject sponsor = snippet.value(QStringLiteral("newSponsorDetails")).toObject();
    parsed.sponsorLevel = sponsor.value(QStringLiteral("memberLevelName")).toString();
    parsed.sponsorUpgrade = sponsor.value(QStringLiteral("isUpgrade")).toBool();
    const QJsonObject gifting = snippet.value(QStringLiteral("membershipGiftingDetails")).toObject();
    parsed.giftingCount = gifting.value(QStringLiteral("giftMembershipsCount")).toInt();
    parsed.giftingLevel = gifting.value(QStringLiteral("giftMembershipsLevelName")).toString();
    parsed.giftReceivedLevel = snippet.value(QStringLiteral("giftMembershipReceivedDetails")).toObject().value(QStringLiteral("memberLevelName")).toString();
    parsed.pollQuestion = snippet.value(QStringLiteral("pollDetails")).toObject().value(QStringLiteral("metadata")).toObject().value(QStringLiteral("questionText")).toString();
    const QJsonObject giftMetadata = snippet.value(QStringLiteral("giftEventDetails")).toObject().value(QStringLiteral("giftMetadata")).toObject();
    parsed.giftName = giftMetadata.value(QStringLiteral("giftName")).toString();
    parsed.giftJewels = giftMetadata.value(QStringLiteral("jewelsAmount")).toInt();
    return parsed;
}

#if BOTMANAGER_HAS_YT_STREAMLIST
QString protoTypeToString(::youtube::api::v3::LiveChatMessageSnippet_TypeWrapper_Type type)
{
    using Type = ::youtube::api::v3::LiveChatMessageSnippet_TypeWrapper_Type;
    switch (type) {
    case Type::LiveChatMessageSnippet_TypeWrapper_Type_TEXT_MESSAGE_EVENT: return QStringLiteral("textMessageEvent");
    case Type::LiveChatMessageSnippet_TypeWrapper_Type_TOMBSTONE: return QStringLiteral("tombstone");
    case Type::LiveChatMessageSnippet_TypeWrapper_Type_FAN_FUNDING_EVENT: return QStringLiteral("fanFundingEvent");
    case Type::LiveChatMessageSnippet_TypeWrapper_Type_CHAT_ENDED_EVENT: return QStringLiteral("chatEndedEvent");
    case Type::LiveChatMessageSnippet_TypeWrapper_Type_SPONSOR_ONLY_MODE_STARTED_EVENT: return QStringLiteral("sponsorOnlyModeStartedEvent");
    case Type::LiveChatMessageSnippet_TypeWrapper_Type_SPONSOR_ONLY_MODE_ENDED_EVENT: return QStringLiteral("sponsorOnlyModeEndedEvent");
    case Type::LiveChatMessageSnippet_TypeWrapper_Type_NEW_SPONSOR_EVENT: return QStringLiteral("newSponsorEvent");
    case Type::LiveChatMessageSnippet_TypeWrapper_Type_MESSAGE_DELETED_EVENT: return QStringLiteral("messageDeletedEvent");
    case Type::LiveChatMessageSnippet_TypeWrapper_Type_MESSAGE_RETRACTED_EVENT: return QStringLiteral("messageRetractedEvent");
    case Type::LiveChatMessageSnippet_TypeWrapper_Type_USER_BANNED_EVENT: return QStringLiteral("userBannedEvent");
    case Type::LiveChatMessageSnippet_TypeWrapper_Type_SUPER_CHAT_EVENT: return QStringLiteral("superChatEvent");
    case Type::LiveChatMessageSnippet_TypeWrapper_Type_SUPER_STICKER_EVENT: return QStringLiteral("superStickerEvent");
    case Type::LiveChatMessageSnippet_TypeWrapper_Type_MEMBER_MILESTONE_CHAT_EVENT: return QStringLiteral("memberMilestoneChatEvent");
    case Type::LiveChatMessageSnippet_TypeWrapper_Type_MEMBERSHIP_GIFTING_EVENT: return QStringLiteral("membershipGiftingEvent");
    case Type::LiveChatMessageSnippet_TypeWrapper_Type_GIFT_MEMBERSHIP_RECEIVED_EVENT: return QStringLiteral("giftMembershipReceivedEvent");
    case Type::LiveChatMessageSnippet_TypeWrapper_Type_POLL_EVENT: return QStringLiteral("pollEvent");
    case Type::LiveChatMessageSnippet_TypeWrapper_Type_GIFT_EVENT: return QStringLiteral("giftEvent");
    default: return QString();
    }
}

ParsedYouTubeSnippet parseProtoSnippet(const ::youtube::api::v3::LiveChatMessageSnippet& snippet)
{
    ParsedYouTubeSnippet parsed;
    parsed.liveChatId = QString::fromStdString(snippet.live_chat_id());
    parsed.authorChannelId = QString::fromStdString(snippet.author_channel_id());
    parsed.publishedAt = QString::fromStdString(snippet.published_at());
    parsed.type = protoTypeToString(snippet.type());
    parsed.hasDisplayContent = !snippet.has_has_display_content() || snippet.has_display_content();
    parsed.displayMessage = QString::fromStdString(snippet.display_message());
    if (snippet.has_text_message_details()) {
        parsed.textMessage = QString::fromStdString(snippet.text_message_details().message_text());
    }
    if (snippet.has_message_deleted_details()) {
        parsed.deletedMessageId = QString::fromStdString(snippet.message_deleted_details().deleted_message_id());
    }
    if (snippet.has_user_banned_details()) {
        const auto& banned = snippet.user_banned_details();
        if (banned.has_banned_user_details()) {
            parsed.bannedDisplayName = QString::fromStdString(banned.banned_user_details().display_name());
        }
        parsed.banType = QString::fromStdString(banned.ban_type());
        parsed.banDurationSeconds = banned.ban_duration_seconds();
    }
    if (snippet.has_super_chat_details()) {
        parsed.superChatAmount = QString::fromStdString(snippet.super_chat_details().amount_display_string());
        parsed.superChatComment = QString::fromStdString(snippet.super_chat_details().user_comment());
    }
    if (snippet.has_super_sticker_details()) {
        const auto& details = snippet.super_sticker_details();
        parsed.superStickerAmount = QString::fromStdString(details.amount_display_string());
        if (details.has_super_sticker_metadata()) {
            parsed.superStickerAlt = QString::fromStdString(details.super_sticker_metadata().alt_text());
        }
    }
    if (snippet.has_member_milestone_chat_details()) {
        const auto& details = snippet.member_milestone_chat_details();
        parsed.milestoneMonths = details.member_month();
        parsed.milestoneComment = QString::fromStdString(details.user_comment());
    }
    if (snippet.has_new_sponsor_details()) {
        const auto& details = snippet.new_sponsor_details();
        parsed.sponsorLevel = QString::fromStdString(details.member_level_name());
        parsed.sponsorUpgrade = details.is_upgrade();
    }
    if (snippet.has_membership_gifting_details()) {
        const auto& details = snippet.membership_gifting_details();
        parsed.giftingCount = details.gift_memberships_count();
        parsed.giftingLevel = QString::fromStdString(details.gift_memberships_level_name());
    }
    if (snippet.has_gift_membership_received_details()) {
        parsed.giftReceivedLevel = QString::fromStdString(snippet.gift_membership_received_details().member_level_name());
    }
    if (snippet.has_poll_details() && snippet.poll_details().has_metadata()) {
        parsed.pollQuestion = QString::fromStdString(snippet.poll_details().metadata().question_text());
    }
    if (snippet.has_gift_event_details() && snippet.gift_event_details().has_gift_metadata()) {
        const auto& gift = snippet.gift_event_details().gift_metadata();
        parsed.giftName = QString::fromStdString(gift.gift_name());
        parsed.giftJewels = gift.jewels_amount();
    }
    return parsed;
}
#endif
} // namespace

UnifiedChatMessage parseYouTubeChatMessageJson(const QJsonObject& item)
{
    const QJsonObject snippet = item.value(QStringLiteral("snippet")).toObject();
    const QJsonObject author = item.value(QStringLiteral("authorDetails")).toObject();
    return buildUnifiedChatMessage(
        item.value(QStringLiteral("id")).toString(),
        firstNonEmpty({
            author.value(QStringLiteral("channelId")).toString(),
            snippet.value(QStringLiteral("authorChannelId")).toString(),
        }),
        author.value(QStringLiteral("displayName")).toString(),
        author.value(QStringLiteral("displayName")).toString(),
        author.value(QStringLiteral("channelId")).toString(),
        author.value(QStringLiteral("isChatOwner")).toBool(),
        author.value(QStringLiteral("isChatModerator")).toBool(),
        author.value(QStringLiteral("isChatSponsor")).toBool(),
        author.value(QStringLiteral("isVerified")).toBool(),
        parseJsonSnippet(snippet));
}

UnifiedChatMessage parseInnerTubeChatRenderer(const QJsonObject& renderer, const QString& rendererType)
{
    UnifiedChatMessage msg;
    msg.platform = PlatformId::YouTube;
    msg.messageId = renderer.value(QStringLiteral("id")).toString().trimmed();
    msg.authorId = renderer.value(QStringLiteral("authorExternalChannelId")).toString().trimmed();
    msg.authorName = renderer.value(QStringLiteral("authorName")).toObject()
                         .value(QStringLiteral("simpleText")).toString().trimmed();
    msg.rawAuthorDisplayName = msg.authorName;
    msg.rawAuthorChannelId = msg.authorId;

    // Parse message text + richText from runs[]
    const QJsonArray runs = renderer.value(QStringLiteral("message")).toObject()
                                .value(QStringLiteral("runs")).toArray();
    QString text;
    QString richText;
    QVector<ChatEmojiInfo> emojiList;
    for (const QJsonValue& runVal : runs) {
        const QJsonObject run = runVal.toObject();
        if (run.contains(QStringLiteral("text"))) {
            const QString t = run.value(QStringLiteral("text")).toString();
            text += t;
            richText += t.toHtmlEscaped();
        } else if (run.contains(QStringLiteral("emoji"))) {
            const QJsonObject emoji = run.value(QStringLiteral("emoji")).toObject();
            const bool isCustom = emoji.value(QStringLiteral("isCustomEmoji")).toBool(false);
            const QString emojiId = emoji.value(QStringLiteral("emojiId")).toString();
            if (!isCustom && !emojiId.isEmpty() && !emojiId.contains(QLatin1Char('/'))) {
                text += emojiId;
                richText += emojiId.toHtmlEscaped();
            } else {
                // Extract image URL from thumbnails
                QString imageUrl;
                const QJsonArray thumbnails = emoji.value(QStringLiteral("image"))
                    .toObject().value(QStringLiteral("thumbnails")).toArray();
                for (const QJsonValue& t : thumbnails) {
                    const QJsonObject thumb = t.toObject();
                    const int w = thumb.value(QStringLiteral("width")).toInt();
                    if (w >= 24 && w <= 48) {
                        imageUrl = thumb.value(QStringLiteral("url")).toString();
                        break;
                    }
                }
                if (imageUrl.isEmpty() && !thumbnails.isEmpty()) {
                    imageUrl = thumbnails.first().toObject().value(QStringLiteral("url")).toString();
                }

                const QJsonArray shortcuts = emoji.value(QStringLiteral("shortcuts")).toArray();
                const QString shortcut = shortcuts.isEmpty() ? QString() : shortcuts.first().toString();
                const QString fallback = shortcut.isEmpty() ? emojiId : shortcut;
                text += fallback.isEmpty() ? emojiId : fallback;

                if (!imageUrl.isEmpty() && !emojiId.isEmpty()) {
                    richText += QStringLiteral("<img src='emoji://%1' width='24' height='24' alt='%2'/>")
                        .arg(emojiId, fallback.toHtmlEscaped());
                    ChatEmojiInfo info;
                    info.emojiId = emojiId;
                    info.imageUrl = imageUrl;
                    info.fallbackText = fallback;
                    emojiList.append(info);
                } else {
                    richText += fallback.toHtmlEscaped();
                }
            }
        }
    }

    // Super Chat: use purchaseAmountText + headerSubtext/message
    if (text.isEmpty() && rendererType.contains(QStringLiteral("Paid"))) {
        const QString amount = renderer.value(QStringLiteral("purchaseAmountText")).toObject()
                                   .value(QStringLiteral("simpleText")).toString().trimmed();
        if (!amount.isEmpty()) {
            text = QStringLiteral("[SuperChat %1]").arg(amount);
        }
    }

    // Membership: use headerSubtext
    if (text.isEmpty() && rendererType.contains(QStringLiteral("Membership"))) {
        const QJsonArray headerRuns = renderer.value(QStringLiteral("headerSubtext")).toObject()
                                          .value(QStringLiteral("runs")).toArray();
        for (const QJsonValue& runVal : headerRuns) {
            text += runVal.toObject().value(QStringLiteral("text")).toString();
        }
        if (text.isEmpty()) {
            text = QStringLiteral("[Membership]");
        }
    }

    msg.text = text.trimmed();
    msg.richText = richText;
    msg.emojis = emojiList;

    // Parse timestamp from timestampUsec (microseconds string)
    bool tsOk = false;
    const qint64 timestampUsec = renderer.value(QStringLiteral("timestampUsec")).toString().toLongLong(&tsOk);
    if (tsOk && timestampUsec > 0) {
        msg.timestamp = QDateTime::fromMSecsSinceEpoch(timestampUsec / 1000);
    } else {
        msg.timestamp = QDateTime::currentDateTime();
    }

    // Parse author badges for roles
    const QJsonArray badges = renderer.value(QStringLiteral("authorBadges")).toArray();
    for (const QJsonValue& badgeVal : badges) {
        const QJsonObject badge = badgeVal.toObject()
                                      .value(QStringLiteral("liveChatAuthorBadgeRenderer")).toObject();
        const QString iconType = badge.value(QStringLiteral("icon")).toObject()
                                     .value(QStringLiteral("iconType")).toString();
        if (iconType == QStringLiteral("OWNER")) {
            msg.authorIsChatOwner = true;
        } else if (iconType == QStringLiteral("MODERATOR")) {
            msg.authorIsChatModerator = true;
        } else if (iconType == QStringLiteral("CHECK_CIRCLE_THICK")) {
            msg.authorIsVerified = true;
        }
        if (badge.contains(QStringLiteral("customThumbnail"))) {
            msg.authorIsChatSponsor = true;
        }
    }

    return msg;
}

#if BOTMANAGER_HAS_YT_STREAMLIST
UnifiedChatMessage parseYouTubeChatMessageProto(const ::youtube::api::v3::LiveChatMessage& item)
{
    QString authorId;
    QString authorName;
    if (item.has_author_details()) {
        authorId = QString::fromStdString(item.author_details().channel_id());
        authorName = QString::fromStdString(item.author_details().display_name());
    }
    const bool isChatOwner = item.has_author_details() && item.author_details().is_chat_owner();
    const bool isChatModerator = item.has_author_details() && item.author_details().is_chat_moderator();
    const bool isChatSponsor = item.has_author_details() && item.author_details().is_chat_sponsor();
    const bool isVerified = item.has_author_details() && item.author_details().is_verified();
    const ParsedYouTubeSnippet parsed = item.has_snippet()
        ? parseProtoSnippet(item.snippet())
        : ParsedYouTubeSnippet {};
    return buildUnifiedChatMessage(
        QString::fromStdString(item.id()),
        authorId,
        authorName,
        authorName,
        authorId,
        isChatOwner,
        isChatModerator,
        isChatSponsor,
        isVerified,
        parsed);
}
#endif
