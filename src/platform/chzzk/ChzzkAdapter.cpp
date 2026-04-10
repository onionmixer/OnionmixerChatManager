#include "platform/chzzk/ChzzkAdapter.h"

#include <QDateTime>
#include <QTimer>

ChzzkAdapter::ChzzkAdapter(QObject* parent)
    : IChatPlatformAdapter(parent)
{
    m_chatTimer = new QTimer(this);
    m_chatTimer->setInterval(2400);
    connect(m_chatTimer, &QTimer::timeout, this, [this]() {
        if (!m_connected) {
            return;
        }

        UnifiedChatMessage msg;
        msg.platform = platformId();
        msg.messageId = QStringLiteral("chz_%1").arg(++m_messageSeq);
        msg.channelId = QStringLiteral("chzzk_live_channel");
        msg.channelName = QStringLiteral("CHZZK Live");
        msg.authorId = QStringLiteral("chz_author_%1").arg((m_messageSeq % 4) + 1);
        msg.authorName = QStringLiteral("CHZ_User_%1").arg((m_messageSeq % 4) + 1);
        msg.text = QStringLiteral("CHZZK test message #%1").arg(m_messageSeq);
        msg.timestamp = QDateTime::currentDateTime();
        emit chatReceived(msg);
    });
}

PlatformId ChzzkAdapter::platformId() const
{
    return PlatformId::Chzzk;
}

void ChzzkAdapter::start(const PlatformSettings& settings)
{
    if (m_connected) {
        emit connected(platformId());
        return;
    }

    if (settings.clientId.trimmed().isEmpty() || settings.clientSecret.trimmed().isEmpty() || settings.redirectUri.trimmed().isEmpty() || settings.scope.trimmed().isEmpty()) {
        emit error(platformId(), QStringLiteral("INVALID_CONFIG"), QStringLiteral("CHZZK config is incomplete."));
        return;
    }

    QTimer::singleShot(300, this, [this]() {
        m_connected = true;
        m_chatTimer->start();
        emit connected(platformId());
    });
}

void ChzzkAdapter::stop()
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

bool ChzzkAdapter::isConnected() const
{
    return m_connected;
}
