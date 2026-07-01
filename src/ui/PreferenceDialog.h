#pragma once

#include "core/AppPreferences.h"
#include "services/AutomationService.h"

#include <QDialog>
#include <QFont>
#include <QJsonDocument>
#include <QString>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QIcon;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QScrollBar;
class QSpinBox;
class QStackedWidget;
class QWidget;
class AutomationShortcutEditorWidget;
class GroupStyleEditorWidget;

class PreferenceDialog final : public QDialog {
    Q_OBJECT

public:
    PreferenceDialog(QString preferencePath, labelqt::core::AppPreferences currentPreferences,
                     QVector<labelqt::services::AutomationScript> automationScripts, QWidget* parent = nullptr);

signals:
    void preferencesApplied(labelqt::core::AppPreferencesLoadResult result);

private:
    void createUi();
    void addPreferencePage(const QString& title, const QIcon& icon, QWidget* page);
    void filterPreferencePages(const QString& filterText);
    void updateCurrentPageTitle(QListWidgetItem* currentItem);
    QWidget* createGeneralPage(QWidget* parent);
    QWidget* createAppearancePage(QWidget* parent);
    QWidget* createLabelDisplayPage(QWidget* parent);
    QWidget* createProofreadingPage(QWidget* parent);
    QWidget* createKeyMappingPage(QWidget* parent);
    QWidget* createAutomationPage(QWidget* parent);
    QWidget* createAutomationShortcutsPage(QWidget* parent);
    QWidget* createGroupStylesPage(QWidget* parent);
    QWidget* createJsonPage(QWidget* parent);
    void connectPreferenceChangeSignals();
    void loadFromDisk();
    void loadDocument(const QJsonDocument& document);
    QJsonDocument documentFromUi() const;
    void updateJsonPreview();
    void setMessage(const QString& message, bool warning = false);
    void chooseLabelTableFont();
    void resetLabelTableFont();
    void updateLabelTableFontSummary();
    void chooseTextEditorFont();
    void resetTextEditorFont();
    void updateTextEditorFontSummary();
    void chooseMarkerTextBubbleFont();
    void resetMarkerTextBubbleFont();
    void updateMarkerTextBubbleFontSummary();
    void chooseAutomationPythonCommand();
    void chooseConfiguredFont(QFont* targetFont, bool* usesDefaultFont, QLabel* summaryLabel, const QString& title);
    void resetConfiguredFont(QFont* targetFont, bool* usesDefaultFont, QLabel* summaryLabel);
    void updateFontSummary(QLabel* summaryLabel, const QFont& targetFont, bool usesDefaultFont);
    void savePreferences();
    void openPreferenceFile();

    QString m_preferencePath;
    labelqt::core::AppPreferences m_currentPreferences;
    QVector<labelqt::services::AutomationScript> m_automationScripts;
    QListWidget* m_categoryList{nullptr};
    QStackedWidget* m_pageStack{nullptr};
    QLabel* m_pageTitleLabel{nullptr};
    QDoubleSpinBox* m_markerDiameterSpinBox{nullptr};
    QDoubleSpinBox* m_markerFontSpinBox{nullptr};
    QSpinBox* m_tableMaxRowsSpinBox{nullptr};
    QComboBox* m_applicationStyleComboBox{nullptr};
    QComboBox* m_applicationThemeComboBox{nullptr};
    QComboBox* m_applicationLanguageComboBox{nullptr};
    QComboBox* m_textDiffCleanupComboBox{nullptr};
    QCheckBox* m_showAutomationRunLogCheckBox{nullptr};
    QLineEdit* m_automationPythonCommandEdit{nullptr};
    QLineEdit* m_automationPythonArgumentsEdit{nullptr};
    QCheckBox* m_automationAutoInstallRequirementsCheckBox{nullptr};
    QLineEdit* m_automationPipIndexUrlEdit{nullptr};
    QPushButton* m_chooseAutomationPythonButton{nullptr};
    QLabel* m_labelTableFontLabel{nullptr};
    QPushButton* m_chooseLabelTableFontButton{nullptr};
    QPushButton* m_resetLabelTableFontButton{nullptr};
    QLabel* m_textEditorFontLabel{nullptr};
    QPushButton* m_chooseTextEditorFontButton{nullptr};
    QPushButton* m_resetTextEditorFontButton{nullptr};
    QLabel* m_markerTextBubbleFontLabel{nullptr};
    QPushButton* m_chooseMarkerTextBubbleFontButton{nullptr};
    QPushButton* m_resetMarkerTextBubbleFontButton{nullptr};
    QScrollBar* m_markerTextBubbleOpacityScrollBar{nullptr};
    QLabel* m_markerTextBubbleOpacityLabel{nullptr};
    QScrollBar* m_canvasLabelTextEditorOpacityScrollBar{nullptr};
    QLabel* m_canvasLabelTextEditorOpacityLabel{nullptr};
    QComboBox* m_moveModifierComboBox{nullptr};
    QComboBox* m_previousLabelModifierComboBox{nullptr};
    QKeySequenceEdit* m_undoShortcutEdit{nullptr};
    QKeySequenceEdit* m_redoShortcutEdit{nullptr};
    QKeySequenceEdit* m_nextLabelShortcutEdit{nullptr};
    QKeySequenceEdit* m_alternatePreviousLabelShortcutEdit{nullptr};
    QKeySequenceEdit* m_alternateNextLabelShortcutEdit{nullptr};
    QKeySequenceEdit* m_previousPageShortcutEdit{nullptr};
    QKeySequenceEdit* m_nextPageShortcutEdit{nullptr};
    QKeySequenceEdit* m_editLabelTextShortcutEdit{nullptr};
    QKeySequenceEdit* m_commitLabelTextShortcutEdit{nullptr};
    AutomationShortcutEditorWidget* m_automationShortcutEditor{nullptr};
    QLineEdit* m_backupPathEdit{nullptr};
    QSpinBox* m_backupIntervalSpinBox{nullptr};
    GroupStyleEditorWidget* m_groupStyleEditor{nullptr};
    QPlainTextEdit* m_jsonPreview{nullptr};
    QLabel* m_messageLabel{nullptr};
    QPushButton* m_saveButton{nullptr};
    QFont m_labelTableFont;
    QFont m_textEditorFont;
    QFont m_markerTextBubbleFont;
    bool m_usesDefaultLabelTableFont{true};
    bool m_usesDefaultTextEditorFont{true};
    bool m_usesDefaultMarkerTextBubbleFont{true};
};
