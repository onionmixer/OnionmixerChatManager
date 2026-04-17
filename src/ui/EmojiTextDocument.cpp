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

    // Qt의 QUrl은 host 부분을 DNS 규칙에 따라 **자동 소문자화**함.
    // 예: emoji://UCkszU2WH9gy1mb0dV-11UJg/xxx → host "uckszu2wh9gy1mb0dv-11ujg"
    // EmojiImageCache와 m_idToUrl은 원본 case를 유지하므로 비교는 case-insensitive.
    const QString lookupId = extractEmojiId(name);
    if (lookupId.isEmpty()) {
        if (g_traceEnabled) qInfo().noquote() << "[EMOJI-RESOLVE] empty-id url=" << name.toString();
        return QVariant();
    }

    // m_idToUrl에서 case-insensitive 검색 → 원본 case id 획득
    QString originalId;
    QString url;
    for (auto it = m_idToUrl.constBegin(); it != m_idToUrl.constEnd(); ++it) {
        if (it.key().compare(lookupId, Qt::CaseInsensitive) == 0) {
            originalId = it.key();
            url = it.value();
            break;
        }
    }

    if (originalId.isEmpty()) {
        if (g_traceEnabled) qInfo().noquote() << "[EMOJI-RESOLVE] miss-no-url id=" << lookupId;
        return QVariant();
    }

    // 캐시 히트 (원본 case로 조회)
    if (m_cache && m_cache->contains(originalId)) {
        if (g_traceEnabled) qInfo().noquote() << "[EMOJI-RESOLVE] hit id=" << originalId;
        return QVariant(m_cache->get(originalId));
    }

    // 캐시 미스 — 원본 case로 ensureLoaded 자동 트리거
    if (m_cache) {
        if (g_traceEnabled) qInfo().noquote() << "[EMOJI-RESOLVE] miss+load id=" << originalId << "url=" << url;
        m_cache->ensureLoaded(originalId, url);
    }
    // 로드 완료 후 imageReady → viewport 재페인트 시 이 함수 재호출 → 히트 처리.
    return QVariant();
}
