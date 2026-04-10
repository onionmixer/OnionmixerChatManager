#ifndef CHZZK_ADAPTER_H
#define CHZZK_ADAPTER_H

#include "core/IChatPlatformAdapter.h"

class QTimer;

class ChzzkAdapter : public IChatPlatformAdapter {
    Q_OBJECT
public:
    explicit ChzzkAdapter(QObject* parent = nullptr);

    PlatformId platformId() const override;
    void start(const PlatformSettings& settings) override;
    void stop() override;
    bool isConnected() const override;

private:
    bool m_connected = false;
    int m_messageSeq = 0;
    QTimer* m_chatTimer = nullptr;
};

#endif // CHZZK_ADAPTER_H
