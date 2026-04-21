// PLAN_DEV_BROADCHATCLIENT §8.3 v34-13 · §0.7 #14 단위 테스트 (v73).
// OverlayLayout namespace 순수 함수 — 9 viewer position × badge/placeholder 매핑
// + corner→pixel 계산 검증. 실제 QWidget 없이 격리 테스트 가능.

#include "ui/OverlayLayout.h"

#include <QPoint>
#include <QSize>
#include <QString>
#include <QTest>

class OverlayLayoutTest : public QObject
{
    Q_OBJECT
private slots:
    // Badge (#11): viewer_count_position 반대 모서리 9 케이스
    void badge_topleft_maps_to_topright();
    void badge_topright_maps_to_topleft();
    void badge_bottomleft_maps_to_bottomright();
    void badge_bottomright_maps_to_bottomleft();
    void badge_topcenter_maps_to_topright();
    void badge_bottomcenter_maps_to_bottomright();
    void badge_centerleft_maps_to_bottomright();
    void badge_centerright_maps_to_bottomright();
    void badge_hidden_maps_to_topleft();
    void badge_unknown_maps_to_topleft();
    void badge_empty_maps_to_topleft();

    // Placeholder (#12): viewer 위치 근접 + Center/Hidden 은 숨김
    void placeholder_corners_match_viewer();
    void placeholder_top_bottom_center_approximate();
    void placeholder_center_sides_hidden();
    void placeholder_hidden_hidden();

    // cornerToPixel 계산
    void pixel_topleft();
    void pixel_topright();
    void pixel_bottomleft();
    void pixel_bottomright();
    void pixel_defensive_invalid_size();
};

using Corner = OverlayLayout::Corner;

void OverlayLayoutTest::badge_topleft_maps_to_topright()
{
    QCOMPARE(OverlayLayout::badgeCornerFor(QStringLiteral("TopLeft")),
             Corner::TopRight);
}
void OverlayLayoutTest::badge_topright_maps_to_topleft()
{
    QCOMPARE(OverlayLayout::badgeCornerFor(QStringLiteral("TopRight")),
             Corner::TopLeft);
}
void OverlayLayoutTest::badge_bottomleft_maps_to_bottomright()
{
    QCOMPARE(OverlayLayout::badgeCornerFor(QStringLiteral("BottomLeft")),
             Corner::BottomRight);
}
void OverlayLayoutTest::badge_bottomright_maps_to_bottomleft()
{
    QCOMPARE(OverlayLayout::badgeCornerFor(QStringLiteral("BottomRight")),
             Corner::BottomLeft);
}
void OverlayLayoutTest::badge_topcenter_maps_to_topright()
{
    QCOMPARE(OverlayLayout::badgeCornerFor(QStringLiteral("TopCenter")),
             Corner::TopRight);
}
void OverlayLayoutTest::badge_bottomcenter_maps_to_bottomright()
{
    QCOMPARE(OverlayLayout::badgeCornerFor(QStringLiteral("BottomCenter")),
             Corner::BottomRight);
}
void OverlayLayoutTest::badge_centerleft_maps_to_bottomright()
{
    QCOMPARE(OverlayLayout::badgeCornerFor(QStringLiteral("CenterLeft")),
             Corner::BottomRight);
}
void OverlayLayoutTest::badge_centerright_maps_to_bottomright()
{
    QCOMPARE(OverlayLayout::badgeCornerFor(QStringLiteral("CenterRight")),
             Corner::BottomRight);
}
void OverlayLayoutTest::badge_hidden_maps_to_topleft()
{
    QCOMPARE(OverlayLayout::badgeCornerFor(QStringLiteral("Hidden")),
             Corner::TopLeft);
}
void OverlayLayoutTest::badge_unknown_maps_to_topleft()
{
    // 방어: 스펙에 없는 값 → TopLeft 기본
    QCOMPARE(OverlayLayout::badgeCornerFor(QStringLiteral("NotARealPosition")),
             Corner::TopLeft);
}
void OverlayLayoutTest::badge_empty_maps_to_topleft()
{
    QCOMPARE(OverlayLayout::badgeCornerFor(QString()), Corner::TopLeft);
}

void OverlayLayoutTest::placeholder_corners_match_viewer()
{
    // 4 corner 는 viewer 와 동일 위치 (placeholder 가 그 자리에 표시)
    const QStringList corners = {"TopLeft", "TopRight", "BottomLeft", "BottomRight"};
    const Corner expected[] = {Corner::TopLeft, Corner::TopRight,
                               Corner::BottomLeft, Corner::BottomRight};
    for (int i = 0; i < corners.size(); ++i) {
        const auto p = OverlayLayout::placeholderPlacementFor(corners[i]);
        QVERIFY(p.visible);
        QCOMPARE(p.corner, expected[i]);
    }
}

void OverlayLayoutTest::placeholder_top_bottom_center_approximate()
{
    // Top/BottomCenter → 상단/하단 우측 근사 (corner 위젯이므로 중앙 고정 불가)
    auto pTop = OverlayLayout::placeholderPlacementFor(QStringLiteral("TopCenter"));
    QVERIFY(pTop.visible);
    QCOMPARE(pTop.corner, Corner::TopRight);

    auto pBot = OverlayLayout::placeholderPlacementFor(QStringLiteral("BottomCenter"));
    QVERIFY(pBot.visible);
    QCOMPARE(pBot.corner, Corner::BottomRight);
}

void OverlayLayoutTest::placeholder_center_sides_hidden()
{
    // Center(Left|Right) 는 공유 lib 에서 회전 렌더링 — 단순 라벨로 재현 불가 → 숨김.
    QVERIFY(!OverlayLayout::placeholderPlacementFor(QStringLiteral("CenterLeft")).visible);
    QVERIFY(!OverlayLayout::placeholderPlacementFor(QStringLiteral("CenterRight")).visible);
}

void OverlayLayoutTest::placeholder_hidden_hidden()
{
    // 사용자가 viewer 표시를 끈 경우 플레이스홀더도 표시 안 함.
    QVERIFY(!OverlayLayout::placeholderPlacementFor(QStringLiteral("Hidden")).visible);
}

void OverlayLayoutTest::pixel_topleft()
{
    const QPoint p = OverlayLayout::cornerToPixel(
        Corner::TopLeft, QSize(12, 12), QSize(400, 300), 6);
    QCOMPARE(p, QPoint(6, 6));
}

void OverlayLayoutTest::pixel_topright()
{
    const QPoint p = OverlayLayout::cornerToPixel(
        Corner::TopRight, QSize(12, 12), QSize(400, 300), 6);
    // maxX = 400 - 12 - 6 = 382
    QCOMPARE(p, QPoint(382, 6));
}

void OverlayLayoutTest::pixel_bottomleft()
{
    const QPoint p = OverlayLayout::cornerToPixel(
        Corner::BottomLeft, QSize(12, 12), QSize(400, 300), 6);
    // maxY = 300 - 12 - 6 = 282
    QCOMPARE(p, QPoint(6, 282));
}

void OverlayLayoutTest::pixel_bottomright()
{
    const QPoint p = OverlayLayout::cornerToPixel(
        Corner::BottomRight, QSize(12, 12), QSize(400, 300), 6);
    QCOMPARE(p, QPoint(382, 282));
}

void OverlayLayoutTest::pixel_defensive_invalid_size()
{
    // 비정상 입력 방어
    QCOMPARE(OverlayLayout::cornerToPixel(
                 Corner::TopRight, QSize(-1, -1), QSize(400, 300), 6),
             QPoint(0, 0));
    QCOMPARE(OverlayLayout::cornerToPixel(
                 Corner::TopRight, QSize(12, 12), QSize(0, 0), 6),
             QPoint(0, 0));
}

QTEST_GUILESS_MAIN(OverlayLayoutTest)
#include "tst_overlay_layout.moc"
