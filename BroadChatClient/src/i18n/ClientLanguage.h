#pragma once

// PLAN_DEV_BROADCHATCLIENT §20.6 v65 — 클라 전용 번역 로더.
// 메인 앱의 `AppLanguage` 와 **분리** (공유 금지, 별도 .qm 리소스):
//   - 메인: `onionmixerchatmanager_<locale>.qm`
//   - 클라: `onionmixerbroadchatclient_<locale>.qm`
// 두 앱 UI 문자열이 독립적이라 .ts 충돌·drift 방지 + 업데이트 영향 분리.

#include <QString>

class QCoreApplication;

namespace BroadChatClientLanguage {

// 지원 locale: ko_KR · en_US · ja_JP. 그 외는 en_US 로 fallback.
QString normalizeLanguage(const QString& language);

// 현재 적용된 locale. applyLanguage 이전에는 기본 "en_US".
QString currentLanguage();

// translator 로드 · 설치. 실패 시 false + errorMessage (optional).
// language 가 빈 문자열이면 `QLocale::system().name()` 으로 자동 감지.
bool applyLanguage(QCoreApplication& app, const QString& language,
                   QString* errorMessage = nullptr);

} // namespace BroadChatClientLanguage
