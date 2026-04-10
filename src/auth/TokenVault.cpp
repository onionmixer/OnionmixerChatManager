#include "auth/TokenVault.h"

#include <QSettings>

#include <utility>

namespace {
QString groupName(PlatformId platform)
{
    return platform == PlatformId::YouTube ? QStringLiteral("token.youtube") : QStringLiteral("token.chzzk");
}
}

FileTokenVault::FileTokenVault(QString filePath)
    : m_filePath(std::move(filePath))
{
}

bool FileTokenVault::read(PlatformId platform, TokenRecord* out) const
{
    if (!out) {
        return false;
    }

    QSettings s(m_filePath, QSettings::IniFormat);
    s.beginGroup(groupName(platform));

    const QString access = s.value(QStringLiteral("access")).toString();
    const QString refresh = s.value(QStringLiteral("refresh")).toString();
    const QDateTime accessExp = s.value(QStringLiteral("access_expire_at")).toDateTime();
    const QDateTime refreshExp = s.value(QStringLiteral("refresh_expire_at")).toDateTime();
    const QDateTime updated = s.value(QStringLiteral("updated_at")).toDateTime();

    s.endGroup();

    if (access.isEmpty() && refresh.isEmpty()) {
        return false;
    }

    out->accessToken = access;
    out->refreshToken = refresh;
    out->accessExpireAtUtc = accessExp;
    out->refreshExpireAtUtc = refreshExp;
    out->updatedAtUtc = updated;
    return true;
}

bool FileTokenVault::write(PlatformId platform, const TokenRecord& record)
{
    QSettings s(m_filePath, QSettings::IniFormat);
    s.beginGroup(groupName(platform));
    s.setValue(QStringLiteral("access"), record.accessToken);
    s.setValue(QStringLiteral("refresh"), record.refreshToken);
    s.setValue(QStringLiteral("access_expire_at"), record.accessExpireAtUtc);
    s.setValue(QStringLiteral("refresh_expire_at"), record.refreshExpireAtUtc);
    s.setValue(QStringLiteral("updated_at"), record.updatedAtUtc);
    s.endGroup();
    s.sync();
    return s.status() == QSettings::NoError;
}

bool FileTokenVault::clear(PlatformId platform)
{
    QSettings s(m_filePath, QSettings::IniFormat);
    s.beginGroup(groupName(platform));
    s.remove(QString());
    s.endGroup();
    s.sync();
    return s.status() == QSettings::NoError;
}
