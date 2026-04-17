#ifndef YOUTUBE_CHAT_MESSAGE_PARSER_H
#define YOUTUBE_CHAT_MESSAGE_PARSER_H

#include "core/AppTypes.h"

#include <QJsonObject>

#if ONIONMIXERCHATMANAGER_HAS_YT_STREAMLIST
#include "stream_list.pb.h"
#endif

UnifiedChatMessage parseYouTubeChatMessageJson(const QJsonObject& item);
UnifiedChatMessage parseInnerTubeChatRenderer(const QJsonObject& renderer, const QString& rendererType);

#if ONIONMIXERCHATMANAGER_HAS_YT_STREAMLIST
UnifiedChatMessage parseYouTubeChatMessageProto(const ::youtube::api::v3::LiveChatMessage& item);
#endif

#endif // YOUTUBE_CHAT_MESSAGE_PARSER_H
