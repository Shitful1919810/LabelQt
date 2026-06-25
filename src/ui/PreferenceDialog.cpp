#include "ui/PreferenceDialog.h"

#include "core/CommandLineUtils.h"
#include "core/TranslationManager.h"
#include "ui/AutomationShortcutEditorWidget.h"
#include "ui/GroupStyleEditorWidget.h"
#include "ui/PreferenceDialogWidgets.h"
#include "ui/ThemeManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QStyleFactory>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <optional>

namespace {
using namespace labelqt::ui::PreferenceDialogWidgets;

QString comboBoxDataOrText(const QComboBox* comboBox)
{
    if (comboBox == nullptr) {
        return {};
    }

    if (comboBox->currentIndex() >= 0 && comboBox->currentText() == comboBox->itemText(comboBox->currentIndex())) {
        return comboBox->currentData().toString();
    }

    return comboBox->currentText().trimmed();
}

QString themeDisplayName(const QString& themeName)
{
    if (themeName == QStringLiteral("breezeDark")) {
        return PreferenceDialog::tr("Breeze Dark");
    }
    if (themeName == QStringLiteral("breezeLight")) {
        return PreferenceDialog::tr("Breeze Light");
    }
    return themeName;
}

std::optional<QFont> chooseFontWithQtDialog(QWidget* parent, const QFont& initialFont, const QString& title)
{
    QFontDialog dialog(initialFont, parent);
    dialog.setWindowTitle(title);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    QFont font = dialog.selectedFont();
    if (font.pointSizeF() <= 0.0) {
        font.setPointSizeF(parent != nullptr ? parent->font().pointSizeF() : initialFont.pointSizeF());
    }
    return font;
}

const labelqt::core::AppPreferences& defaultPreferences()
{
    static const labelqt::core::AppPreferences preferences;
    return preferences;
}
} // namespace

PreferenceDialog::PreferenceDialog(QString preferencePath, labelqt::core::AppPreferences currentPreferences,
                                   QVector<labelqt::services::AutomationScript> automationScripts, QWidget* parent)
    : QDialog(parent), m_preferencePath(std::move(preferencePath)), m_currentPreferences(std::move(currentPreferences)),
      m_automationScripts(std::move(automationScripts))
{
    createUi();
    loadDocument(m_currentPreferences.toJsonDocument());
    setMessage(tr("Showing current preferences."));
}

void PreferenceDialog::createUi()
{
    setWindowTitle(tr("Preferences"));
    resize(760, 620);

    auto* rootLayout = new QVBoxLayout(this);

    auto* pathLabel = new QLabel(tr("Preference file: %1").arg(m_preferencePath), this);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rootLayout->addWidget(pathLabel);

    auto* tabWidget = new QTabWidget(this);
    tabWidget->addTab(createGeneralPage(tabWidget), tr("General"));
    tabWidget->addTab(createKeyMappingPage(tabWidget), tr("Key mappings"));
    tabWidget->addTab(createAutomationPage(tabWidget), tr("Automation"));
    tabWidget->addTab(createAutomationShortcutsPage(tabWidget), tr("Automation shortcuts"));
    tabWidget->addTab(createGroupStylesPage(tabWidget), tr("Group styles"));
    tabWidget->addTab(createJsonPage(tabWidget), tr("JSON preview"));

    rootLayout->addWidget(tabWidget, 1);

    m_messageLabel = new QLabel(this);
    m_messageLabel->setTextFormat(Qt::PlainText);
    m_messageLabel->setVisible(false);
    rootLayout->addWidget(m_messageLabel);

    auto* buttonBox = new QDialogButtonBox(this);
    auto* reloadButton = buttonBox->addButton(tr("Reload"), QDialogButtonBox::ResetRole);
    auto* openButton = buttonBox->addButton(tr("Open in Text Editor"), QDialogButtonBox::ActionRole);
    m_saveButton = buttonBox->addButton(tr("Save"), QDialogButtonBox::AcceptRole);
    auto* closeButton = buttonBox->addButton(tr("Close"), QDialogButtonBox::RejectRole);
    rootLayout->addWidget(buttonBox);

    connect(reloadButton, &QPushButton::clicked, this, &PreferenceDialog::loadFromDisk);
    connect(openButton, &QPushButton::clicked, this, &PreferenceDialog::openPreferenceFile);
    connect(m_saveButton, &QPushButton::clicked, this, &PreferenceDialog::savePreferences);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

    connectPreferenceChangeSignals();
}

QWidget* PreferenceDialog::createGeneralPage(QTabWidget* tabWidget)
{
    auto* generalPage = new QWidget(tabWidget);
    auto* generalLayout = new QFormLayout(generalPage);

    m_markerDiameterSpinBox = makePositiveDoubleSpinBox(defaultPreferences().labelMarkerDiameterPixels(), 1.0, 256.0);
    m_markerFontSpinBox = makePositiveDoubleSpinBox(defaultPreferences().labelMarkerFontPointSize(), 0.1, 256.0);
    m_tableMaxRowsSpinBox = new QSpinBox(generalPage);
    m_tableMaxRowsSpinBox->setRange(1, 50);
    m_tableMaxRowsSpinBox->setValue(defaultPreferences().labelTableMaxTextRows());
    m_applicationStyleComboBox = new QComboBox(generalPage);
    m_applicationStyleComboBox->setEditable(true);
    m_applicationStyleComboBox->addItem(tr("Use system default"), QString());
    const QStringList availableStyles = QStyleFactory::keys();
    for (const QString& styleName : availableStyles) {
        m_applicationStyleComboBox->addItem(styleName, styleName);
    }
    m_applicationThemeComboBox = new QComboBox(generalPage);
    m_applicationThemeComboBox->addItem(tr("Use no application theme"), QString());
    for (const QString& themeName : labelqt::ui::availableApplicationThemes()) {
        m_applicationThemeComboBox->addItem(themeDisplayName(themeName), themeName);
    }
    m_applicationLanguageComboBox = new QComboBox(generalPage);
    m_applicationLanguageComboBox->addItem(tr("Follow system language"), QString());
    for (const labelqt::core::ApplicationLanguage& language : labelqt::core::availableApplicationLanguages()) {
        m_applicationLanguageComboBox->addItem(language.displayName, language.localeName);
    }

    auto* labelTableFontWidget = makeFontSelectorWidget(generalPage, m_labelTableFontLabel,
                                                        m_chooseLabelTableFontButton, m_resetLabelTableFontButton);
    auto* textEditorFontWidget = makeFontSelectorWidget(generalPage, m_textEditorFontLabel,
                                                        m_chooseTextEditorFontButton, m_resetTextEditorFontButton);
    auto* markerTextBubbleFontWidget =
        makeFontSelectorWidget(generalPage, m_markerTextBubbleFontLabel, m_chooseMarkerTextBubbleFontButton,
                               m_resetMarkerTextBubbleFontButton);
    auto* markerTextBubbleOpacityWidget = makePercentScrollBarWidget(
        generalPage, m_markerTextBubbleOpacityScrollBar, m_markerTextBubbleOpacityLabel,
        static_cast<int>(std::round(defaultPreferences().markerTextBubbleOpacity() * 100.0)));
    auto* canvasLabelTextEditorOpacityWidget = makePercentScrollBarWidget(
        generalPage, m_canvasLabelTextEditorOpacityScrollBar, m_canvasLabelTextEditorOpacityLabel,
        static_cast<int>(std::round(defaultPreferences().canvasLabelTextEditorOpacity() * 100.0)));
    m_backupPathEdit = new QLineEdit(generalPage);
    m_backupPathEdit->setText(defaultPreferences().backupPath());
    m_backupIntervalSpinBox = new QSpinBox(generalPage);
    m_backupIntervalSpinBox->setRange(1, 86400);
    m_backupIntervalSpinBox->setValue(defaultPreferences().backupIntervalSeconds());

    generalLayout->addRow(tr("Default marker diameter"), m_markerDiameterSpinBox);
    generalLayout->addRow(tr("Default marker font size"), m_markerFontSpinBox);
    generalLayout->addRow(tr("Qt widget style"), m_applicationStyleComboBox);
    generalLayout->addRow(tr("Breeze stylesheet theme"), m_applicationThemeComboBox);
    generalLayout->addRow(tr("Language"), m_applicationLanguageComboBox);
    generalLayout->addRow(tr("Maximum label table text rows"), m_tableMaxRowsSpinBox);
    generalLayout->addRow(tr("Label table font"), labelTableFontWidget);
    generalLayout->addRow(tr("Text editor font"), textEditorFontWidget);
    generalLayout->addRow(tr("Marker text bubble font"), markerTextBubbleFontWidget);
    generalLayout->addRow(tr("Marker text bubble opacity"), markerTextBubbleOpacityWidget);
    generalLayout->addRow(tr("Canvas label editor opacity"), canvasLabelTextEditorOpacityWidget);
    generalLayout->addRow(tr("Backup path"), m_backupPathEdit);
    generalLayout->addRow(tr("Backup interval seconds"), m_backupIntervalSpinBox);
    return generalPage;
}

QWidget* PreferenceDialog::createAutomationPage(QTabWidget* tabWidget)
{
    auto* automationPage = new QWidget(tabWidget);
    auto* automationLayout = new QFormLayout(automationPage);

    auto* pythonCommandWidget = new QWidget(automationPage);
    auto* pythonCommandLayout = new QHBoxLayout(pythonCommandWidget);
    pythonCommandLayout->setContentsMargins(0, 0, 0, 0);
    pythonCommandLayout->setSpacing(6);
    m_automationPythonCommandEdit = new QLineEdit(pythonCommandWidget);
    m_automationPythonCommandEdit->setPlaceholderText(tr("Use bundled or system Python"));
    m_chooseAutomationPythonButton = new QPushButton(tr("Browse..."), pythonCommandWidget);
    pythonCommandLayout->addWidget(m_automationPythonCommandEdit, 1);
    pythonCommandLayout->addWidget(m_chooseAutomationPythonButton);

    m_automationPythonArgumentsEdit = new QLineEdit(automationPage);
    m_automationPythonArgumentsEdit->setPlaceholderText(tr("Optional Python interpreter arguments"));
    m_automationAutoInstallRequirementsCheckBox =
        new QCheckBox(tr("Install requirements.txt before running scripts"), automationPage);
    m_automationPipIndexUrlEdit = new QLineEdit(automationPage);
    m_automationPipIndexUrlEdit->setPlaceholderText(tr("Use pip default index"));
    m_showAutomationRunLogCheckBox = new QCheckBox(tr("Show automation run log window"), automationPage);
    m_showAutomationRunLogCheckBox->setChecked(defaultPreferences().showAutomationRunLog());

    automationLayout->addRow(tr("Python command"), pythonCommandWidget);
    automationLayout->addRow(tr("Python arguments"), m_automationPythonArgumentsEdit);
    automationLayout->addRow(tr("Dependencies"), m_automationAutoInstallRequirementsCheckBox);
    automationLayout->addRow(tr("pip index URL"), m_automationPipIndexUrlEdit);
    automationLayout->addRow(tr("Run log"), m_showAutomationRunLogCheckBox);
    return automationPage;
}

QWidget* PreferenceDialog::createKeyMappingPage(QTabWidget* tabWidget)
{
    auto* keyMappingPage = new QWidget(tabWidget);
    auto* keyMappingLayout = new QHBoxLayout(keyMappingPage);
    auto* leftColumn = new QWidget(keyMappingPage);
    auto* leftLayout = new QFormLayout(leftColumn);
    auto* rightColumn = new QWidget(keyMappingPage);
    auto* rightLayout = new QFormLayout(rightColumn);
    keyMappingLayout->addWidget(leftColumn, 1);
    keyMappingLayout->addWidget(rightColumn, 1);

    m_moveModifierComboBox = makeModifierComboBox(keyMappingPage);
    m_previousLabelModifierComboBox = makeModifierComboBox(keyMappingPage);
    m_undoShortcutEdit = new QKeySequenceEdit(defaultPreferences().undoShortcut(), keyMappingPage);
    m_redoShortcutEdit = new QKeySequenceEdit(defaultPreferences().redoShortcut(), keyMappingPage);
    m_nextLabelShortcutEdit = new QKeySequenceEdit(defaultPreferences().nextLabelShortcut(), keyMappingPage);
    m_alternatePreviousLabelShortcutEdit =
        new QKeySequenceEdit(defaultPreferences().alternatePreviousLabelShortcut(), keyMappingPage);
    m_alternateNextLabelShortcutEdit =
        new QKeySequenceEdit(defaultPreferences().alternateNextLabelShortcut(), keyMappingPage);
    m_previousPageShortcutEdit = new QKeySequenceEdit(defaultPreferences().previousPageShortcut(), keyMappingPage);
    m_nextPageShortcutEdit = new QKeySequenceEdit(defaultPreferences().nextPageShortcut(), keyMappingPage);
    m_editLabelTextShortcutEdit = new QKeySequenceEdit(defaultPreferences().editLabelTextShortcut(), keyMappingPage);
    m_commitLabelTextShortcutEdit =
        new QKeySequenceEdit(defaultPreferences().commitLabelTextShortcut(), keyMappingPage);

    leftLayout->addRow(tr("Move-label modifier"), m_moveModifierComboBox);
    leftLayout->addRow(tr("Previous label modifier"), m_previousLabelModifierComboBox);
    leftLayout->addRow(tr("Undo"), m_undoShortcutEdit);
    leftLayout->addRow(tr("Redo"), m_redoShortcutEdit);
    leftLayout->addRow(tr("Edit label text"), m_editLabelTextShortcutEdit);
    leftLayout->addRow(tr("Commit label text"), m_commitLabelTextShortcutEdit);

    rightLayout->addRow(tr("Switch label"), m_nextLabelShortcutEdit);
    rightLayout->addRow(tr("Previous label"), m_alternatePreviousLabelShortcutEdit);
    rightLayout->addRow(tr("Next label"), m_alternateNextLabelShortcutEdit);
    rightLayout->addRow(tr("Previous page"), m_previousPageShortcutEdit);
    rightLayout->addRow(tr("Next page"), m_nextPageShortcutEdit);
    return keyMappingPage;
}

QWidget* PreferenceDialog::createAutomationShortcutsPage(QTabWidget* tabWidget)
{
    auto* page = new QWidget(tabWidget);
    auto* layout = new QVBoxLayout(page);

    m_automationShortcutEditor = new AutomationShortcutEditorWidget(page);
    m_automationShortcutEditor->setScripts(m_automationScripts);
    connect(m_automationShortcutEditor, &AutomationShortcutEditorWidget::changed, this,
            &PreferenceDialog::updateJsonPreview);
    layout->addWidget(m_automationShortcutEditor, 1);

    return page;
}

QWidget* PreferenceDialog::createGroupStylesPage(QTabWidget* tabWidget)
{
    auto* groupPage = new QWidget(tabWidget);
    auto* groupLayout = new QVBoxLayout(groupPage);

    m_groupStyleEditor = new GroupStyleEditorWidget(groupPage);
    groupLayout->addWidget(m_groupStyleEditor, 1);
    connect(m_groupStyleEditor, &GroupStyleEditorWidget::changed, this, &PreferenceDialog::updateJsonPreview);
    return groupPage;
}

QWidget* PreferenceDialog::createJsonPage(QTabWidget* tabWidget)
{
    auto* jsonPage = new QWidget(tabWidget);
    auto* jsonLayout = new QVBoxLayout(jsonPage);
    m_jsonPreview = new QPlainTextEdit(jsonPage);
    m_jsonPreview->setReadOnly(true);
    jsonLayout->addWidget(m_jsonPreview);
    return jsonPage;
}

void PreferenceDialog::connectPreferenceChangeSignals()
{
    connect(m_markerDiameterSpinBox, &QDoubleSpinBox::valueChanged, this, &PreferenceDialog::updateJsonPreview);
    connect(m_markerFontSpinBox, &QDoubleSpinBox::valueChanged, this, &PreferenceDialog::updateJsonPreview);
    connect(m_applicationStyleComboBox, &QComboBox::currentTextChanged, this, &PreferenceDialog::updateJsonPreview);
    connect(m_applicationThemeComboBox, &QComboBox::currentIndexChanged, this, &PreferenceDialog::updateJsonPreview);
    connect(m_applicationLanguageComboBox, &QComboBox::currentIndexChanged, this, &PreferenceDialog::updateJsonPreview);
    connect(m_automationPythonCommandEdit, &QLineEdit::textChanged, this, &PreferenceDialog::updateJsonPreview);
    connect(m_automationPythonArgumentsEdit, &QLineEdit::textChanged, this, &PreferenceDialog::updateJsonPreview);
    connect(m_automationAutoInstallRequirementsCheckBox, &QCheckBox::toggled, this,
            &PreferenceDialog::updateJsonPreview);
    connect(m_automationPipIndexUrlEdit, &QLineEdit::textChanged, this, &PreferenceDialog::updateJsonPreview);
    connect(m_chooseAutomationPythonButton, &QPushButton::clicked, this,
            &PreferenceDialog::chooseAutomationPythonCommand);
    connect(m_showAutomationRunLogCheckBox, &QCheckBox::toggled, this, &PreferenceDialog::updateJsonPreview);
    connect(m_tableMaxRowsSpinBox, &QSpinBox::valueChanged, this, &PreferenceDialog::updateJsonPreview);
    connect(m_chooseLabelTableFontButton, &QPushButton::clicked, this, &PreferenceDialog::chooseLabelTableFont);
    connect(m_resetLabelTableFontButton, &QPushButton::clicked, this, &PreferenceDialog::resetLabelTableFont);
    connect(m_chooseTextEditorFontButton, &QPushButton::clicked, this, &PreferenceDialog::chooseTextEditorFont);
    connect(m_resetTextEditorFontButton, &QPushButton::clicked, this, &PreferenceDialog::resetTextEditorFont);
    connect(m_chooseMarkerTextBubbleFontButton, &QPushButton::clicked, this,
            &PreferenceDialog::chooseMarkerTextBubbleFont);
    connect(m_resetMarkerTextBubbleFontButton, &QPushButton::clicked, this,
            &PreferenceDialog::resetMarkerTextBubbleFont);
    connect(m_markerTextBubbleOpacityScrollBar, &QScrollBar::valueChanged, this, [this](int value) {
        m_markerTextBubbleOpacityLabel->setText(tr("%1%").arg(value));
        updateJsonPreview();
    });
    connect(m_canvasLabelTextEditorOpacityScrollBar, &QScrollBar::valueChanged, this, [this](int value) {
        m_canvasLabelTextEditorOpacityLabel->setText(tr("%1%").arg(value));
        updateJsonPreview();
    });
    connect(m_moveModifierComboBox, &QComboBox::currentTextChanged, this, &PreferenceDialog::updateJsonPreview);
    connect(m_previousLabelModifierComboBox, &QComboBox::currentTextChanged, this,
            &PreferenceDialog::updateJsonPreview);
    connect(m_undoShortcutEdit, &QKeySequenceEdit::keySequenceChanged, this, &PreferenceDialog::updateJsonPreview);
    connect(m_redoShortcutEdit, &QKeySequenceEdit::keySequenceChanged, this, &PreferenceDialog::updateJsonPreview);
    connect(m_nextLabelShortcutEdit, &QKeySequenceEdit::keySequenceChanged, this, &PreferenceDialog::updateJsonPreview);
    connect(m_alternatePreviousLabelShortcutEdit, &QKeySequenceEdit::keySequenceChanged, this,
            &PreferenceDialog::updateJsonPreview);
    connect(m_alternateNextLabelShortcutEdit, &QKeySequenceEdit::keySequenceChanged, this,
            &PreferenceDialog::updateJsonPreview);
    connect(m_previousPageShortcutEdit, &QKeySequenceEdit::keySequenceChanged, this,
            &PreferenceDialog::updateJsonPreview);
    connect(m_nextPageShortcutEdit, &QKeySequenceEdit::keySequenceChanged, this, &PreferenceDialog::updateJsonPreview);
    connect(m_editLabelTextShortcutEdit, &QKeySequenceEdit::keySequenceChanged, this,
            &PreferenceDialog::updateJsonPreview);
    connect(m_commitLabelTextShortcutEdit, &QKeySequenceEdit::keySequenceChanged, this,
            &PreferenceDialog::updateJsonPreview);
    connect(m_backupPathEdit, &QLineEdit::textChanged, this, &PreferenceDialog::updateJsonPreview);
    connect(m_backupIntervalSpinBox, &QSpinBox::valueChanged, this, &PreferenceDialog::updateJsonPreview);
}

void PreferenceDialog::loadFromDisk()
{
    QFile file(m_preferencePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setMessage(tr("Could not read preference file; showing defaults."), true);
        loadDocument(QJsonDocument(QJsonObject{}));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setMessage(tr("Preference file is invalid; showing defaults."), true);
        loadDocument(QJsonDocument(QJsonObject{}));
        return;
    }

    loadDocument(document);
    setMessage(tr("Loaded preference file."));
}

void PreferenceDialog::loadDocument(const QJsonDocument& document)
{
    const QJsonObject root = document.object();
    const QJsonObject labelMarker = root.value(QStringLiteral("labelMarker")).toObject();
    const QJsonObject appearance = root.value(QStringLiteral("appearance")).toObject();
    const QJsonObject automation = root.value(QStringLiteral("automation")).toObject();
    const QJsonObject labelTable = root.value(QStringLiteral("labelTable")).toObject();
    const QJsonObject labelTextEditor = root.value(QStringLiteral("labelTextEditor")).toObject();
    const QJsonObject markerTextBubble = root.value(QStringLiteral("markerTextBubble")).toObject();
    const QJsonObject canvasLabelTextEditor = root.value(QStringLiteral("canvasLabelTextEditor")).toObject();
    const QJsonObject input = root.value(QStringLiteral("input")).toObject();

    m_markerDiameterSpinBox->setValue(
        labelMarker.value(QStringLiteral("diameter")).toDouble(defaultPreferences().labelMarkerDiameterPixels()));
    m_markerFontSpinBox->setValue(
        labelMarker.value(QStringLiteral("fontPointSize")).toDouble(defaultPreferences().labelMarkerFontPointSize()));
    const QString applicationStyle = appearance.value(QStringLiteral("style")).toString().trimmed();
    if (applicationStyle.isEmpty()) {
        m_applicationStyleComboBox->setCurrentIndex(0);
    }
    else {
        m_applicationStyleComboBox->setCurrentText(applicationStyle);
    }
    const QString applicationTheme = appearance.value(QStringLiteral("theme")).toString().trimmed();
    const int themeIndex = m_applicationThemeComboBox->findData(applicationTheme);
    m_applicationThemeComboBox->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);
    const QString applicationLanguage = appearance.value(QStringLiteral("language")).toString().trimmed();
    const int languageIndex = m_applicationLanguageComboBox->findData(applicationLanguage);
    m_applicationLanguageComboBox->setCurrentIndex(languageIndex >= 0 ? languageIndex : 0);
    m_showAutomationRunLogCheckBox->setChecked(
        automation.value(QStringLiteral("showRunLog")).toBool(defaultPreferences().showAutomationRunLog()));
    const QJsonObject automationPython = automation.value(QStringLiteral("python")).toObject();
    m_automationPythonCommandEdit->setText(
        automationPython.value(QStringLiteral("command")).toString(defaultPreferences().automationPythonCommand()));
    QStringList pythonArguments;
    const QJsonArray pythonArgumentArray = automationPython.value(QStringLiteral("arguments")).toArray();
    for (const QJsonValue& argument : pythonArgumentArray) {
        if (argument.isString()) {
            pythonArguments.append(argument.toString());
        }
    }
    if (pythonArgumentArray.isEmpty()) {
        pythonArguments = defaultPreferences().automationPythonArguments();
    }
    m_automationPythonArgumentsEdit->setText(labelqt::core::joinCommandLine(pythonArguments));
    m_automationAutoInstallRequirementsCheckBox->setChecked(
        automationPython.value(QStringLiteral("autoInstallRequirements"))
            .toBool(defaultPreferences().automationAutoInstallRequirements()));
    m_automationPipIndexUrlEdit->setText(
        automationPython.value(QStringLiteral("pipIndexUrl")).toString(defaultPreferences().automationPipIndexUrl()));
    const QJsonObject automationShortcuts = automation.value(QStringLiteral("shortcuts")).toObject();
    m_automationShortcutEditor->loadShortcuts(automationShortcuts);

    m_tableMaxRowsSpinBox->setValue(
        labelTable.value(QStringLiteral("maxTextRows")).toInt(defaultPreferences().labelTableMaxTextRows()));
    const QString labelTableFontFamily = labelTable.value(QStringLiteral("fontFamily")).toString().trimmed();
    const double labelTableFontPointSize = labelTable.value(QStringLiteral("fontPointSize")).toDouble(0.0);
    m_usesDefaultLabelTableFont = labelTableFontFamily.isEmpty() && labelTableFontPointSize <= 0.0;
    m_labelTableFont = font();
    if (!labelTableFontFamily.isEmpty()) {
        m_labelTableFont.setFamily(labelTableFontFamily);
    }
    if (labelTableFontPointSize > 0.0) {
        m_labelTableFont.setPointSizeF(labelTableFontPointSize);
    }
    updateLabelTableFontSummary();

    const QString textEditorFontFamily = labelTextEditor.value(QStringLiteral("fontFamily")).toString().trimmed();
    const double textEditorFontPointSize = labelTextEditor.value(QStringLiteral("fontPointSize")).toDouble(0.0);
    m_usesDefaultTextEditorFont = textEditorFontFamily.isEmpty() && textEditorFontPointSize <= 0.0;
    m_textEditorFont = font();
    if (!textEditorFontFamily.isEmpty()) {
        m_textEditorFont.setFamily(textEditorFontFamily);
    }
    if (textEditorFontPointSize > 0.0) {
        m_textEditorFont.setPointSizeF(textEditorFontPointSize);
    }
    updateTextEditorFontSummary();

    const QString markerTextBubbleFontFamily =
        markerTextBubble.value(QStringLiteral("fontFamily")).toString().trimmed();
    const double markerTextBubbleFontPointSize = markerTextBubble.value(QStringLiteral("fontPointSize")).toDouble(0.0);
    m_usesDefaultMarkerTextBubbleFont = markerTextBubbleFontFamily.isEmpty() && markerTextBubbleFontPointSize <= 0.0;
    m_markerTextBubbleFont = font();
    if (!markerTextBubbleFontFamily.isEmpty()) {
        m_markerTextBubbleFont.setFamily(markerTextBubbleFontFamily);
    }
    if (markerTextBubbleFontPointSize > 0.0) {
        m_markerTextBubbleFont.setPointSizeF(markerTextBubbleFontPointSize);
    }
    updateMarkerTextBubbleFontSummary();
    const int markerTextBubbleOpacity = std::clamp(
        static_cast<int>(std::round(
            markerTextBubble.value(QStringLiteral("opacity")).toDouble(defaultPreferences().markerTextBubbleOpacity()) *
            100.0)),
        0, 100);
    m_markerTextBubbleOpacityScrollBar->setValue(markerTextBubbleOpacity);
    m_markerTextBubbleOpacityLabel->setText(tr("%1%").arg(markerTextBubbleOpacity));

    const int canvasLabelTextEditorOpacity =
        std::clamp(static_cast<int>(std::round(canvasLabelTextEditor.value(QStringLiteral("opacity"))
                                                   .toDouble(defaultPreferences().canvasLabelTextEditorOpacity()) *
                                               100.0)),
                   0, 100);
    m_canvasLabelTextEditorOpacityScrollBar->setValue(canvasLabelTextEditorOpacity);
    m_canvasLabelTextEditorOpacityLabel->setText(tr("%1%").arg(canvasLabelTextEditorOpacity));

    m_moveModifierComboBox->setCurrentText(
        input.value(QStringLiteral("moveLabelModifier")).toString(QStringLiteral("ctrl")));
    m_previousLabelModifierComboBox->setCurrentText(
        input.value(QStringLiteral("previousLabelModifier")).toString(QStringLiteral("ctrl")));
    const QKeySequence undoShortcut = QKeySequence::fromString(
        input.value(QStringLiteral("undoShortcut"))
            .toString(defaultPreferences().undoShortcut().toString(QKeySequence::PortableText)),
        QKeySequence::PortableText);
    const QKeySequence redoShortcut = QKeySequence::fromString(
        input.value(QStringLiteral("redoShortcut"))
            .toString(defaultPreferences().redoShortcut().toString(QKeySequence::PortableText)),
        QKeySequence::PortableText);
    const QKeySequence nextLabelShortcut = QKeySequence::fromString(
        input.value(QStringLiteral("nextLabelShortcut"))
            .toString(defaultPreferences().nextLabelShortcut().toString(QKeySequence::PortableText)),
        QKeySequence::PortableText);
    const QKeySequence alternatePreviousLabelShortcut = QKeySequence::fromString(
        input.value(QStringLiteral("alternatePreviousLabelShortcut"))
            .toString(defaultPreferences().alternatePreviousLabelShortcut().toString(QKeySequence::PortableText)),
        QKeySequence::PortableText);
    const QKeySequence alternateNextLabelShortcut = QKeySequence::fromString(
        input.value(QStringLiteral("alternateNextLabelShortcut"))
            .toString(defaultPreferences().alternateNextLabelShortcut().toString(QKeySequence::PortableText)),
        QKeySequence::PortableText);
    const QKeySequence previousPageShortcut = QKeySequence::fromString(
        input.value(QStringLiteral("previousPageShortcut"))
            .toString(defaultPreferences().previousPageShortcut().toString(QKeySequence::PortableText)),
        QKeySequence::PortableText);
    const QKeySequence nextPageShortcut = QKeySequence::fromString(
        input.value(QStringLiteral("nextPageShortcut"))
            .toString(defaultPreferences().nextPageShortcut().toString(QKeySequence::PortableText)),
        QKeySequence::PortableText);
    const QKeySequence editLabelTextShortcut = QKeySequence::fromString(
        input.value(QStringLiteral("editLabelTextShortcut"))
            .toString(defaultPreferences().editLabelTextShortcut().toString(QKeySequence::PortableText)),
        QKeySequence::PortableText);
    const QKeySequence commitLabelTextShortcut = QKeySequence::fromString(
        input.value(QStringLiteral("commitLabelTextShortcut"))
            .toString(defaultPreferences().commitLabelTextShortcut().toString(QKeySequence::PortableText)),
        QKeySequence::PortableText);
    m_undoShortcutEdit->setKeySequence(undoShortcut.isEmpty() ? defaultPreferences().undoShortcut() : undoShortcut);
    m_redoShortcutEdit->setKeySequence(redoShortcut.isEmpty() ? defaultPreferences().redoShortcut() : redoShortcut);
    m_nextLabelShortcutEdit->setKeySequence(nextLabelShortcut.isEmpty() ? defaultPreferences().nextLabelShortcut()
                                                                        : nextLabelShortcut);
    m_alternatePreviousLabelShortcutEdit->setKeySequence(alternatePreviousLabelShortcut.isEmpty()
                                                             ? defaultPreferences().alternatePreviousLabelShortcut()
                                                             : alternatePreviousLabelShortcut);
    m_alternateNextLabelShortcutEdit->setKeySequence(alternateNextLabelShortcut.isEmpty()
                                                         ? defaultPreferences().alternateNextLabelShortcut()
                                                         : alternateNextLabelShortcut);
    m_previousPageShortcutEdit->setKeySequence(
        previousPageShortcut.isEmpty() ? defaultPreferences().previousPageShortcut() : previousPageShortcut);
    m_nextPageShortcutEdit->setKeySequence(nextPageShortcut.isEmpty() ? defaultPreferences().nextPageShortcut()
                                                                      : nextPageShortcut);
    m_editLabelTextShortcutEdit->setKeySequence(
        editLabelTextShortcut.isEmpty() ? defaultPreferences().editLabelTextShortcut() : editLabelTextShortcut);
    m_commitLabelTextShortcutEdit->setKeySequence(
        commitLabelTextShortcut.isEmpty() ? defaultPreferences().commitLabelTextShortcut() : commitLabelTextShortcut);
    m_backupPathEdit->setText(root.value(QStringLiteral("backupPath")).toString(defaultPreferences().backupPath()));
    m_backupIntervalSpinBox->setValue(
        root.value(QStringLiteral("backupIntervalSeconds")).toInt(defaultPreferences().backupIntervalSeconds()));

    m_groupStyleEditor->setMarkerDefaults(m_markerDiameterSpinBox->value(), m_markerFontSpinBox->value());
    const QJsonArray groupStyles = root.value(QStringLiteral("groupStyles")).toArray();
    m_groupStyleEditor->loadStyles(groupStyles);

    updateJsonPreview();
}

QJsonDocument PreferenceDialog::documentFromUi() const
{
    QJsonObject labelMarker;
    labelMarker.insert(QStringLiteral("diameter"), m_markerDiameterSpinBox->value());
    labelMarker.insert(QStringLiteral("fontPointSize"), m_markerFontSpinBox->value());

    QJsonObject appearance;
    appearance.insert(QStringLiteral("style"), comboBoxDataOrText(m_applicationStyleComboBox));
    appearance.insert(QStringLiteral("theme"), comboBoxDataOrText(m_applicationThemeComboBox));
    appearance.insert(QStringLiteral("language"), comboBoxDataOrText(m_applicationLanguageComboBox));

    QJsonObject automation;
    automation.insert(QStringLiteral("showRunLog"), m_showAutomationRunLogCheckBox->isChecked());
    QJsonObject automationPython;
    automationPython.insert(QStringLiteral("command"), m_automationPythonCommandEdit->text().trimmed());
    QJsonArray automationPythonArguments;
    for (const QString& argument : QProcess::splitCommand(m_automationPythonArgumentsEdit->text())) {
        automationPythonArguments.append(argument);
    }
    automationPython.insert(QStringLiteral("arguments"), automationPythonArguments);
    automationPython.insert(QStringLiteral("autoInstallRequirements"),
                            m_automationAutoInstallRequirementsCheckBox->isChecked());
    automationPython.insert(QStringLiteral("pipIndexUrl"), m_automationPipIndexUrlEdit->text().trimmed());
    automation.insert(QStringLiteral("python"), automationPython);
    const QJsonObject automationShortcuts = m_automationShortcutEditor->shortcuts();
    automation.insert(QStringLiteral("shortcuts"), automationShortcuts);

    QJsonObject labelTable;
    labelTable.insert(QStringLiteral("maxTextRows"), m_tableMaxRowsSpinBox->value());
    labelTable.insert(QStringLiteral("fontFamily"),
                      m_usesDefaultLabelTableFont ? QString() : m_labelTableFont.family());
    labelTable.insert(QStringLiteral("fontPointSize"),
                      m_usesDefaultLabelTableFont ? 0.0 : m_labelTableFont.pointSizeF());

    QJsonObject labelTextEditor;
    labelTextEditor.insert(QStringLiteral("fontFamily"),
                           m_usesDefaultTextEditorFont ? QString() : m_textEditorFont.family());
    labelTextEditor.insert(QStringLiteral("fontPointSize"),
                           m_usesDefaultTextEditorFont ? 0.0 : m_textEditorFont.pointSizeF());

    QJsonObject markerTextBubble;
    markerTextBubble.insert(QStringLiteral("fontFamily"),
                            m_usesDefaultMarkerTextBubbleFont ? QString() : m_markerTextBubbleFont.family());
    markerTextBubble.insert(QStringLiteral("fontPointSize"),
                            m_usesDefaultMarkerTextBubbleFont ? 0.0 : m_markerTextBubbleFont.pointSizeF());
    markerTextBubble.insert(QStringLiteral("opacity"),
                            static_cast<double>(m_markerTextBubbleOpacityScrollBar->value()) / 100.0);

    QJsonObject canvasLabelTextEditor;
    canvasLabelTextEditor.insert(QStringLiteral("opacity"),
                                 static_cast<double>(m_canvasLabelTextEditorOpacityScrollBar->value()) / 100.0);

    QJsonObject input;
    input.insert(QStringLiteral("moveLabelModifier"), m_moveModifierComboBox->currentText().trimmed());
    input.insert(QStringLiteral("previousLabelModifier"), m_previousLabelModifierComboBox->currentText().trimmed());
    input.insert(QStringLiteral("undoShortcut"),
                 m_undoShortcutEdit->keySequence().toString(QKeySequence::PortableText));
    input.insert(QStringLiteral("redoShortcut"),
                 m_redoShortcutEdit->keySequence().toString(QKeySequence::PortableText));
    input.insert(QStringLiteral("nextLabelShortcut"),
                 m_nextLabelShortcutEdit->keySequence().toString(QKeySequence::PortableText));
    input.insert(QStringLiteral("alternatePreviousLabelShortcut"),
                 m_alternatePreviousLabelShortcutEdit->keySequence().toString(QKeySequence::PortableText));
    input.insert(QStringLiteral("alternateNextLabelShortcut"),
                 m_alternateNextLabelShortcutEdit->keySequence().toString(QKeySequence::PortableText));
    input.insert(QStringLiteral("previousPageShortcut"),
                 m_previousPageShortcutEdit->keySequence().toString(QKeySequence::PortableText));
    input.insert(QStringLiteral("nextPageShortcut"),
                 m_nextPageShortcutEdit->keySequence().toString(QKeySequence::PortableText));
    input.insert(QStringLiteral("editLabelTextShortcut"),
                 m_editLabelTextShortcutEdit->keySequence().toString(QKeySequence::PortableText));
    input.insert(QStringLiteral("commitLabelTextShortcut"),
                 m_commitLabelTextShortcutEdit->keySequence().toString(QKeySequence::PortableText));

    const QJsonArray groupStyles = m_groupStyleEditor->styles();

    QJsonObject root;
    root.insert(QStringLiteral("appearance"), appearance);
    root.insert(QStringLiteral("automation"), automation);
    root.insert(QStringLiteral("labelMarker"), labelMarker);
    root.insert(QStringLiteral("labelTable"), labelTable);
    root.insert(QStringLiteral("labelTextEditor"), labelTextEditor);
    root.insert(QStringLiteral("markerTextBubble"), markerTextBubble);
    root.insert(QStringLiteral("canvasLabelTextEditor"), canvasLabelTextEditor);
    root.insert(QStringLiteral("input"), input);
    root.insert(QStringLiteral("backupPath"), m_backupPathEdit->text().trimmed());
    root.insert(QStringLiteral("backupIntervalSeconds"), m_backupIntervalSpinBox->value());
    root.insert(QStringLiteral("groupStyles"), groupStyles);
    return QJsonDocument(root);
}

void PreferenceDialog::updateJsonPreview()
{
    m_jsonPreview->setPlainText(QString::fromUtf8(documentFromUi().toJson(QJsonDocument::Indented)));
}

void PreferenceDialog::setMessage(const QString& message, bool warning)
{
    m_messageLabel->setStyleSheet(warning ? QStringLiteral("QLabel { color: #b26a00; font-weight: 600; }")
                                          : QStringLiteral("QLabel { color: palette(text); }"));
    m_messageLabel->setText(message);
    m_messageLabel->setVisible(!message.isEmpty());
}

void PreferenceDialog::chooseLabelTableFont()
{
    chooseConfiguredFont(&m_labelTableFont, &m_usesDefaultLabelTableFont, m_labelTableFontLabel,
                         tr("Choose label table font"));
}

void PreferenceDialog::resetLabelTableFont()
{
    resetConfiguredFont(&m_labelTableFont, &m_usesDefaultLabelTableFont, m_labelTableFontLabel);
}

void PreferenceDialog::updateLabelTableFontSummary()
{
    updateFontSummary(m_labelTableFontLabel, m_labelTableFont, m_usesDefaultLabelTableFont);
}

void PreferenceDialog::chooseTextEditorFont()
{
    chooseConfiguredFont(&m_textEditorFont, &m_usesDefaultTextEditorFont, m_textEditorFontLabel,
                         tr("Choose text editor font"));
}

void PreferenceDialog::resetTextEditorFont()
{
    resetConfiguredFont(&m_textEditorFont, &m_usesDefaultTextEditorFont, m_textEditorFontLabel);
}

void PreferenceDialog::updateTextEditorFontSummary()
{
    updateFontSummary(m_textEditorFontLabel, m_textEditorFont, m_usesDefaultTextEditorFont);
}

void PreferenceDialog::chooseMarkerTextBubbleFont()
{
    chooseConfiguredFont(&m_markerTextBubbleFont, &m_usesDefaultMarkerTextBubbleFont, m_markerTextBubbleFontLabel,
                         tr("Choose marker text bubble font"));
}

void PreferenceDialog::resetMarkerTextBubbleFont()
{
    resetConfiguredFont(&m_markerTextBubbleFont, &m_usesDefaultMarkerTextBubbleFont, m_markerTextBubbleFontLabel);
}

void PreferenceDialog::updateMarkerTextBubbleFontSummary()
{
    updateFontSummary(m_markerTextBubbleFontLabel, m_markerTextBubbleFont, m_usesDefaultMarkerTextBubbleFont);
}

void PreferenceDialog::chooseConfiguredFont(QFont* targetFont, bool* usesDefaultFont, QLabel* summaryLabel,
                                            const QString& title)
{
    if (targetFont == nullptr || usesDefaultFont == nullptr) {
        return;
    }

    const std::optional<QFont> selectedFont =
        chooseFontWithQtDialog(this, *usesDefaultFont ? font() : *targetFont, title);
    if (!selectedFont.has_value()) {
        return;
    }

    *targetFont = selectedFont.value();
    *usesDefaultFont = false;
    updateFontSummary(summaryLabel, *targetFont, *usesDefaultFont);
    updateJsonPreview();
}

void PreferenceDialog::resetConfiguredFont(QFont* targetFont, bool* usesDefaultFont, QLabel* summaryLabel)
{
    if (targetFont == nullptr || usesDefaultFont == nullptr) {
        return;
    }

    *targetFont = font();
    *usesDefaultFont = true;
    updateFontSummary(summaryLabel, *targetFont, *usesDefaultFont);
    updateJsonPreview();
}

void PreferenceDialog::updateFontSummary(QLabel* summaryLabel, const QFont& targetFont, bool usesDefaultFont)
{
    if (summaryLabel == nullptr) {
        return;
    }

    if (usesDefaultFont) {
        summaryLabel->setText(tr("Default font and size"));
        summaryLabel->setFont(font());
        return;
    }

    summaryLabel->setText(tr("%1, %2 pt").arg(targetFont.family()).arg(targetFont.pointSizeF(), 0, 'f', 1));
    summaryLabel->setFont(font());
}

void PreferenceDialog::chooseAutomationPythonCommand()
{
#ifdef Q_OS_WIN
    const QString filter = tr("Python executable (python.exe pythonw.exe *.exe);;All files (*)");
#else
    const QString filter = tr("Python executable (python python3 python*);;All files (*)");
#endif
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose Python executable"),
                                                      m_automationPythonCommandEdit->text(), filter);
    if (path.isEmpty()) {
        return;
    }

    m_automationPythonCommandEdit->setText(path);
    updateJsonPreview();
}

void PreferenceDialog::savePreferences()
{
    const QString shortcutConflict = m_automationShortcutEditor->conflictText();
    if (!shortcutConflict.isEmpty()) {
        setMessage(shortcutConflict, true);
        return;
    }

    const QJsonDocument document = documentFromUi();
    QFile file(m_preferencePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setMessage(tr("Could not save preference file."), true);
        return;
    }

    file.write(document.toJson(QJsonDocument::Indented));
    file.write("\n");
    file.close();

    labelqt::core::AppPreferencesLoadResult result =
        labelqt::core::AppPreferences::loadFromJson(document.toJson(QJsonDocument::Compact));
    m_currentPreferences = result.preferences;
    emit preferencesApplied(result);
    setMessage(result.warnings.isEmpty()
                   ? tr("Preferences saved and applied.")
                   : tr("Preferences saved and applied with warnings; see the main window status bar."),
               !result.warnings.isEmpty());
}

void PreferenceDialog::openPreferenceFile()
{
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(m_preferencePath))) {
        setMessage(tr("Could not open preference file in the system text editor."), true);
    }
}
