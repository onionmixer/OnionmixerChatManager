#ifndef YOUTUBE_STREAM_LIST_CLIENT_H
#define YOUTUBE_STREAM_LIST_CLIENT_H

#include "core/AppTypes.h"

#include <QObject>
#include <memory>
#include <QVector>

class YouTubeStreamListClient : public QObject {
    Q_OBJECT
public:
    explicit YouTubeStreamListClient(QObject* parent = nullptr);
    ~YouTubeStreamListClient() override;

    bool isSupported() const;
    bool isRunning() const;
    QString supportDetail() const;

public slots:
    void start(const QString& accessToken,
               const QString& liveChatId,
               const QString& pageToken = QString());
    void stop();

signals:
    void started();
    void responseObserved(int itemCount, bool hasNextPageToken, bool hasOfflineAt);
    void messagesReceived(const QVector<UnifiedChatMessage>& messages);
    void streamCheckpoint(const QString& nextPageToken);
    void streamEnded(const QString& reason);
    void streamFailed(const QString& code, const QString& detail);
    void stopped();

private:
    struct Private;
    std::unique_ptr<Private> m_d;
};

#endif // YOUTUBE_STREAM_LIST_CLIENT_H
