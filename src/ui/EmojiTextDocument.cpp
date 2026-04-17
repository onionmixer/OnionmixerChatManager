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
        // 이모지 ID에는 슬래시가 포함될 수 있음 (예: YouTube의
        // "UCkszU2WH9gy1mb0dV-11UJg/6_cfY8HJH8bV5QS5yYDYDg"). QUrl이 이를
        // host/path로 분할하므로 두 조각을 그대로 이어붙여 원본 ID 복원.
        const QString host = name.host();
        const QString path = name.path();  // path가 "/"로 시작 (host가 있을 때)
        QString id;
        if (!host.isEmpty()) {
            id = path.isEmpty() ? host : host + path;
        } else {
            id = path.startsWith('/') ? path.mid(1) : path;
        }
        if (!id.isEmpty() && m_cache && m_cache->contains(id)) {
            return QVariant(m_cache->get(id));
        }
        // 캐시 미스 — 빈 QVariant 반환. 비동기 로드 완료 시
        // EmojiImageCache::imageReady로 viewport 재페인트 → 다시 loadResource
        // 호출되어 캐시 히트 처리.
        return QVariant();
    }
    return QTextDocument::loadResource(type, name);
}
