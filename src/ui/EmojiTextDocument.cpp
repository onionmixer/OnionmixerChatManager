#include "ui/EmojiTextDocument.h"
#include "core/EmojiImageCache.h"

#include <QDebug>
#include <QPixmap>
#include <QUrl>

namespace {
bool g_traceEnabled = false;

// emoji://{id} URL → 원본 id 복원. QUrl의 host/path 분할을 거치지 않고
// 문자열 접두사를 직접 제거해 슬래시·특수문자 포함 id도 안전하게 추출.
QString extractEmojiId(const QUrl& url)
{
    const QString raw = url.toString(QUrl::None);
    static const QString kPrefix = QStringLiteral("emoji://");
    if (raw.startsWith(kPrefix)) {
        return raw.mid(kPrefix.length());
    }
    // 폴백: QUrl host+path 결합
    const QString host = url.host();
    const QString path = url.path();
    if (!host.isEmpty()) return path.isEmpty() ? host : host + path;
    return path.startsWith('/') ? path.mid(1) : path;
}
} // namespace

void EmojiTextDocument::setTraceEnabled(bool enabled)
{
    g_traceEnabled = enabled;
}

EmojiTextDocument::EmojiTextDocument(EmojiImageCache* cache, QObject* parent)
    : QTextDocument(parent)
    , m_cache(cache)
{
}

void EmojiTextDocument::setEmojiList(const QVector<ChatEmojiInfo>& list)
{
    m_idToUrl.clear();
    m_idToUrl.reserve(list.size());
    for (const ChatEmojiInfo& e : list) {
        if (!e.emojiId.isEmpty() && !e.imageUrl.isEmpty()) {
            m_idToUrl.insert(e.emojiId, e.imageUrl);
        }
    }
}

QVariant EmojiTextDocument::loadResource(int type, const QUrl& name)
{
    if (type != QTextDocument::ImageResource
        || name.scheme() != QStringLiteral("emoji")) {
        return QTextDocument::loadResource(type, name);
    }

    const QString id = extractEmojiId(name);
    if (id.isEmpty()) {
        if (g_traceEnabled) qInfo().noquote() << "[EMOJI-RESOLVE] empty-id url=" << name.toString();
        return QVariant();
    }

    // 캐시 히트
    if (m_cache && m_cache->contains(id)) {
        if (g_traceEnabled) qInfo().noquote() << "[EMOJI-RESOLVE] hit id=" << id;
        return QVariant(m_cache->get(id));
    }

    // 캐시 미스 — id→url 맵에서 조회해 자동 ensureLoaded 트리거
    if (m_cache && m_idToUrl.contains(id)) {
        const QString url = m_idToUrl.value(id);
        if (g_traceEnabled) qInfo().noquote() << "[EMOJI-RESOLVE] miss+load id=" << id << "url=" << url;
        m_cache->ensureLoaded(id, url);
    } else {
        if (g_traceEnabled) qInfo().noquote() << "[EMOJI-RESOLVE] miss-no-url id=" << id;
    }
    // 로드 완료 후 imageReady → viewport 재페인트 시 이 함수 재호출 → 히트 처리.
    return QVariant();
}
