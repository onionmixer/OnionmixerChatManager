#ifndef ONIONMIXERCHATMANAGER_BROADCHATENDPOINT_H
#define ONIONMIXERCHATMANAGER_BROADCHATENDPOINT_H

#include <QString>
#include <QtGlobal>

// BroadChatEndpoint — PLAN_MAINPROJECT_MIGRATION.md §5.2 v18-6 (TCP 전환)
// TCP 기본 상수 + 설정값 정규화 helper.
// 메인 앱·클라이언트가 동일 헤더 참조 → 기본 포트·주소 불일치 방지.
namespace BroadChatEndpoint {

// v18-4·v22-4 TCP 포트 범위
constexpr quint16 kDefaultTcpPort = 47123;
constexpr quint16 kMinTcpPort = 1024;   // privileged 포트 차단
constexpr quint16 kMaxTcpPort = 65535;

// v18-3 bind 기본 주소 — 로컬만 허용 (보안 우선)
constexpr const char* kDefaultBindAddress = "127.0.0.1";

// bind address 정규화.
//   빈 값·잘못된 IP → kDefaultBindAddress 반환
//   특수 값 "0.0.0.0" 허용 (모든 인터페이스)
//   IPv4/IPv6 주소 허용
QString normalizeBindAddress(const QString& iniValue);

// port 정규화. 범위 밖이면 kDefaultTcpPort 반환.
quint16 normalizePort(int iniValue);

} // namespace BroadChatEndpoint

#endif
