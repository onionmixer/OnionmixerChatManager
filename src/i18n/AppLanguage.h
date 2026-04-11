#ifndef APP_LANGUAGE_H
#define APP_LANGUAGE_H

#include <QString>

class QCoreApplication;

namespace AppLanguage {

QString normalizeLanguage(const QString& language);
QString currentLanguage();
bool applyLanguage(QCoreApplication& app, const QString& language, QString* errorMessage = nullptr);

} // namespace AppLanguage

#endif // APP_LANGUAGE_H
