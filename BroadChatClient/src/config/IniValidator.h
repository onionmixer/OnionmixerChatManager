#pragma once

#include "core/AppTypes.h"

#include <QString>

// PLAN_DEV_BROADCHATCLIENT §5.6.8 v57-1 + §10(v48-17) ini value fallback 규약.
// v74: v64 "isEmpty 기반 healing" 을 "isEmpty OR isInvalid" 로 확장 + 전 필드 cover.
//
// 이전 (v64): color 필드 4종만 `isEmpty()` 검사 → 빈 문자열 healing.
// 문제: 사용자 ini 에 비어있지 않지만 **파싱 불가능한 값** (예: `#ZZZZ` · `blue` ·
//       `transparent`) 이 들어오면 healing 미발동 → ChatBubbleDelegate 가 fallback
//       `#111111` 로 렌더 → 어두운 바탕에 투명 모드 시 채팅 본문 불가시.
//
// v74: 모든 필드를 검증 (색상 QColor::isValid · enum set 검사 · 숫자 range 검사) +
//      유효하지 않으면 opinionated default 로 교체 + heal 플래그 set. 호출자는
//      heal=true 반환 시 saveConfig 로 ini write-back 하여 다음 실행부터 정상화.
//
// Pure functions — Qt widget 의존 없이 단위 테스트 가능.
namespace IniValidator {

struct ConnectionFields {
    QString host;
    quint16 port = 0;
    QString authToken;
};

// AppSettingsSnapshot 의 broadcast·chat·window 필드 전수 검증.
// 반환값: 어떤 필드라도 수정됐으면 true (호출자가 saveConfig 트리거).
bool healSnapshot(AppSettingsSnapshot& s);

// connection 필드 (host/port/authToken) 검증.
// 반환값: 수정 시 true.
bool healConnection(ConnectionFields& c);

// 개별 검증 헬퍼 — 테스트·재사용 목적 노출.
bool isValidColor(const QString& s);           // QColor::isValid
bool isValidViewerPosition(const QString& s);  // 9종 enum
bool isValidPort(quint16 p);                   // 1024~65535

}  // namespace IniValidator
