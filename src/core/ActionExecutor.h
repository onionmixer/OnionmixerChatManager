#ifndef ACTION_EXECUTOR_H
#define ACTION_EXECUTOR_H

#include "core/AppTypes.h"

#include <QMap>
#include <QString>

struct ActionExecutionResult {
    bool ok = false;
    QString errorCode;
    QString message;
    int httpStatus = 0;
};

class ActionExecutor {
public:
    ActionExecutionResult execute(const QString& actionId,
        const UnifiedChatMessage& target,
        const QMap<PlatformId, bool>& connectionStates) const;
};

#endif // ACTION_EXECUTOR_H
