#ifndef CHATTER_LIST_DIALOG_H
#define CHATTER_LIST_DIALOG_H

#include "core/AppTypes.h"

#include <QDateTime>
#include <QDialog>
#include <QVector>

class QTableWidget;

struct ChatterListEntry {
    PlatformId platform = PlatformId::YouTube;
    QString nickname;
    int count = 0;
    QDateTime lastSeen;
};

class ChatterListDialog : public QDialog {
    Q_OBJECT
public:
    explicit ChatterListDialog(QWidget* parent = nullptr);
    void setEntries(const QVector<ChatterListEntry>& entries);

signals:
    void resetRequested();

private:
    QTableWidget* m_table = nullptr;
};

#endif // CHATTER_LIST_DIALOG_H
