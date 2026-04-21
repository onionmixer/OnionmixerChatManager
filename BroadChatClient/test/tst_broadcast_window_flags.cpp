#include "core/EmojiImageCache.h"
#include "ui/BroadcastChatWindow.h"
#include "ui/ChatMessageModel.h"

#include <QNetworkAccessManager>
#include <QTest>

class BroadcastWindowFlagsTest : public QObject
{
    Q_OBJECT
private slots:
    void default_state_is_always_on_top();
    void hidden_always_on_top_toggle_does_not_show_window();
    void transparent_mode_preserves_always_on_top_off();
    void opaque_mode_preserves_always_on_top_off();
    void mode_toggles_preserve_always_on_top_on();
};

namespace {
struct WindowFixture {
    QNetworkAccessManager network;
    EmojiImageCache cache{&network};
    ChatMessageModel model;
    BroadcastChatWindow window{&model, &cache};
};

bool hasAlwaysOnTop(const QWidget& w)
{
    return (w.windowFlags() & Qt::WindowStaysOnTopHint)
        == Qt::WindowStaysOnTopHint;
}
} // namespace

void BroadcastWindowFlagsTest::default_state_is_always_on_top()
{
    WindowFixture f;
    QVERIFY(f.window.isTransparentMode());
    QVERIFY(f.window.isAlwaysOnTop());
    QVERIFY(hasAlwaysOnTop(f.window));
}

void BroadcastWindowFlagsTest::hidden_always_on_top_toggle_does_not_show_window()
{
    WindowFixture f;
    QVERIFY(!f.window.isVisible());

    f.window.setAlwaysOnTop(false);

    QVERIFY(!f.window.isVisible());
    QVERIFY(!f.window.isAlwaysOnTop());
    QVERIFY(!hasAlwaysOnTop(f.window));

    f.window.setAlwaysOnTop(true);

    QVERIFY(!f.window.isVisible());
    QVERIFY(f.window.isAlwaysOnTop());
    QVERIFY(hasAlwaysOnTop(f.window));
}

void BroadcastWindowFlagsTest::transparent_mode_preserves_always_on_top_off()
{
    WindowFixture f;
    f.window.setAlwaysOnTop(false);

    f.window.setTransparentMode(true);

    QVERIFY(f.window.isVisible());
    QVERIFY(f.window.isTransparentMode());
    QVERIFY(!f.window.isAlwaysOnTop());
    QVERIFY(!hasAlwaysOnTop(f.window));
}

void BroadcastWindowFlagsTest::opaque_mode_preserves_always_on_top_off()
{
    WindowFixture f;
    f.window.setAlwaysOnTop(false);

    f.window.setTransparentMode(false);

    QVERIFY(f.window.isVisible());
    QVERIFY(!f.window.isTransparentMode());
    QVERIFY(!f.window.isAlwaysOnTop());
    QVERIFY(!hasAlwaysOnTop(f.window));
}

void BroadcastWindowFlagsTest::mode_toggles_preserve_always_on_top_on()
{
    WindowFixture f;
    QVERIFY(f.window.isAlwaysOnTop());

    f.window.setTransparentMode(false);
    QVERIFY(!f.window.isTransparentMode());
    QVERIFY(f.window.isAlwaysOnTop());
    QVERIFY(hasAlwaysOnTop(f.window));

    f.window.setTransparentMode(true);
    QVERIFY(f.window.isTransparentMode());
    QVERIFY(f.window.isAlwaysOnTop());
    QVERIFY(hasAlwaysOnTop(f.window));
}

QTEST_MAIN(BroadcastWindowFlagsTest)
#include "tst_broadcast_window_flags.moc"
