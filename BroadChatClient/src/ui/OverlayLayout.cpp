#include "OverlayLayout.h"

namespace OverlayLayout {

Corner badgeCornerFor(const QString& viewerPos)
{
    if (viewerPos == QStringLiteral("TopLeft"))      return Corner::TopRight;
    if (viewerPos == QStringLiteral("TopRight"))     return Corner::TopLeft;
    if (viewerPos == QStringLiteral("BottomLeft"))   return Corner::BottomRight;
    if (viewerPos == QStringLiteral("BottomRight"))  return Corner::BottomLeft;
    if (viewerPos == QStringLiteral("TopCenter"))    return Corner::TopRight;
    if (viewerPos == QStringLiteral("BottomCenter")) return Corner::BottomRight;
    if (viewerPos == QStringLiteral("CenterLeft"))   return Corner::BottomRight;
    if (viewerPos == QStringLiteral("CenterRight"))  return Corner::BottomRight;
    // Hidden · 미지값: 기본 좌상단 (viewer 가 없어 충돌 없음)
    return Corner::TopLeft;
}

PlaceholderPlacement placeholderPlacementFor(const QString& viewerPos)
{
    PlaceholderPlacement r;
    if (viewerPos == QStringLiteral("TopLeft"))      { r.corner = Corner::TopLeft;     return r; }
    if (viewerPos == QStringLiteral("TopRight"))     { r.corner = Corner::TopRight;    return r; }
    if (viewerPos == QStringLiteral("BottomLeft"))   { r.corner = Corner::BottomLeft;  return r; }
    if (viewerPos == QStringLiteral("BottomRight"))  { r.corner = Corner::BottomRight; return r; }
    if (viewerPos == QStringLiteral("TopCenter"))    { r.corner = Corner::TopRight;    return r; }
    if (viewerPos == QStringLiteral("BottomCenter")) { r.corner = Corner::BottomRight; return r; }
    // CenterLeft / CenterRight / Hidden: 회전 렌더·비활성 케이스 — 플레이스홀더 숨김.
    r.visible = false;
    return r;
}

QPoint cornerToPixel(Corner corner, const QSize& widgetSize,
                     const QSize& hostSize, int margin)
{
    if (widgetSize.width() <= 0 || widgetSize.height() <= 0
        || hostSize.width() <= 0 || hostSize.height() <= 0) {
        return QPoint(0, 0);
    }
    const int maxX = hostSize.width() - widgetSize.width() - margin;
    const int maxY = hostSize.height() - widgetSize.height() - margin;
    switch (corner) {
    case Corner::TopLeft:     return QPoint(margin, margin);
    case Corner::TopRight:    return QPoint(maxX, margin);
    case Corner::BottomLeft:  return QPoint(margin, maxY);
    case Corner::BottomRight: return QPoint(maxX, maxY);
    }
    return QPoint(margin, margin);
}

}  // namespace OverlayLayout
