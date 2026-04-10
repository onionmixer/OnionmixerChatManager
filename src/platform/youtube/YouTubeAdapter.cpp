#include "platform/youtube/YouTubeAdapter.h"

#include <QDateTime>
#include <QTimer>

YouTubeAdapter::YouTubeAdapter(QObject* parent)
    : IChatPlatformAdapter(parent)
{
    m_chatTimer = new QTimer(this);
    m_chatTimer->setInterval(2200);
    connect(m_chatTimer, &QTimer::timeout, this, [this]() {
        if (!m_connected) {
            return;
        }

        UnifiedChatMessage msg;
        msg.platform = platformId();
        msg.messageId = QStringLiteral("yt_%1").arg(++m_messageSeq);
        msg.channelId = QStringLiteral("youtube_live_channel");
        msg.channelName = QStringLiteral("YouTube Live");
        msg.authorId = QStringLiteral("yt_author_%1").arg((m_messageSeq % 3) + 1);
        msg.authorName = QStringLiteral("YT_User_%1").arg((m_messageSeq % 3) + 1);
        msg.text = QStringLiteral("YouTube test message #%1").arg(m_messageSeq);
        msg.timestamp = QDateTime::currentDateTime();
        emit chatReceived(msg);
    });
}

PlatformId YouTubeAdapter::platformId() const
{
    return PlatformId::YouTube;
}

void YouTubeAdapter::start(const PlatformSettings& settings)
{
    if (m_connected) {
        emit connected(platformId());
        return;
    }

    if (settings.clientId.trimmed().isEmpty() || settings.redirectUri.trimmed().isEmpty() || settings.scope.trimmed().isEmpty()) {
        emit error(platformId(), QStringLiteral("INVALID_CONFIG"), QStringLiteral("YouTube config is incomplete."));
        return;
    }

    QTimer::singleShot(250, this, [this]() {
        m_connected = true;
        m_chatTimer->start();
        emit connected(platformId());
    });
}

void YouTubeAdapter::stop()
{
    if (!m_connected) {
        emit disconnected(platformId());
        return;
    }

    QTimer::singleShot(100, this, [this]() {
        m_chatTimer->stop();
        m_connected = false;
        emit disconnected(platformId());
    });
}

bool YouTubeAdapter::isConnected() const
{
    return m_connected;
}
