#include "BroadChatClientApp.h"

#include "BroadChatConnection.h"
#include "config/ConfigPathResolver.h"
#include "config/IniValidator.h"
#include "core/EmojiImageCache.h"
#include "shared/BroadChatEndpoint.h"
#include "shared/BroadChatLogging.h"
#include "ui/BroadcastChatWindow.h"          // applySettings 호출용 — v61
#include "ui/ClientConfigDialog.h"
#include "ui/MainBroadcastWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QUuid>

namespace {
constexpr int kEmojiFailureBackoffMs = 60 * 1000;

// v68 #4: ini schema 현행 버전. 로드 시 미래 버전 감지되면 경고 후 best-effort 로드.
constexpr int kIniSchemaVersion = 1;
} // namespace

BroadChatClientApp::BroadChatClientApp(QApplication* app)
    : QObject(app)
    , m_app(app)
    , m_reconnectTimer(new QTimer(this))
{
    connect(app, &QCoreApplication::aboutToQuit,
            this, &BroadChatClientApp::onAboutToQuit);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout,
            this, &BroadChatClientApp::onReconnectTimerFired);
}

BroadChatClientApp::~BroadChatClientApp() = default;

int BroadChatClientApp::initialize(int /*argc*/, char** /*argv*/)
{
    // v37-3·v39 TCP CLI: --host·--port·--auth-token·--config-dir·--instance-id.
    // §16.12 exit 2: CLI 파싱 에러 (unknown arg, invalid port, missing value) 시 반환.
    const QStringList args = QCoreApplication::arguments();
    QString host;
    quint16 port = 0;
    QString cliToken;
    QString configDir;
    QString instanceId;
    static const QSet<QString> kKnownFlags = {
        "--host", "--port", "--auth-token", "--config-dir", "--instance-id",
        "--language", "--version", "--help", "-h"};
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args.at(i);
        if (a == QStringLiteral("--host") && i + 1 < args.size()) {
            host = args.at(++i);
        } else if (a == QStringLiteral("--port") && i + 1 < args.size()) {
            bool ok = false;
            const uint v = args.at(++i).toUInt(&ok);
            if (!ok || v == 0 || v > 65535) {
                qCCritical(lcBroadChatErr) << "invalid --port value:" << args.at(i);
                return 2;
            }
            port = static_cast<quint16>(v);
        } else if (a == QStringLiteral("--auth-token") && i + 1 < args.size()) {
            cliToken = args.at(++i);
        } else if (a == QStringLiteral("--config-dir") && i + 1 < args.size()) {
            configDir = args.at(++i);
        } else if (a == QStringLiteral("--instance-id") && i + 1 < args.size()) {
            instanceId = args.at(++i);
        } else if (a == QStringLiteral("--language") && i + 1 < args.size()) {
            ++i;  // main.cpp에서 이미 처리
        } else if (a.startsWith(QStringLiteral("--")) || a.startsWith(QLatin1Char('-'))) {
            if (!kKnownFlags.contains(a)) {
                qCCritical(lcBroadChatErr) << "unknown argument:" << a;
                return 2;
            }
            // known flag with missing value
            qCCritical(lcBroadChatErr) << "argument requires a value:" << a;
            return 2;
        }
    }

    // v69 §5.6.1 7-step fallback chain.
    //   Step 1 --config-dir strict · Step 2 env strict · Step 3 portable · Step 4 user-local
    //   Step 5 exe_dir · Step 6 TMPDIR · Step 7 in-memory (미구현 — exit 3 처리)
    const QString envDir = QProcessEnvironment::systemEnvironment()
                               .value(QStringLiteral("ONIONMIXER_BCC_CONFIG_DIR"));
    const ConfigPathResolver::ResolvedConfigDir resolved =
        ConfigPathResolver::resolveConfigDir(configDir, envDir, instanceId);
    if (resolved.step == 0) {
        // v72 #8 (§5.6.1 Step 7 · §5.6.3): CLI/env strict 실패는 여전히 exit 3.
        // 체인 전체 실패 (tried 2건 이상: user-local+exe_dir+tmp 모두 실패)는 in-memory 전환.
        const bool strictFailed =
            !resolved.tried.isEmpty()
            && (resolved.tried.first().startsWith(QStringLiteral("[step1/"))
                || resolved.tried.first().startsWith(QStringLiteral("[step2/")));
        if (strictFailed) {
            qCCritical(lcBroadChatErr)
                << "config dir (CLI/env explicit) failed — tried:" << resolved.tried;
            return 3;
        }
        // in-memory 모드로 전환 — 모든 저장 no-op, UI 배지 표시.
        m_inMemory = true;
        qCWarning(lcBroadChatWarn)
            << "all config dir candidates failed — running in-memory (no persistence) — tried:"
            << resolved.tried;
        m_configPath.clear();  // loadConfig/saveConfig early return 유도
    } else {
        configDir = resolved.path;
    }
    // v69 §5.6.4: 결정된 경로·단계 고지.
    if (!m_inMemory) {
        qCInfo(lcBroadChat) << "config dir resolved: step=" << resolved.step
                            << "path=" << configDir
                            << "(tried:" << resolved.tried << ")";
        // v69 §5.6.5 #6: user-local(step 4) 로 귀결됐으면 exe_dir 기존 설정 자동 migrate.
        if (resolved.step == 4) {
            const QString newIni = configDir + QStringLiteral("/config.ini");
            if (ConfigPathResolver::maybeMigrateFromExeDir(configDir, instanceId)
                && QFileInfo::exists(newIni)) {
                qCInfo(lcBroadChat)
                    << "migrated config from exe_dir to" << newIni
                    << "(one-time, legacy file preserved)";
            }
        }
        m_configPath = configDir + QStringLiteral("/config.ini");

        // v81: 이전 v68 #8 QLockFile 제거 — 다중 인스턴스 허용 (last-write-wins ini).
        // 사용자가 완전 분리된 설정을 원하면 `--instance-id <name>` 으로 별도 config dir
        // 사용 가능. 서버는 duplicate clientId 를 enforcement 하지 않아 여러 프로세스
        // 동시 접속 허용. 기존 `.lock` 잔존 파일은 무해 — 더 이상 read/write 없음.
    }

    // ini에서 먼저 로드, 그 다음 CLI로 덮어쓰기 (v2-19 CLI 우선순위)
    loadConfig();
    if (!host.isEmpty()) m_host = host;
    if (port != 0) m_port = port;
    if (!cliToken.isEmpty()) m_authToken = cliToken;
    if (m_host.isEmpty()) m_host = QString::fromLatin1(BroadChatEndpoint::kDefaultBindAddress);
    if (m_port == 0) m_port = BroadChatEndpoint::kDefaultTcpPort;

    m_window = std::make_unique<MainBroadcastWindow>(this);
    // v68 #13: --instance-id 를 타이틀 suffix 로
    m_window->setInstanceId(instanceId);
    // v72 #8: in-memory 모드면 ⚠ 배지 상시 표시
    if (m_inMemory) {
        m_window->setInMemoryMode(true);
    }
    // v72 #11: 시작 상태는 Connecting (곧 start() 호출되어 실제 접속 시도)
    m_window->setConnectionState(MainBroadcastWindow::ConnectionState::Connecting);
    connect(m_window.get(), &MainBroadcastWindow::settingsRequested,
            this, &BroadChatClientApp::onSettingsRequested);
    connect(m_window.get(), &MainBroadcastWindow::quitRequested,
            this, &BroadChatClientApp::onQuitRequested);
    // Step 7: geometry 저장 signal 연결
    connect(m_window.get(), &MainBroadcastWindow::windowResized,
            this, &BroadChatClientApp::onWindowResized);
    connect(m_window.get(), &MainBroadcastWindow::windowMoved,
            this, &BroadChatClientApp::onWindowMoved);

    // v73 #14: applySettings 전에 클라 overlay 위치 먼저 동기화.
    // 배지/플레이스홀더 배치가 shared window 렌더 시점에 이미 최신 위치에 있도록.
    m_window->setViewerCountPosition(m_snapshot.broadcastViewerCountPosition);
    // v61 저장된 chat·broadcast 스타일을 창에 적용 (초기 로드 시 1회)
    if (m_window->chatWindow()) {
        m_window->chatWindow()->applySettings(m_snapshot);
    }

    // 저장된 geometry 복원 (show 전에) — applySettings의 resize는 저장된 w/h 반영
    m_window->applyInitialGeometry(m_windowX, m_windowY, m_windowWidth, m_windowHeight);

    m_connection = std::make_unique<BroadChatConnection>(this);
    // v68 #7: persist 된 clientId 를 Connection 에 주입 — 없으면 Connection 내부 UUID 사용.
    if (!m_clientId.isEmpty()) {
        m_connection->setClientId(m_clientId);
    } else {
        // loadConfig 에서 비었던 경우 Connection 이 생성한 id 를 역으로 가져와 persist.
        m_clientId = m_connection->clientId();
        saveConfig();
    }
    connect(m_connection.get(), &BroadChatConnection::helloCompleted,
            this, &BroadChatClientApp::onHelloCompleted);
    connect(m_connection.get(), &BroadChatConnection::disconnected,
            this, &BroadChatClientApp::onDisconnected);
    connect(m_connection.get(), &BroadChatConnection::protocolError,
            this, &BroadChatClientApp::onProtocolError);
    connect(m_connection.get(), &BroadChatConnection::byeReceived,
            this, &BroadChatClientApp::onByeReceived);
    connect(m_connection.get(), &BroadChatConnection::chatReceived,
            this, &BroadChatClientApp::onChatReceived);
    connect(m_connection.get(), &BroadChatConnection::historyChunkReceived,
            this, &BroadChatClientApp::onHistoryChunkReceived);
    connect(m_connection.get(), &BroadChatConnection::emojiImageReceived,
            this, &BroadChatClientApp::onEmojiImageReceived);
    connect(m_connection.get(), &BroadChatConnection::viewersReceived,
            this, &BroadChatClientApp::onViewersReceived);
    connect(m_connection.get(), &BroadChatConnection::platformStatusReceived,
            this, &BroadChatClientApp::onPlatformStatusReceived);

    // Attempt initial connect; reconnect backoff (§6.2) lands in step 8.
    m_connection->start(m_host, m_port, m_authToken);
    return 0;
}

void BroadChatClientApp::show()
{
    if (m_window) {
        m_window->show();
    }
}

void BroadChatClientApp::onHelloCompleted(const QString& serverVersion,
                                          int protocolMin, int protocolMax)
{
    qCInfo(lcBroadChat) << "ready — server" << serverVersion
                        << "proto=[" << protocolMin << "," << protocolMax << "]";
    // Step 8: Active 진입 시점 기록. 10초 유지 시 retry reset (§18.1 v34-11).
    m_activeSince = QDateTime::currentDateTime();
    // v68 #1 (§6.1 v34-10): 매 helloCompleted 이후 1회 자동 request_history.
    // 초기 연결·재연결 모두 동일 경로. dedup 은 messageId 기반 향후 ChatMessageModel 책임 (v35-5).
    if (m_connection) {
        m_pendingHistoryReqId = m_connection->requestHistory(50);
    }
    // v70 #15: tray 상태 업데이트
    // v72 #11: 배지 Connected → 5초 후 fade
    if (m_window) {
        m_window->updateTrayStatus(tr("연결됨"));
        m_window->setConnectionState(MainBroadcastWindow::ConnectionState::Connected);
    }
}

void BroadChatClientApp::onDisconnected()
{
    qCInfo(lcBroadChat) << "connection dropped"
                        << "retryCount=" << m_retryCount
                        << "lastBye=" << m_lastByeReason;

    if (m_shuttingDown) {
        qCInfo(lcBroadChat) << "app shutting down — no reconnect";
        return;
    }
    if (m_terminated) {
        qCInfo(lcBroadChat)
            << "reconnect skipped — protocol terminated (auth_failed/version_mismatch)";
        // v70 #15: 종단 상태 tray 표시
        // v72 #11: Terminated 배지 (빨강)
        if (m_window) {
            m_window->updateTrayStatus(m_lastByeReason == QStringLiteral("auth_failed")
                                           ? tr("인증 실패 — 설정 확인 필요")
                                           : tr("버전 불일치 — 업그레이드 필요"));
            m_window->setConnectionState(MainBroadcastWindow::ConnectionState::Terminated);
        }
        return;
    }

    // v70 #15: 재연결 대기 상태 tray 표시
    // v72 #11: Connecting 배지 (노랑) — 재시도 중
    if (m_window) {
        m_window->updateTrayStatus(tr("재연결 중…"));
        m_window->setConnectionState(MainBroadcastWindow::ConnectionState::Connecting);
    }

    // v34-11 reset 조건: Active 10초 이상 유지 시 retryCount=0
    if (m_activeSince.isValid() &&
        m_activeSince.msecsTo(QDateTime::currentDateTime()) >= 10000) {
        qCInfo(lcBroadChat) << "retry counter reset (active >= 10s)";
        m_retryCount = 0;
    }
    m_activeSince = QDateTime();

    // bye reason별 backoff 또는 일반 backoff
    int delayMs = m_lastByeReason.isEmpty()
                      ? computeBackoffMs(m_retryCount)
                      : backoffMsForByeReason(m_lastByeReason);
    if (delayMs < 0) {
        // 재시도 중단 (auth_failed·version_mismatch) — byeReceived에서 이미 terminated 처리됨
        return;
    }
    scheduleReconnect(delayMs);
}

void BroadChatClientApp::onProtocolError(const QString& detail)
{
    qCCritical(lcBroadChatErr) << "protocol error:" << detail;
    // protocol_error는 자체 감지 — byeReceived 경로로도 동일 reason 올 수 있음
    m_lastByeReason = QStringLiteral("protocol_error");
}

void BroadChatClientApp::onByeReceived(const QString& reason,
                                       const QString& detail)
{
    qCInfo(lcBroadChat) << "server bye: reason=" << reason << "detail=" << detail;
    m_lastByeReason = reason;

    // §18.1 Terminated 상태: auth_failed·version_mismatch
    if (reason == QStringLiteral("auth_failed") ||
        reason == QStringLiteral("version_mismatch")) {
        m_terminated = true;
        showTerminationDialog(reason, detail);
    }
}

void BroadChatClientApp::onHistoryChunkReceived(
    const QString& requestId, const QVector<UnifiedChatMessage>& messages,
    bool hasMore)
{
    // v34-10: 첫 chunk 만 사용. 추가 백필은 v1 비지원.
    // requestId 매칭으로 stale 응답 (이전 연결의 late delivery) 필터.
    if (!m_pendingHistoryReqId.isEmpty() && requestId != m_pendingHistoryReqId) {
        qCDebug(lcBroadChatTrace) << "history_chunk ignored (stale reqId)"
                                  << "got=" << requestId
                                  << "pending=" << m_pendingHistoryReqId;
        return;
    }
    m_pendingHistoryReqId.clear();
    qCInfo(lcBroadChat) << "applying history: count=" << messages.size()
                        << "hasMore=" << hasMore;
    // 역방향 삽입: 서버가 내림차순(최신부터)으로 보내는 경우 UI append 는 오름차순이 자연.
    // BroadChatServer::handleRequestHistory 구현은 오름차순(startRow→total)으로 보냄 — 그대로 append.
    for (const UnifiedChatMessage& m : messages) {
        if (m_window) m_window->appendChat(m);
    }
}

void BroadChatClientApp::onChatReceived(const UnifiedChatMessage& message)
{
    m_window->appendChat(message);

    // v5-12: richText 내 미해결 이모지 id를 찾아 request_emoji 송신.
    // 실패 이력이 있는 id는 60초 backoff (v11.2 v12-15).
    EmojiImageCache* cache = m_window->emojiCache();
    const QDateTime now = QDateTime::currentDateTime();
    for (const ChatEmojiInfo& e : message.emojis) {
        if (e.emojiId.isEmpty()) continue;
        if (cache->contains(e.emojiId)) continue;

        const auto failedAt = m_emojiFailedAt.constFind(e.emojiId);
        if (failedAt != m_emojiFailedAt.constEnd() &&
            failedAt.value().msecsTo(now) < kEmojiFailureBackoffMs) {
            continue;
        }
        m_connection->requestEmoji(e.emojiId);
    }
}

void BroadChatClientApp::onEmojiImageReceived(const QString& emojiId,
                                               const QByteArray& bytes,
                                               const QString& mime,
                                               const QString& error)
{
    if (emojiId.isEmpty()) return;

    if (!error.isEmpty() || bytes.isEmpty()) {
        qCWarning(lcBroadChatWarn)
            << "emoji request failed:" << emojiId << "error=" << error;
        m_emojiFailedAt.insert(emojiId, QDateTime::currentDateTime());
        return;
    }

    EmojiImageCache* cache = m_window->emojiCache();
    cache->setImage(emojiId, bytes, mime);

    // 이모지 검토 R7-A: shared lib `setImage`는 `QPixmap::loadFromData` 실패 시
    // silent no-op — 손상 base64·지원 안 하는 MIME·0 크기 이미지 등. 이 경우
    // cache에 entry가 없는데 `m_emojiFailedAt`도 clear되면 다음 chat마다 같은 id
    // 무한 재요청이 발생함. setImage 후 contains() 검증으로 실패 시 backoff 기록.
    if (!cache->contains(emojiId)) {
        qCWarning(lcBroadChatWarn)
            << "emoji image decode failed (corrupt bytes or unsupported mime):"
            << emojiId << "mime=" << mime << "size=" << bytes.size();
        m_emojiFailedAt.insert(emojiId, QDateTime::currentDateTime());
        return;
    }
    m_emojiFailedAt.remove(emojiId);
}

void BroadChatClientApp::onViewersReceived(int youtube, int chzzk, int /*total*/)
{
    // v82: per-update trace 제거 — viewer 카운터는 UI 오버레이에서 직접 확인.
    m_window->setViewerCount(youtube, chzzk);
}

void BroadChatClientApp::onPlatformStatusReceived(const QJsonObject& /*data*/)
{
    // v82: 주기적 platform_status 마다 trace 찍던 로그 제거 — 초당 수회 발생해 콘솔 점령.
    // 현재 구현에서 data 자체로 수행할 UI 업데이트가 없어 slot 자체는 no-op 유지.
    // 향후 상태 배지 반영 (§8.3) 구현 시 재작성.
}

void BroadChatClientApp::onAboutToQuit()
{
    // Step 8: 종료 중에는 더 이상 재연결 시도 금지.
    // `m_shuttingDown` 은 정상 앱 종료 — 프로토콜 Terminated(`m_terminated`)와 구분.
    qCInfo(lcBroadChat) << "aboutToQuit fired — initiating graceful shutdown";
    if (m_reconnectTimer) m_reconnectTimer->stop();
    m_shuttingDown = true;
    if (m_connection) {
        m_connection->stop();
    }
    saveConfig();
}

void BroadChatClientApp::onSettingsRequested()
{
    // FU-J1 (v51-2): 이미 열린 dialog 있으면 raise만 — 중복 생성 방지.
    if (m_configDialog) {
        m_configDialog->raise();
        m_configDialog->activateWindow();
        return;
    }
    // v62: dialog parent = BroadcastChatWindow → 메인 창 하위 윈도우로 처리 → 닫힐 때
    // 상위 창이 영향 받지 않음. quitOnLastWindowClosed와 무관하게 독립 동작.
    QWidget* parentWin = m_window ? static_cast<QWidget*>(m_window->chatWindow())
                                  : nullptr;
    auto* dlg = new ClientConfigDialog(parentWin);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    m_configDialog = dlg;                       // QPointer — close 시 자동 null
    dlg->setValues(m_snapshot, m_host, m_port, m_authToken);
    // v72 #8: in-memory 모드면 Apply inline 경고
    dlg->setInMemoryMode(m_inMemory);
    connect(dlg, &ClientConfigDialog::valuesAccepted, this,
            [this](const AppSettingsSnapshot& snap, const QString& host,
                   quint16 port, const QString& token) {
        const bool netChanged = (m_host != host) || (m_port != port)
                                || (m_authToken != token);

        // v61: chat·broadcast 스타일 즉시 반영 (§16.20 Apply 라우팅 — live 필드)
        m_snapshot = snap;
        // v66: 방송창 크기(snapshot.broadcastWindow*) → m_window* 필드 동기화.
        // applySettings 내부 resize() 가 windowResized signal 을 발동시켜 onWindowResized
        // 에서 다시 저장되지만, 본 반영 직후 saveConfig() 이 이 값을 ini 에 쓰려면 사전 동기 필요.
        m_windowWidth = snap.broadcastWindowWidth;
        m_windowHeight = snap.broadcastWindowHeight;
        // v73 #14: viewer position 변경을 overlay 계층에도 전파 (applySettings 이전).
        if (m_window) {
            m_window->setViewerCountPosition(m_snapshot.broadcastViewerCountPosition);
        }
        if (m_window && m_window->chatWindow()) {
            m_window->chatWindow()->applySettings(m_snapshot);
        }

        // Connection 변경은 재연결 필요
        m_host = host;
        m_port = port;
        m_authToken = token;
        saveConfig();
        if (netChanged && m_connection) {
            qCInfo(lcBroadChat) << "connection settings changed — reconnecting to"
                                << m_host << ":" << m_port;
            m_reconnectTimer->stop();
            m_retryCount = 0;
            m_terminated = false;
            m_lastByeReason.clear();
            m_connection->stop();
            m_connection->start(m_host, m_port, m_authToken);
        }
    });
    dlg->show();
}

void BroadChatClientApp::onQuitRequested()
{
    if (m_app) m_app->quit();
}

void BroadChatClientApp::loadConfig()
{
    if (m_configPath.isEmpty()) return;
    // v64: ini 파일이 없어도 QSettings는 정상 동작(모든 read가 default 반환).
    // healing 로직이 반드시 실행되도록 early return 조건 간소화 — 파일 미존재도
    // "모든 필드 healing 필요" 케이스로 처리되어 defaults 주입 후 saveConfig 됨.
    const bool iniExisted = QFileInfo::exists(m_configPath);

    // v68 #3 (§10 v48-17): 파손된 ini 감지 시 .broken.ini 로 백업 후 기본값 재생성.
    // QSettings는 파손 시 status() = FormatError, 내용은 빈 값. 원본을 보존해 운영자가 진단 가능.
    if (iniExisted) {
        QSettings probe(m_configPath, QSettings::IniFormat);
        if (probe.status() != QSettings::NoError) {
            const QString brokenPath = m_configPath + QStringLiteral(".broken");
            QFile::remove(brokenPath);  // 이전 broken 백업은 덮어씀
            if (QFile::rename(m_configPath, brokenPath)) {
                qCWarning(lcBroadChatWarn)
                    << "config.ini parse failed (status=" << probe.status() << ")"
                    << "— backed up to" << brokenPath << "+ regenerating defaults";
            } else {
                qCCritical(lcBroadChatErr)
                    << "config.ini parse failed but backup rename also failed for"
                    << m_configPath;
            }
        }
    }

    QSettings s(m_configPath, QSettings::IniFormat);

    // v68 #4 (§5.6.9 v57-2): schema_version 검사. 미래 버전 감지 시 경고만, best-effort 로드.
    s.beginGroup(QStringLiteral("meta"));
    const int schema = s.value(QStringLiteral("schema_version"), 0).toInt();
    s.endGroup();
    if (schema > kIniSchemaVersion) {
        qCWarning(lcBroadChatWarn)
            << "config.ini schema_version=" << schema
            << "is newer than supported" << kIniSchemaVersion
            << "— loading best-effort, unknown keys may be lost on save";
    }

    s.beginGroup(QStringLiteral("connection"));
    m_host = s.value(QStringLiteral("host"), QString()).toString().trimmed();
    m_port = static_cast<quint16>(s.value(QStringLiteral("port"), 0).toInt());
    m_authToken = s.value(QStringLiteral("auth_token"), QString()).toString().trimmed();
    s.endGroup();

    // v68 #7 (§16.9 Critical): clientId persist. 없으면 Connection 생성 후 역주입.
    s.beginGroup(QStringLiteral("identity"));
    m_clientId = s.value(QStringLiteral("client_id"), QString()).toString().trimmed();
    s.endGroup();

    // v74 [window] — 원시값 로드. width/height range 검증은 IniValidator 가 처리
    // (snapshot 쪽 broadcastWindowWidth/Height 와 동일 heal). x/y 는 화면 clamp 가 Qt 몫.
    s.beginGroup(QStringLiteral("window"));
    m_windowX = s.value(QStringLiteral("x"), -1).toInt();
    m_windowY = s.value(QStringLiteral("y"), -1).toInt();
    m_snapshot.broadcastWindowWidth = s.value(QStringLiteral("width"), 400).toInt();
    m_snapshot.broadcastWindowHeight = s.value(QStringLiteral("height"), 600).toInt();
    s.endGroup();

    // v74 [chat] — 원시값 로드. range 검증은 IniValidator::healSnapshot 이 일괄 처리.
    s.beginGroup(QStringLiteral("chat"));
    m_snapshot.chatFontFamily = s.value(QStringLiteral("font_family"), QString()).toString();
    m_snapshot.chatFontSize = s.value(QStringLiteral("font_size"), 11).toInt();
    m_snapshot.chatFontBold = s.value(QStringLiteral("font_bold"), false).toBool();
    m_snapshot.chatFontItalic = s.value(QStringLiteral("font_italic"), false).toBool();
    m_snapshot.chatLineSpacing = s.value(QStringLiteral("line_spacing"), 3).toInt();
    m_snapshot.chatMaxMessages = s.value(QStringLiteral("max_messages"), 5000).toInt();
    s.endGroup();

    // v74 [broadcast] — 원시 값 그대로 로드. 검증·fallback 은 IniValidator 가 일괄 처리.
    s.beginGroup(QStringLiteral("broadcast"));
    m_snapshot.broadcastViewerCountPosition =
        s.value(QStringLiteral("viewer_count_position"), QString()).toString();
    m_snapshot.broadcastTransparentBgColor =
        s.value(QStringLiteral("transparent_bg_color"), QString()).toString();
    m_snapshot.broadcastOpaqueBgColor =
        s.value(QStringLiteral("opaque_bg_color"), QString()).toString();
    m_snapshot.broadcastChatBodyFontColor =
        s.value(QStringLiteral("chat_body_font_color"), QString()).toString();
    m_snapshot.broadcastChatOutlineColor =
        s.value(QStringLiteral("chat_outline_color"), QString()).toString();
    s.endGroup();

    // v74 healing — IniValidator 모듈로 전필드 검증 (v64 isEmpty 확장 → isValid).
    // 색상은 QColor::isValid (파싱 실패·garbage 문자열 포함), viewer_count_position 은
    // 9종 enum, 숫자는 range 검사. 비어있지 않지만 invalid 한 값 (예: `#ZZZZ`·`blue`)
    // 도 교체되어 ChatBubbleDelegate fallback `#111111` (dark gray, 투명 모드 + 어두운
    // desktop 에서 불가시) 를 확실히 회피.
    //
    // connection 필드 (host/port/auth_token) 도 함께 검증 — port<1024 이거나 host 가
    // 공백만인 경우 기본값 주입.
    const bool snapshotHealed = IniValidator::healSnapshot(m_snapshot);

    IniValidator::ConnectionFields conn{m_host, m_port, m_authToken};
    const bool connectionHealed = IniValidator::healConnection(conn);
    m_host = conn.host;
    m_port = conn.port;
    m_authToken = conn.authToken;

    // v74 #신규: ini 파일이 없으면 첫 실행으로 간주하고 **무조건 생성**.
    // iniExisted=false → 모든 필드가 defaults (QSettings 미존재 시 default 반환).
    // validator 통과해도 heal 트리거해 파일 생성을 보장. 사용자가 실행 디렉터리/
    // config dir 에 ini 가 없는 상태에서도 다음 실행부터는 기본 설정이 배치됨.
    const bool heal = snapshotHealed || connectionHealed || !iniExisted;
    if (heal) {
        if (!iniExisted) {
            qCInfo(lcBroadChat)
                << "config.ini not found — creating defaults at" << m_configPath;
        } else {
            qCInfo(lcBroadChat)
                << "config healed — snapshotHealed=" << snapshotHealed
                << "connectionHealed=" << connectionHealed;
        }
        saveConfig();
        // saveConfig 후 실제 파일 존재 확인 — 실패 시 운영자 진단 가능하도록 critical 로그.
        if (!QFileInfo::exists(m_configPath)) {
            qCCritical(lcBroadChatErr)
                << "config save failed — expected file not present:" << m_configPath;
        }
    }

    // v74: IniValidator 가 이미 broadcastWindowWidth/Height 를 healed snapshot 에 기록.
    // m_windowWidth/Height 로 미러링 (applyInitialGeometry·onWindowResized 에서 사용).
    m_windowWidth = m_snapshot.broadcastWindowWidth;
    m_windowHeight = m_snapshot.broadcastWindowHeight;
    m_snapshot.broadcastWindowX = m_windowX;
    m_snapshot.broadcastWindowY = m_windowY;
}

void BroadChatClientApp::saveConfig()
{
    if (m_configPath.isEmpty()) return;
    QSettings s(m_configPath, QSettings::IniFormat);

    // v68 #4: schema_version 태그
    s.beginGroup(QStringLiteral("meta"));
    s.setValue(QStringLiteral("schema_version"), kIniSchemaVersion);
    s.endGroup();

    s.beginGroup(QStringLiteral("connection"));
    s.setValue(QStringLiteral("host"), m_host);
    s.setValue(QStringLiteral("port"), m_port);
    s.setValue(QStringLiteral("auth_token"), m_authToken);
    s.endGroup();

    // v68 #7: clientId persist
    s.beginGroup(QStringLiteral("identity"));
    s.setValue(QStringLiteral("client_id"), m_clientId);
    s.endGroup();

    // Step 7: [window] 섹션
    s.beginGroup(QStringLiteral("window"));
    s.setValue(QStringLiteral("x"), m_windowX);
    s.setValue(QStringLiteral("y"), m_windowY);
    s.setValue(QStringLiteral("width"), m_windowWidth);
    s.setValue(QStringLiteral("height"), m_windowHeight);
    s.endGroup();

    // v61 [chat] 섹션
    s.beginGroup(QStringLiteral("chat"));
    s.setValue(QStringLiteral("font_family"), m_snapshot.chatFontFamily);
    s.setValue(QStringLiteral("font_size"), m_snapshot.chatFontSize);
    s.setValue(QStringLiteral("font_bold"), m_snapshot.chatFontBold);
    s.setValue(QStringLiteral("font_italic"), m_snapshot.chatFontItalic);
    s.setValue(QStringLiteral("line_spacing"), m_snapshot.chatLineSpacing);
    s.setValue(QStringLiteral("max_messages"), m_snapshot.chatMaxMessages);
    s.endGroup();

    // v61 [broadcast] 섹션
    s.beginGroup(QStringLiteral("broadcast"));
    s.setValue(QStringLiteral("viewer_count_position"),
               m_snapshot.broadcastViewerCountPosition);
    s.setValue(QStringLiteral("transparent_bg_color"),
               m_snapshot.broadcastTransparentBgColor);
    s.setValue(QStringLiteral("opaque_bg_color"),
               m_snapshot.broadcastOpaqueBgColor);
    s.setValue(QStringLiteral("chat_body_font_color"),
               m_snapshot.broadcastChatBodyFontColor);
    s.setValue(QStringLiteral("chat_outline_color"),
               m_snapshot.broadcastChatOutlineColor);
    s.endGroup();

    s.sync();
    // v68 #9 (§16.11 Critical): sync 후 status 검사 — FormatError/AccessError 시 경고.
    // QSettings 자체가 완전 atomic rename 보장하지 않으므로 검사만으로 최소 방어.
    if (s.status() != QSettings::NoError) {
        qCCritical(lcBroadChatErr)
            << "config save failed: status=" << s.status()
            << "path=" << m_configPath;
    }
    // v21-γ-8 0600 권한
    QFile::setPermissions(m_configPath, QFile::ReadOwner | QFile::WriteOwner);
}

void BroadChatClientApp::onWindowResized(int width, int height)
{
    m_windowWidth = width;
    m_windowHeight = height;
    m_snapshot.broadcastWindowWidth = width;
    m_snapshot.broadcastWindowHeight = height;
    saveConfig();
}

void BroadChatClientApp::onWindowMoved(int x, int y)
{
    m_windowX = x;
    m_windowY = y;
    m_snapshot.broadcastWindowX = x;
    m_snapshot.broadcastWindowY = y;
    saveConfig();
}

// Step 8: 재연결 상태 머신 helpers (§18.1 · §5.2 v37-6 backoff matrix)

void BroadChatClientApp::scheduleReconnect(int delayMs)
{
    ++m_retryCount;
    qCInfo(lcBroadChat) << "reconnect scheduled in" << delayMs
                        << "ms (retry #" << m_retryCount << ")";
    m_reconnectTimer->start(delayMs);
}

int BroadChatClientApp::computeBackoffMs(int retryCount) const
{
    // §5.2 v2-9: 1 → 2 → 4 → 8 → 16 → 30s cap
    const int seconds = qMin(1 << qMin(retryCount, 5), 30);
    return seconds * 1000;
}

int BroadChatClientApp::backoffMsForByeReason(const QString& reason) const
{
    // §5.2 v37-6 매트릭스
    if (reason == QStringLiteral("protocol_error")) return 30 * 1000;
    if (reason == QStringLiteral("duplicate_client_id")) return 60 * 1000;
    if (reason == QStringLiteral("too_many_clients")) return 30 * 1000;
    if (reason == QStringLiteral("auth_failed")) return -1;       // terminated
    if (reason == QStringLiteral("version_mismatch")) return -1;  // terminated
    // shutdown·disconnect·timeout·normal·unknown → 일반 backoff
    return computeBackoffMs(m_retryCount);
}

void BroadChatClientApp::showTerminationDialog(const QString& reason,
                                                const QString& detail)
{
    QString title;
    QString body;
    if (reason == QStringLiteral("auth_failed")) {
        title = tr("인증 실패");
        body = tr("서버 인증 토큰 불일치. 설정 창에서 토큰을 수정한 뒤 재시도하거나 종료하세요.");
    } else if (reason == QStringLiteral("version_mismatch")) {
        title = tr("버전 불일치");
        body = tr("서버와 클라이언트 프로토콜 버전이 호환되지 않습니다. 업그레이드가 필요합니다.");
    } else {
        title = tr("연결 중단");
        body = tr("서버와 연결이 영구 중단되었습니다 (사유: %1).").arg(reason);
    }
    if (!detail.isEmpty()) {
        body += QStringLiteral("\n\n") + detail;
    }

    QMessageBox box(QMessageBox::Warning, title, body,
                    QMessageBox::NoButton, nullptr);
    QAbstractButton* editBtn = box.addButton(tr("설정 수정"), QMessageBox::AcceptRole);
    box.addButton(tr("종료"), QMessageBox::RejectRole);
    box.exec();

    if (box.clickedButton() == editBtn) {
        // Terminated 해제 — 사용자가 설정 수정 후 수동으로 reconnect 유도
        m_terminated = false;
        m_retryCount = 0;
        m_lastByeReason.clear();
        onSettingsRequested();
    } else {
        if (m_app) m_app->quit();
    }
}

void BroadChatClientApp::onReconnectTimerFired()
{
    if (m_shuttingDown || m_terminated) return;
    if (!m_connection) return;
    qCInfo(lcBroadChat) << "reconnect firing: host=" << m_host
                        << "port=" << m_port;
    m_lastByeReason.clear();                 // 다음 disconnect 진단을 위해 초기화
    m_connection->start(m_host, m_port, m_authToken);
}
