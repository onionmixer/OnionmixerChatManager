#ifndef YOUTUBE_ADAPTER_H
#define YOUTUBE_ADAPTER_H

#include "core/IChatPlatformAdapter.h"

class QTimer;

class YouTubeAdapter : public IChatPlatformAdapter {
    Q_OBJECT
public:
    explicit YouTubeAdapter(QObject* parent = nullptr);

    PlatformId platformId() const override;
    void start(const PlatformSettings& settings) override;
    void stop() override;
    bool isConnected() const override;

private:
    bool m_connected = false;
    int m_messageSeq = 0;
    QTimer* m_chatTimer = nullptr;
};

#endif // YOUTUBE_ADAPTER_H
