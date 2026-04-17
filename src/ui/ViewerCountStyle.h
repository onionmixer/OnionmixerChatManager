#ifndef VIEWER_COUNT_STYLE_H
#define VIEWER_COUNT_STYLE_H

#include "core/AppTypes.h"

#include <QColor>
#include <QPair>
#include <QString>
#include <QVector>

namespace ViewerCountStyle {
    constexpr int kPaddingX = 8;
    constexpr int kPaddingY = 4;
    constexpr int kRadius = 4;
    constexpr QRgb kBg = qRgba(0, 0, 0, 160);
    constexpr QRgb kFg = qRgb(255, 255, 255);

    // 합계(Σ) 아이콘은 플랫폼 무관 — 별도 상수.
    constexpr QRgb kIconTotal = qRgb(0xC5, 0x86, 0xFF);  // 보라

    // viewer count 오버레이 HTML 빌더.
    //
    // entries: 표시할 플랫폼별 (id, count) 튜플. count < 0은 "\u2014"로 표시.
    //          순서는 호출 측이 지정(예: YouTube 먼저, CHZZK 다음).
    // 반환: <span style="color:#XXX">아이콘</span> 숫자 ... Σ 합계 형태의 rich
    //       text. 기본 텍스트 색(숫자·Σ가 아닌 아이콘 제외 문자)은 호출 측이
    //       QPalette::Text 또는 QLabel 스타일시트로 설정 (기본 kFg 흰색).
    QString buildViewerHtml(const QVector<QPair<PlatformId, int>>& entries);
}

#endif // VIEWER_COUNT_STYLE_H
