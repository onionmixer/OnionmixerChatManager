#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

// PLAN_DEV_BROADCHATCLIENT §0.7 "클라 단독" 권장안 (v73).
// BroadChatClient 의 overlay 위젯(#11 배지·#12 viewer placeholder·#8 in-memory 배지)
// 배치를 `viewer_count_position` 에 따라 동적 결정. 순수 함수만 노출 — Qt widget deps
// 없이 단위 테스트 가능 (§0.7 F 의 헬퍼 추출 방침).
//
// 공유 lib `BroadcastChatWindow` 는 수정하지 않음 — client 가 `m_snapshot.broadcastViewerCountPosition`
// 에서 가져온 값을 `MainBroadcastWindow::setViewerCountPosition()` 로 주입.
namespace OverlayLayout {

// 4방향 corner enum. 배지·플레이스홀더 배치 결과.
enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };

// 연결 상태 배지(#11) 가 위치할 corner — `viewer_count_position` 반대 모서리.
// §8.3 v34-13 매핑 표:
//   TopLeft      → TopRight       BottomLeft    → BottomRight
//   TopRight     → TopLeft        BottomRight   → BottomLeft
//   TopCenter    → TopRight       BottomCenter  → BottomRight
//   CenterLeft   → BottomRight    CenterRight   → BottomRight
//   Hidden/기타  → TopLeft        (viewer 미표시 · 충돌 없음)
Corner badgeCornerFor(const QString& viewerPos);

// viewer count "—" 플레이스홀더(#12) 가 위치할 corner — viewer 가 표시됐을 위치 근접.
// Center* / Hidden 은 QPoint{-1,-1} 로 반환(= 플레이스홀더 숨김 신호 — 회전 렌더링·비활성 케이스).
// 나머지 corner 는 viewer 와 동일:
//   TopLeft/TopRight/BottomLeft/BottomRight → 동일
//   TopCenter    → TopRight (근사)   BottomCenter → BottomRight (근사)
//   CenterLeft/CenterRight/Hidden  → 숨김 (QPoint{-1,-1} sentinel)
//
// 반환 pair: { Corner, bool visible }
struct PlaceholderPlacement {
    Corner corner = Corner::TopRight;
    bool visible = true;
};
PlaceholderPlacement placeholderPlacementFor(const QString& viewerPos);

// Corner + 위젯 크기 + 부모(호스트) 크기 + margin 으로 최종 좌상단 pixel 좌표 계산.
// 음수 size 등 비정상 입력은 {0,0} 반환 (방어).
QPoint cornerToPixel(Corner corner, const QSize& widgetSize,
                     const QSize& hostSize, int margin);

}  // namespace OverlayLayout
