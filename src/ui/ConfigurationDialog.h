#ifndef CONFIGURATION_DIALOG_H
#define CONFIGURATION_DIALOG_H

#include "core/AppTypes.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLineEdit;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStatusBar;
class QTabWidget;
class QTableWidget;

class ConfigurationDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConfigurationDialog(QWidget* parent = nullptr);

    void setSnapshot(const AppSettingsSnapshot& snapshot);

signals:
    void configApplyRequested(const AppSettingsSnapshot& snapshot);
    void tokenRefreshRequested(PlatformId platform, const PlatformSettings& settings);
    void interactiveAuthRequested(PlatformId platform, const PlatformSettings& settings);
    void tokenDeleteRequested(PlatformId platform);
    void platformConfigValidationRequested(PlatformId platform, const PlatformSettings& settings);

public slots:
    void onTokenOperationStarted(PlatformId platform, const QString& operation);
    void onTokenStateUpdated(PlatformId platform, TokenState state, const QString& detail);
    void onTokenActionFinished(PlatformId platform, bool ok, const QString& message);
    void onTokenRecordUpdated(PlatformId platform, TokenState state, const TokenRecord& record, const QString& detail);
    void onTokenAuditAppended(PlatformId platform, const QString& action, bool ok, const QString& detail);

private slots:
    void onApplyClicked();
    void onYouTubeTestConfigClicked();
    void onChzzkTestConfigClicked();
    void onTokenAuditCellDoubleClicked(int row, int column);

private:
    QWidget* createGeneralTab();
    QWidget* createYouTubeTab();
    QWidget* createChzzkTab();
    QWidget* createSecurityTab();

    AppSettingsSnapshot collectSnapshot() const;
    PlatformSettings collectPlatformSettings(PlatformId platform) const;
    bool validateSnapshot(const AppSettingsSnapshot& snapshot, QString* errorMessage) const;
    void updateTokenUiLockState();
    void setYouTubeConfigEditable(bool enabled);
    void setChzzkConfigEditable(bool enabled);
    void setTokenButtonsEnabled(PlatformId platform, bool enabled);
    QString elideAuditDetailText(const QString& fullDetail) const;
    void refreshAuditDetailElision();

    QTabWidget* m_tabConfig = nullptr;
    QStatusBar* m_statusBar = nullptr;

    QComboBox* m_cmbLanguage = nullptr;
    QComboBox* m_cmbLogLevel = nullptr;
    QComboBox* m_cmbMergeOrder = nullptr;
    QCheckBox* m_chkAutoReconnect = nullptr;
    QCheckBox* m_chkDetailLog = nullptr;
    QFontComboBox* m_fntChat = nullptr;
    QSpinBox* m_spnChatFontSize = nullptr;
    QCheckBox* m_chkChatFontBold = nullptr;
    QCheckBox* m_chkChatFontItalic = nullptr;
    QSpinBox* m_spnChatLineSpacing = nullptr;
    QSpinBox* m_spnChatMaxMessages = nullptr;
    QWidget* m_chatPreviewContainer = nullptr;
    void updateChatPreview();

    QCheckBox* m_ytChkEnabled = nullptr;
    QLineEdit* m_ytEdtClientId = nullptr;
    QLineEdit* m_ytEdtClientSecret = nullptr;
    QLineEdit* m_ytEdtRedirectUri = nullptr;
    QLineEdit* m_ytEdtAuthEndpoint = nullptr;
    QLineEdit* m_ytEdtTokenEndpoint = nullptr;
    QPlainTextEdit* m_ytEdtScope = nullptr;
    QLineEdit* m_ytEdtChannelId = nullptr;
    QLineEdit* m_ytEdtChannelHandle = nullptr;
    QLineEdit* m_ytEdtLiveVideoOverride = nullptr;
    QLabel* m_ytLblAccount = nullptr;
    QLabel* m_ytLblTokenState = nullptr;
    QLabel* m_ytLblAccessExpireAt = nullptr;
    QLabel* m_ytLblLastRefreshResult = nullptr;
    QLabel* m_ytLblOperation = nullptr;
    QPushButton* m_ytBtnTokenRefresh = nullptr;
    QPushButton* m_ytBtnReauthBrowser = nullptr;
    QPushButton* m_ytBtnTokenDelete = nullptr;
    QPushButton* m_ytBtnTestConfig = nullptr;

    QCheckBox* m_chzChkEnabled = nullptr;
    QLineEdit* m_chzEdtClientId = nullptr;
    QLineEdit* m_chzEdtClientSecret = nullptr;
    QLineEdit* m_chzEdtRedirectUri = nullptr;
    QLineEdit* m_chzEdtAuthEndpoint = nullptr;
    QLineEdit* m_chzEdtTokenEndpoint = nullptr;
    QPlainTextEdit* m_chzEdtScope = nullptr;
    QLineEdit* m_chzEdtChannelId = nullptr;
    QLineEdit* m_chzEdtChannelName = nullptr;
    QLineEdit* m_chzEdtAccountLabel = nullptr;
    QLabel* m_chzLblAccount = nullptr;
    QLabel* m_chzLblTokenState = nullptr;
    QLabel* m_chzLblAccessExpireAt = nullptr;
    QLabel* m_chzLblLastRefreshResult = nullptr;
    QLabel* m_chzLblOperation = nullptr;
    QPushButton* m_chzBtnTokenRefresh = nullptr;
    QPushButton* m_chzBtnReauthBrowser = nullptr;
    QPushButton* m_chzBtnTokenDelete = nullptr;
    QPushButton* m_chzBtnTestConfig = nullptr;

    QLabel* m_lblVaultProvider = nullptr;
    QLabel* m_lblVaultHealth = nullptr;
    QTableWidget* m_tblTokenAudit = nullptr;
    QPushButton* m_btnClearTokenAudit = nullptr;

    QHash<PlatformId, bool> m_tokenBusy;
    QHash<PlatformId, QString> m_tokenBusyOperation;
};

#endif // CONFIGURATION_DIALOG_H
