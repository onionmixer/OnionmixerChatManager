#ifndef ONIONMIXERCHATMANAGER_BROADCHATPROTOCOL_H
#define ONIONMIXERCHATMANAGER_BROADCHATPROTOCOL_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

// BroadChatProtocol — MAINPROJECT §8.1 v15-5
// NDJSON envelope 직렬화·파싱 단일 구현. 서버·클라가 동일 헤더 참조.
namespace BroadChatProtocol {

// MAINPROJECT v14-17
constexpr int kProtocolVersion = 1;

// MAINPROJECT v11-19
constexpr qint64 kMaxLineBytes = 1 << 20; // 1 MB

struct Envelope {
    int v = 0;
    QString type;
    QString id;
    qint64 t = -1;
    QJsonObject data;
    bool valid = false;
    QString parseError;
};

// encodeEnvelope: JSON + "\n" 종결 1라인 바이트 반환. 실패 시 빈 QByteArray.
QByteArray encodeEnvelope(const QString& type,
                          const QJsonObject& data = {},
                          const QString& id = {},
                          qint64 timestampMs = -1);

// parseEnvelope: 1라인 (개행 제외) 파싱. valid=false 시 parseError에 원인.
Envelope parseEnvelope(const QByteArray& line);

} // namespace BroadChatProtocol

// UnifiedChatMessage → JSON data 빌더. §6.4.3 스키마.
// AppTypes.h의 struct 참조 — 별도 헤더 include 필요.
#include "core/AppTypes.h"
namespace BroadChatProtocol {
QJsonObject buildChatData(const UnifiedChatMessage& msg);
QJsonObject buildViewersData(int youtube, int chzzk);
QJsonObject buildPlatformStatusData(PlatformId platform, const QString& state,
                                    bool live, const QString& runtimePhase);
} // namespace BroadChatProtocol

#endif
