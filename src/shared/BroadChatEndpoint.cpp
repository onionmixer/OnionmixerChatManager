#include "BroadChatEndpoint.h"

#include <QHostAddress>

namespace BroadChatEndpoint {

QString normalizeBindAddress(const QString& iniValue)
{
    const QString trimmed = iniValue.trimmed();
    if (trimmed.isEmpty()) {
        return QString::fromLatin1(kDefaultBindAddress);
    }
    // "0.0.0.0"·"::"·"127.0.0.1"·특정 IP 등 QHostAddress로 파싱.
    QHostAddress addr;
    if (!addr.setAddress(trimmed)) {
        return QString::fromLatin1(kDefaultBindAddress);
    }
    return trimmed;
}

quint16 normalizePort(int iniValue)
{
    if (iniValue < kMinTcpPort || iniValue > kMaxTcpPort) {
        return kDefaultTcpPort;
    }
    return static_cast<quint16>(iniValue);
}

} // namespace BroadChatEndpoint
