#ifndef CHATTER_STATS_MANAGER_H
#define CHATTER_STATS_MANAGER_H

#include "core/AppTypes.h"
#include "ui/ChatterListDialog.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

class ChatterStatsManager : public QObject {
    Q_OBJECT
public:
    explicit ChatterStatsManager(QObject* parent = nullptr);

    void recordChatter(const UnifiedChatMessage& message, const QString& authorLabel);
    void rebuildFromMessages(const QVector<UnifiedChatMessage>& messages,
                             std::function<QString(const UnifiedChatMessage&)> labelFn);
    void clear();
    QVector<ChatterListEntry> sortedEntries() const;

signals:
    void statsChanged();

private:
    QHash<QString, ChatterListEntry> m_stats;
};

#endif // CHATTER_STATS_MANAGER_H
