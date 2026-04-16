#ifndef CHAT_MESSAGE_MODEL_H
#define CHAT_MESSAGE_MODEL_H

#include "core/AppTypes.h"

#include <QAbstractListModel>
#include <QVector>

class ChatMessageModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        PlatformRole = Qt::UserRole + 1,
        AuthorNameRole,
        AuthorIdRole,
        MessageTextRole,
        RichTextRole,
        TimestampRole,
        MessageIdRole,
        CopyTextRole,
        EmojisRole,
        AuthorIsChatOwnerRole,
        AuthorIsChatModeratorRole,
    };

    explicit ChatMessageModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    void appendMessage(const UnifiedChatMessage& message);
    void trimOldest(int keepCount);
    void clear();

    const UnifiedChatMessage* messageAt(int row) const;
    int messageCount() const;

private:
    QVector<UnifiedChatMessage> m_messages;
};

#endif // CHAT_MESSAGE_MODEL_H
