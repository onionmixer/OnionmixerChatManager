// PLAN_DEV_BROADCHATCLIENT §5.6.1 7-step fallback 체인 단위 테스트 (v72).
// ConfigPathResolver namespace 의 순수 함수들을 격리 테스트.
// 각 step 의 성공/실패 시나리오 + migration copy 동작을 임시 디렉토리 기반으로 검증.

#include "config/ConfigPathResolver.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class ConfigResolverTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void platform_app_name_not_empty();
    void can_write_dir_creates_and_probes();
    void can_write_dir_rejects_empty();
    void user_local_includes_bucket();
    void tmp_config_dir_includes_bucket();

    void step1_cli_strict_success();
    void step1_cli_strict_failure_no_fallback();
    void step2_env_strict_success();
    void step2_env_strict_failure_no_fallback();
    void step4_user_local_default();
    void migration_copies_legacy_ini();
    void migration_skips_when_target_exists();
    void migration_skips_when_legacy_absent();

private:
    // 테스트 중에 실제 home 의 user-local 에 쓰지 않도록 격리. QStandardPaths 를
    // TEST location 으로 설정 — Linux 에서는 $XDG_CONFIG_HOME 오버라이드로 동작.
    QString m_savedXdg;
};

void ConfigResolverTest::initTestCase()
{
    // QStandardPaths::setTestModeEnabled() 로 user-local 을 테스트 격리 경로로 이동.
    QStandardPaths::setTestModeEnabled(true);
}

void ConfigResolverTest::platform_app_name_not_empty()
{
    const QString name = ConfigPathResolver::platformAppName();
    QVERIFY(!name.isEmpty());
#if defined(Q_OS_LINUX)
    QCOMPARE(name, QStringLiteral("onionmixer-bcc"));
#else
    QCOMPARE(name, QStringLiteral("OnionmixerBroadChatClient"));
#endif
}

void ConfigResolverTest::can_write_dir_creates_and_probes()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString sub = tmp.path() + QStringLiteral("/deeply/nested/dir");
    QVERIFY(ConfigPathResolver::canWriteDir(sub));
    QVERIFY(QFileInfo(sub).isDir());
}

void ConfigResolverTest::can_write_dir_rejects_empty()
{
    QVERIFY(!ConfigPathResolver::canWriteDir(QString()));
}

void ConfigResolverTest::user_local_includes_bucket()
{
    const QString d1 = ConfigPathResolver::userLocalConfigDir(QStringLiteral("custom-instance"));
    QVERIFY(!d1.isEmpty());
    QVERIFY(d1.endsWith(QStringLiteral("/custom-instance")));
    const QString d2 = ConfigPathResolver::userLocalConfigDir(QString());
    QVERIFY(d2.endsWith(QStringLiteral("/default")));
}

void ConfigResolverTest::tmp_config_dir_includes_bucket()
{
    const QString d = ConfigPathResolver::tmpConfigDir(QStringLiteral("xyz"));
    QVERIFY(!d.isEmpty());
    QVERIFY(d.endsWith(QStringLiteral("/xyz")));
    // Linux/macOS UID scoping 이 포함되는지
#if defined(Q_OS_UNIX)
    QVERIFY(d.contains(QStringLiteral("onionmixer-bcc-")));
#endif
}

void ConfigResolverTest::step1_cli_strict_success()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dir = tmp.path() + QStringLiteral("/explicit");
    QDir().mkpath(dir);

    const auto r = ConfigPathResolver::resolveConfigDir(dir, QString(), QString());
    QCOMPARE(r.step, 1);
    QCOMPARE(r.path, dir);
    QVERIFY(r.tried.isEmpty());
}

void ConfigResolverTest::step1_cli_strict_failure_no_fallback()
{
    // 존재하지 않는 부모 경로 + read-only 부모 — mkpath 자체는 성공할 수 있어서
    // canWriteDir 이 실패하려면 실제 read-only FS 가 필요. 대신 잘못된 형식의 경로 사용.
    // Linux 는 빈 문자열 외엔 거의 모든 경로가 mkpath 성공 — 대안으로 기존 파일을
    // 디렉토리 경로로 지정해 mkpath 실패 유도.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString filePath = tmp.path() + QStringLiteral("/file.txt");
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();
    // filePath 가 기존 파일이므로 그 하위를 mkpath 하려면 실패
    const QString bogus = filePath + QStringLiteral("/subdir");

    const auto r = ConfigPathResolver::resolveConfigDir(bogus, QString(), QString());
    QCOMPARE(r.step, 0);           // strict 실패 → fallback 없음
    QCOMPARE(r.tried.size(), 1);
    QVERIFY(r.tried.first().startsWith(QStringLiteral("[step1/cli]")));
}

void ConfigResolverTest::step2_env_strict_success()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dir = tmp.path() + QStringLiteral("/env-dir");
    QDir().mkpath(dir);

    const auto r = ConfigPathResolver::resolveConfigDir(QString(), dir, QString());
    QCOMPARE(r.step, 2);
    QCOMPARE(r.path, dir);
}

void ConfigResolverTest::step2_env_strict_failure_no_fallback()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString filePath = tmp.path() + QStringLiteral("/f2");
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();
    const QString bogus = filePath + QStringLiteral("/sub");

    const auto r = ConfigPathResolver::resolveConfigDir(QString(), bogus, QString());
    QCOMPARE(r.step, 0);
    QCOMPARE(r.tried.size(), 1);
    QVERIFY(r.tried.first().startsWith(QStringLiteral("[step2/env]")));
}

void ConfigResolverTest::step4_user_local_default()
{
    // CLI/env 없음, portable 마커 없음 → Step 4 (user-local) 가 채택.
    // QStandardPaths::setTestModeEnabled(true) 가 실제 홈 디렉토리 오염 방지.
    const auto r = ConfigPathResolver::resolveConfigDir(QString(), QString(),
                                                        QStringLiteral("unit-test"));
    QCOMPARE(r.step, 4);
    QVERIFY(!r.path.isEmpty());
    QVERIFY(r.path.endsWith(QStringLiteral("/unit-test")));
}

void ConfigResolverTest::migration_copies_legacy_ini()
{
    // exe_dir 의 legacy 경로를 시뮬레이션하기 위해 applicationDirPath() 기준으로
    // /BroadChatClient/<instance>/config.ini 를 실제 생성.
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString instance = QStringLiteral("mig-test-A");
    const QString legacyDir = exeDir + QStringLiteral("/BroadChatClient/") + instance;
    QVERIFY(QDir().mkpath(legacyDir));
    const QString legacyIni = legacyDir + QStringLiteral("/config.ini");
    QFile lf(legacyIni);
    QVERIFY(lf.open(QIODevice::WriteOnly));
    lf.write("[app]\nlanguage=ko_KR\n");
    lf.close();

    QTemporaryDir target;
    QVERIFY(target.isValid());
    const bool migrated =
        ConfigPathResolver::maybeMigrateFromExeDir(target.path(), instance);
    QVERIFY(migrated);
    QVERIFY(QFileInfo::exists(target.path() + QStringLiteral("/config.ini")));
    QVERIFY(QFileInfo::exists(target.path() + QStringLiteral("/.migrated_from_exe_dir")));
    // 원본 보존
    QVERIFY(QFileInfo::exists(legacyIni));

    // cleanup
    QFile::remove(legacyIni);
    QDir(legacyDir).removeRecursively();
}

void ConfigResolverTest::migration_skips_when_target_exists()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString instance = QStringLiteral("mig-test-B");
    const QString legacyDir = exeDir + QStringLiteral("/BroadChatClient/") + instance;
    QVERIFY(QDir().mkpath(legacyDir));
    QFile lf(legacyDir + QStringLiteral("/config.ini"));
    QVERIFY(lf.open(QIODevice::WriteOnly));
    lf.close();

    QTemporaryDir target;
    QVERIFY(target.isValid());
    QFile existing(target.path() + QStringLiteral("/config.ini"));
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.write("pre-existing");
    existing.close();

    const bool migrated =
        ConfigPathResolver::maybeMigrateFromExeDir(target.path(), instance);
    QVERIFY(!migrated);
    // 마커는 생성되지 않아야 함
    QVERIFY(!QFileInfo::exists(target.path() + QStringLiteral("/.migrated_from_exe_dir")));

    QDir(legacyDir).removeRecursively();
}

void ConfigResolverTest::migration_skips_when_legacy_absent()
{
    QTemporaryDir target;
    QVERIFY(target.isValid());
    const bool migrated = ConfigPathResolver::maybeMigrateFromExeDir(
        target.path(), QStringLiteral("nonexistent-instance-XYZ"));
    QVERIFY(!migrated);
}

QTEST_GUILESS_MAIN(ConfigResolverTest)
#include "tst_config_resolver.moc"
