#include "ui/MainWindow.h"

#include "core/AppTypes.h"
#include "config/AppSettings.h"
#include "i18n/AppLanguage.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
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
