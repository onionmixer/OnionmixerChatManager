#include "ui/ChatterStatsManager.h"
#include "core/Constants.h"

#include <QDateTime>
#include <QPair>
#include <QVector>
#include <algorithm>

ChatterStatsManager::ChatterStatsManager(QObject* parent)
    : QObject(parent)
{
}

void ChatterStatsManager::recordChatter(const UnifiedChatMessage& message, const QString& authorLabel)
{
    QString nickname = authorLabel.trimmed();
    if (nickname.isEmpty()) {
        nickname = QStringLiteral("-");
    }

    const QString key = QStringLiteral("%1|%2").arg(platformKey(message.platform), nickname);
    ChatterListEntry entry = m_stats.value(key);
    const bool isNew = (entry.count == 0);
    if (isNew) {
        if (m_stats.size() >= OnionmixerChatManager::Limits::kChatterStatsMax) {
            return;
        }
        entry.platform = message.platform;
        entry.nickname = nickname;
    }
    ++entry.count;
    entry.lastSeen = message.timestamp.isValid() ? message.timestamp : QDateTime::currentDateTime();
    m_stats.insert(key, entry);

    emit statsChanged();
}

void ChatterStatsManager::rebuildFromMessages(const QVector<UnifiedChatMessage>& messages,
                                               std::function<QString(const UnifiedChatMessage&)> labelFn)
{
    QHash<QString, ChatterListEntry> updated;
    for (const UnifiedChatMessage& message : messages) {
        QString nickname = labelFn(message).trimmed();
        if (nickname.isEmpty()) {
            nickname = QStringLiteral("-");
        }
        const QString key = QStringLiteral("%1|%2").arg(platformKey(message.platform), nickname);
        ChatterListEntry entry = updated.value(key);
        if (entry.count == 0) {
            entry.platform = message.platform;
            entry.nickname = nickname;
        }
        ++entry.count;
        entry.lastSeen = message.timestamp.isValid() ? message.timestamp : QDateTime::currentDateTime();
        updated.insert(key, entry);
    }

    // Preserve old stats for chatters whose messages were trimmed
    for (auto it = m_stats.cbegin(); it != m_stats.cend(); ++it) {
        if (!updated.contains(it.key())) {
            updated.insert(it.key(), it.value());
        }
    }

    const int kMax = OnionmixerChatManager::Limits::kChatterStatsMax;
    if (updated.size() > kMax) {
        QVector<QPair<QDateTime, QString>> byAge;
        byAge.reserve(updated.size());
        for (auto it = updated.cbegin(); it != updated.cend(); ++it) {
            byAge.append({ it.value().lastSeen, it.key() });
        }
        std::sort(byAge.begin(), byAge.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
        const int removeCount = updated.size() - kMax;
        for (int i = 0; i < removeCount; ++i) {
            updated.remove(byAge.at(i).second);
        }
    }

    m_stats = updated;
}

void ChatterStatsManager::clear()
{
    m_stats.clear();
}

QVector<ChatterListEntry> ChatterStatsManager::sortedEntries() const
{
    QVector<ChatterListEntry> rows;
    rows.reserve(m_stats.size());
    for (auto it = m_stats.cbegin(); it != m_stats.cend(); ++it) {
        rows.push_back(it.value());
    }
    std::sort(rows.begin(), rows.end(), [](const ChatterListEntry& a, const ChatterListEntry& b) {
        if (a.count != b.count) return a.count > b.count;
        if (a.lastSeen != b.lastSeen) return a.lastSeen > b.lastSeen;
        if (a.platform != b.platform) return platformKey(a.platform) < platformKey(b.platform);
        return a.nickname < b.nickname;
    });
    return rows;
}
