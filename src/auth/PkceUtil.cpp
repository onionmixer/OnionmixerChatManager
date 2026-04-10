#include "auth/PkceUtil.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QRandomGenerator>

namespace {
QString base64UrlNoPad(const QByteArray& input)
{
    return QString::fromLatin1(input.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
} // namespace

namespace pkce {

QString generateCodeVerifier()
{
    QByteArray randomBytes;
    randomBytes.resize(48);

    QRandomGenerator* rng = QRandomGenerator::global();
    for (int i = 0; i < randomBytes.size(); ++i) {
        randomBytes[i] = static_cast<char>(rng->bounded(256));
    }

    QString verifier = base64UrlNoPad(randomBytes);
    if (verifier.size() < 43) {
        verifier = verifier + QStringLiteral("abcdefghijklmnopqrstuvwxyz0123456789-_~");
        verifier = verifier.left(43);
    } else if (verifier.size() > 128) {
        verifier = verifier.left(128);
    }

    return verifier;
}

QString makeCodeChallengeS256(const QString& codeVerifier)
{
    if (codeVerifier.trimmed().isEmpty()) {
        return QString();
    }

    const QByteArray hash = QCryptographicHash::hash(codeVerifier.toUtf8(), QCryptographicHash::Sha256);
    return base64UrlNoPad(hash);
}

} // namespace pkce
