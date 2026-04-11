#include "ui/ChatterListDialog.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

ChatterListDialog::ChatterListDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Chatter List"));
    resize(680, 520);

    auto* rootLayout = new QVBoxLayout(this);
    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("tblChatterList"));
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({
        tr("Platform"),
        tr("Nickname"),
        tr("Count"),
        tr("Last Seen"),
    });
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

    rootLayout->addWidget(m_table);

    auto* bottomLayout = new QHBoxLayout;
    bottomLayout->addStretch();
    auto* btnReset = new QPushButton(tr("Reset"), this);
    btnReset->setObjectName(QStringLiteral("btnChatterListReset"));
    connect(btnReset, &QPushButton::clicked, this, &ChatterListDialog::resetRequested);
    bottomLayout->addWidget(btnReset);
    rootLayout->addLayout(bottomLayout);
}

void ChatterListDialog::setEntries(const QVector<ChatterListEntry>& entries)
{
    if (!m_table) {
        return;
    }

    m_table->setRowCount(0);
    for (const ChatterListEntry& entry : entries) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);

        const QString platformText = entry.platform == PlatformId::YouTube ? tr("YouTube") : tr("CHZZK");
        const QString lastSeenText = entry.lastSeen.isValid()
            ? entry.lastSeen.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
            : QStringLiteral("-");

        auto* itemPlatform = new QTableWidgetItem(platformText);
        auto* itemNickname = new QTableWidgetItem(entry.nickname);
        auto* itemCount = new QTableWidgetItem(QString::number(entry.count));
        auto* itemLastSeen = new QTableWidgetItem(lastSeenText);
        itemCount->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_table->setItem(row, 0, itemPlatform);
        m_table->setItem(row, 1, itemNickname);
        m_table->setItem(row, 2, itemCount);
        m_table->setItem(row, 3, itemLastSeen);
    }
}
