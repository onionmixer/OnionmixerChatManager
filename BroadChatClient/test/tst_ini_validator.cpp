// PLAN_DEV_BROADCHATCLIENT §5.6.8 v57-1 · v74 IniValidator 단위 테스트.
// 핵심 회귀 방지: "비어있지 않지만 invalid 한 값" (예: #ZZZZ · garbage · blue)
// 이 healing 을 통과해 ChatBubbleDelegate fallback #111111 로 불가시 렌더링되는
// 이슈 (투명 모드 + 어두운 desktop) 의 재발 방지.

#include "config/IniValidator.h"

#include <QString>
#include <QTest>

class IniValidatorTest : public QObject
{
    Q_OBJECT
private slots:
    // isValidColor
    void color_valid_hex_rgb();
    void color_valid_hex_argb();
    void color_valid_named();
    void color_invalid_empty();
    void color_invalid_garbage();
    void color_invalid_short_hash();

    // isValidViewerPosition
    void viewer_pos_valid_all_nine();
    void viewer_pos_invalid_unknown();
    void viewer_pos_invalid_empty();
    void viewer_pos_invalid_wrong_case();

    // isValidPort
    void port_valid_default();
    void port_valid_high();
    void port_invalid_reserved();

    // healSnapshot — color 필드 4종 invalid 시 heal
    void heal_body_color_garbage();
    void heal_outline_color_empty();
    void heal_transparent_bg_empty();
    void heal_opaque_bg_garbage();
    void heal_all_valid_no_change();

    // healSnapshot — viewer_count_position
    void heal_viewer_pos_invalid();
    void heal_viewer_pos_valid();

    // healSnapshot — 숫자 range
    void heal_font_size_out_of_range();
    void heal_line_spacing_clamped();
    void heal_max_messages_clamped();
    void heal_window_size_clamped();

    // healConnection
    void heal_conn_empty_host();
    void heal_conn_reserved_port();
    void heal_conn_token_trim();
    void heal_conn_all_valid();
};

void IniValidatorTest::color_valid_hex_rgb()
{
    QVERIFY(IniValidator::isValidColor(QStringLiteral("#FFFFFF")));
    QVERIFY(IniValidator::isValidColor(QStringLiteral("#000000")));
}
void IniValidatorTest::color_valid_hex_argb()
{
    QVERIFY(IniValidator::isValidColor(QStringLiteral("#FFFFFFFF")));
    QVERIFY(IniValidator::isValidColor(QStringLiteral("#00000000")));
    QVERIFY(IniValidator::isValidColor(QStringLiteral("#80112233")));
}
void IniValidatorTest::color_valid_named()
{
    QVERIFY(IniValidator::isValidColor(QStringLiteral("red")));
    QVERIFY(IniValidator::isValidColor(QStringLiteral("transparent")));
}
void IniValidatorTest::color_invalid_empty()
{
    QVERIFY(!IniValidator::isValidColor(QString()));
    QVERIFY(!IniValidator::isValidColor(QStringLiteral("")));
}
void IniValidatorTest::color_invalid_garbage()
{
    QVERIFY(!IniValidator::isValidColor(QStringLiteral("#ZZZZZZZZ")));
    QVERIFY(!IniValidator::isValidColor(QStringLiteral("not-a-color")));
    QVERIFY(!IniValidator::isValidColor(QStringLiteral("#")));
}
void IniValidatorTest::color_invalid_short_hash()
{
    // Qt 는 #RGB 3자리 hex 도 허용 — 이건 valid
    QVERIFY(IniValidator::isValidColor(QStringLiteral("#FFF")));
    // 하지만 5자리 hash 는 invalid
    QVERIFY(!IniValidator::isValidColor(QStringLiteral("#FFFFF")));
}

void IniValidatorTest::viewer_pos_valid_all_nine()
{
    const QStringList all = {"TopLeft","TopCenter","TopRight",
                             "CenterLeft","CenterRight",
                             "BottomLeft","BottomCenter","BottomRight","Hidden"};
    for (const QString& p : all) {
        QVERIFY2(IniValidator::isValidViewerPosition(p), qPrintable(p));
    }
}
void IniValidatorTest::viewer_pos_invalid_unknown()
{
    QVERIFY(!IniValidator::isValidViewerPosition(QStringLiteral("Middle")));
    QVERIFY(!IniValidator::isValidViewerPosition(QStringLiteral("Center")));
}
void IniValidatorTest::viewer_pos_invalid_empty()
{
    QVERIFY(!IniValidator::isValidViewerPosition(QString()));
}
void IniValidatorTest::viewer_pos_invalid_wrong_case()
{
    QVERIFY(!IniValidator::isValidViewerPosition(QStringLiteral("topleft")));
    QVERIFY(!IniValidator::isValidViewerPosition(QStringLiteral("TOPLEFT")));
}

void IniValidatorTest::port_valid_default()
{
    QVERIFY(IniValidator::isValidPort(47123));
}
void IniValidatorTest::port_valid_high()
{
    QVERIFY(IniValidator::isValidPort(65535));
    QVERIFY(IniValidator::isValidPort(1024));
}
void IniValidatorTest::port_invalid_reserved()
{
    QVERIFY(!IniValidator::isValidPort(0));
    QVERIFY(!IniValidator::isValidPort(80));
    QVERIFY(!IniValidator::isValidPort(1023));
}

void IniValidatorTest::heal_body_color_garbage()
{
    AppSettingsSnapshot s;
    s.broadcastChatBodyFontColor = QStringLiteral("#ZZZZ");   // invalid 한 non-empty
    s.broadcastChatOutlineColor = QStringLiteral("#FF000000");
    s.broadcastTransparentBgColor = QStringLiteral("#00000000");
    s.broadcastOpaqueBgColor = QStringLiteral("#FFFFFFFF");
    s.broadcastViewerCountPosition = QStringLiteral("TopLeft");
    const bool heal = IniValidator::healSnapshot(s);
    QVERIFY(heal);
    QCOMPARE(s.broadcastChatBodyFontColor, QStringLiteral("#FFFFFFFF"));
}
void IniValidatorTest::heal_outline_color_empty()
{
    AppSettingsSnapshot s;
    s.broadcastChatBodyFontColor = QStringLiteral("#FFFFFFFF");
    s.broadcastChatOutlineColor = QString();  // empty
    s.broadcastTransparentBgColor = QStringLiteral("#00000000");
    s.broadcastOpaqueBgColor = QStringLiteral("#FFFFFFFF");
    s.broadcastViewerCountPosition = QStringLiteral("TopLeft");
    QVERIFY(IniValidator::healSnapshot(s));
    QCOMPARE(s.broadcastChatOutlineColor, QStringLiteral("#FF000000"));
}
void IniValidatorTest::heal_transparent_bg_empty()
{
    AppSettingsSnapshot s;
    s.broadcastChatBodyFontColor = QStringLiteral("#FFFFFFFF");
    s.broadcastChatOutlineColor = QStringLiteral("#FF000000");
    s.broadcastTransparentBgColor = QString();
    s.broadcastOpaqueBgColor = QStringLiteral("#FFFFFFFF");
    s.broadcastViewerCountPosition = QStringLiteral("TopLeft");
    QVERIFY(IniValidator::healSnapshot(s));
    QCOMPARE(s.broadcastTransparentBgColor, QStringLiteral("#00000000"));
}
void IniValidatorTest::heal_opaque_bg_garbage()
{
    AppSettingsSnapshot s;
    s.broadcastChatBodyFontColor = QStringLiteral("#FFFFFFFF");
    s.broadcastChatOutlineColor = QStringLiteral("#FF000000");
    s.broadcastTransparentBgColor = QStringLiteral("#00000000");
    s.broadcastOpaqueBgColor = QStringLiteral("opaque");  // invalid named color
    s.broadcastViewerCountPosition = QStringLiteral("TopLeft");
    QVERIFY(IniValidator::healSnapshot(s));
    QCOMPARE(s.broadcastOpaqueBgColor, QStringLiteral("#FFFFFFFF"));
}
void IniValidatorTest::heal_all_valid_no_change()
{
    AppSettingsSnapshot s;
    s.broadcastChatBodyFontColor = QStringLiteral("#FFFFFFFF");
    s.broadcastChatOutlineColor = QStringLiteral("#FF000000");
    s.broadcastTransparentBgColor = QStringLiteral("#00000000");
    s.broadcastOpaqueBgColor = QStringLiteral("#FFFFFFFF");
    s.broadcastViewerCountPosition = QStringLiteral("CenterRight");
    // Default ranges OK
    QVERIFY(!IniValidator::healSnapshot(s));
    // 값 보존 확인
    QCOMPARE(s.broadcastViewerCountPosition, QStringLiteral("CenterRight"));
}

void IniValidatorTest::heal_viewer_pos_invalid()
{
    AppSettingsSnapshot s;
    s.broadcastChatBodyFontColor = QStringLiteral("#FFFFFFFF");
    s.broadcastChatOutlineColor = QStringLiteral("#FF000000");
    s.broadcastTransparentBgColor = QStringLiteral("#00000000");
    s.broadcastOpaqueBgColor = QStringLiteral("#FFFFFFFF");
    s.broadcastViewerCountPosition = QStringLiteral("Middle");  // invalid
    QVERIFY(IniValidator::healSnapshot(s));
    QCOMPARE(s.broadcastViewerCountPosition, QStringLiteral("TopLeft"));
}
void IniValidatorTest::heal_viewer_pos_valid()
{
    AppSettingsSnapshot s;
    s.broadcastChatBodyFontColor = QStringLiteral("#FFFFFFFF");
    s.broadcastChatOutlineColor = QStringLiteral("#FF000000");
    s.broadcastTransparentBgColor = QStringLiteral("#00000000");
    s.broadcastOpaqueBgColor = QStringLiteral("#FFFFFFFF");
    s.broadcastViewerCountPosition = QStringLiteral("Hidden");
    QVERIFY(!IniValidator::healSnapshot(s));
    QCOMPARE(s.broadcastViewerCountPosition, QStringLiteral("Hidden"));
}

void IniValidatorTest::heal_font_size_out_of_range()
{
    AppSettingsSnapshot s;
    s.broadcastChatBodyFontColor = QStringLiteral("#FFFFFFFF");
    s.broadcastChatOutlineColor = QStringLiteral("#FF000000");
    s.broadcastTransparentBgColor = QStringLiteral("#00000000");
    s.broadcastOpaqueBgColor = QStringLiteral("#FFFFFFFF");
    s.broadcastViewerCountPosition = QStringLiteral("TopLeft");
    s.chatFontSize = 999;  // out of range
    QVERIFY(IniValidator::healSnapshot(s));
    QCOMPARE(s.chatFontSize, 11);

    s.chatFontSize = 2;  // below 6
    QVERIFY(IniValidator::healSnapshot(s));
    QCOMPARE(s.chatFontSize, 11);
}
void IniValidatorTest::heal_line_spacing_clamped()
{
    AppSettingsSnapshot s;
    s.broadcastChatBodyFontColor = QStringLiteral("#FFFFFFFF");
    s.broadcastChatOutlineColor = QStringLiteral("#FF000000");
    s.broadcastTransparentBgColor = QStringLiteral("#00000000");
    s.broadcastOpaqueBgColor = QStringLiteral("#FFFFFFFF");
    s.broadcastViewerCountPosition = QStringLiteral("TopLeft");
    s.chatLineSpacing = -5;  // below 0
    QVERIFY(IniValidator::healSnapshot(s));
    QCOMPARE(s.chatLineSpacing, 3);
}
void IniValidatorTest::heal_max_messages_clamped()
{
    AppSettingsSnapshot s;
    s.broadcastChatBodyFontColor = QStringLiteral("#FFFFFFFF");
    s.broadcastChatOutlineColor = QStringLiteral("#FF000000");
    s.broadcastTransparentBgColor = QStringLiteral("#00000000");
    s.broadcastOpaqueBgColor = QStringLiteral("#FFFFFFFF");
    s.broadcastViewerCountPosition = QStringLiteral("TopLeft");
    s.chatMaxMessages = 10;  // below 100
    QVERIFY(IniValidator::healSnapshot(s));
    QCOMPARE(s.chatMaxMessages, 5000);
}
void IniValidatorTest::heal_window_size_clamped()
{
    AppSettingsSnapshot s;
    s.broadcastChatBodyFontColor = QStringLiteral("#FFFFFFFF");
    s.broadcastChatOutlineColor = QStringLiteral("#FF000000");
    s.broadcastTransparentBgColor = QStringLiteral("#00000000");
    s.broadcastOpaqueBgColor = QStringLiteral("#FFFFFFFF");
    s.broadcastViewerCountPosition = QStringLiteral("TopLeft");
    s.broadcastWindowWidth = 99999;   // too wide
    s.broadcastWindowHeight = 50;     // too short
    QVERIFY(IniValidator::healSnapshot(s));
    QCOMPARE(s.broadcastWindowWidth, 400);
    QCOMPARE(s.broadcastWindowHeight, 600);
}

void IniValidatorTest::heal_conn_empty_host()
{
    IniValidator::ConnectionFields c{QString(), 47123, QString()};
    QVERIFY(IniValidator::healConnection(c));
    QVERIFY(!c.host.isEmpty());  // loopback 으로 설정됨
}
void IniValidatorTest::heal_conn_reserved_port()
{
    IniValidator::ConnectionFields c{QStringLiteral("127.0.0.1"), 80, QString()};
    QVERIFY(IniValidator::healConnection(c));
    QVERIFY(IniValidator::isValidPort(c.port));
}
void IniValidatorTest::heal_conn_token_trim()
{
    IniValidator::ConnectionFields c{QStringLiteral("127.0.0.1"), 47123,
                                      QStringLiteral("  abc  ")};
    QVERIFY(IniValidator::healConnection(c));
    QCOMPARE(c.authToken, QStringLiteral("abc"));
}
void IniValidatorTest::heal_conn_all_valid()
{
    IniValidator::ConnectionFields c{QStringLiteral("192.168.0.1"), 47123,
                                      QStringLiteral("token")};
    QVERIFY(!IniValidator::healConnection(c));
}

QTEST_GUILESS_MAIN(IniValidatorTest)
#include "tst_ini_validator.moc"
