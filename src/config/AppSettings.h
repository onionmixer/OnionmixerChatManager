#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include "core/AppTypes.h"

#include <QString>

class AppSettings {
public:
    explicit AppSettings(QString iniPath = QStringLiteral("config/app.ini"));

    AppSettingsSnapshot load() const;
    bool save(const AppSettingsSnapshot& snapshot) const;

private:
    QString m_iniPath;
};

#endif // APP_SETTINGS_H
