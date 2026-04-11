#include "i18n/AppLanguage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QTranslator>
#include <memory>

namespace {
std::unique_ptr<QTranslator> g_translator;
QString g_currentLanguage = QStringLiteral("en_US");

QString qmBaseNameForLanguage(const QString& language)
{
    return QStringLiteral("botmanager_%1").arg(language);
}

QStringList translationSearchDirs()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    return {
        appDir + QStringLiteral("/translations"),
        appDir + QStringLiteral("/../translations"),
        QDir::currentPath() + QStringLiteral("/translations"),
        QStringLiteral("translations"),
    };
}
} // namespace

namespace AppLanguage {

QString normalizeLanguage(const QString& language)
{
    const QString normalized = language.trimmed();
    if (normalized == QStringLiteral("ko_KR")) {
        return normalized;
    }
    if (normalized == QStringLiteral("ja_JP")) {
        return normalized;
    }
    return QStringLiteral("en_US");
}

QString currentLanguage()
{
    return g_currentLanguage;
}

bool applyLanguage(QCoreApplication& app, const QString& language, QString* errorMessage)
{
    const QString normalized = normalizeLanguage(language);
    if (g_translator) {
        app.removeTranslator(g_translator.get());
    }
    g_translator = std::make_unique<QTranslator>();
    g_currentLanguage = normalized;

    const QString baseName = qmBaseNameForLanguage(normalized);
    for (const QString& dirPath : translationSearchDirs()) {
        const QString filePath = dirPath + QStringLiteral("/") + baseName + QStringLiteral(".qm");
        if (!QFileInfo::exists(filePath)) {
            continue;
        }
        if (g_translator->load(filePath)) {
            app.installTranslator(g_translator.get());
            return true;
        }
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("translation file not found: %1.qm").arg(baseName);
    }
    return normalized == QStringLiteral("en_US");
}

} // namespace AppLanguage
