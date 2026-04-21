#include "ui/ViewerCountStyle.h"
#include "core/PlatformTraits.h"

#include <QLocale>
#include <QStringList>

namespace ViewerCountStyle {

namespace {
QString formatCount(int count)
{
    return count >= 0 ? QLocale().toString(count) : QStringLiteral("—");
}

QString totalColorHex()
{
    const QColor c = QColor::fromRgb(kIconTotal);
    return c.name(QColor::HexRgb);
}

QString fgColorHex()
{
    const QColor c = QColor::fromRgb(kFg);
    return c.name(QColor::HexRgb);
}
} // namespace

QString buildViewerHtml(const QVector<QPair<PlatformId, int>>& entries)
{
    QStringList parts;
    parts.reserve(entries.size() + 1);

    bool hasAny = false;
    int total = 0;

    // v80: 숫자도 명시 <span> 으로 kFg (#FFFFFF) 색 지정. 이전엔 아이콘만 span 으로
    // 색 지정되고 숫자는 호출 측 palette 상속에 의존했는데, Qt 5.15 + 일부 KDE 테마에서
    // QLabel stylesheet `color: #ffffff` 가 RichText span 외부 텍스트에 cascade 안 되는
    // 렌더 이슈로 숫자가 테마 기본 text color (검정 등) 로 표시되는 경우 있음. 명시 래핑으로
    // 제거. 메인 앱·클라 양쪽 일관 적용.
    const QString fgHex = fgColorHex();

    for (const auto& e : entries) {
        const PlatformId platform = e.first;
        const int count = e.second;
        if (count >= 0) { total += count; hasAny = true; }

        // 아이콘: 플랫폼 색 · 숫자: 명시 흰색 (kFg)
        parts << QStringLiteral("<span style=\"color:%1\">%2</span>&nbsp;<span style=\"color:%3\">%4</span>")
                     .arg(PlatformTraits::viewerIconColor(platform),
                          PlatformTraits::badgeSymbol(platform),
                          fgHex,
                          formatCount(count));
    }

    // 합계(Σ): 플랫폼 무관 색 · 숫자는 동일하게 흰색 명시
    parts << QStringLiteral("<span style=\"color:%1\">Σ</span>&nbsp;<span style=\"color:%2\">%3</span>")
                 .arg(totalColorHex(),
                      fgHex,
                      hasAny ? QLocale().toString(total) : QStringLiteral("—"));

    // QTextDocument/QLabel RichText가 연속 공백을 2칸으로 보존하도록 &nbsp; 사용
    return parts.join(QStringLiteral("&nbsp;&nbsp;"));
}

} // namespace ViewerCountStyle
