#include "platform/youtube/YouTubeUrlUtils.h"

#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace YouTubeUrlUtils {

QString parseManualVideoIdOverride(const QString& raw)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    const QUrl url(trimmed);
    if (url.isValid() && !url.scheme().isEmpty() && !url.host().trimmed().isEmpty()) {
        const QUrlQuery query(url);
        const QString v = query.queryItemValue(QStringLiteral("v")).trimmed();
        if (!v.isEmpty()) {
            return v;
        }

        const QStringList segments = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (!segments.isEmpty()) {
            if (url.host().contains(QStringLiteral("youtu.be"), Qt::CaseInsensitive)) {
                return segments.last().trimmed();
            }
            const int liveIndex = segments.indexOf(QStringLiteral("live"));
            if (liveIndex >= 0 && liveIndex + 1 < segments.size()) {
                return segments.at(liveIndex + 1).trimmed();
            }
            const int embedIndex = segments.indexOf(QStringLiteral("embed"));
            if (embedIndex >= 0 && embedIndex + 1 < segments.size()) {
                return segments.at(embedIndex + 1).trimmed();
            }
        }
    }

    return trimmed;
}

QString normalizeHandleForUrl(const QString& raw)
{
    QString value = raw.trimmed();
    if (value.isEmpty()) {
        return QString();
    }
    if (value.startsWith(QStringLiteral("http://")) || value.startsWith(QStringLiteral("https://"))) {
        const QUrl url(value);
        const QStringList segments = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (const QString& segment : segments) {
            if (segment.startsWith(QStringLiteral("@"))) {
                return segment;
            }
        }
        return QString();
    }
    if (value.startsWith(QStringLiteral("@"))) {
        return value;
    }
    if (value.contains(QLatin1Char(' '))) {
        return QString();
    }
    return QStringLiteral("@%1").arg(value);
}

bool isLikelyVideoIdCandidate(const QString& value)
{
    const QString trimmed = value.trimmed();
    static const QRegularExpression reVideoId(QStringLiteral("^[A-Za-z0-9_-]{11}$"));
    if (!reVideoId.match(trimmed).hasMatch()) {
        return false;
    }
    const QString lowered = trimmed.toLower();
    return lowered != QStringLiteral("live_stream");
}

QString extractVideoIdFromUrl(const QUrl& url)
{
    if (!url.isValid()) {
        return QString();
    }

    const QUrlQuery query(url);
    const QString v = query.queryItemValue(QStringLiteral("v")).trimmed();
    if (isLikelyVideoIdCandidate(v)) {
        return v;
    }

    const QStringList segments = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (url.host().contains(QStringLiteral("youtu.be"), Qt::CaseInsensitive) && !segments.isEmpty()) {
        const QString candidate = segments.last().trimmed();
        if (isLikelyVideoIdCandidate(candidate)) {
            return candidate;
        }
    }
    const int liveIndex = segments.indexOf(QStringLiteral("live"));
    if (liveIndex >= 0 && liveIndex + 1 < segments.size()) {
        const QString candidate = segments.at(liveIndex + 1).trimmed();
        if (isLikelyVideoIdCandidate(candidate)) {
            return candidate;
        }
    }
    return QString();
}

QString extractVideoIdFromHtml(const QString& html)
{
    QString normalized = html;
    normalized.replace(QStringLiteral("\\u0026"), QStringLiteral("&"));
    normalized.replace(QStringLiteral("\\u003D"), QStringLiteral("="));
    normalized.replace(QStringLiteral("\\u003d"), QStringLiteral("="));
    normalized.replace(QStringLiteral("\\u003F"), QStringLiteral("?"));
    normalized.replace(QStringLiteral("\\u003f"), QStringLiteral("?"));
    normalized.replace(QStringLiteral("\\u002F"), QStringLiteral("/"));
    normalized.replace(QStringLiteral("\\u002f"), QStringLiteral("/"));
    normalized.replace(QStringLiteral("\\/"), QStringLiteral("/"));
    normalized.replace(QStringLiteral("&amp;"), QStringLiteral("&"));

    static const QList<QRegularExpression> patterns = {
        QRegularExpression(QStringLiteral("https?://www\\.youtube\\.com/watch\\?v=([A-Za-z0-9_-]{11})")),
        QRegularExpression(QStringLiteral("https?://youtube\\.com/watch\\?v=([A-Za-z0-9_-]{11})")),
        QRegularExpression(QStringLiteral("content=\"https?://www\\.youtube\\.com/watch\\?v=([A-Za-z0-9_-]{11})")),
        QRegularExpression(QStringLiteral("href=\"https?://www\\.youtube\\.com/watch\\?v=([A-Za-z0-9_-]{11})")),
        QRegularExpression(QStringLiteral("/watch\\?v=([A-Za-z0-9_-]{11})")),
        QRegularExpression(QStringLiteral("watch\\?v=([A-Za-z0-9_-]{11})")),
        QRegularExpression(QStringLiteral("\"videoId\"\\s*:\\s*\"([A-Za-z0-9_-]{11})\"")),
        QRegularExpression(QStringLiteral("'VIDEO_ID'\\s*:\\s*\"([A-Za-z0-9_-]{11})\"")),
        QRegularExpression(QStringLiteral("\"canonicalBaseUrl\"\\s*:\\s*\"/watch\\?v=([A-Za-z0-9_-]{11})")),
        QRegularExpression(QStringLiteral("\"url\"\\s*:\\s*\"/watch\\?v=([A-Za-z0-9_-]{11})"))
    };

    for (const QRegularExpression& pattern : patterns) {
        const QRegularExpressionMatch match = pattern.match(normalized);
        if (match.hasMatch()) {
            const QString candidate = match.captured(1).trimmed();
            if (isLikelyVideoIdCandidate(candidate)) {
                return candidate;
            }
        }
    }
    return QString();
}

QString normalizeHandle(const QString& value)
{
    QString normalized = value.trimmed();
    if (normalized.isEmpty()) {
        return QString();
    }
    if (normalized.startsWith(QStringLiteral("@"))) {
        return normalized;
    }
    if (normalized.contains(QLatin1Char(' '))) {
        return QString();
    }
    return QStringLiteral("@%1").arg(normalized);
}

} // namespace YouTubeUrlUtils
