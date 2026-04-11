#ifndef YOUTUBE_LIVE_CHAT_WEB_CLIENT_H
#define YOUTUBE_LIVE_CHAT_WEB_CLIENT_H

#include "core/AppTypes.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

class YouTubeLiveChatWebClient : public QObject {
    Q_OBJECT
public:
    explicit YouTubeLiveChatWebClient(QObject* parent = nullptr);

    bool isRunning() const;

public slots:
    void start(const QString& videoId);
    void stop();

signals:
    void started();
    void messagesReceived(const QVector<UnifiedChatMessage>& messages);
    void failed(const QString& code, const QString& detail);
    void ended(const QString& reason);

private:
    void fetchInitialPage();
    void pollContinuation();
    void handleInitialPageResponse(QNetworkReply* reply);
    void handleContinuationResponse(QNetworkReply* reply);
    QString extractYtInitialData(const QString& html) const;
    QString extractInnertubeApiKey(const QString& html) const;
    QString extractClientVersion(const QString& html) const;
    QString extractContinuationToken(const QJsonObject& root) const;
    int extractTimeoutMs(const QJsonObject& root) const;
    QVector<UnifiedChatMessage> parseActions(const QJsonArray& actions) const;

    QNetworkAccessManager* m_network = nullptr;
    QTimer* m_pollTimer = nullptr;
    QString m_videoId;
    QString m_continuationToken;
    QString m_innertubeApiKey;
    QString m_clientVersion;
    bool m_running = false;
    int m_generation = 0;
    int m_consecutiveEmptyCount = 0;
};

#endif // YOUTUBE_LIVE_CHAT_WEB_CLIENT_H
