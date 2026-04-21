#include "ClientLanguage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QStringList>
#include <QTranslator>

#include <memory>

namespace {

std::unique_ptr<QTranslator> g_translator;
QString g_currentLanguage = QStringLiteral("en_US");

// 메인 앱과 분리된 qm 파일명 — drift·충돌 방지.
QString qmBaseNameForLanguage(const QString& language)
{
    return QStringLiteral("onionmixerbroadchatclient_%1").arg(language);
}

QStringList translationSearchDirs()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString appName = QStringLiteral("OnionmixerBroadChatClient");
    return {
        appDir + QStringLiteral("/translations"),
        appDir + QStringLiteral("/../translations"),
        appDir + QStringLiteral("/../share/") + appName + QStringLiteral("/translations"),
        QStringLiteral("/usr/share/") + appName + QStringLiteral("/translations"),
        QStringLiteral("/usr/local/share/") + appName + QStringLiteral("/translations"),
        QDir::currentPath() + QStringLiteral("/translations"),
        QStringLiteral("translations"),
    };
}

} // namespace

namespace BroadChatClientLanguage {

QString normalizeLanguage(const QString& language)
{
    const QString normalized = language.trimmed();
    if (normalized == QStringLiteral("ko_KR")) return normalized;
    if (normalized == QStringLiteral("ja_JP")) return normalized;
    if (normalized == QStringLiteral("en_US")) return normalized;
    return QStringLiteral("en_US");
}

QString currentLanguage()
{
    return g_currentLanguage;
}

bool applyLanguage(QCoreApplication& app, const QString& language,
                   QString* errorMessage)
{
    QString target = language.trimmed();
    if (target.isEmpty()) {
        // ini에 language 미지정 시 OS locale 기반 자동 감지.
        target = QLocale::system().name();   // "ko_KR" / "en_US" / etc.
    }
    const QString normalized = normalizeLanguage(target);

    if (g_translator) {
        app.removeTranslator(g_translator.get());
    }
    g_translator = std::make_unique<QTranslator>();
    g_currentLanguage = normalized;

    const QString baseName = qmBaseNameForLanguage(normalized);
    for (const QString& dirPath : translationSearchDirs()) {
        const QString filePath =
            dirPath + QStringLiteral("/") + baseName + QStringLiteral(".qm");
        if (!QFileInfo::exists(filePath)) continue;
        if (g_translator->load(filePath)) {
            app.installTranslator(g_translator.get());
            return true;
        }
    }

    if (errorMessage) {
        *errorMessage =
            QStringLiteral("translation file not found: %1.qm").arg(baseName);
    }
    // en_US가 번역 파일 없어도 기본 영어 문자열이 source에 존재 → 성공 간주.
    return normalized == QStringLiteral("en_US");
}

} // namespace BroadChatClientLanguage
