#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <QDateTime>
#include <QHash>
#include <QMap>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

enum class PlatformId {
    YouTube,
    Chzzk
};

inline QString platformKey(PlatformId platform)
{
    return platform == PlatformId::YouTube ? QStringLiteral("youtube") : QStringLiteral("chzzk");
}

inline uint qHash(PlatformId key, uint seed = 0)
{
    return ::qHash(static_cast<int>(key), seed);
}

struct PlatformSettings {
    bool enabled = false;
    QString clientId;
    QString clientSecret;
    QString redirectUri;
    QString authEndpoint;
    QString tokenEndpoint;
    QString scope;
    QString channelId;
    QString channelName;
    QString accountLabel;
    QString liveVideoIdOverride;
    QString runtimeAccessToken; // runtime-only, not persisted to app.ini
};

struct AppSettingsSnapshot {
    QString language;
    QString logLevel;
    QString mergeOrder;
    bool autoReconnect = true;
    bool detailLogEnabled = false;
    QString chatFontFamily;
    int chatFontSize = 11;
    bool chatFontBold = false;
    bool chatFontItalic = false;
    int chatLineSpacing = 3;
    PlatformSettings youtube;
    PlatformSettings chzzk;
    QDateTime loadedAtUtc;
};

enum class ConnectionState {
    IDLE,
    CONNECTING,
    PARTIALLY_CONNECTED,
    CONNECTED,
    DISCONNECTING,
    ERROR
};

struct ConnectSessionResult {
    ConnectionState state = ConnectionState::IDLE;
    QStringList connectedPlatforms;
    QMap<QString, QString> failedPlatforms;
};

struct TokenRecord {
    QString accessToken;
    QString refreshToken;
    QDateTime accessExpireAtUtc;
    QDateTime refreshExpireAtUtc;
    QDateTime updatedAtUtc;
};

struct UnifiedChatMessage {
    PlatformId platform = PlatformId::YouTube;
    QString messageId;
    QString channelId;
    QString channelName;
    QString authorId;
    QString authorName;
    QString rawAuthorDisplayName;
    QString rawAuthorChannelId;
    bool authorIsChatOwner = false;
    bool authorIsChatModerator = false;
    bool authorIsChatSponsor = false;
    bool authorIsVerified = false;
    QString text;
    QDateTime timestamp;
};

enum class TokenState {
    NO_TOKEN,
    VALID,
    EXPIRING_SOON,
    EXPIRED,
    REFRESHING,
    AUTH_REQUIRED,
    ERROR
};

Q_DECLARE_METATYPE(PlatformId)
Q_DECLARE_METATYPE(ConnectionState)
Q_DECLARE_METATYPE(AppSettingsSnapshot)
Q_DECLARE_METATYPE(PlatformSettings)
Q_DECLARE_METATYPE(ConnectSessionResult)
Q_DECLARE_METATYPE(TokenState)
Q_DECLARE_METATYPE(UnifiedChatMessage)
Q_DECLARE_METATYPE(QVector<UnifiedChatMessage>)
Q_DECLARE_METATYPE(TokenRecord)

#endif // APP_TYPES_H
