#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include "core/AppTypes.h"

#include <QString>
#include <QStringList>

class AppSettings {
public:
    explicit AppSettings(QString iniPath);

    static QString resolveConfigDir(const QStringList& args);

    AppSettingsSnapshot load() const;
    bool save(const AppSettingsSnapshot& snapshot) const;

private:
    QString m_iniPath;
};

#endif // APP_SETTINGS_H
