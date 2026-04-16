#include "platform/youtube/YouTubeUrlUtils.h"

#include <QTest>
#include <QUrl>

class TestYouTubeUrlUtils : public QObject {
    Q_OBJECT

private slots:
    // --- isLikelyVideoIdCandidate ---
    void videoIdCandidate_valid11chars()
    {
        QVERIFY(YouTubeUrlUtils::isLikelyVideoIdCandidate(QStringLiteral("dQw4w9WgXcQ")));
    }

    void videoIdCandidate_withDashUnderscore()
    {
        QVERIFY(YouTubeUrlUtils::isLikelyVideoIdCandidate(QStringLiteral("abc-_12AB9z")));
    }

    void videoIdCandidate_tooShort()
    {
        QVERIFY(!YouTubeUrlUtils::isLikelyVideoIdCandidate(QStringLiteral("abc123")));
    }

    void videoIdCandidate_tooLong()
    {
        QVERIFY(!YouTubeUrlUtils::isLikelyVideoIdCandidate(QStringLiteral("dQw4w9WgXcQx")));
    }

    void videoIdCandidate_empty()
    {
        QVERIFY(!YouTubeUrlUtils::isLikelyVideoIdCandidate(QString()));
    }

    void videoIdCandidate_liveStreamReserved()
    {
        QVERIFY(!YouTubeUrlUtils::isLikelyVideoIdCandidate(QStringLiteral("live_stream")));
        QVERIFY(!YouTubeUrlUtils::isLikelyVideoIdCandidate(QStringLiteral("LIVE_STREAM")));
    }

    void videoIdCandidate_whitespace()
    {
        QVERIFY(YouTubeUrlUtils::isLikelyVideoIdCandidate(QStringLiteral("  dQw4w9WgXcQ  ")));
    }

    // --- extractVideoIdFromUrl ---
    void extractFromUrl_watchParam()
    {
        QUrl url(QStringLiteral("https://www.youtube.com/watch?v=dQw4w9WgXcQ"));
        QCOMPARE(YouTubeUrlUtils::extractVideoIdFromUrl(url), QStringLiteral("dQw4w9WgXcQ"));
    }

    void extractFromUrl_shortUrl()
    {
        QUrl url(QStringLiteral("https://youtu.be/dQw4w9WgXcQ"));
        QCOMPARE(YouTubeUrlUtils::extractVideoIdFromUrl(url), QStringLiteral("dQw4w9WgXcQ"));
    }

    void extractFromUrl_livePath()
    {
        QUrl url(QStringLiteral("https://www.youtube.com/live/dQw4w9WgXcQ"));
        QCOMPARE(YouTubeUrlUtils::extractVideoIdFromUrl(url), QStringLiteral("dQw4w9WgXcQ"));
    }

    void extractFromUrl_invalid()
    {
        QUrl url(QStringLiteral("https://www.youtube.com/"));
        QVERIFY(YouTubeUrlUtils::extractVideoIdFromUrl(url).isEmpty());
    }

    void extractFromUrl_invalidUrl()
    {
        QVERIFY(YouTubeUrlUtils::extractVideoIdFromUrl(QUrl()).isEmpty());
    }

    // --- extractVideoIdFromHtml ---
    void extractFromHtml_watchUrl()
    {
        const QString html = QStringLiteral("<a href=\"https://www.youtube.com/watch?v=dQw4w9WgXcQ\">link</a>");
        QCOMPARE(YouTubeUrlUtils::extractVideoIdFromHtml(html), QStringLiteral("dQw4w9WgXcQ"));
    }

    void extractFromHtml_videoIdJson()
    {
        const QString html = QStringLiteral("{\"videoId\":\"dQw4w9WgXcQ\"}");
        QCOMPARE(YouTubeUrlUtils::extractVideoIdFromHtml(html), QStringLiteral("dQw4w9WgXcQ"));
    }

    void extractFromHtml_escapedUrl()
    {
        const QString html = QStringLiteral("https:\\u002F\\u002Fwww.youtube.com\\u002Fwatch?v=dQw4w9WgXcQ");
        QCOMPARE(YouTubeUrlUtils::extractVideoIdFromHtml(html), QStringLiteral("dQw4w9WgXcQ"));
    }

    void extractFromHtml_noMatch()
    {
        const QString html = QStringLiteral("<html><body>no video here</body></html>");
        QVERIFY(YouTubeUrlUtils::extractVideoIdFromHtml(html).isEmpty());
    }

    // --- normalizeHandle ---
    void normalizeHandle_withAt()
    {
        QCOMPARE(YouTubeUrlUtils::normalizeHandle(QStringLiteral("@onionmixer")), QStringLiteral("@onionmixer"));
    }

    void normalizeHandle_withoutAt()
    {
        QCOMPARE(YouTubeUrlUtils::normalizeHandle(QStringLiteral("onionmixer")), QStringLiteral("@onionmixer"));
    }

    void normalizeHandle_empty()
    {
        QVERIFY(YouTubeUrlUtils::normalizeHandle(QString()).isEmpty());
    }

    void normalizeHandle_withSpaces()
    {
        QVERIFY(YouTubeUrlUtils::normalizeHandle(QStringLiteral("some name")).isEmpty());
    }

    void normalizeHandle_trimmed()
    {
        QCOMPARE(YouTubeUrlUtils::normalizeHandle(QStringLiteral("  @onionmixer  ")), QStringLiteral("@onionmixer"));
    }

    // --- normalizeHandleForUrl ---
    void normalizeHandleForUrl_plainHandle()
    {
        QCOMPARE(YouTubeUrlUtils::normalizeHandleForUrl(QStringLiteral("onionmixer")), QStringLiteral("@onionmixer"));
    }

    void normalizeHandleForUrl_atHandle()
    {
        QCOMPARE(YouTubeUrlUtils::normalizeHandleForUrl(QStringLiteral("@onionmixer")), QStringLiteral("@onionmixer"));
    }

    void normalizeHandleForUrl_urlWithHandle()
    {
        QCOMPARE(YouTubeUrlUtils::normalizeHandleForUrl(QStringLiteral("https://www.youtube.com/@onionmixer")),
            QStringLiteral("@onionmixer"));
    }

    void normalizeHandleForUrl_urlWithoutHandle()
    {
        QVERIFY(YouTubeUrlUtils::normalizeHandleForUrl(QStringLiteral("https://www.youtube.com/channel/UCxxx")).isEmpty());
    }

    void normalizeHandleForUrl_empty()
    {
        QVERIFY(YouTubeUrlUtils::normalizeHandleForUrl(QString()).isEmpty());
    }

    // --- parseManualVideoIdOverride ---
    void parseOverride_plainVideoId()
    {
        QCOMPARE(YouTubeUrlUtils::parseManualVideoIdOverride(QStringLiteral("dQw4w9WgXcQ")),
            QStringLiteral("dQw4w9WgXcQ"));
    }

    void parseOverride_watchUrl()
    {
        QCOMPARE(YouTubeUrlUtils::parseManualVideoIdOverride(QStringLiteral("https://www.youtube.com/watch?v=dQw4w9WgXcQ")),
            QStringLiteral("dQw4w9WgXcQ"));
    }

    void parseOverride_shortUrl()
    {
        QCOMPARE(YouTubeUrlUtils::parseManualVideoIdOverride(QStringLiteral("https://youtu.be/dQw4w9WgXcQ")),
            QStringLiteral("dQw4w9WgXcQ"));
    }

    void parseOverride_liveUrl()
    {
        QCOMPARE(YouTubeUrlUtils::parseManualVideoIdOverride(QStringLiteral("https://www.youtube.com/live/dQw4w9WgXcQ")),
            QStringLiteral("dQw4w9WgXcQ"));
    }

    void parseOverride_embedUrl()
    {
        QCOMPARE(YouTubeUrlUtils::parseManualVideoIdOverride(QStringLiteral("https://www.youtube.com/embed/dQw4w9WgXcQ")),
            QStringLiteral("dQw4w9WgXcQ"));
    }

    void parseOverride_empty()
    {
        QVERIFY(YouTubeUrlUtils::parseManualVideoIdOverride(QString()).isEmpty());
    }
};

QTEST_MAIN(TestYouTubeUrlUtils)
#include "test_YouTubeUrlUtils.moc"
