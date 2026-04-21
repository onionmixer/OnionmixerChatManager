#include "ConfigPathResolver.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QLatin1Char>
#include <QStandardPaths>
#include <QTemporaryFile>

#if defined(Q_OS_UNIX)
#include <unistd.h>
#endif

namespace ConfigPathResolver {

QString platformAppName()
{
#if defined(Q_OS_LINUX)
    return QStringLiteral("onionmixer-bcc");
#else
    return QStringLiteral("OnionmixerBroadChatClient");
#endif
}

bool canWriteDir(const QString& dir)
{
    if (dir.isEmpty()) return false;
    if (!QDir().mkpath(dir)) return false;
    QTemporaryFile probe(dir + QStringLiteral("/.probe_XXXXXX"));
    probe.setAutoRemove(true);
    return probe.open();
}

QString userLocalConfigDir(const QString& instance)
{
    const QString bucket = instance.isEmpty() ? QStringLiteral("default") : instance;
#if defined(Q_OS_LINUX)
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (base.isEmpty()) return {};
    return base + QLatin1Char('/') + platformAppName() + QLatin1Char('/') + bucket;
#else
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) return {};
    return base + QLatin1Char('/') + bucket;
#endif
}

QString tmpConfigDir(const QString& instance)
{
    const QString bucket = instance.isEmpty() ? QStringLiteral("default") : instance;
#if defined(Q_OS_UNIX)
    const uint uid = static_cast<uint>(geteuid());
    return QDir::tempPath() + QStringLiteral("/onionmixer-bcc-")
           + QString::number(uid) + QLatin1Char('/') + bucket;
#else
    return QDir::tempPath() + QStringLiteral("/OnionmixerBroadChatClient/") + bucket;
#endif
}

ResolvedConfigDir resolveConfigDir(const QString& cliDir,
                                   const QString& envDir,
                                   const QString& instance)
{
    ResolvedConfigDir r;
    const QString bucket = instance.isEmpty() ? QStringLiteral("default") : instance;

    // Step 1: CLI --config-dir (strict)
    if (!cliDir.isEmpty()) {
        if (canWriteDir(cliDir)) {
            r.path = cliDir;
            r.step = 1;
            return r;
        }
        r.tried << QStringLiteral("[step1/cli] ") + cliDir;
        return r;
    }

    // Step 2: $ONIONMIXER_BCC_CONFIG_DIR (strict)
    if (!envDir.isEmpty()) {
        if (canWriteDir(envDir)) {
            r.path = envDir;
            r.step = 2;
            return r;
        }
        r.tried << QStringLiteral("[step2/env] ") + envDir;
        return r;
    }

    // Step 3: Portable 모드 (<exe_dir>/BroadChatClient/.portable 마커)
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString portableMarker = exeDir + QStringLiteral("/BroadChatClient/.portable");
    if (QFileInfo::exists(portableMarker)) {
        const QString p = exeDir + QStringLiteral("/BroadChatClient/") + bucket;
        if (canWriteDir(p)) {
            r.path = p;
            r.step = 3;
            return r;
        }
        r.tried << QStringLiteral("[step3/portable] ") + p;
    }

    // Step 4: User-local
    const QString userLocal = userLocalConfigDir(instance);
    if (canWriteDir(userLocal)) {
        r.path = userLocal;
        r.step = 4;
        return r;
    }
    if (!userLocal.isEmpty()) {
        r.tried << QStringLiteral("[step4/user-local] ") + userLocal;
    }

    // Step 5: exe_dir (portable 마커 없어도 쓰기 가능하면 사용)
    const QString exeBucket = exeDir + QStringLiteral("/BroadChatClient/") + bucket;
    if (canWriteDir(exeBucket)) {
        r.path = exeBucket;
        r.step = 5;
        return r;
    }
    r.tried << QStringLiteral("[step5/exe-dir] ") + exeBucket;

    // Step 6: TMPDIR
    const QString tmp = tmpConfigDir(instance);
    if (canWriteDir(tmp)) {
        r.path = tmp;
        r.step = 6;
        return r;
    }
    r.tried << QStringLiteral("[step6/tmpdir] ") + tmp;

    return r;  // step=0 → 호출자가 in-memory 또는 exit 3 결정
}

bool maybeMigrateFromExeDir(const QString& targetDir, const QString& instance)
{
    const QString bucket = instance.isEmpty() ? QStringLiteral("default") : instance;
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString legacy =
        exeDir + QStringLiteral("/BroadChatClient/") + bucket + QStringLiteral("/config.ini");
    if (!QFileInfo::exists(legacy)) return false;

    const QString newIni = targetDir + QStringLiteral("/config.ini");
    if (QFileInfo::exists(newIni)) return false;  // 이미 존재 — migrate 불필요

    if (!QFile::copy(legacy, newIni)) return false;

    QFile marker(targetDir + QStringLiteral("/.migrated_from_exe_dir"));
    if (marker.open(QIODevice::WriteOnly | QIODevice::Text)) {
        marker.write(legacy.toUtf8());
        marker.write("\n");
        marker.close();
    }
    return true;
}

}  // namespace ConfigPathResolver
