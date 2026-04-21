#ifndef CHAT_DISPLAY_CONTROLLER_H
#define CHAT_DISPLAY_CONTROLLER_H

#include "core/AppTypes.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

class ChatBubbleDelegate;
class ChatMessageModel;
class EmojiImageCache;
class QListView;
class QStackedWidget;
class QTableWidget;

class ChatDisplayController : public QObject {
    Q_OBJECT
public:
    explicit ChatDisplayController(QStackedWidget* chatStack,
                                    QListView* chatListView,
                                    QTableWidget* tblChat,
                                    ChatMessageModel* chatModel,
                                    ChatBubbleDelegate* chatDelegate,
                                    EmojiImageCache* emojiCache,
                                    const AppSettingsSnapshot* snapshot,
                                    QObject* parent = nullptr);

    void appendMessage(const UnifiedChatMessage& message, const QString& authorLabel);
    void configureChatTableForCurrentView();
    void rebuildChatTable();
    void toggleViewMode();
    int selectedChatRow() const;
    const UnifiedChatMessage* selectedChatMessage() const;
    void copySelectedChat();

    enum class ChatViewMode { Messenger, Table };
    ChatViewMode viewMode() const { return m_chatViewMode; }
    const QVector<UnifiedChatMessage>& messages() const { return m_chatMessages; }

    // YouTube authorId → resolved @handle 맵을 받아 메시지의 rawAuthorDisplayName을
    // 일괄 갱신하고 현재 뷰를 재렌더한다. 변경된 메시지가 있으면 true 반환.
    bool applyAuthorHandleUpdate(const QHash<QString, QString>& channelIdToHandle);

signals:
    void selectionChanged();

private:
    void appendChatRow(int row, const UnifiedChatMessage& message, const QString& authorLabel);

    QStackedWidget* m_chatStack = nullptr;
    QListView* m_chatListView = nullptr;
    QTableWidget* m_tblChat = nullptr;
    ChatMessageModel* m_chatModel = nullptr;
    ChatBubbleDelegate* m_chatDelegate = nullptr;
    EmojiImageCache* m_emojiCache = nullptr;
    const AppSettingsSnapshot* m_snapshot = nullptr;

    ChatViewMode m_chatViewMode = ChatViewMode::Messenger;
    QVector<UnifiedChatMessage> m_chatMessages;
    QSet<QString> m_recentMessageIds;
};

#endif // CHAT_DISPLAY_CONTROLLER_H
