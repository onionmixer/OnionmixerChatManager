#include "ui/MainWindow.h"

#include "core/AppTypes.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    qRegisterMetaType<PlatformId>("PlatformId");
    qRegisterMetaType<ConnectionState>("ConnectionState");
    qRegisterMetaType<AppSettingsSnapshot>("AppSettingsSnapshot");
    qRegisterMetaType<PlatformSettings>("PlatformSettings");
    qRegisterMetaType<ConnectSessionResult>("ConnectSessionResult");
    qRegisterMetaType<TokenState>("TokenState");
    qRegisterMetaType<UnifiedChatMessage>("UnifiedChatMessage");
    qRegisterMetaType<TokenRecord>("TokenRecord");

    MainWindow w;
    w.show();
    return app.exec();
}
