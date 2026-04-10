#include "core/ConnectionCoordinator.h"

ConnectionCoordinator::ConnectionCoordinator(QObject* parent)
    : QObject(parent)
{
}

ConnectionState ConnectionCoordinator::state() const
{
    return m_state;
}

bool ConnectionCoordinator::isBusy() const
{
    return m_state == ConnectionState::CONNECTING || m_state == ConnectionState::DISCONNECTING;
}

void ConnectionCoordinator::bindAdapters(const QMap<PlatformId, IChatPlatformAdapter*>& adapters)
{
    for (auto it = adapters.cbegin(); it != adapters.cend(); ++it) {
        IChatPlatformAdapter* adapter = it.value();
        if (!adapter) {
            continue;
        }

        if (m_adapters.contains(it.key())) {
            disconnect(m_adapters.value(it.key()), nullptr, this, nullptr);
        }

        m_adapters.insert(it.key(), adapter);
        connect(adapter, &IChatPlatformAdapter::connected, this, &ConnectionCoordinator::onAdapterConnected);
        connect(adapter, &IChatPlatformAdapter::disconnected, this, &ConnectionCoordinator::onAdapterDisconnected);
        connect(adapter, &IChatPlatformAdapter::error, this, &ConnectionCoordinator::onAdapterError);
    }
}

void ConnectionCoordinator::connectAll(const AppSettingsSnapshot& snapshot)
{
    if (m_state == ConnectionState::DISCONNECTING) {
        emit warningRaised(QStringLiteral("BUSY"), QStringLiteral("Disconnect in progress."));
        return;
    }

    if (m_state == ConnectionState::CONNECTING) {
        emit warningRaised(QStringLiteral("BUSY"), QStringLiteral("Connect already in progress."));
        return;
    }

    resetConnectTracking();

    if (snapshot.youtube.enabled && m_adapters.contains(PlatformId::YouTube)) {
        m_targets.insert(PlatformId::YouTube);
    }
    if (snapshot.chzzk.enabled && m_adapters.contains(PlatformId::Chzzk)) {
        m_targets.insert(PlatformId::Chzzk);
    }

    if (m_targets.isEmpty()) {
        setState(ConnectionState::ERROR);
        emit warningRaised(QStringLiteral("NO_ENABLED_PLATFORM"), QStringLiteral("No enabled platform in configuration."));

        ConnectSessionResult result;
        result.state = m_state;
        emit connectFinished(result);
        return;
    }

    setState(ConnectionState::CONNECTING);

    for (PlatformId platform : m_targets) {
        IChatPlatformAdapter* adapter = m_adapters.value(platform, nullptr);
        if (!adapter) {
            m_failed.insert(platform, QStringLiteral("Adapter not bound"));
            continue;
        }

        m_pending.insert(platform);
        emit connectProgress(platform, QStringLiteral("STARTING"));
        adapter->start(settingsForPlatform(snapshot, platform));
    }

    if (m_pending.isEmpty()) {
        finalizeConnectResult();
    }
}

void ConnectionCoordinator::disconnectAll()
{
    if (m_state == ConnectionState::DISCONNECTING) {
        emit warningRaised(QStringLiteral("BUSY"), QStringLiteral("Disconnect already in progress."));
        return;
    }

    if (m_state == ConnectionState::CONNECTING) {
        m_pendingDisconnect = true;
        emit warningRaised(QStringLiteral("QUEUED_DISCONNECT"), QStringLiteral("Disconnect queued until connect is finished."));
        return;
    }

    setState(ConnectionState::DISCONNECTING);
    m_pending.clear();

    for (auto it = m_adapters.cbegin(); it != m_adapters.cend(); ++it) {
        IChatPlatformAdapter* adapter = it.value();
        if (!adapter) {
            continue;
        }

        if (adapter->isConnected()) {
            m_pending.insert(it.key());
            adapter->stop();
        }
    }

    if (m_pending.isEmpty()) {
        setState(ConnectionState::IDLE);
        emit disconnectFinished();
    }
}

void ConnectionCoordinator::onAdapterConnected(PlatformId platform)
{
    if (m_state != ConnectionState::CONNECTING && m_state != ConnectionState::PARTIALLY_CONNECTED) {
        return;
    }

    if (!m_targets.contains(platform)) {
        return;
    }

    m_connected.insert(platform);
    m_failed.remove(platform);
    m_pending.remove(platform);
    emit connectProgress(platform, QStringLiteral("CONNECTED"));

    if (m_pending.isEmpty()) {
        finalizeConnectResult();
    }
}

void ConnectionCoordinator::onAdapterDisconnected(PlatformId platform)
{
    if (m_state == ConnectionState::DISCONNECTING) {
        m_pending.remove(platform);
        m_connected.remove(platform);
        if (m_pending.isEmpty()) {
            setState(ConnectionState::IDLE);
            emit disconnectFinished();
        }
        return;
    }

    if (m_state == ConnectionState::CONNECTED || m_state == ConnectionState::PARTIALLY_CONNECTED) {
        m_connected.remove(platform);
        if (m_connected.isEmpty()) {
            setState(ConnectionState::IDLE);
        } else {
            setState(ConnectionState::PARTIALLY_CONNECTED);
        }
    }
}

void ConnectionCoordinator::onAdapterError(PlatformId platform, const QString& code, const QString& message)
{
    emit warningRaised(QStringLiteral("%1:%2").arg(platformKey(platform), code), message);
    if (code.startsWith(QStringLiteral("TRACE_")) || code.startsWith(QStringLiteral("INFO_"))) {
        return;
    }

    if (m_state != ConnectionState::CONNECTING && m_state != ConnectionState::PARTIALLY_CONNECTED) {
        return;
    }
    if (!m_targets.contains(platform)) {
        return;
    }
    if (!m_pending.contains(platform)) {
        return;
    }

    m_pending.remove(platform);
    m_failed.insert(platform, message);
    emit connectProgress(platform, QStringLiteral("FAILED"));

    if (m_pending.isEmpty()) {
        finalizeConnectResult();
    }
}

void ConnectionCoordinator::setState(ConnectionState next)
{
    if (m_state == next) {
        return;
    }

    m_state = next;
    emit stateChanged(m_state);
}

void ConnectionCoordinator::resetConnectTracking()
{
    m_targets.clear();
    m_pending.clear();
    m_connected.clear();
    m_failed.clear();
    m_pendingDisconnect = false;
}

void ConnectionCoordinator::finalizeConnectResult()
{
    if (m_connected.isEmpty()) {
        setState(ConnectionState::ERROR);
    } else if (m_failed.isEmpty()) {
        setState(ConnectionState::CONNECTED);
    } else {
        setState(ConnectionState::PARTIALLY_CONNECTED);
    }

    ConnectSessionResult result;
    result.state = m_state;

    for (PlatformId platform : m_connected) {
        result.connectedPlatforms.append(platformKey(platform));
    }

    for (auto it = m_failed.cbegin(); it != m_failed.cend(); ++it) {
        result.failedPlatforms.insert(platformKey(it.key()), it.value());
    }

    emit connectFinished(result);

    if (m_pendingDisconnect) {
        m_pendingDisconnect = false;
        disconnectAll();
    }
}

PlatformSettings ConnectionCoordinator::settingsForPlatform(const AppSettingsSnapshot& snapshot, PlatformId platform) const
{
    if (platform == PlatformId::YouTube) {
        return snapshot.youtube;
    }
    return snapshot.chzzk;
}
