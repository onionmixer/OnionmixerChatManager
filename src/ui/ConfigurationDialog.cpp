#include "ui/ConfigurationDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QFormLayout>
#include <QFontMetrics>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextEdit>
#include <QHeaderView>
#include <QTabWidget>
#include <QUrl>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace {
QString tokenStateText(TokenState state)
{
    switch (state) {
    case TokenState::NO_TOKEN:
        return QStringLiteral("NO_TOKEN");
    case TokenState::VALID:
        return QStringLiteral("VALID");
    case TokenState::EXPIRING_SOON:
        return QStringLiteral("EXPIRING_SOON");
    case TokenState::EXPIRED:
        return QStringLiteral("EXPIRED");
    case TokenState::REFRESHING:
        return QStringLiteral("REFRESHING");
    case TokenState::AUTH_REQUIRED:
        return QStringLiteral("AUTH_REQUIRED");
    case TokenState::ERROR:
        return QStringLiteral("ERROR");
    }
    return QStringLiteral("UNKNOWN");
}

bool isValidLoopbackUri(const QString& uri, const QString& expectedPath)
{
    const QUrl parsed(uri);
    return parsed.isValid() && parsed.scheme() == QStringLiteral("http") && parsed.host() == QStringLiteral("127.0.0.1")
        && parsed.path() == expectedPath && parsed.port() > 0;
}

bool isValidHttpsUri(const QString& uri)
{
    const QUrl parsed(uri);
    return parsed.isValid() && parsed.scheme() == QStringLiteral("https") && !parsed.host().trimmed().isEmpty();
}

QString normalizeGoogleOAuthClientId(const QString& raw)
{
    QString value = raw.trimmed();
    if (value.isEmpty()) {
        return value;
    }

    static const QRegularExpression reExtract(QStringLiteral("([A-Za-z0-9._-]+\\.googleusercontent\\.com)"));
    const QRegularExpressionMatch m = reExtract.match(value);
    if (m.hasMatch()) {
        value = m.captured(1);
    }

    if ((value.startsWith('"') && value.endsWith('"')) || (value.startsWith('\'') && value.endsWith('\''))) {
        value = value.mid(1, value.size() - 2).trimmed();
    }
    return value;
}

bool isLikelyGoogleOAuthClientId(const QString& clientId)
{
    const QString normalized = normalizeGoogleOAuthClientId(clientId);
    return !normalized.isEmpty()
        && !normalized.contains(' ')
        && normalized.endsWith(QStringLiteral(".googleusercontent.com"), Qt::CaseInsensitive);
}

QString googleClientIdValidationMessage(const QString& parsedClientId)
{
    if (parsedClientId.isEmpty()) {
        return QStringLiteral("YouTube client_id format is invalid. Parsed value is empty.");
    }
    if (!parsedClientId.contains(QStringLiteral("googleusercontent.com"), Qt::CaseInsensitive)
        && parsedClientId.contains('.')) {
        return QStringLiteral(
            "YouTube client_id format is invalid. Parsed='%1'. This looks like a bundle/package id. "
            "Use OAuth Client ID ending with *.googleusercontent.com.")
            .arg(parsedClientId);
    }
    return QStringLiteral("YouTube client_id format is invalid. Parsed='%1'. Expected domain: *.googleusercontent.com")
        .arg(parsedClientId);
}

void applyOperationStyle(QLabel* label, bool busy, bool lastResultKnown, bool lastResultOk)
{
    if (!label) {
        return;
    }

    if (busy) {
        label->setStyleSheet(QStringLiteral("QLabel { color: #9a5a00; background: #fff4e5; padding: 2px 6px; border-radius: 4px; font-weight: 600; }"));
        return;
    }

    if (!lastResultKnown) {
        label->setStyleSheet(QStringLiteral("QLabel { color: #4a4a4a; background: #f2f2f2; padding: 2px 6px; border-radius: 4px; }"));
        return;
    }

    if (lastResultOk) {
        label->setStyleSheet(QStringLiteral("QLabel { color: #0a5c2b; background: #e7f6ed; padding: 2px 6px; border-radius: 4px; }"));
        return;
    }

    label->setStyleSheet(QStringLiteral("QLabel { color: #8a1f11; background: #fdecea; padding: 2px 6px; border-radius: 4px; }"));
}

void applyAuditResultStyle(QTableWidgetItem* item, bool ok)
{
    if (!item) {
        return;
    }

    if (ok) {
        item->setForeground(QBrush(QColor(QStringLiteral("#0a5c2b"))));
        item->setBackground(QBrush(QColor(QStringLiteral("#e7f6ed"))));
        return;
    }

    item->setForeground(QBrush(QColor(QStringLiteral("#8a1f11"))));
    item->setBackground(QBrush(QColor(QStringLiteral("#fdecea"))));
}

void applyAuditPlatformStyle(QTableWidgetItem* item, PlatformId platform)
{
    if (!item) {
        return;
    }

    if (platform == PlatformId::YouTube) {
        item->setForeground(QBrush(QColor(QStringLiteral("#103a8c"))));
        item->setBackground(QBrush(QColor(QStringLiteral("#eaf2ff"))));
        return;
    }

    item->setForeground(QBrush(QColor(QStringLiteral("#0f4c3a"))));
    item->setBackground(QBrush(QColor(QStringLiteral("#e8f8f2"))));
}

QString actionLabel(const QString& actionId)
{
    if (actionId == QStringLiteral("token_refresh")) {
        return QStringLiteral("TOKEN_REFRESH");
    }
    if (actionId == QStringLiteral("interactive_auth")) {
        return QStringLiteral("INTERACTIVE_AUTH");
    }
    if (actionId == QStringLiteral("token_delete")) {
        return QStringLiteral("TOKEN_DELETE");
    }
    if (actionId == QStringLiteral("token_revoke")) {
        return QStringLiteral("TOKEN_REVOKE");
    }
    if (actionId == QStringLiteral("refresh_token")) {
        return QStringLiteral("REFRESH_GRANT");
    }
    if (actionId == QStringLiteral("authorization_code")) {
        return QStringLiteral("AUTH_CODE_GRANT");
    }
    return actionId.toUpper();
}

void applyAuditActionStyle(QTableWidgetItem* item, const QString& actionId)
{
    if (!item) {
        return;
    }

    if (actionId == QStringLiteral("token_refresh") || actionId == QStringLiteral("refresh_token")) {
        item->setForeground(QBrush(QColor(QStringLiteral("#0a4d70"))));
        item->setBackground(QBrush(QColor(QStringLiteral("#e8f4fb"))));
        return;
    }

    if (actionId == QStringLiteral("interactive_auth") || actionId == QStringLiteral("authorization_code")) {
        item->setForeground(QBrush(QColor(QStringLiteral("#7a3f00"))));
        item->setBackground(QBrush(QColor(QStringLiteral("#fff4e5"))));
        return;
    }

    if (actionId == QStringLiteral("token_delete")) {
        item->setForeground(QBrush(QColor(QStringLiteral("#5a2b86"))));
        item->setBackground(QBrush(QColor(QStringLiteral("#f3ebff"))));
        return;
    }
    if (actionId == QStringLiteral("token_revoke")) {
        item->setForeground(QBrush(QColor(QStringLiteral("#5a2b86"))));
        item->setBackground(QBrush(QColor(QStringLiteral("#f3ebff"))));
        return;
    }

    item->setForeground(QBrush(QColor(QStringLiteral("#333333"))));
    item->setBackground(QBrush(QColor(QStringLiteral("#f5f5f5"))));
}

QString buildAuditCopyAllText(const QString& summary, const QString& detail)
{
    const QString normalizedSummary = summary.trimmed();
    const QString normalizedDetail = detail.trimmed().isEmpty() ? QStringLiteral("-") : detail.trimmed();
    const QString copiedAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QString copiedAtLocal = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);

    return QStringLiteral(
               "=== TOKEN AUDIT ===\n"
               "copied_at_utc: %1\n"
               "copied_at_local: %2\n"
               "\n"
               "=== SUMMARY ===\n"
               "%3\n"
               "\n"
               "=== DETAIL ===\n"
               "%4\n"
               "===================\n")
        .arg(copiedAtUtc, copiedAtLocal, normalizedSummary, normalizedDetail);
}
} // namespace

ConfigurationDialog::ConfigurationDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("dlgConfiguration"));
    setWindowTitle(QStringLiteral("Configuration"));
    resize(900, 700);

    m_tabConfig = new QTabWidget(this);
    m_tabConfig->setObjectName(QStringLiteral("tabConfig"));
    m_tabConfig->addTab(createGeneralTab(), QStringLiteral("General"));
    m_tabConfig->addTab(createYouTubeTab(), QStringLiteral("YouTube"));
    m_tabConfig->addTab(createChzzkTab(), QStringLiteral("CHZZK"));
    m_tabConfig->addTab(createSecurityTab(), QStringLiteral("Security"));

    auto* btnApply = new QPushButton(QStringLiteral("Apply"), this);
    btnApply->setObjectName(QStringLiteral("btnCfgApply"));
    auto* btnClose = new QPushButton(QStringLiteral("Close"), this);
    btnClose->setObjectName(QStringLiteral("btnCfgClose"));

    auto* buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    buttonLayout->addWidget(btnApply);
    buttonLayout->addWidget(btnClose);

    m_statusBar = new QStatusBar(this);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->addWidget(m_tabConfig);
    rootLayout->addLayout(buttonLayout);
    rootLayout->addWidget(m_statusBar);

    connect(btnApply, &QPushButton::clicked, this, &ConfigurationDialog::onApplyClicked);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::reject);

    connect(m_ytBtnTokenRefresh, &QPushButton::clicked, this, [this]() { emit tokenRefreshRequested(PlatformId::YouTube, collectPlatformSettings(PlatformId::YouTube)); });
    connect(m_ytBtnReauthBrowser, &QPushButton::clicked, this, [this]() { emit interactiveAuthRequested(PlatformId::YouTube, collectPlatformSettings(PlatformId::YouTube)); });
    connect(m_ytBtnTokenDelete, &QPushButton::clicked, this, [this]() { emit tokenDeleteRequested(PlatformId::YouTube); });
    connect(m_ytBtnTestConfig, &QPushButton::clicked, this, &ConfigurationDialog::onYouTubeTestConfigClicked);

    connect(m_chzBtnTokenRefresh, &QPushButton::clicked, this, [this]() { emit tokenRefreshRequested(PlatformId::Chzzk, collectPlatformSettings(PlatformId::Chzzk)); });
    connect(m_chzBtnReauthBrowser, &QPushButton::clicked, this, [this]() { emit interactiveAuthRequested(PlatformId::Chzzk, collectPlatformSettings(PlatformId::Chzzk)); });
    connect(m_chzBtnTokenDelete, &QPushButton::clicked, this, [this]() { emit tokenDeleteRequested(PlatformId::Chzzk); });
    connect(m_chzBtnTestConfig, &QPushButton::clicked, this, &ConfigurationDialog::onChzzkTestConfigClicked);

    m_tokenBusy.insert(PlatformId::YouTube, false);
    m_tokenBusy.insert(PlatformId::Chzzk, false);
    applyOperationStyle(m_ytLblOperation, false, false, false);
    applyOperationStyle(m_chzLblOperation, false, false, false);
    updateTokenUiLockState();
}

void ConfigurationDialog::setSnapshot(const AppSettingsSnapshot& snapshot)
{
    m_cmbLanguage->setCurrentText(snapshot.language);
    m_cmbLogLevel->setCurrentText(snapshot.logLevel);
    m_cmbMergeOrder->setCurrentText(snapshot.mergeOrder);
    m_chkAutoReconnect->setChecked(snapshot.autoReconnect);

    m_ytChkEnabled->setChecked(snapshot.youtube.enabled);
    m_ytEdtClientId->setText(snapshot.youtube.clientId);
    m_ytEdtClientSecret->setText(snapshot.youtube.clientSecret);
    m_ytEdtRedirectUri->setText(snapshot.youtube.redirectUri);
    m_ytEdtAuthEndpoint->setText(snapshot.youtube.authEndpoint);
    m_ytEdtTokenEndpoint->setText(snapshot.youtube.tokenEndpoint);
    m_ytEdtScope->setPlainText(snapshot.youtube.scope);
    m_ytEdtChannelId->setText(snapshot.youtube.channelId);
    m_ytEdtChannelHandle->setText(snapshot.youtube.accountLabel);
    m_ytLblAccount->setText(snapshot.youtube.channelId.isEmpty() ? QStringLiteral("-") : snapshot.youtube.channelId);

    m_chzChkEnabled->setChecked(snapshot.chzzk.enabled);
    m_chzEdtClientId->setText(snapshot.chzzk.clientId);
    m_chzEdtClientSecret->setText(snapshot.chzzk.clientSecret);
    m_chzEdtRedirectUri->setText(snapshot.chzzk.redirectUri);
    m_chzEdtAuthEndpoint->setText(snapshot.chzzk.authEndpoint);
    m_chzEdtTokenEndpoint->setText(snapshot.chzzk.tokenEndpoint);
    m_chzEdtScope->setPlainText(snapshot.chzzk.scope);
    m_chzEdtChannelId->setText(snapshot.chzzk.channelId);
    m_chzEdtChannelName->setText(snapshot.chzzk.channelName);
    m_chzEdtAccountLabel->setText(snapshot.chzzk.accountLabel);
    m_chzLblAccount->setText(snapshot.chzzk.channelId.isEmpty() ? QStringLiteral("-") : snapshot.chzzk.channelId);
}

void ConfigurationDialog::onTokenOperationStarted(PlatformId platform, const QString& operation)
{
    m_tokenBusy.insert(platform, true);
    m_tokenBusyOperation.insert(platform, operation);
    if (platform == PlatformId::YouTube) {
        if (m_ytLblOperation) {
            m_ytLblOperation->setText(QStringLiteral("BUSY: %1").arg(operation));
        }
        applyOperationStyle(m_ytLblOperation, true, false, false);
    } else {
        if (m_chzLblOperation) {
            m_chzLblOperation->setText(QStringLiteral("BUSY: %1").arg(operation));
        }
        applyOperationStyle(m_chzLblOperation, true, false, false);
    }
    updateTokenUiLockState();
}

void ConfigurationDialog::onTokenStateUpdated(PlatformId platform, TokenState state, const QString& detail)
{
    const QString stateText = tokenStateText(state);
    if (platform == PlatformId::YouTube) {
        m_ytLblTokenState->setText(stateText);
        if (state == TokenState::NO_TOKEN) {
            m_ytLblAccessExpireAt->setText(QStringLiteral("-"));
        }
        if (!detail.isEmpty()) {
            m_ytLblLastRefreshResult->setText(detail);
        }
        return;
    }

    m_chzLblTokenState->setText(stateText);
    if (state == TokenState::NO_TOKEN) {
        m_chzLblAccessExpireAt->setText(QStringLiteral("-"));
    }
    if (!detail.isEmpty()) {
        m_chzLblLastRefreshResult->setText(detail);
    }
}

void ConfigurationDialog::onTokenActionFinished(PlatformId platform, bool ok, const QString& message)
{
    m_tokenBusy.insert(platform, false);
    m_tokenBusyOperation.remove(platform);
    if (platform == PlatformId::YouTube) {
        if (m_ytLblOperation) {
            m_ytLblOperation->setText(QStringLiteral("IDLE"));
        }
        applyOperationStyle(m_ytLblOperation, false, true, ok);
    } else {
        if (m_chzLblOperation) {
            m_chzLblOperation->setText(QStringLiteral("IDLE"));
        }
        applyOperationStyle(m_chzLblOperation, false, true, ok);
    }
    updateTokenUiLockState();

    const QString prefix = ok ? QStringLiteral("OK") : QStringLiteral("FAIL");
    if (platform == PlatformId::YouTube) {
        m_ytLblLastRefreshResult->setText(prefix + QStringLiteral(": ") + message);
    } else {
        m_chzLblLastRefreshResult->setText(prefix + QStringLiteral(": ") + message);
    }
}

void ConfigurationDialog::onTokenRecordUpdated(PlatformId platform, TokenState state, const TokenRecord& record, const QString& detail)
{
    onTokenStateUpdated(platform, state, detail);

    const QString exp = record.accessExpireAtUtc.isValid()
        ? record.accessExpireAtUtc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : QStringLiteral("-");

    if (platform == PlatformId::YouTube) {
        m_ytLblAccessExpireAt->setText(exp);
        return;
    }
    m_chzLblAccessExpireAt->setText(exp);
}

void ConfigurationDialog::onTokenAuditAppended(PlatformId platform, const QString& action, bool ok, const QString& detail)
{
    if (!m_tblTokenAudit) {
        return;
    }

    const int row = m_tblTokenAudit->rowCount();
    m_tblTokenAudit->insertRow(row);

    const QString timeText = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const QString platformText = platform == PlatformId::YouTube ? QStringLiteral("YouTube") : QStringLiteral("CHZZK");
    const QString resultText = ok ? QStringLiteral("OK") : QStringLiteral("FAIL");
    const QString fullDetail = detail.trimmed();

    auto* timeItem = new QTableWidgetItem(timeText);
    m_tblTokenAudit->setItem(row, 0, timeItem);
    auto* platformItem = new QTableWidgetItem(platformText);
    applyAuditPlatformStyle(platformItem, platform);
    m_tblTokenAudit->setItem(row, 1, platformItem);
    auto* actionItem = new QTableWidgetItem(actionLabel(action));
    applyAuditActionStyle(actionItem, action);
    m_tblTokenAudit->setItem(row, 2, actionItem);
    auto* resultItem = new QTableWidgetItem(resultText);
    applyAuditResultStyle(resultItem, ok);
    m_tblTokenAudit->setItem(row, 3, resultItem);
    auto* detailItem = new QTableWidgetItem(elideAuditDetailText(fullDetail));
    detailItem->setData(Qt::UserRole, fullDetail);
    detailItem->setToolTip(fullDetail);
    m_tblTokenAudit->setItem(row, 4, detailItem);

    timeItem->setToolTip(timeText);
    platformItem->setToolTip(platformText);
    actionItem->setToolTip(actionLabel(action));
    resultItem->setToolTip(resultText);
    m_tblTokenAudit->scrollToBottom();
}

QString ConfigurationDialog::elideAuditDetailText(const QString& fullDetail) const
{
    if (!m_tblTokenAudit) {
        return fullDetail.simplified();
    }

    const QString normalized = fullDetail.simplified();
    const int available = qMax(30, m_tblTokenAudit->columnWidth(4) - 18);
    const QFontMetrics fm(m_tblTokenAudit->font());
    return fm.elidedText(normalized, Qt::ElideRight, available);
}

void ConfigurationDialog::refreshAuditDetailElision()
{
    if (!m_tblTokenAudit) {
        return;
    }

    for (int row = 0; row < m_tblTokenAudit->rowCount(); ++row) {
        QTableWidgetItem* detailItem = m_tblTokenAudit->item(row, 4);
        if (!detailItem) {
            continue;
        }

        const QString fullDetail = detailItem->data(Qt::UserRole).toString();
        if (fullDetail.isEmpty()) {
            continue;
        }

        detailItem->setText(elideAuditDetailText(fullDetail));
        detailItem->setToolTip(fullDetail);
    }
}

void ConfigurationDialog::updateTokenUiLockState()
{
    const bool ytBusy = m_tokenBusy.value(PlatformId::YouTube, false);
    const bool chzBusy = m_tokenBusy.value(PlatformId::Chzzk, false);
    const bool interactiveInProgress = (ytBusy && m_tokenBusyOperation.value(PlatformId::YouTube) == QStringLiteral("interactive_auth"))
        || (chzBusy && m_tokenBusyOperation.value(PlatformId::Chzzk) == QStringLiteral("interactive_auth"));

    setYouTubeConfigEditable(!ytBusy);
    setChzzkConfigEditable(!chzBusy);

    if (interactiveInProgress) {
        if (!ytBusy) {
            setTokenButtonsEnabled(PlatformId::YouTube, false);
        }
        if (!chzBusy) {
            setTokenButtonsEnabled(PlatformId::Chzzk, false);
        }
    }

    const QString ytReason = ytBusy
        ? QStringLiteral("Token operation in progress: %1").arg(m_tokenBusyOperation.value(PlatformId::YouTube))
        : (interactiveInProgress ? QStringLiteral("Interactive auth is running on another platform") : QString());
    const QString chzReason = chzBusy
        ? QStringLiteral("Token operation in progress: %1").arg(m_tokenBusyOperation.value(PlatformId::Chzzk))
        : (interactiveInProgress ? QStringLiteral("Interactive auth is running on another platform") : QString());

    if (m_ytBtnTokenRefresh) m_ytBtnTokenRefresh->setToolTip(m_ytBtnTokenRefresh->isEnabled() ? QString() : ytReason);
    if (m_ytBtnReauthBrowser) m_ytBtnReauthBrowser->setToolTip(m_ytBtnReauthBrowser->isEnabled() ? QString() : ytReason);
    if (m_ytBtnTokenDelete) m_ytBtnTokenDelete->setToolTip(m_ytBtnTokenDelete->isEnabled() ? QString() : ytReason);
    if (m_chzBtnTokenRefresh) m_chzBtnTokenRefresh->setToolTip(m_chzBtnTokenRefresh->isEnabled() ? QString() : chzReason);
    if (m_chzBtnReauthBrowser) m_chzBtnReauthBrowser->setToolTip(m_chzBtnReauthBrowser->isEnabled() ? QString() : chzReason);
    if (m_chzBtnTokenDelete) m_chzBtnTokenDelete->setToolTip(m_chzBtnTokenDelete->isEnabled() ? QString() : chzReason);
}

void ConfigurationDialog::setYouTubeConfigEditable(bool enabled)
{
    if (m_ytChkEnabled) m_ytChkEnabled->setEnabled(enabled);
    if (m_ytEdtClientId) m_ytEdtClientId->setEnabled(enabled);
    if (m_ytEdtClientSecret) m_ytEdtClientSecret->setEnabled(enabled);
    if (m_ytEdtRedirectUri) m_ytEdtRedirectUri->setEnabled(enabled);
    if (m_ytEdtAuthEndpoint) m_ytEdtAuthEndpoint->setEnabled(enabled);
    if (m_ytEdtTokenEndpoint) m_ytEdtTokenEndpoint->setEnabled(enabled);
    if (m_ytEdtScope) m_ytEdtScope->setEnabled(enabled);
    if (m_ytEdtChannelId) m_ytEdtChannelId->setEnabled(enabled);
    if (m_ytEdtChannelHandle) m_ytEdtChannelHandle->setEnabled(enabled);
    if (m_ytBtnTokenRefresh) m_ytBtnTokenRefresh->setEnabled(enabled);
    if (m_ytBtnReauthBrowser) m_ytBtnReauthBrowser->setEnabled(enabled);
    if (m_ytBtnTokenDelete) m_ytBtnTokenDelete->setEnabled(enabled);
    if (m_ytBtnTestConfig) m_ytBtnTestConfig->setEnabled(enabled);
}

void ConfigurationDialog::setChzzkConfigEditable(bool enabled)
{
    if (m_chzChkEnabled) m_chzChkEnabled->setEnabled(enabled);
    if (m_chzEdtClientId) m_chzEdtClientId->setEnabled(enabled);
    if (m_chzEdtClientSecret) m_chzEdtClientSecret->setEnabled(enabled);
    if (m_chzEdtRedirectUri) m_chzEdtRedirectUri->setEnabled(enabled);
    if (m_chzEdtAuthEndpoint) m_chzEdtAuthEndpoint->setEnabled(enabled);
    if (m_chzEdtTokenEndpoint) m_chzEdtTokenEndpoint->setEnabled(enabled);
    if (m_chzEdtScope) m_chzEdtScope->setEnabled(enabled);
    if (m_chzEdtChannelId) m_chzEdtChannelId->setEnabled(enabled);
    if (m_chzEdtChannelName) m_chzEdtChannelName->setEnabled(enabled);
    if (m_chzEdtAccountLabel) m_chzEdtAccountLabel->setEnabled(enabled);
    if (m_chzBtnTokenRefresh) m_chzBtnTokenRefresh->setEnabled(enabled);
    if (m_chzBtnReauthBrowser) m_chzBtnReauthBrowser->setEnabled(enabled);
    if (m_chzBtnTokenDelete) m_chzBtnTokenDelete->setEnabled(enabled);
    if (m_chzBtnTestConfig) m_chzBtnTestConfig->setEnabled(enabled);
}

void ConfigurationDialog::setTokenButtonsEnabled(PlatformId platform, bool enabled)
{
    if (platform == PlatformId::YouTube) {
        if (m_ytBtnTokenRefresh) m_ytBtnTokenRefresh->setEnabled(enabled);
        if (m_ytBtnReauthBrowser) m_ytBtnReauthBrowser->setEnabled(enabled);
        if (m_ytBtnTokenDelete) m_ytBtnTokenDelete->setEnabled(enabled);
        return;
    }
    if (m_chzBtnTokenRefresh) m_chzBtnTokenRefresh->setEnabled(enabled);
    if (m_chzBtnReauthBrowser) m_chzBtnReauthBrowser->setEnabled(enabled);
    if (m_chzBtnTokenDelete) m_chzBtnTokenDelete->setEnabled(enabled);
}

void ConfigurationDialog::onApplyClicked()
{
    const AppSettingsSnapshot snapshot = collectSnapshot();
    QString errorMessage;
    if (!validateSnapshot(snapshot, &errorMessage)) {
        m_statusBar->showMessage(errorMessage, 8000);
        return;
    }

    emit configApplyRequested(snapshot);
    m_statusBar->showMessage(QStringLiteral("Configuration applied."), 3000);
}

void ConfigurationDialog::onYouTubeTestConfigClicked()
{
    emit platformConfigValidationRequested(PlatformId::YouTube, collectPlatformSettings(PlatformId::YouTube));
}

void ConfigurationDialog::onChzzkTestConfigClicked()
{
    emit platformConfigValidationRequested(PlatformId::Chzzk, collectPlatformSettings(PlatformId::Chzzk));
}

void ConfigurationDialog::onTokenAuditCellDoubleClicked(int row, int column)
{
    Q_UNUSED(column)
    if (!m_tblTokenAudit) {
        return;
    }
    if (row < 0 || row >= m_tblTokenAudit->rowCount()) {
        return;
    }

    QTableWidgetItem* detailItem = m_tblTokenAudit->item(row, 4);
    if (!detailItem) {
        return;
    }

    const QString fullDetail = detailItem->data(Qt::UserRole).toString().trimmed();
    if (fullDetail.isEmpty()) {
        return;
    }

    const QString timeText = m_tblTokenAudit->item(row, 0) ? m_tblTokenAudit->item(row, 0)->text() : QStringLiteral("-");
    const QString platformText = m_tblTokenAudit->item(row, 1) ? m_tblTokenAudit->item(row, 1)->text() : QStringLiteral("-");
    const QString actionText = m_tblTokenAudit->item(row, 2) ? m_tblTokenAudit->item(row, 2)->text() : QStringLiteral("-");
    const QString resultText = m_tblTokenAudit->item(row, 3) ? m_tblTokenAudit->item(row, 3)->text() : QStringLiteral("-");
    const QString summaryText = QStringLiteral("Time: %1\nPlatform: %2\nAction: %3\nResult: %4")
                                    .arg(timeText, platformText, actionText, resultText);

    auto* dialog = new QDialog(this);
    dialog->setWindowTitle(QStringLiteral("Token Audit Detail"));
    dialog->resize(760, 460);

    auto* layout = new QVBoxLayout(dialog);

    auto* lblSummary = new QLabel(summaryText, dialog);
    lblSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* txtDetail = new QTextEdit(dialog);
    txtDetail->setReadOnly(true);
    txtDetail->setPlainText(fullDetail);

    auto* buttons = new QDialogButtonBox(Qt::Horizontal, dialog);
    auto* btnCopySummary = buttons->addButton(QStringLiteral("Copy Summary"), QDialogButtonBox::ActionRole);
    auto* btnCopy = buttons->addButton(QStringLiteral("Copy Detail"), QDialogButtonBox::ActionRole);
    auto* btnCopyAll = buttons->addButton(QStringLiteral("Copy All"), QDialogButtonBox::ActionRole);
    auto* btnClose = buttons->addButton(QDialogButtonBox::Close);

    connect(btnCopySummary, &QPushButton::clicked, this, [this, summaryText]() {
        if (QGuiApplication::clipboard()) {
            QGuiApplication::clipboard()->setText(summaryText);
            if (m_statusBar) {
                m_statusBar->showMessage(QStringLiteral("Token audit summary copied."), 2000);
            }
        }
    });
    connect(btnCopy, &QPushButton::clicked, this, [this, txtDetail]() {
        if (QGuiApplication::clipboard()) {
            QGuiApplication::clipboard()->setText(txtDetail->toPlainText());
            if (m_statusBar) {
                m_statusBar->showMessage(QStringLiteral("Token audit detail copied."), 2000);
            }
        }
    });
    connect(btnCopyAll, &QPushButton::clicked, this, [this, summaryText, txtDetail]() {
        if (QGuiApplication::clipboard()) {
            const QString merged = buildAuditCopyAllText(summaryText, txtDetail->toPlainText());
            QGuiApplication::clipboard()->setText(merged);
            if (m_statusBar) {
                m_statusBar->showMessage(QStringLiteral("Token audit summary+detail copied."), 2000);
            }
        }
    });
    connect(btnClose, &QPushButton::clicked, dialog, &QDialog::accept);

    layout->addWidget(lblSummary);
    layout->addWidget(txtDetail, 1);
    layout->addWidget(buttons);

    dialog->exec();
    dialog->deleteLater();
}

QWidget* ConfigurationDialog::createGeneralTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QFormLayout(page);

    m_cmbLanguage = new QComboBox(page);
    m_cmbLanguage->setObjectName(QStringLiteral("cmbLanguage"));
    m_cmbLanguage->addItems({ QStringLiteral("ko_KR"), QStringLiteral("en_US") });

    m_cmbLogLevel = new QComboBox(page);
    m_cmbLogLevel->setObjectName(QStringLiteral("cmbLogLevel"));
    m_cmbLogLevel->addItems({ QStringLiteral("debug"), QStringLiteral("info"), QStringLiteral("warn"), QStringLiteral("error") });

    m_cmbMergeOrder = new QComboBox(page);
    m_cmbMergeOrder->setObjectName(QStringLiteral("cmbMergeOrder"));
    m_cmbMergeOrder->addItems({ QStringLiteral("timestamp") });

    m_chkAutoReconnect = new QCheckBox(QStringLiteral("Enable Auto Reconnect"), page);
    m_chkAutoReconnect->setObjectName(QStringLiteral("chkAutoReconnect"));

    layout->addRow(QStringLiteral("Language"), m_cmbLanguage);
    layout->addRow(QStringLiteral("Log Level"), m_cmbLogLevel);
    layout->addRow(QStringLiteral("Merge Order"), m_cmbMergeOrder);
    layout->addRow(QString(), m_chkAutoReconnect);

    return page;
}

QWidget* ConfigurationDialog::createYouTubeTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* form = new QFormLayout;
    m_ytChkEnabled = new QCheckBox(QStringLiteral("Enabled"), page);
    m_ytChkEnabled->setObjectName(QStringLiteral("ytChkEnabled"));

    m_ytEdtClientId = new QLineEdit(page);
    m_ytEdtClientId->setObjectName(QStringLiteral("ytEdtClientId"));

    m_ytEdtClientSecret = new QLineEdit(page);
    m_ytEdtClientSecret->setObjectName(QStringLiteral("ytEdtClientSecret"));
    m_ytEdtClientSecret->setEchoMode(QLineEdit::Password);
    m_ytEdtClientSecret->setPlaceholderText(QStringLiteral("Optional (required by some OAuth clients)"));

    m_ytEdtRedirectUri = new QLineEdit(page);
    m_ytEdtRedirectUri->setObjectName(QStringLiteral("ytEdtRedirectUri"));

    m_ytEdtAuthEndpoint = new QLineEdit(page);
    m_ytEdtAuthEndpoint->setObjectName(QStringLiteral("ytEdtAuthEndpoint"));

    m_ytEdtTokenEndpoint = new QLineEdit(page);
    m_ytEdtTokenEndpoint->setObjectName(QStringLiteral("ytEdtTokenEndpoint"));

    m_ytEdtScope = new QPlainTextEdit(page);
    m_ytEdtScope->setObjectName(QStringLiteral("ytEdtScope"));
    m_ytEdtScope->setFixedHeight(60);

    m_ytEdtChannelId = new QLineEdit(page);
    m_ytEdtChannelId->setObjectName(QStringLiteral("ytEdtChannelId"));

    m_ytEdtChannelHandle = new QLineEdit(page);
    m_ytEdtChannelHandle->setObjectName(QStringLiteral("ytEdtChannelHandle"));

    form->addRow(QString(), m_ytChkEnabled);
    form->addRow(QStringLiteral("Client ID"), m_ytEdtClientId);
    form->addRow(QStringLiteral("Client Secret (Optional)"), m_ytEdtClientSecret);
    form->addRow(QStringLiteral("Redirect URI"), m_ytEdtRedirectUri);
    form->addRow(QStringLiteral("Auth Endpoint"), m_ytEdtAuthEndpoint);
    form->addRow(QStringLiteral("Token Endpoint"), m_ytEdtTokenEndpoint);
    form->addRow(QStringLiteral("Scope"), m_ytEdtScope);
    form->addRow(QStringLiteral("Channel ID"), m_ytEdtChannelId);
    form->addRow(QStringLiteral("Channel Handle"), m_ytEdtChannelHandle);

    auto* statusBox = new QGroupBox(QStringLiteral("Token / Account"), page);
    auto* statusGrid = new QGridLayout(statusBox);
    m_ytLblAccount = new QLabel(QStringLiteral("-"), statusBox);
    m_ytLblAccount->setObjectName(QStringLiteral("ytLblAccount"));
    m_ytLblTokenState = new QLabel(QStringLiteral("NO_TOKEN"), statusBox);
    m_ytLblTokenState->setObjectName(QStringLiteral("ytLblTokenState"));
    m_ytLblAccessExpireAt = new QLabel(QStringLiteral("-"), statusBox);
    m_ytLblAccessExpireAt->setObjectName(QStringLiteral("ytLblAccessExpireAt"));
    m_ytLblLastRefreshResult = new QLabel(QStringLiteral("-"), statusBox);
    m_ytLblLastRefreshResult->setObjectName(QStringLiteral("ytLblLastRefreshResult"));
    m_ytLblOperation = new QLabel(QStringLiteral("IDLE"), statusBox);
    m_ytLblOperation->setObjectName(QStringLiteral("ytLblOperation"));

    statusGrid->addWidget(new QLabel(QStringLiteral("Account"), statusBox), 0, 0);
    statusGrid->addWidget(m_ytLblAccount, 0, 1);
    statusGrid->addWidget(new QLabel(QStringLiteral("Token State"), statusBox), 1, 0);
    statusGrid->addWidget(m_ytLblTokenState, 1, 1);
    statusGrid->addWidget(new QLabel(QStringLiteral("Access Expire At"), statusBox), 2, 0);
    statusGrid->addWidget(m_ytLblAccessExpireAt, 2, 1);
    statusGrid->addWidget(new QLabel(QStringLiteral("Last Result"), statusBox), 3, 0);
    statusGrid->addWidget(m_ytLblLastRefreshResult, 3, 1);
    statusGrid->addWidget(new QLabel(QStringLiteral("Operation"), statusBox), 4, 0);
    statusGrid->addWidget(m_ytLblOperation, 4, 1);

    auto* actionLayout = new QHBoxLayout;
    m_ytBtnTokenRefresh = new QPushButton(QStringLiteral("Token Refresh"), page);
    m_ytBtnTokenRefresh->setObjectName(QStringLiteral("ytBtnTokenRefresh"));
    m_ytBtnReauthBrowser = new QPushButton(QStringLiteral("Re-Auth Browser"), page);
    m_ytBtnReauthBrowser->setObjectName(QStringLiteral("ytBtnReauthBrowser"));
    m_ytBtnTokenDelete = new QPushButton(QStringLiteral("Delete Token"), page);
    m_ytBtnTokenDelete->setObjectName(QStringLiteral("ytBtnTokenDelete"));
    m_ytBtnTestConfig = new QPushButton(QStringLiteral("Test Config"), page);
    m_ytBtnTestConfig->setObjectName(QStringLiteral("ytBtnTestConfig"));

    actionLayout->addWidget(m_ytBtnTokenRefresh);
    actionLayout->addWidget(m_ytBtnReauthBrowser);
    actionLayout->addWidget(m_ytBtnTokenDelete);
    actionLayout->addWidget(m_ytBtnTestConfig);

    layout->addLayout(form);
    layout->addWidget(statusBox);
    layout->addLayout(actionLayout);
    layout->addStretch();
    return page;
}

QWidget* ConfigurationDialog::createChzzkTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* form = new QFormLayout;
    m_chzChkEnabled = new QCheckBox(QStringLiteral("Enabled"), page);
    m_chzChkEnabled->setObjectName(QStringLiteral("chzChkEnabled"));

    m_chzEdtClientId = new QLineEdit(page);
    m_chzEdtClientId->setObjectName(QStringLiteral("chzEdtClientId"));

    m_chzEdtClientSecret = new QLineEdit(page);
    m_chzEdtClientSecret->setObjectName(QStringLiteral("chzEdtClientSecret"));
    m_chzEdtClientSecret->setEchoMode(QLineEdit::Password);

    m_chzEdtRedirectUri = new QLineEdit(page);
    m_chzEdtRedirectUri->setObjectName(QStringLiteral("chzEdtRedirectUri"));

    m_chzEdtAuthEndpoint = new QLineEdit(page);
    m_chzEdtAuthEndpoint->setObjectName(QStringLiteral("chzEdtAuthEndpoint"));

    m_chzEdtTokenEndpoint = new QLineEdit(page);
    m_chzEdtTokenEndpoint->setObjectName(QStringLiteral("chzEdtTokenEndpoint"));

    m_chzEdtScope = new QPlainTextEdit(page);
    m_chzEdtScope->setObjectName(QStringLiteral("chzEdtScope"));
    m_chzEdtScope->setFixedHeight(60);

    m_chzEdtChannelId = new QLineEdit(page);
    m_chzEdtChannelId->setObjectName(QStringLiteral("chzEdtChannelId"));

    m_chzEdtChannelName = new QLineEdit(page);
    m_chzEdtChannelName->setObjectName(QStringLiteral("chzEdtChannelName"));

    m_chzEdtAccountLabel = new QLineEdit(page);
    m_chzEdtAccountLabel->setObjectName(QStringLiteral("chzEdtAccountLabel"));

    form->addRow(QString(), m_chzChkEnabled);
    form->addRow(QStringLiteral("Client ID"), m_chzEdtClientId);
    form->addRow(QStringLiteral("Client Secret"), m_chzEdtClientSecret);
    form->addRow(QStringLiteral("Redirect URI"), m_chzEdtRedirectUri);
    form->addRow(QStringLiteral("Auth Endpoint"), m_chzEdtAuthEndpoint);
    form->addRow(QStringLiteral("Token Endpoint"), m_chzEdtTokenEndpoint);
    form->addRow(QStringLiteral("Scope"), m_chzEdtScope);
    form->addRow(QStringLiteral("Channel ID"), m_chzEdtChannelId);
    form->addRow(QStringLiteral("Channel Name"), m_chzEdtChannelName);
    form->addRow(QStringLiteral("Account Label"), m_chzEdtAccountLabel);

    auto* statusBox = new QGroupBox(QStringLiteral("Token / Account"), page);
    auto* statusGrid = new QGridLayout(statusBox);
    m_chzLblAccount = new QLabel(QStringLiteral("-"), statusBox);
    m_chzLblAccount->setObjectName(QStringLiteral("chzLblAccount"));
    m_chzLblTokenState = new QLabel(QStringLiteral("NO_TOKEN"), statusBox);
    m_chzLblTokenState->setObjectName(QStringLiteral("chzLblTokenState"));
    m_chzLblAccessExpireAt = new QLabel(QStringLiteral("-"), statusBox);
    m_chzLblAccessExpireAt->setObjectName(QStringLiteral("chzLblAccessExpireAt"));
    m_chzLblLastRefreshResult = new QLabel(QStringLiteral("-"), statusBox);
    m_chzLblLastRefreshResult->setObjectName(QStringLiteral("chzLblLastRefreshResult"));
    m_chzLblOperation = new QLabel(QStringLiteral("IDLE"), statusBox);
    m_chzLblOperation->setObjectName(QStringLiteral("chzLblOperation"));

    statusGrid->addWidget(new QLabel(QStringLiteral("Account"), statusBox), 0, 0);
    statusGrid->addWidget(m_chzLblAccount, 0, 1);
    statusGrid->addWidget(new QLabel(QStringLiteral("Token State"), statusBox), 1, 0);
    statusGrid->addWidget(m_chzLblTokenState, 1, 1);
    statusGrid->addWidget(new QLabel(QStringLiteral("Access Expire At"), statusBox), 2, 0);
    statusGrid->addWidget(m_chzLblAccessExpireAt, 2, 1);
    statusGrid->addWidget(new QLabel(QStringLiteral("Last Result"), statusBox), 3, 0);
    statusGrid->addWidget(m_chzLblLastRefreshResult, 3, 1);
    statusGrid->addWidget(new QLabel(QStringLiteral("Operation"), statusBox), 4, 0);
    statusGrid->addWidget(m_chzLblOperation, 4, 1);

    auto* actionLayout = new QHBoxLayout;
    m_chzBtnTokenRefresh = new QPushButton(QStringLiteral("Token Refresh"), page);
    m_chzBtnTokenRefresh->setObjectName(QStringLiteral("chzBtnTokenRefresh"));
    m_chzBtnReauthBrowser = new QPushButton(QStringLiteral("Re-Auth Browser"), page);
    m_chzBtnReauthBrowser->setObjectName(QStringLiteral("chzBtnReauthBrowser"));
    m_chzBtnTokenDelete = new QPushButton(QStringLiteral("Delete Token"), page);
    m_chzBtnTokenDelete->setObjectName(QStringLiteral("chzBtnTokenDelete"));
    m_chzBtnTestConfig = new QPushButton(QStringLiteral("Test Config"), page);
    m_chzBtnTestConfig->setObjectName(QStringLiteral("chzBtnTestConfig"));

    actionLayout->addWidget(m_chzBtnTokenRefresh);
    actionLayout->addWidget(m_chzBtnReauthBrowser);
    actionLayout->addWidget(m_chzBtnTokenDelete);
    actionLayout->addWidget(m_chzBtnTestConfig);

    layout->addLayout(form);
    layout->addWidget(statusBox);
    layout->addLayout(actionLayout);
    layout->addStretch();

    return page;
}

QWidget* ConfigurationDialog::createSecurityTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    auto* form = new QFormLayout;

    m_lblVaultProvider = new QLabel(QStringLiteral("QtKeychain (planned)"), page);
    m_lblVaultProvider->setObjectName(QStringLiteral("lblVaultProvider"));
    m_lblVaultHealth = new QLabel(QStringLiteral("UNKNOWN"), page);
    m_lblVaultHealth->setObjectName(QStringLiteral("lblVaultHealth"));

    form->addRow(QStringLiteral("Vault Provider"), m_lblVaultProvider);
    form->addRow(QStringLiteral("Vault Health"), m_lblVaultHealth);

    m_tblTokenAudit = new QTableWidget(page);
    m_tblTokenAudit->setObjectName(QStringLiteral("tblTokenAudit"));
    m_tblTokenAudit->setColumnCount(5);
    m_tblTokenAudit->setHorizontalHeaderLabels({ QStringLiteral("Time"), QStringLiteral("Platform"), QStringLiteral("Action"), QStringLiteral("Result"), QStringLiteral("Detail") });
    m_tblTokenAudit->verticalHeader()->setVisible(false);
    m_tblTokenAudit->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tblTokenAudit->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tblTokenAudit->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tblTokenAudit->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tblTokenAudit->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tblTokenAudit->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tblTokenAudit->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tblTokenAudit->horizontalHeader()->setStretchLastSection(true);
    connect(m_tblTokenAudit, &QTableWidget::cellDoubleClicked, this, &ConfigurationDialog::onTokenAuditCellDoubleClicked);
    connect(m_tblTokenAudit->horizontalHeader(), &QHeaderView::sectionResized, this,
        [this](int logicalIndex, int oldSize, int newSize) {
            Q_UNUSED(oldSize)
            Q_UNUSED(newSize)
            if (logicalIndex == 4) {
                refreshAuditDetailElision();
            }
        });

    m_btnClearTokenAudit = new QPushButton(QStringLiteral("Clear Audit"), page);
    m_btnClearTokenAudit->setObjectName(QStringLiteral("btnClearTokenAudit"));
    connect(m_btnClearTokenAudit, &QPushButton::clicked, this, [this]() {
        if (m_tblTokenAudit) {
            m_tblTokenAudit->setRowCount(0);
        }
    });

    layout->addLayout(form);
    layout->addWidget(m_tblTokenAudit, 1);
    layout->addWidget(m_btnClearTokenAudit, 0, Qt::AlignRight);
    return page;
}

AppSettingsSnapshot ConfigurationDialog::collectSnapshot() const
{
    AppSettingsSnapshot snapshot;
    snapshot.language = m_cmbLanguage->currentText();
    snapshot.logLevel = m_cmbLogLevel->currentText();
    snapshot.mergeOrder = m_cmbMergeOrder->currentText();
    snapshot.autoReconnect = m_chkAutoReconnect->isChecked();
    snapshot.youtube = collectPlatformSettings(PlatformId::YouTube);
    snapshot.chzzk = collectPlatformSettings(PlatformId::Chzzk);
    snapshot.loadedAtUtc = QDateTime::currentDateTimeUtc();
    return snapshot;
}

PlatformSettings ConfigurationDialog::collectPlatformSettings(PlatformId platform) const
{
    PlatformSettings s;
    if (platform == PlatformId::YouTube) {
        s.enabled = m_ytChkEnabled->isChecked();
        s.clientId = normalizeGoogleOAuthClientId(m_ytEdtClientId->text());
        s.clientSecret = m_ytEdtClientSecret->text().trimmed();
        s.redirectUri = m_ytEdtRedirectUri->text().trimmed();
        s.authEndpoint = m_ytEdtAuthEndpoint->text().trimmed();
        s.tokenEndpoint = m_ytEdtTokenEndpoint->text().trimmed();
        s.scope = m_ytEdtScope->toPlainText().trimmed();
        s.channelId = m_ytEdtChannelId->text().trimmed();
        s.accountLabel = m_ytEdtChannelHandle->text().trimmed();
        return s;
    }

    s.enabled = m_chzChkEnabled->isChecked();
    s.clientId = m_chzEdtClientId->text().trimmed();
    s.clientSecret = m_chzEdtClientSecret->text().trimmed();
    s.redirectUri = m_chzEdtRedirectUri->text().trimmed();
    s.authEndpoint = m_chzEdtAuthEndpoint->text().trimmed();
    s.tokenEndpoint = m_chzEdtTokenEndpoint->text().trimmed();
    s.scope = m_chzEdtScope->toPlainText().trimmed();
    s.channelId = m_chzEdtChannelId->text().trimmed();
    s.channelName = m_chzEdtChannelName->text().trimmed();
    s.accountLabel = m_chzEdtAccountLabel->text().trimmed();
    return s;
}

bool ConfigurationDialog::validateSnapshot(const AppSettingsSnapshot& snapshot, QString* errorMessage) const
{
    if (snapshot.youtube.enabled) {
        if (snapshot.youtube.clientId.isEmpty() || snapshot.youtube.redirectUri.isEmpty() || snapshot.youtube.authEndpoint.isEmpty() || snapshot.youtube.tokenEndpoint.isEmpty() || snapshot.youtube.scope.isEmpty()) {
            *errorMessage = QStringLiteral("YouTube enabled but required fields are missing.");
            return false;
        }
        if (!isLikelyGoogleOAuthClientId(snapshot.youtube.clientId)) {
            *errorMessage = googleClientIdValidationMessage(normalizeGoogleOAuthClientId(snapshot.youtube.clientId));
            return false;
        }
        if (!isValidLoopbackUri(snapshot.youtube.redirectUri, QStringLiteral("/youtube/callback"))) {
            *errorMessage = QStringLiteral("YouTube redirect_uri must be http://127.0.0.1:{port}/youtube/callback");
            return false;
        }
        if (!isValidHttpsUri(snapshot.youtube.authEndpoint) || !isValidHttpsUri(snapshot.youtube.tokenEndpoint)) {
            *errorMessage = QStringLiteral("YouTube auth/token endpoint must be https URL.");
            return false;
        }
    }

    if (snapshot.chzzk.enabled) {
        if (snapshot.chzzk.clientId.isEmpty() || snapshot.chzzk.clientSecret.isEmpty() || snapshot.chzzk.redirectUri.isEmpty() || snapshot.chzzk.authEndpoint.isEmpty() || snapshot.chzzk.tokenEndpoint.isEmpty() || snapshot.chzzk.scope.isEmpty()) {
            *errorMessage = QStringLiteral("CHZZK enabled but required fields are missing.");
            return false;
        }
        if (!isValidLoopbackUri(snapshot.chzzk.redirectUri, QStringLiteral("/chzzk/callback"))) {
            *errorMessage = QStringLiteral("CHZZK redirect_uri must be http://127.0.0.1:{port}/chzzk/callback");
            return false;
        }
        if (!isValidHttpsUri(snapshot.chzzk.authEndpoint) || !isValidHttpsUri(snapshot.chzzk.tokenEndpoint)) {
            *errorMessage = QStringLiteral("CHZZK auth/token endpoint must be https URL.");
            return false;
        }
    }

    return true;
}
