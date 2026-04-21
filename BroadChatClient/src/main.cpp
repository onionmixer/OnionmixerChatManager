#include "BroadChatClientApp.h"
#include "i18n/ClientLanguage.h"            // v65 클라 전용 translator (메인 앱 AppLanguage 와 분리)
#include "shared/BroadChatProtocol.h"
#include "version.h"                        // FU-K13 git-describe 생성

#include "core/AppTypes.h"

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QFileInfo>
#include <QLocale>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QString>

#include <cstdio>
#include <cstring>

namespace {

// Step 9 v2-18: --version·--help는 QApplication 생성 전 즉시 처리 → exit 0.
// GUI 창 열지 않고 stdout으로만 출력.

void printVersion()
{
    // FU-K13 · v52-19: git hash + build date 포함
    std::printf("OnionmixerBroadChatClient %s (%s) proto=%d built=%s\n",
                BroadChatClientVersion::kVersion,
                BroadChatClientVersion::kGitHash,
                BroadChatProtocol::kProtocolVersion,
                BroadChatClientVersion::kBuildDate);
}

void printHelp()
{
    std::printf(
        "Usage: OnionmixerBroadChatClient [OPTIONS]\n"
        "\n"
        "OPTIONS:\n"
        "  --host <addr>         Server host (IPv4/IPv6/hostname). Default: 127.0.0.1\n"
        "  --port <n>            Server TCP port (1024-65535). Default: 47123\n"
        "  --auth-token <str>    Pre-shared token if server requires auth.\n"
        "                        WARNING: visible in shell history/process list.\n"
        "  --config-dir <path>   Explicit config directory (see fallback chain).\n"
        "  --language <code>     UI language (ko_KR, en_US, ja_JP). Default: system locale.\n"
        "  --version             Print version and exit.\n"
        "  --help                Print this help and exit.\n"
        "\n"
        "EXIT CODES:\n"
        "  0  Normal exit (user quit, auth_failed/version_mismatch \"종료\" button)\n"
        "  1  Runtime error (config load failed, init failure)\n"
        "  2  CLI parse error (unknown arg, invalid port)\n"
        "  3  Environment error (instance-id lock, explicit config dir failed)\n"
        "\n"
        "Examples:\n"
        "  OnionmixerBroadChatClient\n"
        "  OnionmixerBroadChatClient --host 192.168.1.10 --port 47123\n"
        "  OnionmixerBroadChatClient --config-dir ~/.config/onionmixer-bcc/obs\n"
        "\n"
        "See PLAN_DEV_BROADCHATCLIENT.md for full protocol and configuration details.\n");
}

// Returns true if early-exit handled (caller should return code).
// sets *exitCode when returning true.
bool handleEarlyArgs(int argc, char** argv, int* exitCode)
{
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--version") == 0) {
            printVersion();
            *exitCode = 0;
            return true;
        }
        if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            printHelp();
            *exitCode = 0;
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    // Step 9 v2-18: 조기 파싱 — QApplication 없이 처리 가능한 CLI.
    int earlyExit = 0;
    if (handleEarlyArgs(argc, argv, &earlyExit)) {
        return earlyExit;
    }

    // FU-M4 (v54-6): HighDPI awareness. QApplication 생성 이전에 attribute 설정.
    // Qt 5.14+에서 `AA_EnableHighDpiScaling` 기본 on이나 명시로 명확화.
    // `UseHighDpiPixmaps`은 24x24 이모지 pixmap 등 고해상도 처리.
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);

    // v77: v62 `AA_DontUseNativeDialogs=true` 제거 — 메인 앱과 동일하게 native color dialog
    // (KDE·GNOME·macOS·Windows OS 팔레트) 허용. 사용자 요구 "메인 앱 팔레트 이식" 본질은
    // native dialog 였음. modal dialog 는 QEventLoop 를 돌리므로 socket readyRead 는 정상
    // 처리됨 — 메인 앱 자체가 동일 환경에서 native dialog + TCP 서버 문제 없이 구동 중이므로
    // v62 가 걱정한 ping/pong 지연은 pre-emptive 보호였으며 실 재현 사례 없음. 재발 시 선택적
    // 복구 가능.
    QApplication app(argc, argv);
    qSetMessagePattern(QStringLiteral(
        "%{if-category}[%{category}] %{endif}%{message}"));

    // v62: 설정 다이얼로그·창 close 이벤트가 연쇄로 app 종료를 트리거하지 않도록
    // lastWindowClosed 자동 quit 비활성화. 종료는 우클릭 "종료" 메뉴 · SIGTERM 만 허용.
    app.setQuitOnLastWindowClosed(false);

    // v25 i18n: CLI --language > config.ini [app] language > QLocale::system() > "ko_KR"
    // v69 §5.6.1: BroadChatClientApp 이 최종 config dir 결정 전이지만, 언어 로드는
    // QApplication 생성 직후 필요 (translator install 시점). 같은 우선순위를 축약 재현:
    //   CLI --config-dir → env ONIONMIXER_BCC_CONFIG_DIR → user-local (step 4) → exe_dir (step 5).
    {
        QString language;
        QString cliConfigDir;
        QString instanceId;
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--language") == 0 && i + 1 < argc) {
                language = QString::fromLatin1(argv[++i]);
            } else if (std::strcmp(argv[i], "--config-dir") == 0 && i + 1 < argc) {
                cliConfigDir = QString::fromLocal8Bit(argv[++i]);
            } else if (std::strcmp(argv[i], "--instance-id") == 0 && i + 1 < argc) {
                instanceId = QString::fromLocal8Bit(argv[++i]);
            }
        }
        if (language.isEmpty()) {
            const QString bucket =
                instanceId.isEmpty() ? QStringLiteral("default") : instanceId;
            const QString envDir = QProcessEnvironment::systemEnvironment()
                                       .value(QStringLiteral("ONIONMIXER_BCC_CONFIG_DIR"));
            QStringList candidates;
            if (!cliConfigDir.isEmpty()) candidates << cliConfigDir;
            else if (!envDir.isEmpty()) candidates << envDir;
            else {
#if defined(Q_OS_LINUX)
                const QString base =
                    QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
                if (!base.isEmpty())
                    candidates << base + QStringLiteral("/onionmixer-bcc/") + bucket;
#else
                const QString base =
                    QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
                if (!base.isEmpty()) candidates << base + QLatin1Char('/') + bucket;
#endif
                const QString exeDir = QCoreApplication::applicationDirPath();
                candidates << exeDir + QStringLiteral("/BroadChatClient/") + bucket;
            }
            for (const QString& dir : candidates) {
                const QString iniPath = dir + QStringLiteral("/config.ini");
                if (!QFileInfo::exists(iniPath)) continue;
                QSettings s(iniPath, QSettings::IniFormat);
                s.beginGroup(QStringLiteral("app"));
                language = s.value(QStringLiteral("language")).toString().trimmed();
                s.endGroup();
                if (!language.isEmpty()) break;
            }
        }
        if (language.isEmpty()) {
            language = QLocale::system().name(); // e.g. "ko_KR"
        }
        // v65: 클라 전용 translator (onionmixerbroadchatclient_<locale>.qm)
        // 메인 앱의 AppLanguage (onionmixerchatmanager_*.qm) 와 완전 분리 — drift 방지.
        BroadChatClientLanguage::applyLanguage(app, language);
    }

    // v2-8 defensive: same-thread DirectConnection은 사실 불필요하나
    // 추후 worker-thread 도입 대비 명시 등록.
    qRegisterMetaType<UnifiedChatMessage>();
    qRegisterMetaType<QVector<UnifiedChatMessage>>();
    qRegisterMetaType<ChatEmojiInfo>();
    qRegisterMetaType<QVector<ChatEmojiInfo>>();

    BroadChatClientApp orchestrator(&app);
    // v68 #10 (§16.12 Critical): initialize 반환값이 exit code. 0=성공, 1=runtime, 2=CLI 파싱, 3=환경.
    const int initResult = orchestrator.initialize(argc, argv);
    if (initResult != 0) {
        return initResult;
    }
    orchestrator.show();

    return app.exec();
}
