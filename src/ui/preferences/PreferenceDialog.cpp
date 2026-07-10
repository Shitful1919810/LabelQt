#include "ui/preferences/PreferenceDialog.h"

#include "core/CommandLineUtils.h"
#include "core/TranslationManager.h"
#include "ui/automation/AutomationShortcutEditorWidget.h"
#include "ui/preferences/GroupStyleEditorWidget.h"
#include "ui/preferences/PreferenceDialogWidgets.h"
#include "ui/widgets/ThemeManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleFactory>
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

QWidget* createScrolledFormPage(QWidget* parent, QFormLayout*& formLayout)
{
    auto* scrollArea = new QScrollArea(parent);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget(scrollArea);
    formLayout = new QFormLayout(content);
    formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    scrollArea->setWidget(content);
    return scrollArea;
}
} // namespace

PreferenceDialog::PreferenceDialog(QString preferencePath, labelqt::core::AppPreferences currentPreferences,
                                   QVector<labelqt::services::AutomationScript> automationScripts, QWidget* parent)
    : QDialog(parent), m_preferencePath(std::move(preferencePath)), m_currentPreferences(std::move(currentPreferences)),
      m_automationScripts(std::move(automationScripts))
{
    createUi();
    loadPreferences(m_currentPreferences);
    setMessage(tr("Showing current preferences."));
}

void PreferenceDialog::createUi()
{
    setWindowTitle(tr("Preferences"));
    resize(980, 680);

    auto* rootLayout = new QVBoxLayout(this);

    auto* pathLabel = new QLabel(tr("Preference file: %1").arg(m_preferencePath), this);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rootLayout->addWidget(pathLabel);

    auto* contentLayout = new QHBoxLayout;
    contentLayout->setSpacing(0);

    auto* sidebar = new QWidget(this);
    sidebar->setMinimumWidth(150);
    sidebar->setMaximumWidth(210);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 8, 0);
    auto* searchEdit = new QLineEdit(sidebar);
    searchEdit->setPlaceholderText(tr("Search..."));
    m_categoryList = new QListWidget(sidebar);
    m_categoryList->setIconSize(QSize(28, 28));
    m_categoryList->setMovement(QListView::Static);
    m_categoryList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_categoryList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sidebarLayout->addWidget(searchEdit);
    sidebarLayout->addWidget(m_categoryList, 1);

    auto* pageArea = new QWidget(this);
    auto* pageAreaLayout = new QVBoxLayout(pageArea);
    pageAreaLayout->setContentsMargins(12, 0, 0, 0);
    m_pageTitleLabel = new QLabel(pageArea);
    QFont titleFont = m_pageTitleLabel->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 4.0);
    titleFont.setBold(true);
    m_pageTitleLabel->setFont(titleFont);
    m_pageStack = new QStackedWidget(pageArea);
    pageAreaLayout->addWidget(m_pageTitleLabel);
    pageAreaLayout->addWidget(m_pageStack, 1);

    contentLayout->addWidget(sidebar);
    contentLayout->addWidget(pageArea, 1);
    rootLayout->addLayout(contentLayout, 1);

    addPreferencePage(tr("General"), style()->standardIcon(QStyle::SP_FileDialogDetailedView),
                      createGeneralPage(m_pageStack));
    addPreferencePage(tr("Appearance"), style()->standardIcon(QStyle::SP_DesktopIcon),
                      createAppearancePage(m_pageStack));
    addPreferencePage(tr("Label display"), style()->standardIcon(QStyle::SP_FileDialogContentsView),
                      createLabelDisplayPage(m_pageStack));
    addPreferencePage(tr("Key mappings"), style()->standardIcon(QStyle::SP_ArrowRight),
                      createKeyMappingPage(m_pageStack));
    addPreferencePage(tr("Automation"), style()->standardIcon(QStyle::SP_ComputerIcon),
                      createAutomationPage(m_pageStack));
    addPreferencePage(tr("Automation shortcuts"), style()->standardIcon(QStyle::SP_CommandLink),
                      createAutomationShortcutsPage(m_pageStack));
    addPreferencePage(tr("Group styles"), style()->standardIcon(QStyle::SP_DialogResetButton),
                      createGroupStylesPage(m_pageStack));
    addPreferencePage(tr("Proofreading"), style()->standardIcon(QStyle::SP_FileDialogInfoView),
                      createProofreadingPage(m_pageStack));
    addPreferencePage(tr("JSON preview"), style()->standardIcon(QStyle::SP_FileIcon), createJsonPage(m_pageStack));

    connect(searchEdit, &QLineEdit::textChanged, this, &PreferenceDialog::filterPreferencePages);
    connect(m_categoryList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        if (current == nullptr) {
            return;
        }
        const int pageIndex = current->data(Qt::UserRole).toInt();
        m_pageStack->setCurrentIndex(pageIndex);
        updateCurrentPageTitle(current);
    });
    m_categoryList->setCurrentRow(0);

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

void PreferenceDialog::addPreferencePage(const QString& title, const QIcon& icon, QWidget* page)
{
    const int pageIndex = m_pageStack->addWidget(page);
    auto* item = new QListWidgetItem(icon, title, m_categoryList);
    item->setData(Qt::UserRole, pageIndex);
    item->setSizeHint(QSize(item->sizeHint().width(), 46));
}

void PreferenceDialog::filterPreferencePages(const QString& filterText)
{
    const QString normalizedFilter = filterText.trimmed();
    QListWidgetItem* firstVisibleItem = nullptr;
    for (int row = 0; row < m_categoryList->count(); ++row) {
        QListWidgetItem* item = m_categoryList->item(row);
        const bool visible =
            normalizedFilter.isEmpty() || item->text().contains(normalizedFilter, Qt::CaseInsensitive);
        item->setHidden(!visible);
        if (visible && firstVisibleItem == nullptr) {
            firstVisibleItem = item;
        }
    }

    if (m_categoryList->currentItem() == nullptr || m_categoryList->currentItem()->isHidden()) {
        m_categoryList->setCurrentItem(firstVisibleItem);
    }
}

void PreferenceDialog::updateCurrentPageTitle(QListWidgetItem* currentItem)
{
    if (currentItem == nullptr || m_pageTitleLabel == nullptr) {
        return;
    }
    m_pageTitleLabel->setText(currentItem->text());
}

QWidget* PreferenceDialog::createGeneralPage(QWidget* parent)
{
    QFormLayout* generalLayout = nullptr;
    auto* generalPage = createScrolledFormPage(parent, generalLayout);

    m_applicationLanguageComboBox = new QComboBox(generalPage);
    m_applicationLanguageComboBox->addItem(tr("Follow system language"), QString());
    for (const labelqt::core::ApplicationLanguage& language : labelqt::core::availableApplicationLanguages()) {
        m_applicationLanguageComboBox->addItem(language.displayName, language.localeName);
    }
    m_backupPathEdit = new QLineEdit(generalPage);
    m_backupPathEdit->setText(defaultPreferences().backupPath());
    m_backupIntervalSpinBox = new QSpinBox(generalPage);
    m_backupIntervalSpinBox->setRange(1, 86400);
    m_backupIntervalSpinBox->setValue(defaultPreferences().backupIntervalSeconds());

    generalLayout->addRow(tr("Language"), m_applicationLanguageComboBox);
    generalLayout->addRow(tr("Backup path"), m_backupPathEdit);
    generalLayout->addRow(tr("Backup interval seconds"), m_backupIntervalSpinBox);
    return generalPage;
}

QWidget* PreferenceDialog::createAppearancePage(QWidget* parent)
{
    QFormLayout* appearanceLayout = nullptr;
    auto* appearancePage = createScrolledFormPage(parent, appearanceLayout);

    m_applicationStyleComboBox = new QComboBox(appearancePage);
    m_applicationStyleComboBox->setEditable(true);
    m_applicationStyleComboBox->addItem(tr("Use system default"), QString());
    const QStringList availableStyles = QStyleFactory::keys();
    for (const QString& styleName : availableStyles) {
        m_applicationStyleComboBox->addItem(styleName, styleName);
    }
    m_applicationThemeComboBox = new QComboBox(appearancePage);
    m_applicationThemeComboBox->addItem(tr("Use no application theme"), QString());
    for (const QString& themeName : labelqt::ui::availableApplicationThemes()) {
        m_applicationThemeComboBox->addItem(themeDisplayName(themeName), themeName);
    }

    appearanceLayout->addRow(tr("Qt widget style"), m_applicationStyleComboBox);
    appearanceLayout->addRow(tr("Breeze stylesheet theme"), m_applicationThemeComboBox);
    return appearancePage;
}

QWidget* PreferenceDialog::createLabelDisplayPage(QWidget* parent)
{
    QFormLayout* labelLayout = nullptr;
    auto* labelPage = createScrolledFormPage(parent, labelLayout);

    m_markerDiameterSpinBox = makePositiveDoubleSpinBox(defaultPreferences().labelMarkerDiameterPixels(), 1.0, 256.0);
    m_markerFontSpinBox = makePositiveDoubleSpinBox(defaultPreferences().labelMarkerFontPointSize(), 0.1, 256.0);
    m_tableMaxRowsSpinBox = new QSpinBox(labelPage);
    m_tableMaxRowsSpinBox->setRange(1, 50);
    m_tableMaxRowsSpinBox->setValue(defaultPreferences().labelTableMaxTextRows());

    auto* labelTableFontWidget = makeFontSelectorWidget(labelPage, m_labelTableFontLabel,
                                                        m_chooseLabelTableFontButton, m_resetLabelTableFontButton);
    auto* textEditorFontWidget = makeFontSelectorWidget(labelPage, m_textEditorFontLabel,
                                                        m_chooseTextEditorFontButton, m_resetTextEditorFontButton);
    auto* markerTextBubbleFontWidget =
        makeFontSelectorWidget(labelPage, m_markerTextBubbleFontLabel, m_chooseMarkerTextBubbleFontButton,
                               m_resetMarkerTextBubbleFontButton);
    m_markerTextBubbleMaxLineCharactersSpinBox = new QSpinBox(labelPage);
    m_markerTextBubbleMaxLineCharactersSpinBox->setRange(1, 200);
    m_markerTextBubbleMaxLineCharactersSpinBox->setValue(defaultPreferences().markerTextBubbleMaxLineCharacters());
    auto* markerTextBubbleOpacityWidget = makePercentScrollBarWidget(
        labelPage, m_markerTextBubbleOpacityScrollBar, m_markerTextBubbleOpacityLabel,
        static_cast<int>(std::round(defaultPreferences().markerTextBubbleOpacity() * 100.0)));
    auto* canvasLabelTextEditorOpacityWidget = makePercentScrollBarWidget(
        labelPage, m_canvasLabelTextEditorOpacityScrollBar, m_canvasLabelTextEditorOpacityLabel,
        static_cast<int>(std::round(defaultPreferences().canvasLabelTextEditorOpacity() * 100.0)));

    labelLayout->addRow(tr("Default marker diameter"), m_markerDiameterSpinBox);
    labelLayout->addRow(tr("Default marker font size"), m_markerFontSpinBox);
    labelLayout->addRow(tr("Maximum label table text rows"), m_tableMaxRowsSpinBox);
    labelLayout->addRow(tr("Label table font"), labelTableFontWidget);
    labelLayout->addRow(tr("Text editor font"), textEditorFontWidget);
    labelLayout->addRow(tr("Marker text bubble font"), markerTextBubbleFontWidget);
    labelLayout->addRow(tr("Marker text bubble line wrap threshold"), m_markerTextBubbleMaxLineCharactersSpinBox);
    labelLayout->addRow(tr("Marker text bubble opacity"), markerTextBubbleOpacityWidget);
    labelLayout->addRow(tr("Canvas label editor opacity"), canvasLabelTextEditorOpacityWidget);
    return labelPage;
}

QWidget* PreferenceDialog::createProofreadingPage(QWidget* parent)
{
    QFormLayout* proofreadingLayout = nullptr;
    auto* proofreadingPage = createScrolledFormPage(parent, proofreadingLayout);
    m_textDiffCleanupComboBox = new QComboBox(proofreadingPage);
    m_textDiffCleanupComboBox->addItem(tr("Auto: CJK text uses raw diff"), QStringLiteral("auto"));
    m_textDiffCleanupComboBox->addItem(tr("Use semantic cleanup for non-CJK text"), QStringLiteral("semantic"));
    m_textDiffCleanupComboBox->addItem(tr("Never use semantic cleanup"), QStringLiteral("raw"));

    proofreadingLayout->addRow(tr("Text diff cleanup"), m_textDiffCleanupComboBox);
    return proofreadingPage;
}

QWidget* PreferenceDialog::createAutomationPage(QWidget* parent)
{
    QFormLayout* automationLayout = nullptr;
    auto* automationPage = createScrolledFormPage(parent, automationLayout);

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

QWidget* PreferenceDialog::createKeyMappingPage(QWidget* parent)
{
    auto* keyMappingPage = new QWidget(parent);
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
    m_deleteLabelShortcutEdit = new QKeySequenceEdit(defaultPreferences().deleteLabelShortcut(), keyMappingPage);

    leftLayout->addRow(tr("Move-label modifier"), m_moveModifierComboBox);
    leftLayout->addRow(tr("Previous label modifier"), m_previousLabelModifierComboBox);
    leftLayout->addRow(tr("Undo"), m_undoShortcutEdit);
    leftLayout->addRow(tr("Redo"), m_redoShortcutEdit);
    leftLayout->addRow(tr("Delete label"), m_deleteLabelShortcutEdit);
    leftLayout->addRow(tr("Edit label text"), m_editLabelTextShortcutEdit);
    leftLayout->addRow(tr("Commit label text"), m_commitLabelTextShortcutEdit);

    rightLayout->addRow(tr("Switch label"), m_nextLabelShortcutEdit);
    rightLayout->addRow(tr("Previous label"), m_alternatePreviousLabelShortcutEdit);
    rightLayout->addRow(tr("Next label"), m_alternateNextLabelShortcutEdit);
    rightLayout->addRow(tr("Previous page"), m_previousPageShortcutEdit);
    rightLayout->addRow(tr("Next page"), m_nextPageShortcutEdit);
    return keyMappingPage;
}

QWidget* PreferenceDialog::createAutomationShortcutsPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);

    m_automationShortcutEditor = new AutomationShortcutEditorWidget(page);
    m_automationShortcutEditor->setScripts(m_automationScripts);
    connect(m_automationShortcutEditor, &AutomationShortcutEditorWidget::changed, this,
            &PreferenceDialog::updateJsonPreview);
    layout->addWidget(m_automationShortcutEditor, 1);

    return page;
}

QWidget* PreferenceDialog::createGroupStylesPage(QWidget* parent)
{
    auto* groupPage = new QWidget(parent);
    auto* groupLayout = new QVBoxLayout(groupPage);

    m_groupStyleEditor = new GroupStyleEditorWidget(groupPage);
    groupLayout->addWidget(m_groupStyleEditor, 1);
    connect(m_groupStyleEditor, &GroupStyleEditorWidget::changed, this, &PreferenceDialog::updateJsonPreview);
    return groupPage;
}

QWidget* PreferenceDialog::createJsonPage(QWidget* parent)
{
    auto* jsonPage = new QWidget(parent);
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
    connect(m_textDiffCleanupComboBox, &QComboBox::currentIndexChanged, this, &PreferenceDialog::updateJsonPreview);
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
    connect(m_markerTextBubbleMaxLineCharactersSpinBox, &QSpinBox::valueChanged, this,
            &PreferenceDialog::updateJsonPreview);
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
    connect(m_deleteLabelShortcutEdit, &QKeySequenceEdit::keySequenceChanged, this,
            &PreferenceDialog::updateJsonPreview);
    connect(m_backupPathEdit, &QLineEdit::textChanged, this, &PreferenceDialog::updateJsonPreview);
    connect(m_backupIntervalSpinBox, &QSpinBox::valueChanged, this, &PreferenceDialog::updateJsonPreview);
}

void PreferenceDialog::loadFromDisk()
{
    const labelqt::core::AppPreferencesLoadResult result =
        labelqt::core::AppPreferences::loadFromFile(m_preferencePath);
    m_currentPreferences = result.preferences;
    loadPreferences(m_currentPreferences);
    setMessage(result.warnings.isEmpty() ? tr("Loaded preference file.")
                                         : tr("Preference file is invalid; showing defaults."),
               !result.warnings.isEmpty());
}

void PreferenceDialog::loadPreferences(const labelqt::core::AppPreferences& preferences)
{
    m_markerDiameterSpinBox->setValue(preferences.labelMarkerDiameterPixels());
    m_markerFontSpinBox->setValue(preferences.labelMarkerFontPointSize());
    const QString applicationStyle = preferences.applicationStyle();
    if (applicationStyle.isEmpty()) {
        m_applicationStyleComboBox->setCurrentIndex(0);
    }
    else {
        m_applicationStyleComboBox->setCurrentText(applicationStyle);
    }
    const QString applicationTheme = preferences.applicationTheme();
    const int themeIndex = m_applicationThemeComboBox->findData(applicationTheme);
    m_applicationThemeComboBox->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);
    const QString applicationLanguage = preferences.applicationLanguage();
    const int languageIndex = m_applicationLanguageComboBox->findData(applicationLanguage);
    m_applicationLanguageComboBox->setCurrentIndex(languageIndex >= 0 ? languageIndex : 0);
    const QString textDiffCleanup = preferences.textDiffCleanupMode() == labelqt::core::TextDiffCleanupMode::Semantic
                                        ? QStringLiteral("semantic")
                                    : preferences.textDiffCleanupMode() == labelqt::core::TextDiffCleanupMode::Raw
                                        ? QStringLiteral("raw")
                                        : QStringLiteral("auto");
    const int textDiffCleanupIndex = m_textDiffCleanupComboBox->findData(textDiffCleanup);
    m_textDiffCleanupComboBox->setCurrentIndex(textDiffCleanupIndex >= 0 ? textDiffCleanupIndex : 0);
    m_showAutomationRunLogCheckBox->setChecked(preferences.showAutomationRunLog());
    m_automationPythonCommandEdit->setText(preferences.automationPythonCommand());
    m_automationPythonArgumentsEdit->setText(labelqt::core::joinCommandLine(preferences.automationPythonArguments()));
    m_automationAutoInstallRequirementsCheckBox->setChecked(preferences.automationAutoInstallRequirements());
    m_automationPipIndexUrlEdit->setText(preferences.automationPipIndexUrl());
    QJsonObject automationShortcuts;
    for (auto it = preferences.automationShortcuts().cbegin(); it != preferences.automationShortcuts().cend(); ++it) {
        automationShortcuts.insert(it.key(), it.value().toString(QKeySequence::PortableText));
    }
    m_automationShortcutEditor->loadShortcuts(automationShortcuts);

    m_tableMaxRowsSpinBox->setValue(preferences.labelTableMaxTextRows());
    const QString labelTableFontFamily = preferences.labelTableFontFamily();
    const double labelTableFontPointSize = preferences.labelTableFontPointSize();
    m_usesDefaultLabelTableFont = labelTableFontFamily.isEmpty() && labelTableFontPointSize <= 0.0;
    m_labelTableFont = font();
    if (!labelTableFontFamily.isEmpty()) {
        m_labelTableFont.setFamily(labelTableFontFamily);
    }
    if (labelTableFontPointSize > 0.0) {
        m_labelTableFont.setPointSizeF(labelTableFontPointSize);
    }
    updateLabelTableFontSummary();

    const QString textEditorFontFamily = preferences.labelTextEditorFontFamily();
    const double textEditorFontPointSize = preferences.labelTextEditorFontPointSize();
    m_usesDefaultTextEditorFont = textEditorFontFamily.isEmpty() && textEditorFontPointSize <= 0.0;
    m_textEditorFont = font();
    if (!textEditorFontFamily.isEmpty()) {
        m_textEditorFont.setFamily(textEditorFontFamily);
    }
    if (textEditorFontPointSize > 0.0) {
        m_textEditorFont.setPointSizeF(textEditorFontPointSize);
    }
    updateTextEditorFontSummary();

    const QString markerTextBubbleFontFamily = preferences.markerTextBubbleFontFamily();
    const double markerTextBubbleFontPointSize = preferences.markerTextBubbleFontPointSize();
    m_usesDefaultMarkerTextBubbleFont = markerTextBubbleFontFamily.isEmpty() && markerTextBubbleFontPointSize <= 0.0;
    m_markerTextBubbleFont = font();
    if (!markerTextBubbleFontFamily.isEmpty()) {
        m_markerTextBubbleFont.setFamily(markerTextBubbleFontFamily);
    }
    if (markerTextBubbleFontPointSize > 0.0) {
        m_markerTextBubbleFont.setPointSizeF(markerTextBubbleFontPointSize);
    }
    updateMarkerTextBubbleFontSummary();
    m_markerTextBubbleMaxLineCharactersSpinBox->setValue(preferences.markerTextBubbleMaxLineCharacters());
    const int markerTextBubbleOpacity =
        std::clamp(static_cast<int>(std::round(preferences.markerTextBubbleOpacity() * 100.0)), 0, 100);
    m_markerTextBubbleOpacityScrollBar->setValue(markerTextBubbleOpacity);
    m_markerTextBubbleOpacityLabel->setText(tr("%1%").arg(markerTextBubbleOpacity));

    const int canvasLabelTextEditorOpacity =
        std::clamp(static_cast<int>(std::round(preferences.canvasLabelTextEditorOpacity() * 100.0)), 0, 100);
    m_canvasLabelTextEditorOpacityScrollBar->setValue(canvasLabelTextEditorOpacity);
    m_canvasLabelTextEditorOpacityLabel->setText(tr("%1%").arg(canvasLabelTextEditorOpacity));

    auto modifierText = [](Qt::KeyboardModifiers modifiers) {
        if (modifiers == Qt::NoModifier) {
            return QStringLiteral("none");
        }
        QStringList parts;
        if (modifiers.testFlag(Qt::ControlModifier)) parts.append(QStringLiteral("ctrl"));
        if (modifiers.testFlag(Qt::ShiftModifier)) parts.append(QStringLiteral("shift"));
        if (modifiers.testFlag(Qt::AltModifier)) parts.append(QStringLiteral("alt"));
        if (modifiers.testFlag(Qt::MetaModifier)) parts.append(QStringLiteral("meta"));
        return parts.join(QLatin1Char('+'));
    };
    m_moveModifierComboBox->setCurrentText(modifierText(preferences.moveLabelModifiers()));
    m_previousLabelModifierComboBox->setCurrentText(modifierText(preferences.previousLabelModifiers()));
    m_undoShortcutEdit->setKeySequence(preferences.undoShortcut());
    m_redoShortcutEdit->setKeySequence(preferences.redoShortcut());
    m_nextLabelShortcutEdit->setKeySequence(preferences.nextLabelShortcut());
    m_alternatePreviousLabelShortcutEdit->setKeySequence(preferences.alternatePreviousLabelShortcut());
    m_alternateNextLabelShortcutEdit->setKeySequence(preferences.alternateNextLabelShortcut());
    m_previousPageShortcutEdit->setKeySequence(preferences.previousPageShortcut());
    m_nextPageShortcutEdit->setKeySequence(preferences.nextPageShortcut());
    m_editLabelTextShortcutEdit->setKeySequence(preferences.editLabelTextShortcut());
    m_commitLabelTextShortcutEdit->setKeySequence(preferences.commitLabelTextShortcut());
    m_deleteLabelShortcutEdit->setKeySequence(preferences.deleteLabelShortcut());
    m_backupPathEdit->setText(preferences.backupPath());
    m_backupIntervalSpinBox->setValue(preferences.backupIntervalSeconds());

    m_groupStyleEditor->setMarkerDefaults(m_markerDiameterSpinBox->value(), m_markerFontSpinBox->value());
    m_groupStyleEditor->loadStyles(preferences.groupStyles());

    updateJsonPreview();
}

labelqt::core::AppPreferences PreferenceDialog::preferencesFromUi() const
{
    labelqt::core::AppPreferencesValues values = m_currentPreferences.values();
    values.labelMarkerDiameterPixels = m_markerDiameterSpinBox->value();
    values.labelMarkerFontPointSize = m_markerFontSpinBox->value();
    values.labelTableMaxTextRows = m_tableMaxRowsSpinBox->value();
    values.labelTableFontFamily = m_usesDefaultLabelTableFont ? QString() : m_labelTableFont.family();
    values.labelTableFontPointSize = m_usesDefaultLabelTableFont ? 0.0 : m_labelTableFont.pointSizeF();
    values.labelTextEditorFontFamily = m_usesDefaultTextEditorFont ? QString() : m_textEditorFont.family();
    values.labelTextEditorFontPointSize = m_usesDefaultTextEditorFont ? 0.0 : m_textEditorFont.pointSizeF();
    values.markerTextBubbleFontFamily =
        m_usesDefaultMarkerTextBubbleFont ? QString() : m_markerTextBubbleFont.family();
    values.markerTextBubbleFontPointSize =
        m_usesDefaultMarkerTextBubbleFont ? 0.0 : m_markerTextBubbleFont.pointSizeF();
    values.markerTextBubbleMaxLineCharacters = m_markerTextBubbleMaxLineCharactersSpinBox->value();
    values.markerTextBubbleOpacity = m_markerTextBubbleOpacityScrollBar->value() / 100.0;
    values.canvasLabelTextEditorOpacity = m_canvasLabelTextEditorOpacityScrollBar->value() / 100.0;
    values.applicationStyle = comboBoxDataOrText(m_applicationStyleComboBox);
    values.applicationTheme = comboBoxDataOrText(m_applicationThemeComboBox);
    values.applicationLanguage = comboBoxDataOrText(m_applicationLanguageComboBox);
    const QString cleanup = comboBoxDataOrText(m_textDiffCleanupComboBox);
    values.textDiffCleanupMode = cleanup == QStringLiteral("semantic") ? labelqt::core::TextDiffCleanupMode::Semantic
                               : cleanup == QStringLiteral("raw")      ? labelqt::core::TextDiffCleanupMode::Raw
                                                                        : labelqt::core::TextDiffCleanupMode::Auto;
    values.showAutomationRunLog = m_showAutomationRunLogCheckBox->isChecked();
    values.automationPythonCommand = m_automationPythonCommandEdit->text().trimmed();
    values.automationPythonArguments = QProcess::splitCommand(m_automationPythonArgumentsEdit->text());
    values.automationAutoInstallRequirements = m_automationAutoInstallRequirementsCheckBox->isChecked();
    values.automationPipIndexUrl = m_automationPipIndexUrlEdit->text().trimmed();
    values.automationShortcuts.clear();
    const QJsonObject shortcutObject = m_automationShortcutEditor->shortcuts();
    for (auto it = shortcutObject.begin(); it != shortcutObject.end(); ++it) {
        values.automationShortcuts.insert(
            it.key(), QKeySequence::fromString(it.value().toString(), QKeySequence::PortableText));
    }
    const auto parseModifiers = [](const QString& text) {
        Qt::KeyboardModifiers modifiers;
        for (const QString& part : text.toLower().split(QLatin1Char('+'), Qt::SkipEmptyParts)) {
            if (part.trimmed() == QStringLiteral("ctrl")) modifiers |= Qt::ControlModifier;
            else if (part.trimmed() == QStringLiteral("shift")) modifiers |= Qt::ShiftModifier;
            else if (part.trimmed() == QStringLiteral("alt")) modifiers |= Qt::AltModifier;
            else if (part.trimmed() == QStringLiteral("meta")) modifiers |= Qt::MetaModifier;
        }
        return modifiers;
    };
    values.moveLabelModifiers = parseModifiers(m_moveModifierComboBox->currentText());
    values.previousLabelModifiers = parseModifiers(m_previousLabelModifierComboBox->currentText());
    values.undoShortcut = m_undoShortcutEdit->keySequence();
    values.redoShortcut = m_redoShortcutEdit->keySequence();
    values.nextLabelShortcut = m_nextLabelShortcutEdit->keySequence();
    values.alternatePreviousLabelShortcut = m_alternatePreviousLabelShortcutEdit->keySequence();
    values.alternateNextLabelShortcut = m_alternateNextLabelShortcutEdit->keySequence();
    values.previousPageShortcut = m_previousPageShortcutEdit->keySequence();
    values.nextPageShortcut = m_nextPageShortcutEdit->keySequence();
    values.editLabelTextShortcut = m_editLabelTextShortcutEdit->keySequence();
    values.commitLabelTextShortcut = m_commitLabelTextShortcutEdit->keySequence();
    values.deleteLabelShortcut = m_deleteLabelShortcutEdit->keySequence();
    values.backupPath = m_backupPathEdit->text().trimmed();
    values.backupIntervalSeconds = m_backupIntervalSpinBox->value();
    values.groupStyles = m_groupStyleEditor->styles();
    return labelqt::core::AppPreferences::fromValues(std::move(values));
}

QJsonDocument PreferenceDialog::documentFromUi() const
{
    return preferencesFromUi().toJsonDocument();
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
    const QDir preferenceDirectory = QFileInfo(m_preferencePath).absoluteDir();
    if (!preferenceDirectory.exists() && !preferenceDirectory.mkpath(QStringLiteral("."))) {
        setMessage(tr("Could not save preference file."), true);
        return;
    }

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
