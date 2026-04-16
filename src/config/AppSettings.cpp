#include "config/AppSettings.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFontDatabase>
#include <QSettings>
#include <utility>

namespace {
PlatformSettings loadPlatform(QSettings& s, const QString& group)
{
    PlatformSettings ps;
    s.beginGroup(group);
    ps.enabled = s.value(QStringLiteral("enabled"), false).toBool();
    ps.clientId = s.value(QStringLiteral("client_id")).toString();
    ps.clientSecret = s.value(QStringLiteral("client_secret")).toString();
    ps.redirectUri = s.value(QStringLiteral("redirect_uri")).toString();
    ps.authEndpoint = s.value(QStringLiteral("auth_endpoint")).toString();
    ps.tokenEndpoint = s.value(QStringLiteral("token_endpoint")).toString();
    ps.scope = s.value(QStringLiteral("scope")).toString();
    ps.channelId = s.value(QStringLiteral("channel_id")).toString();
    ps.channelName = s.value(QStringLiteral("channel_name")).toString();
    ps.accountLabel = s.value(QStringLiteral("account_label")).toString();
    ps.liveVideoIdOverride = s.value(QStringLiteral("live_video_id_override")).toString();
    s.endGroup();
    return ps;
}

void savePlatform(QSettings& s, const QString& group, const PlatformSettings& ps)
{
    s.beginGroup(group);
    s.setValue(QStringLiteral("enabled"), ps.enabled);
    s.setValue(QStringLiteral("client_id"), ps.clientId);
    s.setValue(QStringLiteral("client_secret"), ps.clientSecret);
    s.setValue(QStringLiteral("redirect_uri"), ps.redirectUri);
    s.setValue(QStringLiteral("auth_endpoint"), ps.authEndpoint);
    s.setValue(QStringLiteral("token_endpoint"), ps.tokenEndpoint);
    s.setValue(QStringLiteral("scope"), ps.scope);
    s.setValue(QStringLiteral("channel_id"), ps.channelId);
    s.setValue(QStringLiteral("channel_name"), ps.channelName);
    s.setValue(QStringLiteral("account_label"), ps.accountLabel);
    s.setValue(QStringLiteral("live_video_id_override"), ps.liveVideoIdOverride);
    s.endGroup();
}

PlatformSettings defaultYouTube()
{
    PlatformSettings s;
    s.enabled = false;
    s.redirectUri = QStringLiteral("http://127.0.0.1:18080/youtube/callback");
    s.authEndpoint = QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth");
    s.tokenEndpoint = QStringLiteral("https://oauth2.googleapis.com/token");
    s.scope = QStringLiteral("https://www.googleapis.com/auth/youtube.readonly https://www.googleapis.com/auth/youtube.force-ssl");
    return s;
}

PlatformSettings defaultChzzk()
{
    PlatformSettings s;
    s.enabled = false;
    s.redirectUri = QStringLiteral("http://127.0.0.1:18081/chzzk/callback");
    s.authEndpoint = QStringLiteral("https://chzzk.example.com/oauth/authorize");
    s.tokenEndpoint = QStringLiteral("https://chzzk.example.com/oauth/token");
    s.scope = QStringLiteral("유저 정보 조회 채팅 메시지 조회 채팅 메시지 전송");
    return s;
}
} // namespace

AppSettings::AppSettings(QString iniPath)
    : m_iniPath(std::move(iniPath))
{
}

QString AppSettings::resolveConfigDir(const QStringList& args)
{
    for (int i = 0; i < args.size() - 1; ++i) {
        if (args.at(i) == QStringLiteral("--config-dir")) {
            const QString candidate = args.at(i + 1).trimmed();
            if (!candidate.isEmpty() && QDir(candidate).exists()) {
                return QDir(candidate).absolutePath();
            }
        }
    }

    const QString envDir = QString::fromUtf8(qgetenv("BOTMANAGER_CONFIG_DIR")).trimmed();
    if (!envDir.isEmpty() && QDir(envDir).exists()) {
        return QDir(envDir).absolutePath();
    }

    return QCoreApplication::applicationDirPath() + QStringLiteral("/config");
}

AppSettingsSnapshot AppSettings::load() const
{
    QSettings s(m_iniPath, QSettings::IniFormat);
    AppSettingsSnapshot snapshot;

    s.beginGroup(QStringLiteral("app"));
    snapshot.language = s.value(QStringLiteral("language"), QStringLiteral("ko_KR")).toString();
    snapshot.logLevel = s.value(QStringLiteral("log_level"), QStringLiteral("info")).toString();
    snapshot.mergeOrder = s.value(QStringLiteral("merge_order"), QStringLiteral("timestamp")).toString();
    snapshot.autoReconnect = s.value(QStringLiteral("auto_reconnect"), true).toBool();
    snapshot.detailLogEnabled = s.value(QStringLiteral("detail_log_enabled"), false).toBool();
    snapshot.chatFontFamily = s.value(QStringLiteral("chat_font_family")).toString().trimmed();
    if (!snapshot.chatFontFamily.isEmpty()) {
        const QStringList available = QFontDatabase().families();
        bool found = false;
        for (const QString& f : available) {
            if (f.compare(snapshot.chatFontFamily, Qt::CaseInsensitive) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            snapshot.chatFontFamily.clear();
        }
    }
    snapshot.chatFontSize = s.value(QStringLiteral("chat_font_size"), 11).toInt();
    snapshot.chatFontBold = s.value(QStringLiteral("chat_font_bold"), false).toBool();
    snapshot.chatFontItalic = s.value(QStringLiteral("chat_font_italic"), false).toBool();
    snapshot.chatLineSpacing = s.value(QStringLiteral("chat_line_spacing"), 3).toInt();
    snapshot.chatMaxMessages = s.value(QStringLiteral("chat_max_messages"), 5000).toInt();
    s.endGroup();

    snapshot.youtube = loadPlatform(s, QStringLiteral("youtube"));
    snapshot.chzzk = loadPlatform(s, QStringLiteral("chzzk"));

    if (snapshot.youtube.redirectUri.isEmpty()) {
        snapshot.youtube.redirectUri = defaultYouTube().redirectUri;
    }
    if (snapshot.youtube.scope.isEmpty()) {
        snapshot.youtube.scope = defaultYouTube().scope;
    }
    if (snapshot.youtube.authEndpoint.isEmpty()) {
        snapshot.youtube.authEndpoint = defaultYouTube().authEndpoint;
    }
    if (snapshot.youtube.tokenEndpoint.isEmpty()) {
        snapshot.youtube.tokenEndpoint = defaultYouTube().tokenEndpoint;
    }
    if (snapshot.chzzk.redirectUri.isEmpty()) {
        snapshot.chzzk.redirectUri = defaultChzzk().redirectUri;
    }
    if (snapshot.chzzk.scope.isEmpty()) {
        snapshot.chzzk.scope = defaultChzzk().scope;
    }
    if (snapshot.chzzk.authEndpoint.isEmpty()) {
        snapshot.chzzk.authEndpoint = defaultChzzk().authEndpoint;
    }
    if (snapshot.chzzk.tokenEndpoint.isEmpty()) {
        snapshot.chzzk.tokenEndpoint = defaultChzzk().tokenEndpoint;
    }

    s.beginGroup(QStringLiteral("broadcast"));
    snapshot.broadcastViewerCountPosition = s.value(QStringLiteral("viewer_count_position"), QStringLiteral("TopLeft")).toString();
    snapshot.broadcastWindowX = s.value(QStringLiteral("window_x"), -1).toInt();
    snapshot.broadcastWindowY = s.value(QStringLiteral("window_y"), -1).toInt();
    snapshot.broadcastWindowWidth = s.value(QStringLiteral("window_width"), 400).toInt();
    snapshot.broadcastWindowHeight = s.value(QStringLiteral("window_height"), 600).toInt();
    snapshot.broadcastTransparentBgColor = s.value(QStringLiteral("transparent_bg_color"), QStringLiteral("#00000000")).toString();
    snapshot.broadcastOpaqueBgColor = s.value(QStringLiteral("opaque_bg_color"), QStringLiteral("#FFFFFFFF")).toString();
    s.endGroup();

    snapshot.loadedAtUtc = QDateTime::currentDateTimeUtc();
    return snapshot;
}

bool AppSettings::save(const AppSettingsSnapshot& snapshot) const
{
    QSettings s(m_iniPath, QSettings::IniFormat);

    s.beginGroup(QStringLiteral("app"));
    s.setValue(QStringLiteral("language"), snapshot.language);
    s.setValue(QStringLiteral("log_level"), snapshot.logLevel);
    s.setValue(QStringLiteral("merge_order"), snapshot.mergeOrder);
    s.setValue(QStringLiteral("auto_reconnect"), snapshot.autoReconnect);
    s.setValue(QStringLiteral("detail_log_enabled"), snapshot.detailLogEnabled);
    s.setValue(QStringLiteral("chat_font_family"), snapshot.chatFontFamily);
    s.setValue(QStringLiteral("chat_font_size"), snapshot.chatFontSize);
    s.setValue(QStringLiteral("chat_font_bold"), snapshot.chatFontBold);
    s.setValue(QStringLiteral("chat_font_italic"), snapshot.chatFontItalic);
    s.setValue(QStringLiteral("chat_line_spacing"), snapshot.chatLineSpacing);
    s.setValue(QStringLiteral("chat_max_messages"), snapshot.chatMaxMessages);
    s.endGroup();

    savePlatform(s, QStringLiteral("youtube"), snapshot.youtube);
    savePlatform(s, QStringLiteral("chzzk"), snapshot.chzzk);

    s.beginGroup(QStringLiteral("broadcast"));
    s.setValue(QStringLiteral("viewer_count_position"), snapshot.broadcastViewerCountPosition);
    s.setValue(QStringLiteral("window_x"), snapshot.broadcastWindowX);
    s.setValue(QStringLiteral("window_y"), snapshot.broadcastWindowY);
    s.setValue(QStringLiteral("window_width"), snapshot.broadcastWindowWidth);
    s.setValue(QStringLiteral("window_height"), snapshot.broadcastWindowHeight);
    s.setValue(QStringLiteral("transparent_bg_color"), snapshot.broadcastTransparentBgColor);
    s.setValue(QStringLiteral("opaque_bg_color"), snapshot.broadcastOpaqueBgColor);
    s.endGroup();

    s.sync();
    return s.status() == QSettings::NoError;
}
