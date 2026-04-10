#ifndef PKCE_UTIL_H
#define PKCE_UTIL_H

#include <QString>

namespace pkce {

QString generateCodeVerifier();
QString makeCodeChallengeS256(const QString& codeVerifier);

} // namespace pkce

#endif // PKCE_UTIL_H
