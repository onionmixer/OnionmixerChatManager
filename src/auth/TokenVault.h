#ifndef TOKEN_VAULT_H
#define TOKEN_VAULT_H

#include "core/AppTypes.h"

#include <QString>

class TokenVault {
public:
    virtual ~TokenVault() = default;

    virtual bool read(PlatformId platform, TokenRecord* out) const = 0;
    virtual bool write(PlatformId platform, const TokenRecord& record) = 0;
    virtual bool clear(PlatformId platform) = 0;
};

class FileTokenVault : public TokenVault {
public:
    explicit FileTokenVault(QString filePath = QStringLiteral("config/tokens.ini"));

    bool read(PlatformId platform, TokenRecord* out) const override;
    bool write(PlatformId platform, const TokenRecord& record) override;
    bool clear(PlatformId platform) override;

private:
    QString m_filePath;
};

#endif // TOKEN_VAULT_H
