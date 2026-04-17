#include "ui/ViewerCountStyle.h"
#include "core/PlatformTraits.h"

#include <QLocale>
#include <QStringList>

namespace ViewerCountStyle {

namespace {
QString formatCount(int count)
{
    return count >= 0 ? QLocale().toString(count) : QStringLiteral("\u2014");
}

QString totalColorHex()
{
    const QColor c = QColor::fromRgb(kIconTotal);
    return c.name(QColor::HexRgb);
}
} // namespace

QString buildViewerHtml(const QVector<QPair<PlatformId, int>>& entries)
{
    QStringList parts;
    parts.reserve(entries.size() + 1);

    bool hasAny = false;
    int total = 0;

    for (const auto& e : entries) {
        const PlatformId platform = e.first;
        const int count = e.second;
        if (count >= 0) { total += count; hasAny = true; }

        // 아이콘은 플랫폼 색, 숫자는 기본 텍스트 색(호출 측 팔레트에서 결정)
        parts << QStringLiteral("<span style=\"color:%1\">%2</span> %3")
                     .arg(PlatformTraits::viewerIconColor(platform),
                          PlatformTraits::badgeSymbol(platform),
                          formatCount(count));
    }

    // 합계(Σ): 플랫폼 무관 색
    parts << QStringLiteral("<span style=\"color:%1\">\u03A3</span> %2")
                 .arg(totalColorHex(),
                      hasAny ? QLocale().toString(total) : QStringLiteral("\u2014"));

    // QTextDocument/QLabel RichText가 연속 공백을 2칸으로 보존하도록 &nbsp; 사용
    return parts.join(QStringLiteral("&nbsp;&nbsp;"));
}

} // namespace ViewerCountStyle
