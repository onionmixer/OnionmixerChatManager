#include "ui/ChatDisplayController.h"
#include "core/Constants.h"
#include "core/EmojiImageCache.h"
#include "core/PlatformTraits.h"
#include "ui/ChatBubbleDelegate.h"
#include "ui/ChatMessageModel.h"

#include <QAbstractItemView>
#include <QClipboard>
#include <QGuiApplication>
#include <QHeaderView>
#include <QListView>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableWidget>

ChatDisplayController::ChatDisplayController(QStackedWidget* chatStack,
                                              QListView* chatListView,
                                              QTableWidget* tblChat,
                                              ChatMessageModel* chatModel,
                                              ChatBubbleDelegate* chatDelegate,
                                              EmojiImageCache* emojiCache,
                                              const AppSettingsSnapshot* snapshot,
                                              QObject* parent)
    : QObject(parent)
    , m_chatStack(chatStack)
    , m_chatListView(chatListView)
    , m_tblChat(tblChat)
    , m_chatModel(chatModel)
    , m_chatDelegate(chatDelegate)
    , m_emojiCache(emojiCache)
    , m_snapshot(snapshot)
{
}

void ChatDisplayController::appendMessage(const UnifiedChatMessage& message, const QString& authorLabel)
{
    const QString msgId = message.messageId.trimmed();
    if (!msgId.isEmpty()) {
        if (m_recentMessageIds.contains(msgId)) {
            return;
        }
        m_recentMessageIds.insert(msgId);
        if (m_recentMessageIds.size() > BotManager::Limits::kRecentMessageIdsMax) {
            m_recentMessageIds.clear();
            m_recentMessageIds.insert(msgId);
        }
    }

    m_chatMessages.push_back(message);
    m_chatModel->appendMessage(message);

    const int maxMessages = m_snapshot ? m_snapshot->chatMaxMessages : 5000;
    if (maxMessages > 0 && m_chatMessages.size() > maxMessages) {
        const int keepCount = maxMessages * 4 / 5;
        m_chatMessages = m_chatMessages.mid(m_chatMessages.size() - keepCount);
        m_chatModel->trimOldest(keepCount);
        if (m_chatViewMode == ChatViewMode::Table) {
            rebuildChatTable();
        }
    }

    if (m_chatViewMode == ChatViewMode::Table) {
        const int row = m_tblChat->rowCount();
        m_tblChat->insertRow(row);
        appendChatRow(row, message, authorLabel);
        m_tblChat->scrollToBottom();
    } else {
        m_chatListView->scrollToBottom();
    }
    emit selectionChanged();
}

void ChatDisplayController::configureChatTableForCurrentView()
{
    if (!m_chatStack) return;

    if (m_chatViewMode == ChatViewMode::Messenger) {
        m_chatStack->setCurrentWidget(m_chatListView);
        if (m_chatDelegate && m_snapshot) {
            m_chatDelegate->setFontFamily(m_snapshot->chatFontFamily);
            m_chatDelegate->setFontSize(m_snapshot->chatFontSize);
            m_chatDelegate->setFontBold(m_snapshot->chatFontBold);
            m_chatDelegate->setFontItalic(m_snapshot->chatFontItalic);
            m_chatDelegate->setLineSpacing(m_snapshot->chatLineSpacing);
        }
        return;
    }

    m_chatStack->setCurrentWidget(m_tblChat);
    if (!m_tblChat) return;
    m_tblChat->setColumnCount(4);
    m_tblChat->setHorizontalHeaderLabels({ tr("Time"), tr("Platform"), tr("Author"), tr("Message") });
    m_tblChat->horizontalHeader()->setVisible(true);
    m_tblChat->horizontalHeader()->setStretchLastSection(true);
    m_tblChat->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tblChat->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tblChat->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tblChat->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_tblChat->setShowGrid(true);
    m_tblChat->setFrameShape(QFrame::StyledPanel);
    m_tblChat->setStyleSheet(QString());
}

void ChatDisplayController::rebuildChatTable()
{
    configureChatTableForCurrentView();

    if (m_chatViewMode == ChatViewMode::Messenger) {
        if (m_chatModel) emit m_chatModel->layoutChanged();
        if (m_chatListView) m_chatListView->viewport()->update();
        emit selectionChanged();
        return;
    }

    if (!m_tblChat) return;
    const int previousRow = selectedChatRow();
    QSignalBlocker blocker(m_tblChat);
    m_tblChat->setRowCount(0);
    for (int i = 0; i < m_chatMessages.size(); ++i) {
        m_tblChat->insertRow(i);
        appendChatRow(i, m_chatMessages.at(i), QString());
    }
    if (previousRow >= 0 && previousRow < m_tblChat->rowCount()) {
        m_tblChat->selectRow(previousRow);
    }
    emit selectionChanged();
}

void ChatDisplayController::toggleViewMode()
{
    m_chatViewMode = (m_chatViewMode == ChatViewMode::Messenger)
        ? ChatViewMode::Table : ChatViewMode::Messenger;
    rebuildChatTable();
}

void ChatDisplayController::appendChatRow(int row, const UnifiedChatMessage& message, const QString& authorLabel)
{
    if (m_chatViewMode != ChatViewMode::Table || !m_tblChat) return;
    const QString platform = message.platform == PlatformId::YouTube ? tr("YouTube") : tr("CHZZK");
    const QString timeText = message.timestamp.isValid()
        ? message.timestamp.toString(QStringLiteral("HH:mm:ss")) : QStringLiteral("-");
    const QString authorDisplay = authorLabel.isEmpty() ? message.authorName : authorLabel;

    m_tblChat->setItem(row, 0, new QTableWidgetItem(timeText));
    m_tblChat->setItem(row, 1, new QTableWidgetItem(platform));
    m_tblChat->setItem(row, 2, new QTableWidgetItem(authorDisplay));
    m_tblChat->setItem(row, 3, new QTableWidgetItem(message.text));
}

int ChatDisplayController::selectedChatRow() const
{
    if (m_chatViewMode == ChatViewMode::Messenger) {
        if (!m_chatListView || !m_chatListView->selectionModel()) return -1;
        const QModelIndexList sel = m_chatListView->selectionModel()->selectedIndexes();
        return sel.isEmpty() ? -1 : sel.first().row();
    }
    if (!m_tblChat || !m_tblChat->selectionModel()) return -1;
    const QModelIndexList rows = m_tblChat->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}

const UnifiedChatMessage* ChatDisplayController::selectedChatMessage() const
{
    const int row = selectedChatRow();
    if (row < 0 || row >= m_chatMessages.size()) return nullptr;
    return &m_chatMessages.at(row);
}

void ChatDisplayController::copySelectedChat()
{
    if (!QGuiApplication::clipboard()) return;
    const int row = selectedChatRow();
    if (row < 0) return;

    QString copyText;
    if (m_chatViewMode == ChatViewMode::Messenger) {
        const QModelIndex idx = m_chatModel->index(row, 0);
        copyText = idx.data(ChatMessageModel::CopyTextRole).toString();
    } else {
        QStringList cells;
        for (int col = 0; col < m_tblChat->columnCount(); ++col) {
            const QTableWidgetItem* item = m_tblChat->item(row, col);
            QString value = item ? item->text() : QString();
            if (value.isEmpty() && item) value = item->data(Qt::UserRole).toString();
            cells.push_back(value);
        }
        copyText = cells.join(QStringLiteral("\t"));
    }
    QGuiApplication::clipboard()->setText(copyText);
}
