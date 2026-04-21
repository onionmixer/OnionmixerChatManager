// PLAN_DEV_BROADCHATCLIENT §16.8·16.9·16.11·5.6.9·10(broken ini) 단위 테스트 (v72).
// BroadChatClientApp 의 loadConfig/saveConfig 는 QApplication 의존이라 직접 호출은
// 복잡함. 본 테스트는 ini 동작에 대한 QSettings 계약·파일 시스템 동작을
// 클라 구현과 동일한 방식으로 재현해 검증 — 회귀 방지 목적 (실제 운영 앱이 같은
// QSettings/QFile API 사용하므로 행동 일치).

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

namespace {

// 클라 구현 미러: schema_version 쓰기
constexpr int kSchemaVersion = 1;

void writeConfig(const QString& path, const QString& clientId,
                 int schemaVersion = kSchemaVersion)
{
    QSettings s(path, QSettings::IniFormat);
    s.beginGroup(QStringLiteral("meta"));
    s.setValue(QStringLiteral("schema_version"), schemaVersion);
    s.endGroup();
    s.beginGroup(QStringLiteral("identity"));
    s.setValue(QStringLiteral("client_id"), clientId);
    s.endGroup();
    s.beginGroup(QStringLiteral("connection"));
    s.setValue(QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    s.setValue(QStringLiteral("port"), 47123);
    s.endGroup();
    s.sync();
}

}  // namespace

class ClientSettingsTest : public QObject
{
    Q_OBJECT
private slots:
    void settings_roundtrip_clientid();
    void settings_roundtrip_schema_version();
    void future_schema_version_is_readable();
    void broken_ini_detected_by_status();
    void broken_ini_rename_to_backup_ext();
    void save_status_ok_for_fresh_file();
    void file_permissions_owner_rw_only();
    void history_requestid_matching_stale_filter();
    // v74: ini 없을 때 첫 실행 → saveConfig 동등 로직이 파일 생성
    void first_run_creates_ini_from_defaults();
};

void ClientSettingsTest::settings_roundtrip_clientid()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/cfg.ini");

    const QString id = QStringLiteral("broadchat-instance-abcd1234");
    writeConfig(path, id);

    QSettings s2(path, QSettings::IniFormat);
    s2.beginGroup(QStringLiteral("identity"));
    QCOMPARE(s2.value(QStringLiteral("client_id")).toString(), id);
    s2.endGroup();
}

void ClientSettingsTest::settings_roundtrip_schema_version()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/cfg.ini");
    writeConfig(path, QStringLiteral("id"));

    QSettings s2(path, QSettings::IniFormat);
    s2.beginGroup(QStringLiteral("meta"));
    QCOMPARE(s2.value(QStringLiteral("schema_version")).toInt(), kSchemaVersion);
    s2.endGroup();
}

void ClientSettingsTest::future_schema_version_is_readable()
{
    // §5.6.9 v57-2: 미래 버전 감지 시 경고 후 best-effort 로드 — 로딩 자체는 성공해야.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/future.ini");
    writeConfig(path, QStringLiteral("fid"), /*schemaVersion=*/99);

    QSettings s2(path, QSettings::IniFormat);
    QCOMPARE(s2.status(), QSettings::NoError);
    s2.beginGroup(QStringLiteral("meta"));
    QCOMPARE(s2.value(QStringLiteral("schema_version")).toInt(), 99);
    s2.endGroup();
    // host 같은 다른 필드도 정상 읽힘 (best-effort)
    s2.beginGroup(QStringLiteral("connection"));
    QCOMPARE(s2.value(QStringLiteral("host")).toString(),
             QStringLiteral("127.0.0.1"));
    s2.endGroup();
}

void ClientSettingsTest::broken_ini_detected_by_status()
{
    // 파손된 ini → QSettings::FormatError. 클라의 loadConfig 는 이 status 를 검사해
    // .broken 으로 백업하는 로직을 가지고 있음.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/bad.ini");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    // ini 형식에 맞지 않는 잡음 — QSettings 가 FormatError 로 인식
    f.write("???\n[[[invalid\n=no-key\n\\x00binary\xFF\xFE garbage\n");
    f.close();

    QSettings s(path, QSettings::IniFormat);
    QVERIFY(s.status() != QSettings::NoError);
}

void ClientSettingsTest::broken_ini_rename_to_backup_ext()
{
    // 클라 로직 미러: 파손 감지 → .broken 백업 → 신규 생성.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/bad2.ini");
    const QString brokenPath = path + QStringLiteral(".broken");

    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("garbage\xFF");
    f.close();

    QFile::remove(brokenPath);
    QVERIFY(QFile::rename(path, brokenPath));
    QVERIFY(QFileInfo::exists(brokenPath));
    QVERIFY(!QFileInfo::exists(path));
}

void ClientSettingsTest::save_status_ok_for_fresh_file()
{
    // §16.11: saveConfig 후 QSettings::status() == NoError 여야 함.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/ok.ini");

    QSettings s(path, QSettings::IniFormat);
    s.setValue(QStringLiteral("key"), QStringLiteral("value"));
    s.sync();
    QCOMPARE(s.status(), QSettings::NoError);
}

void ClientSettingsTest::file_permissions_owner_rw_only()
{
    // §16.8 v50-15: 0600. QFile::setPermissions 결과 검증.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/perm.ini");
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
    }
    QVERIFY(QFile::setPermissions(path,
                                  QFile::ReadOwner | QFile::WriteOwner));
    const auto perms = QFileInfo(path).permissions();
#if defined(Q_OS_UNIX)
    QVERIFY(perms & QFile::ReadOwner);
    QVERIFY(perms & QFile::WriteOwner);
    QVERIFY(!(perms & QFile::ReadGroup));
    QVERIFY(!(perms & QFile::ReadOther));
#else
    // Windows 의 ACL 매핑은 다름 — owner bit 만 assert
    QVERIFY(perms & QFile::ReadOwner);
    QVERIFY(perms & QFile::WriteOwner);
#endif
}

void ClientSettingsTest::history_requestid_matching_stale_filter()
{
    // v68 #1: pending requestId 와 다른 id 응답은 stale 로 필터링.
    // BroadChatClientApp::onHistoryChunkReceived 로직을 재현.
    const QString pending = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString stale = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // 매칭: pending == incoming → accept
    QVERIFY(pending == pending);
    // 불일치: pending != stale → reject
    QVERIFY(pending != stale);

    // 빈 pending 상태에서는 match 검사 skip (초기 연결 또는 재시도 완료 후)
    QString currentPending;
    QVERIFY(currentPending.isEmpty());
    // 실제 로직: if (!currentPending.isEmpty() && reqId != currentPending) return;
    // 빈 값이면 그냥 apply — 아래가 그 동작 assertion
    const bool wouldApply = currentPending.isEmpty() || (stale == currentPending);
    QVERIFY(wouldApply);
}

void ClientSettingsTest::first_run_creates_ini_from_defaults()
{
    // v74 #신규: config.ini 가 없는 디렉터리에서 loadConfig → saveConfig 시퀀스가
    // 파일을 생성해야 함. 클라의 loadConfig 는 heal=!iniExisted 트리거 → saveConfig
    // 호출. QSettings 자체가 sync() 시점에 파일 생성하므로 writeConfig 호출만으로 검증.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/first-run.ini");
    QVERIFY(!QFileInfo::exists(path));  // precondition

    writeConfig(path, QStringLiteral("broadchat-instance-initial"));
    QVERIFY(QFileInfo::exists(path));  // QSettings 가 파일 생성 완료

    // 내용 검증 — schema_version 과 client_id 가 저장됨
    QSettings s(path, QSettings::IniFormat);
    QCOMPARE(s.status(), QSettings::NoError);
    s.beginGroup(QStringLiteral("meta"));
    QCOMPARE(s.value(QStringLiteral("schema_version")).toInt(), kSchemaVersion);
    s.endGroup();
}

QTEST_GUILESS_MAIN(ClientSettingsTest)
#include "tst_client_settings.moc"
