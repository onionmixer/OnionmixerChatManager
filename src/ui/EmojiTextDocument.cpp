#include "ui/EmojiTextDocument.h"
#include "core/EmojiImageCache.h"

#include <QPixmap>
#include <QUrl>

EmojiTextDocument::EmojiTextDocument(EmojiImageCache* cache, QObject* parent)
    : QTextDocument(parent)
    , m_cache(cache)
{
}

QVariant EmojiTextDocument::loadResource(int type, const QUrl& name)
{
    if (type == QTextDocument::ImageResource
        && name.scheme() == QStringLiteral("emoji")) {
        // emoji://{id} 형태 — id는 host 또는 path 첫 문자 제외로 추출
        QString id = name.host();
        if (id.isEmpty()) {
            const QString path = name.path();
            id = path.startsWith('/') ? path.mid(1) : path;
        }
        if (!id.isEmpty() && m_cache && m_cache->contains(id)) {
            return QVariant(m_cache->get(id));
        }
        // 캐시 미스 — 빈 QVariant 반환 (Qt는 이 경우 img alt 또는 broken-image 표시).
        // 비동기 로드 완료 시 EmojiImageCache::imageReady가 emit되어 viewport가
        // 재페인트되면 그 때 다시 loadResource 호출되고 캐시 히트로 처리됨.
        return QVariant();
    }
    return QTextDocument::loadResource(type, name);
}
