#include "ui/ConfigurationDialog.h"
#include "core/Constants.h"
#include "core/PlatformTraits.h"
#include "ui/ChatBubbleDelegate.h"
#include "ui/ChatMessageModel.h"
#include "ui/ViewerCountStyle.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QColorDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFontMetrics>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QClipboard>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QPainter>
#include <QResizeEvent>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextEdit>
#include <QHeaderView>
#include <QTabWidget>
#include <QUrl>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString cfgText(const char* sourceText)
{
    return QCoreApplication::translate("ConfigurationDialog", sourceText);
}

QString tokenStateText(TokenState state)
{
    switch (state) {
    case TokenState::NO_TOKEN:
        return cfgText("NO_TOKEN");
    case TokenState::VALID:
        return cfgText("VALID");
    case TokenState::EXPIRING_SOON:
        return cfgText("EXPIRING_SOON");
    case TokenState::EXPIRED:
        return cfgText("EXPIRED");
    case TokenState::REFRESHING:
        return cfgText("REFRESHING");
    case TokenState::AUTH_REQUIRED:
        return cfgText("AUTH_REQUIRED");
    case TokenState::ERROR:
        return cfgText("ERROR");
    }
    return cfgText("UNKNOWN");
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
        return cfgText("YouTube client_id format is invalid. Parsed value is empty.");
    }
    if (!parsedClientId.contains(QStringLiteral("googleusercontent.com"), Qt::CaseInsensitive)
        && parsedClientId.contains('.')) {
        return cfgText(
            "YouTube client_id format is invalid. Parsed='%1'. This looks like a bundle/package id. "
            "Use OAuth Client ID ending with *.googleusercontent.com.")
            .arg(parsedClientId);
    }
    return cfgText("YouTube client_id format is invalid. Parsed='%1'. Expected domain: *.googleusercontent.com")
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

    return cfgText(
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
    setWindowTitle(tr("Configuration"));
    resize(900, 700);

    m_tabConfig = new QTabWidget(this);
    m_tabConfig->setObjectName(QStringLiteral("tabConfig"));
    m_tabConfig->addTab(createGeneralTab(), tr("General"));
    m_tabConfig->addTab(createYouTubeTab(), tr("YouTube"));
    m_tabConfig->addTab(createChzzkTab(), tr("CHZZK"));
    m_tabConfig->addTab(createSecurityTab(), tr("Security"));
    m_tabConfig->addTab(createBroadcastTab(), tr("BroadChat"));

    auto* btnApply = new QPushButton(tr("Apply"), this);
    btnApply->setObjectName(QStringLiteral("btnCfgApply"));
    auto* btnClose = new QPushButton(tr("Close"), this);
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
    m_chkDetailLog->setChecked(snapshot.detailLogEnabled);
    if (!snapshot.chatFontFamily.isEmpty()) {
        m_fntChat->setCurrentFont(QFont(snapshot.chatFontFamily));
    } else {
        m_fntChat->setCurrentFont(QGuiApplication::font());
    }
    m_spnChatFontSize->setValue(snapshot.chatFontSize > 0 ? snapshot.chatFontSize : 11);
    m_chkChatFontBold->setChecked(snapshot.chatFontBold);
    m_chkChatFontItalic->setChecked(snapshot.chatFontItalic);
    m_spnChatLineSpacing->setValue(snapshot.chatLineSpacing >= 0 ? snapshot.chatLineSpacing : 3);
    m_spnChatMaxMessages->setValue(snapshot.chatMaxMessages > 0 ? snapshot.chatMaxMessages : 5000);

    m_ytChkEnabled->setChecked(snapshot.youtube.enabled);
    m_ytEdtClientId->setText(snapshot.youtube.clientId);
    m_ytEdtClientSecret->setText(snapshot.youtube.clientSecret);
    m_ytEdtRedirectUri->setText(snapshot.youtube.redirectUri);
    m_ytEdtAuthEndpoint->setText(snapshot.youtube.authEndpoint);
    m_ytEdtTokenEndpoint->setText(snapshot.youtube.tokenEndpoint);
    m_ytEdtScope->setPlainText(snapshot.youtube.scope);
    m_ytEdtChannelId->setText(snapshot.youtube.channelId);
    m_ytEdtChannelHandle->setText(snapshot.youtube.accountLabel);
    m_ytEdtLiveVideoOverride->setText(snapshot.youtube.liveVideoIdOverride);
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

    if (m_cmbBroadcastViewerPosition)
        m_cmbBroadcastViewerPosition->setCurrentText(snapshot.broadcastViewerCountPosition);
    if (m_spnBroadcastWidth) m_spnBroadcastWidth->setValue(snapshot.broadcastWindowWidth);
    if (m_spnBroadcastHeight) m_spnBroadcastHeight->setValue(snapshot.broadcastWindowHeight);
    if (m_btnBroadcastTransparentBg)
        applyColorButtonStyle(m_btnBroadcastTransparentBg, QColor(snapshot.broadcastTransparentBgColor));
    if (m_btnBroadcastOpaqueBg)
        applyColorButtonStyle(m_btnBroadcastOpaqueBg, QColor(snapshot.broadcastOpaqueBgColor));
}

void ConfigurationDialog::onTokenOperationStarted(PlatformId platform, const QString& operation)
{
    m_tokenBusy.insert(platform, true);
    m_tokenBusyOperation.insert(platform, operation);
    if (platform == PlatformId::YouTube) {
        if (m_ytLblOperation) {
            m_ytLblOperation->setText(tr("BUSY: %1").arg(operation));
        }
        applyOperationStyle(m_ytLblOperation, true, false, false);
    } else {
        if (m_chzLblOperation) {
            m_chzLblOperation->setText(tr("BUSY: %1").arg(operation));
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
            m_ytLblOperation->setText(tr("IDLE"));
        }
        applyOperationStyle(m_ytLblOperation, false, true, ok);
    } else {
        if (m_chzLblOperation) {
            m_chzLblOperation->setText(tr("IDLE"));
        }
        applyOperationStyle(m_chzLblOperation, false, true, ok);
    }
    updateTokenUiLockState();

    const QString prefix = ok ? tr("OK") : tr("FAIL");
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
    const QString platformText = platform == PlatformId::YouTube ? tr("YouTube") : tr("CHZZK");
    const QString resultText = ok ? tr("OK") : tr("FAIL");
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
        ? tr("Token operation in progress: %1").arg(m_tokenBusyOperation.value(PlatformId::YouTube))
        : (interactiveInProgress ? tr("Interactive auth is running on another platform") : QString());
    const QString chzReason = chzBusy
        ? tr("Token operation in progress: %1").arg(m_tokenBusyOperation.value(PlatformId::Chzzk))
        : (interactiveInProgress ? tr("Interactive auth is running on another platform") : QString());

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
    if (m_ytEdtLiveVideoOverride) m_ytEdtLiveVideoOverride->setEnabled(enabled);
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
        m_statusBar->showMessage(tr("Configuration applied."), OnionmixerChatManager::Timings::kStatusBarDisplayMs);
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
    const QString summaryText = tr("Time: %1\nPlatform: %2\nAction: %3\nResult: %4")
                                    .arg(timeText, platformText, actionText, resultText);

    auto* dialog = new QDialog(this);
    dialog->setWindowTitle(tr("Token Audit Detail"));
    dialog->resize(760, 460);

    auto* layout = new QVBoxLayout(dialog);

    auto* lblSummary = new QLabel(summaryText, dialog);
    lblSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* txtDetail = new QTextEdit(dialog);
    txtDetail->setReadOnly(true);
    txtDetail->setPlainText(fullDetail);

    auto* buttons = new QDialogButtonBox(Qt::Horizontal, dialog);
    auto* btnCopySummary = buttons->addButton(tr("Copy Summary"), QDialogButtonBox::ActionRole);
    auto* btnCopy = buttons->addButton(tr("Copy Detail"), QDialogButtonBox::ActionRole);
    auto* btnCopyAll = buttons->addButton(tr("Copy All"), QDialogButtonBox::ActionRole);
    auto* btnClose = buttons->addButton(QDialogButtonBox::Close);

    connect(btnCopySummary, &QPushButton::clicked, this, [this, summaryText]() {
        if (QGuiApplication::clipboard()) {
            QGuiApplication::clipboard()->setText(summaryText);
            if (m_statusBar) {
                m_statusBar->showMessage(tr("Token audit summary copied."), 2000);
            }
        }
    });
    connect(btnCopy, &QPushButton::clicked, this, [this, txtDetail]() {
        if (QGuiApplication::clipboard()) {
            QGuiApplication::clipboard()->setText(txtDetail->toPlainText());
            if (m_statusBar) {
                m_statusBar->showMessage(tr("Token audit detail copied."), 2000);
            }
        }
    });
    connect(btnCopyAll, &QPushButton::clicked, this, [this, summaryText, txtDetail]() {
        if (QGuiApplication::clipboard()) {
            const QString merged = buildAuditCopyAllText(summaryText, txtDetail->toPlainText());
            QGuiApplication::clipboard()->setText(merged);
            if (m_statusBar) {
                m_statusBar->showMessage(tr("Token audit summary+detail copied."), 2000);
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
    m_cmbLanguage->addItems({ QStringLiteral("ko_KR"), QStringLiteral("en_US"), QStringLiteral("ja_JP") });

    m_cmbLogLevel = new QComboBox(page);
    m_cmbLogLevel->setObjectName(QStringLiteral("cmbLogLevel"));
    m_cmbLogLevel->addItems({ QStringLiteral("debug"), QStringLiteral("info"), QStringLiteral("warn"), QStringLiteral("error") });

    m_cmbMergeOrder = new QComboBox(page);
    m_cmbMergeOrder->setObjectName(QStringLiteral("cmbMergeOrder"));
    m_cmbMergeOrder->addItems({ QStringLiteral("timestamp") });

    m_chkAutoReconnect = new QCheckBox(tr("Enable Auto Reconnect"), page);
    m_chkAutoReconnect->setObjectName(QStringLiteral("chkAutoReconnect"));
    m_chkDetailLog = new QCheckBox(tr("Enable Detail Log (TRACE/INFO)"), page);
    m_chkDetailLog->setObjectName(QStringLiteral("chkDetailLog"));

    m_fntChat = new QFontComboBox(page);
    m_fntChat->setObjectName(QStringLiteral("fntChatFont"));

    m_spnChatFontSize = new QSpinBox(page);
    m_spnChatFontSize->setObjectName(QStringLiteral("spnChatFontSize"));
    m_spnChatFontSize->setRange(8, 24);
    m_spnChatFontSize->setValue(11);
    m_spnChatFontSize->setSuffix(QStringLiteral("px"));

    m_chkChatFontBold = new QCheckBox(tr("Bold"), page);
    m_chkChatFontBold->setObjectName(QStringLiteral("chkChatFontBold"));
    m_chkChatFontItalic = new QCheckBox(tr("Italic"), page);
    m_chkChatFontItalic->setObjectName(QStringLiteral("chkChatFontItalic"));

    auto* fontStyleWrap = new QWidget(page);
    auto* fontStyleLayout = new QHBoxLayout(fontStyleWrap);
    fontStyleLayout->setContentsMargins(0, 0, 0, 0);
    fontStyleLayout->setSpacing(12);
    fontStyleLayout->addWidget(m_chkChatFontBold);
    fontStyleLayout->addWidget(m_chkChatFontItalic);
    fontStyleLayout->addStretch();

    m_spnChatLineSpacing = new QSpinBox(page);
    m_spnChatLineSpacing->setObjectName(QStringLiteral("spnChatLineSpacing"));
    m_spnChatLineSpacing->setRange(0, 20);
    m_spnChatLineSpacing->setValue(3);
    m_spnChatLineSpacing->setSuffix(QStringLiteral("px"));

    m_spnChatMaxMessages = new QSpinBox(page);
    m_spnChatMaxMessages->setObjectName(QStringLiteral("spnChatMaxMessages"));
    m_spnChatMaxMessages->setRange(500, 50000);
    m_spnChatMaxMessages->setValue(5000);
    m_spnChatMaxMessages->setSingleStep(500);

    layout->addRow(tr("Language"), m_cmbLanguage);
    layout->addRow(tr("Log Level"), m_cmbLogLevel);
    layout->addRow(tr("Merge Order"), m_cmbMergeOrder);
    layout->addRow(QString(), m_chkAutoReconnect);
    layout->addRow(QString(), m_chkDetailLog);
    layout->addRow(tr("Chat Font"), m_fntChat);
    layout->addRow(tr("Chat Font Size"), m_spnChatFontSize);
    layout->addRow(tr("Chat Font Style"), fontStyleWrap);
    layout->addRow(tr("Chat Line Spacing"), m_spnChatLineSpacing);
    layout->addRow(tr("Chat Max Messages"), m_spnChatMaxMessages);

    auto* previewGroup = new QGroupBox(tr("Chat Preview"), page);
    m_chatPreviewModel = new ChatMessageModel(this);
    m_chatPreviewDelegate = new ChatBubbleDelegate(this);
    m_chatPreviewList = new QListView(previewGroup);
    m_chatPreviewList->setModel(m_chatPreviewModel);
    m_chatPreviewList->setItemDelegate(m_chatPreviewDelegate);
    m_chatPreviewList->setSelectionMode(QAbstractItemView::NoSelection);
    m_chatPreviewList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_chatPreviewList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_chatPreviewList->setUniformItemSizes(false);
    m_chatPreviewList->setFrameShape(QFrame::NoFrame);
    auto* previewGroupLayout = new QVBoxLayout(previewGroup);
    previewGroupLayout->setContentsMargins(4, 4, 4, 4);
    previewGroupLayout->addWidget(m_chatPreviewList);
    previewGroup->setStyleSheet(QStringLiteral("QGroupBox { background: #FFFFFF; }"));
    layout->addRow(previewGroup);

    connect(m_fntChat, &QFontComboBox::currentFontChanged, this, &ConfigurationDialog::updateChatPreview);
    connect(m_spnChatFontSize, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigurationDialog::updateChatPreview);
    connect(m_chkChatFontBold, &QCheckBox::toggled, this, &ConfigurationDialog::updateChatPreview);
    connect(m_chkChatFontItalic, &QCheckBox::toggled, this, &ConfigurationDialog::updateChatPreview);
    connect(m_spnChatLineSpacing, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigurationDialog::updateChatPreview);

    QTimer::singleShot(0, this, &ConfigurationDialog::updateChatPreview);

    return page;
}

void ConfigurationDialog::updateChatPreview()
{
    if (!m_chatPreviewDelegate || !m_chatPreviewModel || !m_chatPreviewList) {
        return;
    }

    // Update delegate fonts
    const QString fontFamily = m_fntChat->currentFont().family().trimmed();
    m_chatPreviewDelegate->setFontFamily(fontFamily);
    m_chatPreviewDelegate->setFontSize(qBound(8, m_spnChatFontSize->value(), 24));
    m_chatPreviewDelegate->setFontBold(m_chkChatFontBold->isChecked());
    m_chatPreviewDelegate->setFontItalic(m_chkChatFontItalic->isChecked());
    m_chatPreviewDelegate->setLineSpacing(qBound(0, m_spnChatLineSpacing->value(), 20));

    // Populate sample messages (only on first call)
    if (m_chatPreviewModel->messageCount() == 0) {
        UnifiedChatMessage m1;
        m1.platform = PlatformId::YouTube;
        m1.authorName = QStringLiteral("SampleUser");
        m1.text = QStringLiteral("Hello, this is a YouTube message!");
        m1.timestamp = QDateTime::fromString(QStringLiteral("2026-04-11 15:30:00"), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        m_chatPreviewModel->appendMessage(m1);

        UnifiedChatMessage m2;
        m2.platform = PlatformId::Chzzk;
        m2.authorName = QString::fromUtf8("\xEC\xB9\x98\xEC\xA7\x80\xEC\xA7\x81\xEC\x8A\xA4\xED\x8A\xB8\xEB\xA6\xAC\xEB\xA8\xB8");
        m2.text = QString::fromUtf8("\xEC\xB9\x98\xEC\xA7\x80\xEC\xA7\x81\xEC\x97\x90\xEC\x84\x9C \xEB\xB3\xB4\xEB\x82\xB8 \xEB\xA9\x94\xEC\x8B\x9C\xEC\xA7\x80\xEC\x9E\x85\xEB\x8B\x88\xEB\x8B\xA4.");
        m2.timestamp = QDateTime::fromString(QStringLiteral("2026-04-11 15:30:05"), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        m_chatPreviewModel->appendMessage(m2);

        UnifiedChatMessage m3;
        m3.platform = PlatformId::YouTube;
        m3.authorName = QString::fromUtf8("\xE3\x81\x95\xE3\x81\x8F\xE3\x82\x89");
        m3.text = QString::fromUtf8("\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF\xEF\xBC\x81\xE9\x85\x8D\xE4\xBF\xA1\xE6\xA5\xBD\xE3\x81\x97\xE3\x81\xBF\xE3\x81\xAB\xE3\x81\x97\xE3\x81\xA6\xE3\x81\x84\xE3\x81\xBE\xE3\x81\x99\xE3\x80\x82");
        m3.timestamp = QDateTime::fromString(QStringLiteral("2026-04-11 15:30:10"), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        m_chatPreviewModel->appendMessage(m3);
    }

    // Notify view that row sizes may have changed (font/spacing update)
    emit m_chatPreviewModel->layoutChanged();
    m_chatPreviewList->viewport()->update();
}

QWidget* ConfigurationDialog::createYouTubeTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* form = new QFormLayout;
    m_ytChkEnabled = new QCheckBox(tr("Enabled"), page);
    m_ytChkEnabled->setObjectName(QStringLiteral("ytChkEnabled"));

    m_ytEdtClientId = new QLineEdit(page);
    m_ytEdtClientId->setObjectName(QStringLiteral("ytEdtClientId"));

    m_ytEdtClientSecret = new QLineEdit(page);
    m_ytEdtClientSecret->setObjectName(QStringLiteral("ytEdtClientSecret"));
    m_ytEdtClientSecret->setEchoMode(QLineEdit::Password);
    m_ytEdtClientSecret->setPlaceholderText(tr("Optional (required by some OAuth clients)"));

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

    m_ytEdtLiveVideoOverride = new QLineEdit(page);
    m_ytEdtLiveVideoOverride->setObjectName(QStringLiteral("ytEdtLiveVideoOverride"));
    m_ytEdtLiveVideoOverride->setPlaceholderText(tr("Optional: live watch URL or videoId override"));

    form->addRow(QString(), m_ytChkEnabled);
    form->addRow(tr("Client ID"), m_ytEdtClientId);
    form->addRow(tr("Client Secret (Optional)"), m_ytEdtClientSecret);
    form->addRow(tr("Redirect URI"), m_ytEdtRedirectUri);
    form->addRow(tr("Auth Endpoint"), m_ytEdtAuthEndpoint);
    form->addRow(tr("Token Endpoint"), m_ytEdtTokenEndpoint);
    form->addRow(tr("Scope"), m_ytEdtScope);
    form->addRow(tr("Channel ID"), m_ytEdtChannelId);
    form->addRow(tr("Channel Handle"), m_ytEdtChannelHandle);
    form->addRow(tr("Live Video URL / ID"), m_ytEdtLiveVideoOverride);

    auto* statusBox = new QGroupBox(tr("Token / Account"), page);
    auto* statusGrid = new QGridLayout(statusBox);
    m_ytLblAccount = new QLabel(QStringLiteral("-"), statusBox);
    m_ytLblAccount->setObjectName(QStringLiteral("ytLblAccount"));
    m_ytLblTokenState = new QLabel(tr("NO_TOKEN"), statusBox);
    m_ytLblTokenState->setObjectName(QStringLiteral("ytLblTokenState"));
    m_ytLblAccessExpireAt = new QLabel(QStringLiteral("-"), statusBox);
    m_ytLblAccessExpireAt->setObjectName(QStringLiteral("ytLblAccessExpireAt"));
    m_ytLblLastRefreshResult = new QLabel(QStringLiteral("-"), statusBox);
    m_ytLblLastRefreshResult->setObjectName(QStringLiteral("ytLblLastRefreshResult"));
    m_ytLblOperation = new QLabel(tr("IDLE"), statusBox);
    m_ytLblOperation->setObjectName(QStringLiteral("ytLblOperation"));

    statusGrid->addWidget(new QLabel(tr("Account"), statusBox), 0, 0);
    statusGrid->addWidget(m_ytLblAccount, 0, 1);
    statusGrid->addWidget(new QLabel(tr("Token State"), statusBox), 1, 0);
    statusGrid->addWidget(m_ytLblTokenState, 1, 1);
    statusGrid->addWidget(new QLabel(tr("Access Expire At"), statusBox), 2, 0);
    statusGrid->addWidget(m_ytLblAccessExpireAt, 2, 1);
    statusGrid->addWidget(new QLabel(tr("Last Result"), statusBox), 3, 0);
    statusGrid->addWidget(m_ytLblLastRefreshResult, 3, 1);
    statusGrid->addWidget(new QLabel(tr("Operation"), statusBox), 4, 0);
    statusGrid->addWidget(m_ytLblOperation, 4, 1);

    auto* actionLayout = new QHBoxLayout;
    m_ytBtnTokenRefresh = new QPushButton(tr("Token Refresh"), page);
    m_ytBtnTokenRefresh->setObjectName(QStringLiteral("ytBtnTokenRefresh"));
    m_ytBtnReauthBrowser = new QPushButton(tr("Re-Auth Browser"), page);
    m_ytBtnReauthBrowser->setObjectName(QStringLiteral("ytBtnReauthBrowser"));
    m_ytBtnTokenDelete = new QPushButton(tr("Delete Token"), page);
    m_ytBtnTokenDelete->setObjectName(QStringLiteral("ytBtnTokenDelete"));
    m_ytBtnTestConfig = new QPushButton(tr("Test Config"), page);
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
    m_chzChkEnabled = new QCheckBox(tr("Enabled"), page);
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
    form->addRow(tr("Client ID"), m_chzEdtClientId);
    form->addRow(tr("Client Secret"), m_chzEdtClientSecret);
    form->addRow(tr("Redirect URI"), m_chzEdtRedirectUri);
    form->addRow(tr("Auth Endpoint"), m_chzEdtAuthEndpoint);
    form->addRow(tr("Token Endpoint"), m_chzEdtTokenEndpoint);
    form->addRow(tr("Scope"), m_chzEdtScope);
    form->addRow(tr("Channel ID"), m_chzEdtChannelId);
    form->addRow(tr("Channel Name"), m_chzEdtChannelName);
    form->addRow(tr("Account Label"), m_chzEdtAccountLabel);

    auto* statusBox = new QGroupBox(tr("Token / Account"), page);
    auto* statusGrid = new QGridLayout(statusBox);
    m_chzLblAccount = new QLabel(QStringLiteral("-"), statusBox);
    m_chzLblAccount->setObjectName(QStringLiteral("chzLblAccount"));
    m_chzLblTokenState = new QLabel(tr("NO_TOKEN"), statusBox);
    m_chzLblTokenState->setObjectName(QStringLiteral("chzLblTokenState"));
    m_chzLblAccessExpireAt = new QLabel(QStringLiteral("-"), statusBox);
    m_chzLblAccessExpireAt->setObjectName(QStringLiteral("chzLblAccessExpireAt"));
    m_chzLblLastRefreshResult = new QLabel(QStringLiteral("-"), statusBox);
    m_chzLblLastRefreshResult->setObjectName(QStringLiteral("chzLblLastRefreshResult"));
    m_chzLblOperation = new QLabel(tr("IDLE"), statusBox);
    m_chzLblOperation->setObjectName(QStringLiteral("chzLblOperation"));

    statusGrid->addWidget(new QLabel(tr("Account"), statusBox), 0, 0);
    statusGrid->addWidget(m_chzLblAccount, 0, 1);
    statusGrid->addWidget(new QLabel(tr("Token State"), statusBox), 1, 0);
    statusGrid->addWidget(m_chzLblTokenState, 1, 1);
    statusGrid->addWidget(new QLabel(tr("Access Expire At"), statusBox), 2, 0);
    statusGrid->addWidget(m_chzLblAccessExpireAt, 2, 1);
    statusGrid->addWidget(new QLabel(tr("Last Result"), statusBox), 3, 0);
    statusGrid->addWidget(m_chzLblLastRefreshResult, 3, 1);
    statusGrid->addWidget(new QLabel(tr("Operation"), statusBox), 4, 0);
    statusGrid->addWidget(m_chzLblOperation, 4, 1);

    auto* actionLayout = new QHBoxLayout;
    m_chzBtnTokenRefresh = new QPushButton(tr("Token Refresh"), page);
    m_chzBtnTokenRefresh->setObjectName(QStringLiteral("chzBtnTokenRefresh"));
    m_chzBtnReauthBrowser = new QPushButton(tr("Re-Auth Browser"), page);
    m_chzBtnReauthBrowser->setObjectName(QStringLiteral("chzBtnReauthBrowser"));
    m_chzBtnTokenDelete = new QPushButton(tr("Delete Token"), page);
    m_chzBtnTokenDelete->setObjectName(QStringLiteral("chzBtnTokenDelete"));
    m_chzBtnTestConfig = new QPushButton(tr("Test Config"), page);
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

    m_lblVaultProvider = new QLabel(tr("QtKeychain (planned)"), page);
    m_lblVaultProvider->setObjectName(QStringLiteral("lblVaultProvider"));
    m_lblVaultHealth = new QLabel(tr("UNKNOWN"), page);
    m_lblVaultHealth->setObjectName(QStringLiteral("lblVaultHealth"));

    form->addRow(tr("Vault Provider"), m_lblVaultProvider);
    form->addRow(tr("Vault Health"), m_lblVaultHealth);

    m_tblTokenAudit = new QTableWidget(page);
    m_tblTokenAudit->setObjectName(QStringLiteral("tblTokenAudit"));
    m_tblTokenAudit->setColumnCount(5);
    m_tblTokenAudit->setHorizontalHeaderLabels({ tr("Time"), tr("Platform"), tr("Action"), tr("Result"), tr("Detail") });
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

    m_btnClearTokenAudit = new QPushButton(tr("Clear Audit"), page);
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
    snapshot.detailLogEnabled = m_chkDetailLog->isChecked();
    snapshot.chatFontFamily = m_fntChat->currentFont().family();
    snapshot.chatFontSize = m_spnChatFontSize->value();
    snapshot.chatFontBold = m_chkChatFontBold->isChecked();
    snapshot.chatFontItalic = m_chkChatFontItalic->isChecked();
    snapshot.chatLineSpacing = m_spnChatLineSpacing->value();
    snapshot.chatMaxMessages = m_spnChatMaxMessages->value();
    snapshot.youtube = collectPlatformSettings(PlatformId::YouTube);
    snapshot.chzzk = collectPlatformSettings(PlatformId::Chzzk);

    if (m_cmbBroadcastViewerPosition)
        snapshot.broadcastViewerCountPosition = m_cmbBroadcastViewerPosition->currentText();
    if (m_spnBroadcastWidth) snapshot.broadcastWindowWidth = m_spnBroadcastWidth->value();
    if (m_spnBroadcastHeight) snapshot.broadcastWindowHeight = m_spnBroadcastHeight->value();
    if (m_btnBroadcastTransparentBg)
        snapshot.broadcastTransparentBgColor = m_btnBroadcastTransparentBg->property("colorValue").toString();
    if (m_btnBroadcastOpaqueBg)
        snapshot.broadcastOpaqueBgColor = m_btnBroadcastOpaqueBg->property("colorValue").toString();

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
        s.liveVideoIdOverride = m_ytEdtLiveVideoOverride->text().trimmed();
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
            *errorMessage = tr("YouTube enabled but required fields are missing.");
            return false;
        }
        if (!isLikelyGoogleOAuthClientId(snapshot.youtube.clientId)) {
            *errorMessage = googleClientIdValidationMessage(normalizeGoogleOAuthClientId(snapshot.youtube.clientId));
            return false;
        }
        if (!isValidLoopbackUri(snapshot.youtube.redirectUri, QStringLiteral("/youtube/callback"))) {
            *errorMessage = tr("YouTube redirect_uri must be http://127.0.0.1:{port}/youtube/callback");
            return false;
        }
        if (!isValidHttpsUri(snapshot.youtube.authEndpoint) || !isValidHttpsUri(snapshot.youtube.tokenEndpoint)) {
            *errorMessage = tr("YouTube auth/token endpoint must be https URL.");
            return false;
        }
    }

    if (snapshot.chzzk.enabled) {
        if (snapshot.chzzk.clientId.isEmpty() || snapshot.chzzk.clientSecret.isEmpty() || snapshot.chzzk.redirectUri.isEmpty() || snapshot.chzzk.authEndpoint.isEmpty() || snapshot.chzzk.tokenEndpoint.isEmpty() || snapshot.chzzk.scope.isEmpty()) {
            *errorMessage = tr("CHZZK enabled but required fields are missing.");
            return false;
        }
        if (!isValidLoopbackUri(snapshot.chzzk.redirectUri, QStringLiteral("/chzzk/callback"))) {
            *errorMessage = tr("CHZZK redirect_uri must be http://127.0.0.1:{port}/chzzk/callback");
            return false;
        }
        if (!isValidHttpsUri(snapshot.chzzk.authEndpoint) || !isValidHttpsUri(snapshot.chzzk.tokenEndpoint)) {
            *errorMessage = tr("CHZZK auth/token endpoint must be https URL.");
            return false;
        }
    }

    return true;
}

void ConfigurationDialog::applyColorButtonStyle(QPushButton* button, const QColor& color)
{
    const QString hexArgb = color.name(QColor::HexArgb);
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: rgba(%1,%2,%3,%4); border: 1px solid #888; min-width: 80px; min-height: 20px; color: %5; }")
        .arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha())
        .arg(color.lightness() > 128 ? QStringLiteral("#000000") : QStringLiteral("#ffffff")));
    button->setText(hexArgb);
    button->setProperty("colorValue", hexArgb);
}

void ConfigurationDialog::pickBroadcastColor(QPushButton* button, const QString& title)
{
    const QColor initial(button->property("colorValue").toString());
    const QColor chosen = QColorDialog::getColor(initial, this, title, QColorDialog::ShowAlphaChannel);
    if (chosen.isValid()) {
        applyColorButtonStyle(button, chosen);
        updateBroadcastPreview();
    }
}

QWidget* ConfigurationDialog::createBroadcastTab()
{
    auto* page = new QWidget;
    auto* pageLayout = new QVBoxLayout(page);

    // 상단: 설정 폼 (고정 높이)
    auto* formLayout = new QFormLayout;

    m_cmbBroadcastViewerPosition = new QComboBox(page);
    m_cmbBroadcastViewerPosition->addItems({
        QStringLiteral("TopLeft"), QStringLiteral("TopCenter"), QStringLiteral("TopRight"),
        QStringLiteral("CenterLeft"), QStringLiteral("CenterRight"),
        QStringLiteral("BottomLeft"), QStringLiteral("BottomCenter"), QStringLiteral("BottomRight")
    });
    formLayout->addRow(tr("Viewer Count Position"), m_cmbBroadcastViewerPosition);

    m_spnBroadcastWidth = new QSpinBox(page);
    m_spnBroadcastWidth->setRange(100, 1900);
    m_spnBroadcastWidth->setSuffix(QStringLiteral(" px"));
    formLayout->addRow(tr("Window Width"), m_spnBroadcastWidth);

    m_spnBroadcastHeight = new QSpinBox(page);
    m_spnBroadcastHeight->setRange(150, 1000);
    m_spnBroadcastHeight->setSuffix(QStringLiteral(" px"));
    formLayout->addRow(tr("Window Height"), m_spnBroadcastHeight);

    m_btnBroadcastTransparentBg = new QPushButton(page);
    connect(m_btnBroadcastTransparentBg, &QPushButton::clicked, this, [this]() {
        pickBroadcastColor(m_btnBroadcastTransparentBg, tr("Transparent Mode Background"));
    });
    formLayout->addRow(tr("Transparent Background"), m_btnBroadcastTransparentBg);

    m_btnBroadcastOpaqueBg = new QPushButton(page);
    connect(m_btnBroadcastOpaqueBg, &QPushButton::clicked, this, [this]() {
        pickBroadcastColor(m_btnBroadcastOpaqueBg, tr("Opaque Mode Background"));
    });
    formLayout->addRow(tr("Opaque Background"), m_btnBroadcastOpaqueBg);

    pageLayout->addLayout(formLayout, 0);  // stretch=0: 고정 높이

    // 오프스크린 프리뷰 렌더러 (parent=this, hide)
    m_broadcastPreviewRenderer = new QWidget(this);
    m_broadcastPreviewRenderer->hide();
    m_broadcastPreviewRenderer->setAttribute(Qt::WA_TranslucentBackground, true);
    m_broadcastPreviewRenderer->setAutoFillBackground(false);
    m_broadcastPreviewDelegate = new ChatBubbleDelegate(this);
    m_broadcastPreviewList = new QListView(m_broadcastPreviewRenderer);
    m_broadcastPreviewList->setModel(m_chatPreviewModel);
    m_broadcastPreviewList->setItemDelegate(m_broadcastPreviewDelegate);
    m_broadcastPreviewList->setSelectionMode(QAbstractItemView::NoSelection);
    m_broadcastPreviewList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_broadcastPreviewList->setUniformItemSizes(false);
    m_broadcastPreviewList->setFrameShape(QFrame::NoFrame);
    m_broadcastPreviewList->setStyleSheet(QStringLiteral("background: transparent;"));
    m_broadcastPreviewList->viewport()->setAutoFillBackground(false);
    auto* rendererLayout = new QVBoxLayout(m_broadcastPreviewRenderer);
    rendererLayout->setContentsMargins(0, 0, 0, 0);
    rendererLayout->addWidget(m_broadcastPreviewList);

    // 하단: 프리뷰 2개 (남는 공간 채움)
    auto* previewWrap = new QWidget(page);
    auto* previewLayout = new QHBoxLayout(previewWrap);
    previewLayout->setContentsMargins(0, 0, 0, 0);

    m_transparentPreviewGroup = new QGroupBox(tr("Transparent Preview"), previewWrap);
    auto* transparentPreviewLayout = new QVBoxLayout(m_transparentPreviewGroup);
    m_lblBroadcastPreviewTransparent = new QLabel(m_transparentPreviewGroup);
    m_lblBroadcastPreviewTransparent->setAlignment(Qt::AlignCenter);
    m_lblBroadcastPreviewTransparent->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    transparentPreviewLayout->addWidget(m_lblBroadcastPreviewTransparent);

    m_opaquePreviewGroup = new QGroupBox(tr("Opaque Preview"), previewWrap);
    auto* opaquePreviewLayout = new QVBoxLayout(m_opaquePreviewGroup);
    m_lblBroadcastPreviewOpaque = new QLabel(m_opaquePreviewGroup);
    m_lblBroadcastPreviewOpaque->setAlignment(Qt::AlignCenter);
    m_lblBroadcastPreviewOpaque->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    opaquePreviewLayout->addWidget(m_lblBroadcastPreviewOpaque);

    previewLayout->addWidget(m_transparentPreviewGroup);
    previewLayout->addWidget(m_opaquePreviewGroup);
    pageLayout->addWidget(previewWrap, 1);  // stretch=1: 남는 공간 모두 채움

    // 시그널 연결 — BroadChat 탭 설정 변경 시
    connect(m_spnBroadcastWidth, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigurationDialog::updateBroadcastPreview);
    connect(m_spnBroadcastHeight, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigurationDialog::updateBroadcastPreview);
    connect(m_cmbBroadcastViewerPosition, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigurationDialog::updateBroadcastPreview);

    // General 탭 폰트 설정 변경 시에도 프리뷰 갱신
    connect(m_fntChat, &QFontComboBox::currentFontChanged, this, &ConfigurationDialog::updateBroadcastPreview);
    connect(m_spnChatFontSize, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigurationDialog::updateBroadcastPreview);
    connect(m_chkChatFontBold, &QCheckBox::toggled, this, &ConfigurationDialog::updateBroadcastPreview);
    connect(m_chkChatFontItalic, &QCheckBox::toggled, this, &ConfigurationDialog::updateBroadcastPreview);
    connect(m_spnChatLineSpacing, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigurationDialog::updateBroadcastPreview);

    // 디바운스 타이머 (resizeEvent 대응)
    m_broadcastPreviewDebounce = new QTimer(this);
    m_broadcastPreviewDebounce->setSingleShot(true);
    m_broadcastPreviewDebounce->setInterval(150);
    connect(m_broadcastPreviewDebounce, &QTimer::timeout, this, &ConfigurationDialog::updateBroadcastPreview);

    // 탭 전환 시 프리뷰 갱신 (방송창 탭이 보일 때 GroupBox가 정상 크기를 가짐)
    connect(m_tabConfig, &QTabWidget::currentChanged, this, [this]() {
        m_broadcastPreviewDebounce->start();
    });

    return page;
}

void ConfigurationDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    if (m_broadcastPreviewDebounce) {
        m_broadcastPreviewDebounce->start();
    }
}

void ConfigurationDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (m_broadcastPreviewDebounce) {
        m_broadcastPreviewDebounce->start();
    }
}

void ConfigurationDialog::updateBroadcastPreview()
{
    if (!m_broadcastPreviewRenderer || !m_broadcastPreviewDelegate
        || !m_lblBroadcastPreviewTransparent || !m_lblBroadcastPreviewOpaque) {
        return;
    }

    m_broadcastPreviewDelegate->setFontFamily(m_fntChat->currentFont().family());
    m_broadcastPreviewDelegate->setFontSize(m_spnChatFontSize->value());
    m_broadcastPreviewDelegate->setFontBold(m_chkChatFontBold->isChecked());
    m_broadcastPreviewDelegate->setFontItalic(m_chkChatFontItalic->isChecked());
    m_broadcastPreviewDelegate->setLineSpacing(m_spnChatLineSpacing->value());

    const int w = m_spnBroadcastWidth->value();
    const int h = m_spnBroadcastHeight->value();
    m_broadcastPreviewRenderer->resize(w, h);
    emit m_broadcastPreviewList->model()->layoutChanged();

    // viewer count 오버레이 렌더링 헬퍼
    const QString viewerText = QStringLiteral("%1 \u2014  %2 \u2014  \u03A3 \u2014")
        .arg(PlatformTraits::badgeSymbol(PlatformId::YouTube), PlatformTraits::badgeSymbol(PlatformId::Chzzk));
    const QString position = m_cmbBroadcastViewerPosition->currentText();

    auto drawViewerOverlay = [&](QPainter& painter, int pw, int ph) {
        // Phase 1 Gap 20: QApplication::font() + Bold로 통일 (실제 방송창 QLabel 상속 폰트와 일치)
        QFont overlayFont = QGuiApplication::font();
        overlayFont.setBold(true);
        painter.setFont(overlayFont);
        const QFontMetrics fm(overlayFont);
        const int textW = fm.horizontalAdvance(viewerText) + ViewerCountStyle::kPaddingX * 2;
        const int textH = fm.height() + ViewerCountStyle::kPaddingY * 2;
        const int margin = 8;

        // Phase 2: CenterLeft/CenterRight는 90° CW 회전
        const bool rotate = (position == QStringLiteral("CenterLeft")
                             || position == QStringLiteral("CenterRight"));
        const int bbW = rotate ? textH : textW;
        const int bbH = rotate ? textW : textH;

        int ox = margin;
        if (position.endsWith(QStringLiteral("Right")))       ox = pw - bbW - margin;
        else if (position.endsWith(QStringLiteral("Center"))) ox = (pw - bbW) / 2;

        int oy = margin;
        if (position.startsWith(QStringLiteral("Bottom")))      oy = ph - bbH - margin;
        else if (position.startsWith(QStringLiteral("Center"))) oy = (ph - bbH) / 2;

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (rotate) {
            painter.translate(ox + bbW / 2.0, oy + bbH / 2.0);
            painter.rotate(90);
            painter.translate(-textW / 2.0, -textH / 2.0);
            painter.setBrush(QColor::fromRgba(ViewerCountStyle::kBg));
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(0, 0, textW, textH, ViewerCountStyle::kRadius, ViewerCountStyle::kRadius);
            painter.setPen(QColor::fromRgb(ViewerCountStyle::kFg));
            painter.drawText(QRect(0, 0, textW, textH), Qt::AlignCenter, viewerText);
        } else {
            painter.setBrush(QColor::fromRgba(ViewerCountStyle::kBg));
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(ox, oy, textW, textH, ViewerCountStyle::kRadius, ViewerCountStyle::kRadius);
            painter.setPen(QColor::fromRgb(ViewerCountStyle::kFg));
            painter.drawText(QRect(ox, oy, textW, textH), Qt::AlignCenter, viewerText);
        }
        painter.restore();
    };

    // GroupBox의 내부 영역을 기준으로 비율 유지 스케일링
    const QRect groupRect = m_transparentPreviewGroup->contentsRect();
    const int availW = qMax(groupRect.width() - 8, 50);
    const int availH = qMax(groupRect.height() - 8, 50);

    // 방송창 비율 유지하면서 가용 영역에 맞는 스케일 크기 계산
    const qreal scaleW = static_cast<qreal>(availW) / w;
    const qreal scaleH = static_cast<qreal>(availH) / h;
    const qreal scale = qMin(scaleW, scaleH);
    const int scaledW = qMax(static_cast<int>(w * scale), 1);
    const int scaledH = qMax(static_cast<int>(h * scale), 1);


    // 투명 모드 프리뷰
    {
        QPixmap pm(w, h);
        pm.fill(Qt::transparent);
        QPainter painter(&pm);
        QPixmap tile(16, 16);
        tile.fill(QColor(255, 255, 255));
        {
            QPainter tp(&tile);
            tp.fillRect(0, 0, 8, 8, QColor(204, 204, 204));
            tp.fillRect(8, 8, 8, 8, QColor(204, 204, 204));
        }
        painter.drawTiledPixmap(0, 0, w, h, tile);
        const QColor transparentBg(m_btnBroadcastTransparentBg->property("colorValue").toString());
        painter.fillRect(0, 0, w, h, transparentBg);
        painter.end();
        m_broadcastPreviewRenderer->render(&pm);
        { QPainter op(&pm); drawViewerOverlay(op, w, h); }
        m_lblBroadcastPreviewTransparent->setPixmap(
            pm.scaled(scaledW, scaledH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    // 불투명 모드 프리뷰
    {
        QPixmap pm(w, h);
        const QColor opaqueBg(m_btnBroadcastOpaqueBg->property("colorValue").toString());
        pm.fill(opaqueBg.isValid() ? opaqueBg : QColor(255, 255, 255));
        m_broadcastPreviewRenderer->render(&pm);
        { QPainter op(&pm); drawViewerOverlay(op, w, h); }
        m_lblBroadcastPreviewOpaque->setPixmap(
            pm.scaled(scaledW, scaledH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}
