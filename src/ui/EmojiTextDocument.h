#ifndef EMOJI_TEXT_DOCUMENT_H
#define EMOJI_TEXT_DOCUMENT_H

#include "core/AppTypes.h"

#include <QHash>
#include <QTextDocument>
#include <QVector>

class EmojiImageCache;

// QTextDocument 서브클래스 — <img src='emoji://{id}'/> placeholder를
// EmojiImageCache에서 찾은 QPixmap으로 해석.
//
// C+ 강화(FIX_CHAT_EMOTICON.md §5.2):
// - loadResource가 캐시 미스 시 m_idToUrl 맵 기반으로 ensureLoaded 자동 호출
// - robust URL 파싱 (문자열 접두사 제거 방식)
// - setEmojiList()로 paint 직전 id→url 매핑 주입
class EmojiTextDocument : public QTextDocument {
    Q_OBJECT
public:
    explicit EmojiTextDocument(EmojiImageCache* cache, QObject* parent = nullptr);

    // paint 직전에 message의 emoji 리스트를 주입. loadResource가 캐시 미스일 때
    // 이 맵에서 imageUrl을 찾아 ensureLoaded를 자동 트리거.
    void setEmojiList(const QVector<ChatEmojiInfo>& list);

    // 진단 로그 on/off (qInfo 기반, 기본 false — 빌드 후 UAT 시에만 활성화)
    static void setTraceEnabled(bool enabled);

protected:
    QVariant loadResource(int type, const QUrl& name) override;

private:
    EmojiImageCache* m_cache = nullptr;
    QHash<QString, QString> m_idToUrl;  // id → imageUrl (paint-local)
};

#endif // EMOJI_TEXT_DOCUMENT_H
