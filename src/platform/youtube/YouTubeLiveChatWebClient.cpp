#include "platform/youtube/YouTubeLiveChatWebClient.h"
#include "platform/youtube/YouTubeChatMessageParser.h"
#include "platform/youtube/YouTubeEndpoints.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {
const QString kDefaultClientVersion = QStringLiteral("2.20260206.01.00");
const int kDefaultPollIntervalMs = 3000;
const int kMinPollIntervalMs = 1000;
const int kMaxPollIntervalMs = 10000;
const int kRequestTimeoutMs = 12000;
const int kMaxConsecutiveEmpty = 120;

int clampPollInterval(int ms)
{
    if (ms <= 0) {
        return kDefaultPollIntervalMs;
    }
    if (ms < kMinPollIntervalMs) {
        return kMinPollIntervalMs;
    }
    if (ms > kMaxPollIntervalMs) {
        return kMaxPollIntervalMs;
    }
    return ms;
}
} // namespace

YouTubeLiveChatWebClient::YouTubeLiveChatWebClient(QObject* parent)
    : QObject(parent)
{
    m_network = new QNetworkAccessManager(this);
    m_pollTimer = new QTimer(this);
    m_pollTimer->setSingleShot(true);
    connect(m_pollTimer, &QTimer::timeout, this, &YouTubeLiveChatWebClient::pollContinuation);
}

bool YouTubeLiveChatWebClient::isRunning() const
{
    return m_running;
}

void YouTubeLiveChatWebClient::start(const QString& videoId)
{
    stop();

    const QString trimmed = videoId.trimmed();
    if (trimmed.isEmpty()) {
        emit failed(QStringLiteral("YT_WEBCHAT_NO_VIDEO_ID"),
            QStringLiteral("Video ID is empty."));
        return;
    }

    m_videoId = trimmed;
    m_continuationToken.clear();
    m_innertubeApiKey.clear();
    m_clientVersion = kDefaultClientVersion;
    m_consecutiveEmptyCount = 0;
    ++m_generation;
    m_running = true;

    fetchInitialPage();
}

void YouTubeLiveChatWebClient::stop()
{
    m_running = false;
    ++m_generation;
    if (m_pollTimer) {
        m_pollTimer->stop();
    }
    m_continuationToken.clear();
    m_videoId.clear();
    m_consecutiveEmptyCount = 0;
}

void YouTubeLiveChatWebClient::fetchInitialPage()
{
    if (!m_running) {
        return;
    }

    QUrl url(YouTube::Web::liveChatPage());
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("v"), m_videoId);
    query.addQueryItem(QStringLiteral("is_popout"), QStringLiteral("1"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"));
    req.setRawHeader("Accept-Language", "en-US,en;q=0.9");

    QNetworkReply* reply = m_network->get(req);
    if (!reply) {
        emit failed(QStringLiteral("YT_WEBCHAT_FETCH_FAILED"),
            QStringLiteral("Failed to create initial page request."));
        m_running = false;
        return;
    }

    const int gen = m_generation;
    QPointer<QNetworkReply> guard(reply);
    QTimer::singleShot(kRequestTimeoutMs, this, [gen, guard]() {
        if (!guard || guard->isFinished()) {
            return;
        }
        Q_UNUSED(gen)
        guard->abort();
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        if (gen != m_generation) {
            reply->deleteLater();
            return;
        }
        handleInitialPageResponse(reply);
        reply->deleteLater();
    });
}

void YouTubeLiveChatWebClient::handleInitialPageResponse(QNetworkReply* reply)
{
    if (!m_running) {
        return;
    }

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool httpOk = httpStatus >= 200 && httpStatus < 300;

    if (reply->error() != QNetworkReply::NoError || !httpOk) {
        emit failed(QStringLiteral("YT_WEBCHAT_INITIAL_PAGE_FAILED"),
            QStringLiteral("HTTP %1: %2").arg(httpStatus).arg(reply->errorString()));
        m_running = false;
        return;
    }

    const QString html = QString::fromUtf8(reply->readAll());

    const QString apiKey = extractInnertubeApiKey(html);
    if (!apiKey.isEmpty()) {
        m_innertubeApiKey = apiKey;
    }
    if (m_innertubeApiKey.isEmpty()) {
        emit failed(QStringLiteral("YT_WEBCHAT_NO_API_KEY"),
            QStringLiteral("Could not extract INNERTUBE_API_KEY from live_chat page."));
        m_running = false;
        return;
    }
    const QString clientVersion = extractClientVersion(html);
    if (!clientVersion.isEmpty()) {
        m_clientVersion = clientVersion;
    }

    const QString ytInitialDataStr = extractYtInitialData(html);
    if (ytInitialDataStr.isEmpty()) {
        emit failed(QStringLiteral("YT_WEBCHAT_NO_INITIAL_DATA"),
            QStringLiteral("Could not extract ytInitialData from live_chat page."));
        m_running = false;
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(ytInitialDataStr.toUtf8());
    if (!doc.isObject()) {
        emit failed(QStringLiteral("YT_WEBCHAT_INVALID_INITIAL_DATA"),
            QStringLiteral("ytInitialData is not valid JSON."));
        m_running = false;
        return;
    }

    const QJsonObject root = doc.object();
    m_continuationToken = extractContinuationToken(root);

    if (m_continuationToken.isEmpty()) {
        emit failed(QStringLiteral("YT_WEBCHAT_NO_CONTINUATION"),
            QStringLiteral("Could not extract continuation token from ytInitialData."));
        m_running = false;
        return;
    }

    // Parse initial messages from ytInitialData
    const QJsonArray actions = root.value(QStringLiteral("contents"))
                                   .toObject()
                                   .value(QStringLiteral("liveChatRenderer"))
                                   .toObject()
                                   .value(QStringLiteral("actions"))
                                   .toArray();
    emit started();

    if (!actions.isEmpty()) {
        const QVector<UnifiedChatMessage> messages = parseActions(actions);
        if (!messages.isEmpty()) {
            emit messagesReceived(messages);
        }
    }

    const int timeoutMs = extractTimeoutMs(root);
    m_pollTimer->start(clampPollInterval(timeoutMs));
}

void YouTubeLiveChatWebClient::pollContinuation()
{
    if (!m_running || m_continuationToken.isEmpty()) {
        return;
    }

    QUrl url(YouTube::Web::innerTubeGetLiveChat());
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), m_innertubeApiKey);
    query.addQueryItem(QStringLiteral("prettyPrint"), QStringLiteral("false"));
    url.setQuery(query);

    QJsonObject clientObj;
    clientObj.insert(QStringLiteral("clientName"), QStringLiteral("WEB"));
    clientObj.insert(QStringLiteral("clientVersion"), m_clientVersion);
    clientObj.insert(QStringLiteral("hl"), QStringLiteral("en"));
    clientObj.insert(QStringLiteral("timeZone"), QStringLiteral("UTC"));

    QJsonObject contextObj;
    contextObj.insert(QStringLiteral("client"), clientObj);

    QJsonObject body;
    body.insert(QStringLiteral("context"), contextObj);
    body.insert(QStringLiteral("continuation"), m_continuationToken);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"));
    req.setRawHeader("X-Youtube-Client-Name", "1");
    req.setRawHeader("X-Youtube-Client-Version", m_clientVersion.toUtf8());
    req.setRawHeader("Origin", "https://www.youtube.com");

    QNetworkReply* reply = m_network->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    if (!reply) {
        emit failed(QStringLiteral("YT_WEBCHAT_POLL_FAILED"),
            QStringLiteral("Failed to create continuation poll request."));
        m_running = false;
        return;
    }

    const int gen = m_generation;
    QPointer<QNetworkReply> guard(reply);
    QTimer::singleShot(kRequestTimeoutMs, this, [gen, guard]() {
        if (!guard || guard->isFinished()) {
            return;
        }
        Q_UNUSED(gen)
        guard->abort();
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        if (gen != m_generation) {
            reply->deleteLater();
            return;
        }
        handleContinuationResponse(reply);
        reply->deleteLater();
    });
}

void YouTubeLiveChatWebClient::handleContinuationResponse(QNetworkReply* reply)
{
    if (!m_running) {
        return;
    }

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool httpOk = httpStatus >= 200 && httpStatus < 300;

    if (reply->error() != QNetworkReply::NoError || !httpOk) {
        emit failed(QStringLiteral("YT_WEBCHAT_POLL_HTTP_ERROR"),
            QStringLiteral("HTTP %1: %2").arg(httpStatus).arg(reply->errorString()));
        m_running = false;
        return;
    }

    const QByteArray body = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        emit failed(QStringLiteral("YT_WEBCHAT_POLL_INVALID_JSON"),
            QStringLiteral("Continuation response is not valid JSON."));
        m_running = false;
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonObject liveChatContinuation = root.value(QStringLiteral("continuationContents"))
                                                  .toObject()
                                                  .value(QStringLiteral("liveChatContinuation"))
                                                  .toObject();

    if (liveChatContinuation.isEmpty()) {
        emit ended(QStringLiteral("liveChatContinuation absent in response; chat may have ended."));
        m_running = false;
        return;
    }

    // Extract next continuation token
    const QJsonArray continuations = liveChatContinuation.value(QStringLiteral("continuations")).toArray();
    QString nextToken;
    int timeoutMs = kDefaultPollIntervalMs;
    for (const QJsonValue& v : continuations) {
        const QJsonObject entry = v.toObject();
        for (const QString& key : {
                 QStringLiteral("invalidationContinuationData"),
                 QStringLiteral("timedContinuationData"),
                 QStringLiteral("reloadContinuationData") }) {
            const QJsonObject data = entry.value(key).toObject();
            if (data.isEmpty()) {
                continue;
            }
            const QString token = data.value(QStringLiteral("continuation")).toString().trimmed();
            if (!token.isEmpty()) {
                nextToken = token;
                const int t = data.value(QStringLiteral("timeoutMs")).toInt(0);
                if (t > 0) {
                    timeoutMs = t;
                }
                break;
            }
        }
        if (!nextToken.isEmpty()) {
            break;
        }
    }

    if (nextToken.isEmpty()) {
        emit ended(QStringLiteral("No continuation token in response; chat ended."));
        m_running = false;
        return;
    }
    m_continuationToken = nextToken;

    // Parse chat messages from actions
    const QJsonArray actions = liveChatContinuation.value(QStringLiteral("actions")).toArray();
    if (!actions.isEmpty()) {
        const QVector<UnifiedChatMessage> messages = parseActions(actions);
        if (!messages.isEmpty()) {
            m_consecutiveEmptyCount = 0;
            emit messagesReceived(messages);
        } else {
            ++m_consecutiveEmptyCount;
        }
    } else {
        ++m_consecutiveEmptyCount;
    }

    if (m_consecutiveEmptyCount >= kMaxConsecutiveEmpty) {
        emit ended(QStringLiteral("No messages for extended period; assuming chat ended."));
        m_running = false;
        return;
    }

    m_pollTimer->start(clampPollInterval(timeoutMs));
}

QString YouTubeLiveChatWebClient::extractYtInitialData(const QString& html) const
{
    static const QRegularExpression re(
        QStringLiteral("(?:window\\s*\\[\\s*[\"']ytInitialData[\"']\\s*\\]|ytInitialData)\\s*=\\s*(\\{.+?\\})\\s*;\\s*(?:</script|\\n|var\\s)"));
    const QRegularExpressionMatch match = re.match(html);
    if (match.hasMatch()) {
        return match.captured(1);
    }
    return QString();
}

QString YouTubeLiveChatWebClient::extractInnertubeApiKey(const QString& html) const
{
    static const QRegularExpression re(
        QStringLiteral("[\"']INNERTUBE_API_KEY[\"']\\s*:\\s*[\"']([A-Za-z0-9_-]+)[\"']"));
    const QRegularExpressionMatch match = re.match(html);
    if (match.hasMatch()) {
        return match.captured(1);
    }
    return QString();
}

QString YouTubeLiveChatWebClient::extractClientVersion(const QString& html) const
{
    static const QRegularExpression re(
        QStringLiteral("[\"']INNERTUBE_CLIENT_VERSION[\"']\\s*:\\s*[\"']([0-9.]+)[\"']"));
    const QRegularExpressionMatch match = re.match(html);
    if (match.hasMatch()) {
        return match.captured(1);
    }
    return QString();
}

QString YouTubeLiveChatWebClient::extractContinuationToken(const QJsonObject& root) const
{
    // Path: contents.liveChatRenderer.continuations[].{type}.continuation
    const QJsonArray continuations = root.value(QStringLiteral("contents"))
                                         .toObject()
                                         .value(QStringLiteral("liveChatRenderer"))
                                         .toObject()
                                         .value(QStringLiteral("continuations"))
                                         .toArray();
    for (const QJsonValue& v : continuations) {
        const QJsonObject entry = v.toObject();
        for (const QString& key : {
                 QStringLiteral("invalidationContinuationData"),
                 QStringLiteral("timedContinuationData"),
                 QStringLiteral("reloadContinuationData") }) {
            const QString token = entry.value(key).toObject()
                                      .value(QStringLiteral("continuation"))
                                      .toString()
                                      .trimmed();
            if (!token.isEmpty()) {
                return token;
            }
        }
    }
    return QString();
}

int YouTubeLiveChatWebClient::extractTimeoutMs(const QJsonObject& root) const
{
    const QJsonArray continuations = root.value(QStringLiteral("contents"))
                                         .toObject()
                                         .value(QStringLiteral("liveChatRenderer"))
                                         .toObject()
                                         .value(QStringLiteral("continuations"))
                                         .toArray();
    for (const QJsonValue& v : continuations) {
        const QJsonObject entry = v.toObject();
        for (const QString& key : {
                 QStringLiteral("invalidationContinuationData"),
                 QStringLiteral("timedContinuationData") }) {
            const QJsonObject data = entry.value(key).toObject();
            const int t = data.value(QStringLiteral("timeoutMs")).toInt(0);
            if (t > 0) {
                return t;
            }
        }
    }
    return kDefaultPollIntervalMs;
}

QVector<UnifiedChatMessage> YouTubeLiveChatWebClient::parseActions(const QJsonArray& actions) const
{
    QVector<UnifiedChatMessage> results;
    results.reserve(actions.size());

    for (const QJsonValue& actionVal : actions) {
        const QJsonObject action = actionVal.toObject();
        const QJsonObject addChatItem = action.value(QStringLiteral("addChatItemAction")).toObject();
        if (addChatItem.isEmpty()) {
            continue;
        }
        const QJsonObject item = addChatItem.value(QStringLiteral("item")).toObject();
        if (item.isEmpty()) {
            continue;
        }

        static const QStringList rendererKeys = {
            QStringLiteral("liveChatTextMessageRenderer"),
            QStringLiteral("liveChatPaidMessageRenderer"),
            QStringLiteral("liveChatPaidStickerRenderer"),
            QStringLiteral("liveChatMembershipItemRenderer"),
            QStringLiteral("liveChatSponsorshipsGiftPurchaseAnnouncementRenderer"),
            QStringLiteral("liveChatSponsorshipsGiftRedemptionAnnouncementRenderer"),
        };

        for (const QString& key : rendererKeys) {
            const QJsonObject renderer = item.value(key).toObject();
            if (renderer.isEmpty()) {
                continue;
            }
            const UnifiedChatMessage msg = parseInnerTubeChatRenderer(renderer, key);
            if (!msg.messageId.trimmed().isEmpty()) {
                results.append(msg);
            }
            break;
        }
    }

    return results;
}
