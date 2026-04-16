#ifndef CHZZK_ENDPOINTS_H
#define CHZZK_ENDPOINTS_H

#include <QString>

namespace Chzzk {
namespace OpenApi {
    inline QString sessionAuth()       { return QStringLiteral("https://openapi.chzzk.naver.com/open/v1/sessions/auth"); }
    inline QString sessionAuthClient() { return QStringLiteral("https://openapi.chzzk.naver.com/open/v1/sessions/auth/client"); }
    inline QString subscribeChat()     { return QStringLiteral("https://openapi.chzzk.naver.com/open/v1/sessions/events/subscribe/chat"); }
    inline QString usersMe()           { return QStringLiteral("https://openapi.chzzk.naver.com/open/v1/users/me"); }
    inline QString chatsSend()         { return QStringLiteral("https://openapi.chzzk.naver.com/open/v1/chats/send"); }
}
namespace ServiceApi {
    inline QString liveDetail(const QString& channelId) { return QStringLiteral("https://api.chzzk.naver.com/service/v3/channels/%1/live-detail").arg(channelId); }
    inline QString emojiPacks(const QString& channelId) { return QStringLiteral("https://api.chzzk.naver.com/service/v1/channels/%1/emoji-packs").arg(channelId); }
}
} // namespace Chzzk

#endif // CHZZK_ENDPOINTS_H
