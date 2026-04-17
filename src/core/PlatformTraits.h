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

// 방송창 viewer count 오버레이에서 아이콘(플랫폼 뱃지 심볼)을 그릴 때 사용하는
// 색상. 숫자 부분은 기본 흰색(ViewerCountStyle::kFg)이며 아이콘만 플랫폼 고유
// 색으로 칠한다. 새 플랫폼 추가 시 여기에 분기 한 줄만 추가.
inline QString viewerIconColor(PlatformId p)
{
    return p == PlatformId::YouTube ? QStringLiteral("#FF4444")   // 붉은색
                                    : QStringLiteral("#4AFF7F");  // 밝은 초록
}

} // namespace PlatformTraits

#endif // PLATFORM_TRAITS_H
