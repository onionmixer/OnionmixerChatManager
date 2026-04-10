#include "core/ActionExecutor.h"

namespace {
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
            return fail(QStringLiteral("NOT_CONNECTED"), QStringLiteral("Platform is not connected."));
        }
        return ok(QStringLiteral("Message send queued."));
    }

    if (actionId == QStringLiteral("common.restrict_user")) {
        if (target.authorId.trimmed().isEmpty()) {
            return fail(QStringLiteral("MISSING_AUTHOR_ID"), QStringLiteral("Author id is required."));
        }
        if (!isConnected(target.platform, connectionStates)) {
            return fail(QStringLiteral("NOT_CONNECTED"), QStringLiteral("Platform is not connected."));
        }
        return ok(QStringLiteral("Restrict user request queued."));
    }

    if (actionId == QStringLiteral("youtube.delete_message")) {
        if (target.platform != PlatformId::YouTube) {
            return fail(QStringLiteral("PLATFORM_MISMATCH"), QStringLiteral("Not a YouTube message."));
        }
        if (!isConnected(PlatformId::YouTube, connectionStates)) {
            return fail(QStringLiteral("NOT_CONNECTED"), QStringLiteral("YouTube is not connected."));
        }
        if (target.messageId.trimmed().isEmpty()) {
            return fail(QStringLiteral("MISSING_MESSAGE_ID"), QStringLiteral("YouTube messageId is required."));
        }
        return ok(QStringLiteral("YouTube delete message queued."));
    }

    if (actionId == QStringLiteral("youtube.timeout_5m")) {
        if (target.platform != PlatformId::YouTube) {
            return fail(QStringLiteral("PLATFORM_MISMATCH"), QStringLiteral("Not a YouTube message."));
        }
        if (!isConnected(PlatformId::YouTube, connectionStates)) {
            return fail(QStringLiteral("NOT_CONNECTED"), QStringLiteral("YouTube is not connected."));
        }
        if (target.authorId.trimmed().isEmpty()) {
            return fail(QStringLiteral("MISSING_AUTHOR_ID"), QStringLiteral("YouTube authorId is required."));
        }
        return ok(QStringLiteral("YouTube timeout(5m) queued."));
    }

    if (actionId == QStringLiteral("chzzk.add_restriction")) {
        if (target.platform != PlatformId::Chzzk) {
            return fail(QStringLiteral("PLATFORM_MISMATCH"), QStringLiteral("Not a CHZZK message."));
        }
        if (!isConnected(PlatformId::Chzzk, connectionStates)) {
            return fail(QStringLiteral("NOT_CONNECTED"), QStringLiteral("CHZZK is not connected."));
        }
        if (target.authorId.trimmed().isEmpty()) {
            return fail(QStringLiteral("MISSING_AUTHOR_ID"), QStringLiteral("CHZZK senderChannelId is required."));
        }
        return ok(QStringLiteral("CHZZK add restriction queued."));
    }

    return fail(QStringLiteral("UNSUPPORTED_ACTION"), QStringLiteral("Unsupported action id."));
}
