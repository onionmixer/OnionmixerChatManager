#include "ui/MainWindow.h"

#include "core/AppTypes.h"
#include "config/AppSettings.h"
#include "i18n/AppLanguage.h"
#include "shared/BroadChatVersion.h"

#include <QApplication>
#include <QtGlobal>

#include <cstdio>
#include <cstring>

int main(int argc, char* argv[])
{
    // v25 C5: --version·--help 조기 처리. QApplication 생성 전에 argv 파싱 → exit.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0) {
            std::fprintf(stdout, "OnionmixerChatManagerQt5 %s (%s) proto=1 built=%s\n",
                         BroadChatVersion::kAppVersion,
                         BroadChatVersion::kGitCommitShort,
                         BroadChatVersion::kBuildDate);
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::fprintf(stdout,
                "OnionmixerChatManagerQt5 — usage:\n"
                "  --version, -v        Print version and exit\n"
                "  --help, -h           Show this help and exit\n"
                "  --config-dir <path>  Override config directory (default: ./config)\n"
                "See docs/CHANGELOG.md for release history.\n");
            return 0;
        }
    }

    QApplication app(argc, argv);

    // PLAN §18.3 v15-14: category 기반 로그 prefix 통일.
    // "[broadchat] ...", "[broadchat.warn] ..." 같이 대괄호 포맷 자동 출력.
    qSetMessagePattern(QStringLiteral("%{if-category}[%{category}] %{endif}%{message}"));

    const QString configDir = AppSettings::resolveConfigDir(app.arguments());
    AppSettings settings(configDir + QStringLiteral("/app.ini"));
    const AppSettingsSnapshot snapshot = settings.load();
    AppLanguage::applyLanguage(app, snapshot.language);

    qRegisterMetaType<PlatformId>("PlatformId");
    qRegisterMetaType<ConnectionState>("ConnectionState");
    qRegisterMetaType<AppSettingsSnapshot>("AppSettingsSnapshot");
    qRegisterMetaType<PlatformSettings>("PlatformSettings");
    qRegisterMetaType<ConnectSessionResult>("ConnectSessionResult");
    qRegisterMetaType<TokenState>("TokenState");
    qRegisterMetaType<UnifiedChatMessage>("UnifiedChatMessage");
    qRegisterMetaType<QVector<UnifiedChatMessage>>("QVector<UnifiedChatMessage>");
    qRegisterMetaType<TokenRecord>("TokenRecord");
    qRegisterMetaType<ChatEmojiInfo>("ChatEmojiInfo");
    qRegisterMetaType<QVector<ChatEmojiInfo>>("QVector<ChatEmojiInfo>");

    MainWindow w(configDir);
    w.show();
    return app.exec();
}
