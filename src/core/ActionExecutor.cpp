#include "core/ActionExecutor.h"

#include <QCoreApplication>

namespace {
QString actionText(const char* sourceText)
{
    return QCoreApplication::translate("ActionExecutor", sourceText);
}

bool isConnected(PlatformId platform, const QMap<PlatformId, bool>& states)
{
    return states.value(platform, false);
}

ActionExecutionResult fail(const QString& code, const QString& message)
{
    ActionExecutionResult r;
    r.ok = false;
    r.errorCode = code;
    r.message = message;
    r.httpStatus = 0;
    return r;
}

ActionExecutionResult ok(const QString& message)
{
    ActionExecutionResult r;
    r.ok = true;
    r.errorCode.clear();
    r.message = message;
    r.httpStatus = 200;
    return r;
}
} // namespace

ActionExecutionResult ActionExecutor::execute(const QString& actionId,
    const UnifiedChatMessage& target,
    const QMap<PlatformId, bool>& connectionStates) const
{
    if (actionId == QStringLiteral("common.send_message")) {
        if (!isConnected(target.platform, connectionStates)) {
            return fail(QStringLiteral("NOT_CONNECTED"), actionText("Platform is not connected."));
        }
        return ok(actionText("Message send queued."));
    }

    if (actionId == QStringLiteral("common.restrict_user")) {
        if (target.authorId.trimmed().isEmpty()) {
            return fail(QStringLiteral("MISSING_AUTHOR_ID"), actionText("Author id is required."));
        }
        if (!isConnected(target.platform, connectionStates)) {
            return fail(QStringLiteral("NOT_CONNECTED"), actionText("Platform is not connected."));
        }
        return ok(actionText("Restrict user request queued."));
    }

    if (actionId == QStringLiteral("youtube.delete_message")) {
        if (target.platform != PlatformId::YouTube) {
            return fail(QStringLiteral("PLATFORM_MISMATCH"), actionText("Not a YouTube message."));
        }
        if (!isConnected(PlatformId::YouTube, connectionStates)) {
            return fail(QStringLiteral("NOT_CONNECTED"), actionText("YouTube is not connected."));
        }
        if (target.messageId.trimmed().isEmpty()) {
            return fail(QStringLiteral("MISSING_MESSAGE_ID"), actionText("YouTube messageId is required."));
        }
        return ok(actionText("YouTube delete message queued."));
    }

    if (actionId == QStringLiteral("youtube.timeout_5m")) {
        if (target.platform != PlatformId::YouTube) {
            return fail(QStringLiteral("PLATFORM_MISMATCH"), actionText("Not a YouTube message."));
        }
        if (!isConnected(PlatformId::YouTube, connectionStates)) {
            return fail(QStringLiteral("NOT_CONNECTED"), actionText("YouTube is not connected."));
        }
        if (target.authorId.trimmed().isEmpty()) {
            return fail(QStringLiteral("MISSING_AUTHOR_ID"), actionText("YouTube authorId is required."));
        }
        return ok(actionText("YouTube timeout(5m) queued."));
    }

    if (actionId == QStringLiteral("chzzk.add_restriction")) {
        if (target.platform != PlatformId::Chzzk) {
            return fail(QStringLiteral("PLATFORM_MISMATCH"), actionText("Not a CHZZK message."));
        }
        if (!isConnected(PlatformId::Chzzk, connectionStates)) {
            return fail(QStringLiteral("NOT_CONNECTED"), actionText("CHZZK is not connected."));
        }
        if (target.authorId.trimmed().isEmpty()) {
            return fail(QStringLiteral("MISSING_AUTHOR_ID"), actionText("CHZZK senderChannelId is required."));
        }
        return ok(actionText("CHZZK add restriction queued."));
    }

    return fail(QStringLiteral("UNSUPPORTED_ACTION"), actionText("Unsupported action id."));
}
