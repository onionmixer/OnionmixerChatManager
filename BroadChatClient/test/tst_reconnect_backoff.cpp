// PLAN_DEV_BROADCHATCLIENT §12.1 · §5.2 v37-6 backoff 매트릭스 단위 테스트.
// BroadChatClientApp::computeBackoffMs / backoffMsForByeReason 로직을 순수 함수로
// 격리 테스트. 실제 BroadChatClientApp은 QApplication 의존이라 로직 복제해 검증.

#include <QString>
#include <QTest>

namespace {

// BroadChatClientApp::computeBackoffMs (§5.2 v2-9: 1 → 2 → 4 → 8 → 16 → 30 cap)
int computeBackoffMs(int retryCount)
{
    const int seconds = qMin(1 << qMin(retryCount, 5), 30);
    return seconds * 1000;
}

// BroadChatClientApp::backoffMsForByeReason (§5.2 v37-6 매트릭스)
int backoffMsForByeReason(const QString& reason, int retryCount)
{
    if (reason == QStringLiteral("protocol_error")) return 30 * 1000;
    if (reason == QStringLiteral("duplicate_client_id")) return 60 * 1000;
    if (reason == QStringLiteral("too_many_clients")) return 30 * 1000;
    if (reason == QStringLiteral("auth_failed")) return -1;
    if (reason == QStringLiteral("version_mismatch")) return -1;
    return computeBackoffMs(retryCount);
}

} // namespace

class ReconnectBackoffTest : public QObject
{
    Q_OBJECT
private slots:
    void exponential_backoff_progression();
    void backoff_caps_at_30s();
    void protocol_error_fixed_30s();
    void duplicate_client_id_fixed_60s();
    void too_many_clients_fixed_30s();
    void auth_failed_terminates();
    void version_mismatch_terminates();
    void shutdown_uses_general_backoff();
    void timeout_uses_general_backoff();
    void unknown_reason_uses_general_backoff();
};

void ReconnectBackoffTest::exponential_backoff_progression()
{
    QCOMPARE(computeBackoffMs(0),  1 * 1000);
    QCOMPARE(computeBackoffMs(1),  2 * 1000);
    QCOMPARE(computeBackoffMs(2),  4 * 1000);
    QCOMPARE(computeBackoffMs(3),  8 * 1000);
    QCOMPARE(computeBackoffMs(4), 16 * 1000);
}

void ReconnectBackoffTest::backoff_caps_at_30s()
{
    QCOMPARE(computeBackoffMs(5),  30 * 1000);
    QCOMPARE(computeBackoffMs(6),  30 * 1000);
    QCOMPARE(computeBackoffMs(10), 30 * 1000);
    QCOMPARE(computeBackoffMs(100), 30 * 1000);
}

void ReconnectBackoffTest::protocol_error_fixed_30s()
{
    QCOMPARE(backoffMsForByeReason("protocol_error", 0), 30 * 1000);
    QCOMPARE(backoffMsForByeReason("protocol_error", 5), 30 * 1000);
}

void ReconnectBackoffTest::duplicate_client_id_fixed_60s()
{
    QCOMPARE(backoffMsForByeReason("duplicate_client_id", 0), 60 * 1000);
    QCOMPARE(backoffMsForByeReason("duplicate_client_id", 10), 60 * 1000);
}

void ReconnectBackoffTest::too_many_clients_fixed_30s()
{
    QCOMPARE(backoffMsForByeReason("too_many_clients", 0), 30 * 1000);
}

void ReconnectBackoffTest::auth_failed_terminates()
{
    QCOMPARE(backoffMsForByeReason("auth_failed", 0), -1);
    QCOMPARE(backoffMsForByeReason("auth_failed", 99), -1);
}

void ReconnectBackoffTest::version_mismatch_terminates()
{
    QCOMPARE(backoffMsForByeReason("version_mismatch", 0), -1);
}

void ReconnectBackoffTest::shutdown_uses_general_backoff()
{
    // shutdown·disconnect·normal은 일반 backoff
    QCOMPARE(backoffMsForByeReason("shutdown", 0), 1 * 1000);
    QCOMPARE(backoffMsForByeReason("shutdown", 3), 8 * 1000);
    QCOMPARE(backoffMsForByeReason("shutdown", 10), 30 * 1000);
}

void ReconnectBackoffTest::timeout_uses_general_backoff()
{
    QCOMPARE(backoffMsForByeReason("timeout", 0), 1 * 1000);
    QCOMPARE(backoffMsForByeReason("timeout", 5), 30 * 1000);
}

void ReconnectBackoffTest::unknown_reason_uses_general_backoff()
{
    // forward-compat: 알려지지 않은 reason은 일반 backoff 경로
    QCOMPARE(backoffMsForByeReason("future_new_reason_code", 0), 1 * 1000);
}

QTEST_MAIN(ReconnectBackoffTest)
#include "tst_reconnect_backoff.moc"
