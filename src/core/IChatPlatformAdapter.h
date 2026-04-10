#ifndef I_CHAT_PLATFORM_ADAPTER_H
#define I_CHAT_PLATFORM_ADAPTER_H

#include "core/AppTypes.h"

#include <QObject>
#include <QString>

class IChatPlatformAdapter : public QObject {
    Q_OBJECT
public:
    explicit IChatPlatformAdapter(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~IChatPlatformAdapter() override = default;

    virtual PlatformId platformId() const = 0;
    virtual void start(const PlatformSettings& settings) = 0;
    virtual void stop() = 0;
    virtual bool isConnected() const = 0;

signals:
    void connected(PlatformId platform);
    void disconnected(PlatformId platform);
    void error(PlatformId platform, const QString& code, const QString& message);
    void chatReceived(const UnifiedChatMessage& message);
};

#endif // I_CHAT_PLATFORM_ADAPTER_H
