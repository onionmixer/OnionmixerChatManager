#ifndef YOUTUBE_ENDPOINTS_H
#define YOUTUBE_ENDPOINTS_H

#include <QString>

namespace YouTube {
namespace Api {
    inline QString channels()      { return QStringLiteral("https://www.googleapis.com/youtube/v3/channels"); }
    inline QString liveBroadcasts(){ return QStringLiteral("https://www.googleapis.com/youtube/v3/liveBroadcasts"); }
    inline QString liveChatMessages() { return QStringLiteral("https://www.googleapis.com/youtube/v3/liveChat/messages"); }
    inline QString playlistItems() { return QStringLiteral("https://www.googleapis.com/youtube/v3/playlistItems"); }
    inline QString search()        { return QStringLiteral("https://www.googleapis.com/youtube/v3/search"); }
    inline QString videos()        { return QStringLiteral("https://www.googleapis.com/youtube/v3/videos"); }
}
namespace Web {
    inline QString liveChatPage()  { return QStringLiteral("https://www.youtube.com/live_chat"); }
    inline QString innerTubeGetLiveChat() { return QStringLiteral("https://www.youtube.com/youtubei/v1/live_chat/get_live_chat"); }
    inline QString embedLiveStream() { return QStringLiteral("https://www.youtube.com/embed/live_stream"); }
    inline QString feedsVideosXml(){ return QStringLiteral("https://www.youtube.com/feeds/videos.xml"); }
    inline QString handleLive(const QString& handle)  { return QStringLiteral("https://www.youtube.com/%1/live").arg(handle); }
    inline QString handleStreams(const QString& handle){ return QStringLiteral("https://www.youtube.com/%1/streams").arg(handle); }
    inline QString channelLive(const QString& channelId) { return QStringLiteral("https://www.youtube.com/channel/%1/live").arg(channelId); }
}
} // namespace YouTube

#endif // YOUTUBE_ENDPOINTS_H
