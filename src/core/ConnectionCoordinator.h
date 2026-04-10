#ifndef CONNECTION_COORDINATOR_H
#define CONNECTION_COORDINATOR_H

#include "core/AppTypes.h"
#include "core/IChatPlatformAdapter.h"

#include <QHash>
#include <QMap>
#include <QObject>
#include <QSet>

class ConnectionCoordinator : public QObject {
    Q_OBJECT
public:
    explicit ConnectionCoordinator(QObject* parent = nullptr);

    ConnectionState state() const;
    bool isBusy() const;
    void bindAdapters(const QMap<PlatformId, IChatPlatformAdapter*>& adapters);

public slots:
    void connectAll(const AppSettingsSnapshot& snapshot);
    void disconnectAll();

signals:
    void stateChanged(ConnectionState state);
    void connectProgress(PlatformId platform, const QString& phase);
    void connectFinished(const ConnectSessionResult& result);
    void disconnectFinished();
    void warningRaised(const QString& code, const QString& message);

private slots:
    void onAdapterConnected(PlatformId platform);
    void onAdapterDisconnected(PlatformId platform);
    void onAdapterError(PlatformId platform, const QString& code, const QString& message);

private:
    void setState(ConnectionState next);
    void resetConnectTracking();
    void finalizeConnectResult();
    PlatformSettings settingsForPlatform(const AppSettingsSnapshot& snapshot, PlatformId platform) const;

    ConnectionState m_state = ConnectionState::IDLE;
    QMap<PlatformId, IChatPlatformAdapter*> m_adapters;

    QSet<PlatformId> m_targets;
    QSet<PlatformId> m_pending;
    QSet<PlatformId> m_connected;
    QMap<PlatformId, QString> m_failed;

    bool m_pendingDisconnect = false;
};

#endif // CONNECTION_COORDINATOR_H
