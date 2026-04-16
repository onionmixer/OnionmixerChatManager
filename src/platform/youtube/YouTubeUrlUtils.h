#ifndef YOUTUBE_URL_UTILS_H
#define YOUTUBE_URL_UTILS_H

#include <QString>

class QUrl;

namespace YouTubeUrlUtils {

QString parseManualVideoIdOverride(const QString& raw);
QString normalizeHandleForUrl(const QString& raw);
bool isLikelyVideoIdCandidate(const QString& value);
QString extractVideoIdFromUrl(const QUrl& url);
QString extractVideoIdFromHtml(const QString& html);
QString normalizeHandle(const QString& value);

} // namespace YouTubeUrlUtils

#endif // YOUTUBE_URL_UTILS_H
