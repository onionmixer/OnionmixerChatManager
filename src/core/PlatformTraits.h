#ifndef PLATFORM_TRAITS_H
#define PLATFORM_TRAITS_H

#include "core/AppTypes.h"

#include <QString>

namespace PlatformTraits {

inline QString badgeSymbol(PlatformId p)
{
    return p == PlatformId::YouTube ? QStringLiteral("\u25B6") : QStringLiteral("Z");
}

inline QString badgeBgColor(PlatformId p)
{
    return p == PlatformId::YouTube ? QStringLiteral("#E53935") : QStringLiteral("#16C784");
}

inline QString badgeFgColor(PlatformId p)
{
    return p == PlatformId::YouTube ? QStringLiteral("#ffffff") : QStringLiteral("#101010");
}

inline QString authorColor(PlatformId p)
{
    return p == PlatformId::YouTube ? QStringLiteral("#6A3FA0") : QStringLiteral("#D17A00");
}

inline QString displayName(PlatformId p)
{
    return p == PlatformId::YouTube ? QStringLiteral("YouTube") : QStringLiteral("CHZZK");
}

} // namespace PlatformTraits

#endif // PLATFORM_TRAITS_H
