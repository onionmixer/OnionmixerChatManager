#include "ui/ChatMessageModel.h"

ChatMessageModel::ChatMessageModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int ChatMessageModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_messages.size();
}

QVariant ChatMessageModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_messages.size()) {
        return QVariant();
    }

    const UnifiedChatMessage& msg = m_messages.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
    case MessageTextRole:
        return msg.text;
    case PlatformRole:
        return static_cast<int>(msg.platform);
    case AuthorNameRole:
        return msg.authorName;
    case AuthorIdRole:
        return msg.authorId;
    case RichTextRole:
        return msg.richText;
    case TimestampRole:
        return msg.timestamp;
    case MessageIdRole:
        return msg.messageId;
    case CopyTextRole:
        return QStringLiteral("%1\n%2").arg(msg.authorName, msg.text);
    case EmojisRole:
        return QVariant::fromValue(msg.emojis);
    case AuthorIsChatOwnerRole:
        return msg.authorIsChatOwner;
    case AuthorIsChatModeratorRole:
        return msg.authorIsChatModerator;
    default:
        return QVariant();
    }
}

void ChatMessageModel::appendMessage(const UnifiedChatMessage& message)
{
    const int row = m_messages.size();
    beginInsertRows(QModelIndex(), row, row);
    m_messages.append(message);
    endInsertRows();
}

void ChatMessageModel::trimOldest(int keepCount)
{
    if (keepCount <= 0 || m_messages.size() <= keepCount) {
        return;
    }
    const int removeCount = m_messages.size() - keepCount;
    beginRemoveRows(QModelIndex(), 0, removeCount - 1);
    m_messages = m_messages.mid(removeCount);
    endRemoveRows();
}

void ChatMessageModel::clear()
{
    if (m_messages.isEmpty()) {
        return;
    }
    beginResetModel();
    m_messages.clear();
    endResetModel();
}

const UnifiedChatMessage* ChatMessageModel::messageAt(int row) const
{
    if (row < 0 || row >= m_messages.size()) {
        return nullptr;
    }
    return &m_messages.at(row);
}

int ChatMessageModel::messageCount() const
{
    return m_messages.size();
}
