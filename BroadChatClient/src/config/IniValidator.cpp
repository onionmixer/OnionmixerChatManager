#include "IniValidator.h"

#include "shared/BroadChatEndpoint.h"

#include <QColor>
#include <QLatin1String>
#include <QSet>

namespace IniValidator {

namespace {

// Plan §5.6.8 필드별 fallback 기본값.
// 색상 4종은 반드시 valid 여야 한다 — body 는 #111111 fallback 이 어두운 바탕에서 불가시.
constexpr auto kDefaultBodyColor     = "#FFFFFFFF";  // 흰색 (Alpha 255)
constexpr auto kDefaultOutlineColor  = "#FF000000";  // 검정
constexpr auto kDefaultTransparentBg = "#00000000";  // 완전 투명
constexpr auto kDefaultOpaqueBg      = "#FFFFFFFF";  // 흰색
constexpr auto kDefaultViewerPos     = "TopLeft";

// 숫자 범위 defaults (AppSettingsSnapshot 기본값과 일치)
constexpr int kDefaultFontSize = 11;
constexpr int kDefaultLineSpacing = 3;
constexpr int kDefaultMaxMessages = 5000;
constexpr int kDefaultWindowW = 400;
constexpr int kDefaultWindowH = 600;

bool healColor(QString& field, const char* defaultValue)
{
    if (!isValidColor(field)) {
        field = QString::fromLatin1(defaultValue);
        return true;
    }
    return false;
}

bool clampInt(int& v, int lo, int hi, int def)
{
    if (v < lo || v > hi) {
        v = def;
        return true;
    }
    return false;
}

}  // namespace

bool isValidColor(const QString& s)
{
    if (s.isEmpty()) return false;
    const QColor c(s);
    return c.isValid();
}

bool isValidViewerPosition(const QString& s)
{
    static const QSet<QString> kValid = {
        QStringLiteral("TopLeft"),     QStringLiteral("TopCenter"),
        QStringLiteral("TopRight"),    QStringLiteral("CenterLeft"),
        QStringLiteral("CenterRight"), QStringLiteral("BottomLeft"),
        QStringLiteral("BottomCenter"), QStringLiteral("BottomRight"),
        QStringLiteral("Hidden")};
    return kValid.contains(s);
}

bool isValidPort(quint16 p)
{
    return p >= 1024;  // quint16 최대는 65535 — 타입 자체가 상한 보장
}

bool healSnapshot(AppSettingsSnapshot& s)
{
    bool heal = false;

    // 색상 4종 — QColor::isValid 검사. isEmpty 뿐 아니라 "garbage" 같은 문자열도 교체.
    heal |= healColor(s.broadcastChatBodyFontColor, kDefaultBodyColor);
    heal |= healColor(s.broadcastChatOutlineColor, kDefaultOutlineColor);
    heal |= healColor(s.broadcastTransparentBgColor, kDefaultTransparentBg);
    heal |= healColor(s.broadcastOpaqueBgColor, kDefaultOpaqueBg);

    // viewer_count_position — 9종 enum 검사.
    if (!isValidViewerPosition(s.broadcastViewerCountPosition)) {
        s.broadcastViewerCountPosition = QString::fromLatin1(kDefaultViewerPos);
        heal = true;
    }

    // 숫자 범위 — AppSettingsSnapshot 의 타입은 int 이지만 ini 에서 음수·거대값 올 수 있음.
    heal |= clampInt(s.chatFontSize, 6, 48, kDefaultFontSize);
    heal |= clampInt(s.chatLineSpacing, 0, 20, kDefaultLineSpacing);
    heal |= clampInt(s.chatMaxMessages, 100, 100000, kDefaultMaxMessages);
    heal |= clampInt(s.broadcastWindowWidth, 100, 4000, kDefaultWindowW);
    heal |= clampInt(s.broadcastWindowHeight, 100, 4000, kDefaultWindowH);

    // windowX/Y 는 화면 밖 위치도 Qt 가 clamp → heal 대상 아님 (v48-5 참조).
    // font_family 는 빈 값 허용 (시스템 기본) — validation 없음.
    // font_bold/italic 은 bool — ini 에서 "true"/"false" 외 값도 toBool() 이 처리.

    return heal;
}

bool healConnection(ConnectionFields& c)
{
    bool heal = false;

    // host — 빈 문자열이면 loopback 기본값. 비어있지 않은 값은 DNS 혹은 IP 문자열이라
    // 가정 (실제 해석은 QTcpSocket::connectToHost 가 수행). 공백만 있으면 빈 것으로.
    c.host = c.host.trimmed();
    if (c.host.isEmpty()) {
        c.host = QString::fromLatin1(BroadChatEndpoint::kDefaultBindAddress);
        heal = true;
    }

    // port — 0 또는 1024 미만 (well-known reserved) 는 기본값으로.
    if (!isValidPort(c.port) || c.port == 0) {
        c.port = BroadChatEndpoint::kDefaultTcpPort;
        heal = true;
    }

    // auth_token — trim 만 수행. 빈 값은 "auth off" 의미로 허용.
    const QString trimmed = c.authToken.trimmed();
    if (trimmed != c.authToken) {
        c.authToken = trimmed;
        heal = true;
    }

    return heal;
}

}  // namespace IniValidator
