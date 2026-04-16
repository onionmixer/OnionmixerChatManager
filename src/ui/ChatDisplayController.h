#ifndef CHAT_DISPLAY_CONTROLLER_H
#define CHAT_DISPLAY_CONTROLLER_H

#include "core/AppTypes.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVector>

class ChatBubbleDelegate;
class ChatMessageModel;
class EmojiImageCache;
class QListView;
class QStackedWidget;
class QTableWidget;
class QTimer;

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

    void onChatReceived(const UnifiedChatMessage& message);
    void toggleViewMode();
    void configureChatTableForCurrentView();
    void rebuildChatTable();
    int selectedChatRow() const;
    const UnifiedChatMessage* selectedChatMessage() const;
    void copySelectedChat();

    QString displayAuthorLabel(const UnifiedChatMessage& message) const;
    void setYouTubeAuthorHandleCache(const QString& authorId, const QString& handle);

    enum class ChatViewMode { Messenger, Table };
    ChatViewMode viewMode() const { return m_chatViewMode; }
    const QVector<UnifiedChatMessage>& messages() const { return m_chatMessages; }

signals:
    void selectionChanged();
    void logMessage(const QString& text);
    void chatMessageAppended(const UnifiedChatMessage& message, const QString& authorLabel);

private:
    void appendChatMessage(const UnifiedChatMessage& message, const QString& authorLabel);
    void appendChatRow(int row, const UnifiedChatMessage& message, const QString& authorLabel = QString());
    QWidget* buildMessengerCellWidget(const UnifiedChatMessage& message, const QString& authorDisplay) const;
    QString messengerAuthorLabel(const UnifiedChatMessage& message) const;

    void maybeQueueYouTubeAuthorHandleLookup(const UnifiedChatMessage& message);

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

    QHash<QString, QString> m_youtubeAuthorHandleCache;
    QSet<QString> m_youtubeAuthorHandlePending;
    QStringList m_youtubeAuthorHandleLookupQueue;
    bool m_detailLogEnabled = false;
};

#endif // CHAT_DISPLAY_CONTROLLER_H
