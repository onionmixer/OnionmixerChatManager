// PLAN_DEV_BROADCHATCLIENT §16.13 UAT 자동화 · §부록 K FU-K4.
// MockServer를 사용해 BroadChatConnection 실제 네트워크 경로 통합 테스트.

#include "MockServer.h"

#include "BroadChatConnection.h"
#include "shared/BroadChatProtocol.h"

#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

class ConnectionIntegration : public QObject
{
    Q_OBJECT
private slots:
    // 시나리오 1: 정상 핸드셰이크
    void basic_handshake_server_hello_triggers_client_hello();
    // 시나리오 2: client_hello 필드 검증
    void client_hello_contains_clientId_and_proto();
    // 시나리오 3: authToken 필드 동작
    void auth_token_present_when_set();
    void auth_token_absent_when_empty();
    // 시나리오 4: helloCompleted signal 발생
    void hello_completed_signal_fires_after_server_hello();
    // 시나리오 5: bye shutdown UX
    void bye_shutdown_triggers_bye_received_signal();
    // 시나리오 6: auth_failed forwarded
    void auth_failed_bye_forwarded_with_reason();
    // 시나리오 7: version_mismatch 자발 감지
    void version_mismatch_when_server_proto_max_zero();
    // 시나리오 8: malformed envelope → protocol_error
    void malformed_envelope_triggers_protocol_error();
};

namespace {

// 테스트 공통: mock 서버 시작 + 클라 연결 시도 + clientConnected 대기.
struct TestFixture {
    MockServer server;
    BroadChatConnection conn;

    bool setupAndConnect(const QString& authToken = QString())
    {
        if (!server.start()) return false;
        QSignalSpy connSpy(&server, &MockServer::clientConnected);
        conn.setClientId(QStringLiteral("test-client-01"));
        conn.start(QStringLiteral("127.0.0.1"), server.port(), authToken);
        return connSpy.wait(3000);
    }
};

} // namespace

void ConnectionIntegration::basic_handshake_server_hello_triggers_client_hello()
{
    TestFixture f;
    QVERIFY(f.setupAndConnect());

    QSignalSpy helloSpy(&f.server, &MockServer::clientHelloReceived);

    f.server.sendServerHello();
    QVERIFY(helloSpy.wait(2000));
    QCOMPARE(helloSpy.count(), 1);
}

void ConnectionIntegration::client_hello_contains_clientId_and_proto()
{
    TestFixture f;
    QVERIFY(f.setupAndConnect());

    QSignalSpy helloSpy(&f.server, &MockServer::clientHelloReceived);
    f.server.sendServerHello();
    QVERIFY(helloSpy.wait(2000));

    const auto args = helloSpy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("test-client-01"));
    QCOMPARE(args.at(1).toInt(), BroadChatProtocol::kProtocolVersion);
}

void ConnectionIntegration::auth_token_present_when_set()
{
    TestFixture f;
    QVERIFY(f.setupAndConnect(QStringLiteral("secret-token-123")));

    QSignalSpy helloSpy(&f.server, &MockServer::clientHelloReceived);
    f.server.sendServerHello();
    QVERIFY(helloSpy.wait(2000));

    const auto args = helloSpy.takeFirst();
    QCOMPARE(args.at(2).toString(), QStringLiteral("secret-token-123"));
}

void ConnectionIntegration::auth_token_absent_when_empty()
{
    TestFixture f;
    QVERIFY(f.setupAndConnect(QString()));

    QSignalSpy helloSpy(&f.server, &MockServer::clientHelloReceived);
    f.server.sendServerHello();
    QVERIFY(helloSpy.wait(2000));

    const auto args = helloSpy.takeFirst();
    // 빈 문자열 또는 필드 누락 → 둘 다 "인증 off" 의미
    QVERIFY(args.at(2).toString().isEmpty());
}

void ConnectionIntegration::hello_completed_signal_fires_after_server_hello()
{
    TestFixture f;
    QVERIFY(f.setupAndConnect());

    QSignalSpy completeSpy(&f.conn, &BroadChatConnection::helloCompleted);
    f.server.sendServerHello(1, 1, QStringLiteral("0.1.0-mock"));
    QVERIFY(completeSpy.wait(2000));

    const auto args = completeSpy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("0.1.0-mock"));
    QCOMPARE(args.at(1).toInt(), 1);     // protocolMin
    QCOMPARE(args.at(2).toInt(), 1);     // protocolMax
    QVERIFY(f.conn.isHelloCompleted());
}

void ConnectionIntegration::bye_shutdown_triggers_bye_received_signal()
{
    TestFixture f;
    QVERIFY(f.setupAndConnect());
    f.server.sendServerHello();

    QSignalSpy byeSpy(&f.conn, &BroadChatConnection::byeReceived);
    f.server.sendBye(QStringLiteral("shutdown"), QStringLiteral("scheduled maintenance"));
    QVERIFY(byeSpy.wait(2000));

    const auto args = byeSpy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("shutdown"));
    QCOMPARE(args.at(1).toString(), QStringLiteral("scheduled maintenance"));
}

void ConnectionIntegration::auth_failed_bye_forwarded_with_reason()
{
    TestFixture f;
    QVERIFY(f.setupAndConnect(QStringLiteral("wrong-token")));
    f.server.sendServerHello();

    QSignalSpy byeSpy(&f.conn, &BroadChatConnection::byeReceived);
    f.server.sendBye(QStringLiteral("auth_failed"));
    QVERIFY(byeSpy.wait(2000));

    QCOMPARE(byeSpy.first().at(0).toString(), QStringLiteral("auth_failed"));
}

void ConnectionIntegration::version_mismatch_when_server_proto_max_zero()
{
    TestFixture f;
    QVERIFY(f.setupAndConnect());

    QSignalSpy byeSpy(&f.conn, &BroadChatConnection::byeReceived);
    // 서버가 kProtocolVersion(1) 범위 밖 protocolMax=0 선언 →
    // 클라가 자발적으로 version_mismatch bye 송신 + disconnect
    f.server.sendServerHello(0, 0);
    QVERIFY(byeSpy.wait(2000));

    QCOMPARE(byeSpy.first().at(0).toString(), QStringLiteral("version_mismatch"));
}

void ConnectionIntegration::malformed_envelope_triggers_protocol_error()
{
    TestFixture f;
    QVERIFY(f.setupAndConnect());
    f.server.sendServerHello();

    // Wait for helloCompleted to ensure Active state
    QSignalSpy completeSpy(&f.conn, &BroadChatConnection::helloCompleted);
    QVERIFY(completeSpy.wait(2000));

    QSignalSpy errSpy(&f.conn, &BroadChatConnection::protocolError);
    // Malformed JSON line — 클라가 parseEnvelope 실패 감지 후 protocol_error 경로
    f.server.sendRawBytes(QByteArrayLiteral("{this is broken json\n"));
    QVERIFY(errSpy.wait(2000));
    QCOMPARE(errSpy.count(), 1);
}

QTEST_GUILESS_MAIN(ConnectionIntegration)
#include "tst_connection_integration.moc"
