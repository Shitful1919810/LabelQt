#pragma once

#include "core/TextDiffCleanupMode.h"

#include <QColor>
#include <QKeySequence>
#include <QMap>
#include <QString>
#include <QVector>
#include <Qt>

class QJsonDocument;
class QJsonParseError;

namespace labelqt::core {

inline constexpr int defaultMarkerTextBubbleMaxLineCharacters = 18;

enum class AppPreferenceWarningType {
    FileNotReadable,
    InvalidJson,
    RootNotObject,
    LabelMarkerNotObject,
    MarkerSizeWrongType,
    MarkerSizeOutOfRange,
    LabelTableNotObject,
    LabelTableMaxTextRowsWrongType,
    LabelTableMaxTextRowsOutOfRange,
    LabelTableFontFamilyWrongType,
    LabelTableFontPointSizeWrongType,
    LabelTableFontPointSizeOutOfRange,
    LabelTextEditorNotObject,
    LabelTextEditorFontFamilyWrongType,
    LabelTextEditorFontPointSizeWrongType,
    LabelTextEditorFontPointSizeOutOfRange,
    MarkerTextBubbleNotObject,
    MarkerTextBubbleFontFamilyWrongType,
    MarkerTextBubbleFontPointSizeWrongType,
    MarkerTextBubbleFontPointSizeOutOfRange,
    MarkerTextBubbleOpacityWrongType,
    MarkerTextBubbleOpacityOutOfRange,
    MarkerTextBubbleMaxLineCharactersWrongType,
    MarkerTextBubbleMaxLineCharactersOutOfRange,
    CanvasLabelTextEditorNotObject,
    CanvasLabelTextEditorOpacityWrongType,
    CanvasLabelTextEditorOpacityOutOfRange,
    GroupStylesNotArray,
    GroupStyleNotObject,
    InvalidGroupStyleColor,
    GroupStyleMarkerSizeWrongType,
    GroupStyleMarkerSizeOutOfRange,
    GroupStyleMarkerStyleInvalid,
    InputNotObject,
    MoveLabelModifierInvalid,
    PreviousLabelModifierInvalid,
    UndoShortcutInvalid,
    RedoShortcutInvalid,
    NextLabelShortcutInvalid,
    AlternatePreviousLabelShortcutInvalid,
    AlternateNextLabelShortcutInvalid,
    PreviousPageShortcutInvalid,
    NextPageShortcutInvalid,
    EditLabelTextShortcutInvalid,
    CommitLabelTextShortcutInvalid,
    DeleteLabelShortcutInvalid,
    FindProjectShortcutInvalid,
    NextSearchMatchShortcutInvalid,
    PreviousSearchMatchShortcutInvalid,
    BackupPathWrongType,
    BackupIntervalWrongType,
    BackupIntervalOutOfRange,
    BackupRetentionInvalid,
    SearchShortcutConflict,
    AppearanceNotObject,
    AppearanceStyleWrongType,
    AppearanceThemeWrongType,
    AppearanceLanguageWrongType,
    AutomationNotObject,
    AutomationShowRunLogWrongType,
    AutomationPythonNotObject,
    AutomationPythonCommandWrongType,
    AutomationPythonArgumentsNotArray,
    AutomationPythonArgumentWrongType,
    AutomationPythonAutoInstallRequirementsWrongType,
    AutomationPythonPipIndexUrlWrongType,
    AutomationShortcutsNotObject,
    AutomationShortcutInvalid,
    ProofreadingNotObject,
    TextDiffCleanupModeInvalid,
};

struct AppPreferenceWarning {
    AppPreferenceWarningType type;
    QString key;
    QString detail;
    qsizetype index{-1};
};

struct AppPreferencesLoadResult;

enum class MarkerShape {
    Circle,
    Square,
};

struct LabelGroupStyle {
    QColor groupColor;
    double markerDiameter{20.0};
    double fontPointSize{10.0};
    MarkerShape markerShape{MarkerShape::Circle};
};

QVector<LabelGroupStyle> defaultLabelGroupStyles();

struct AppPreferencesValues {
    double labelMarkerDiameterPixels{20.0};
    double labelMarkerFontPointSize{10.0};
    int labelTableMaxTextRows{4};
    QString labelTableFontFamily;
    double labelTableFontPointSize{0.0};
    QString labelTextEditorFontFamily;
    double labelTextEditorFontPointSize{0.0};
    QString markerTextBubbleFontFamily;
    double markerTextBubbleFontPointSize{0.0};
    double markerTextBubbleOpacity{0.85};
    int markerTextBubbleMaxLineCharacters{defaultMarkerTextBubbleMaxLineCharacters};
    double canvasLabelTextEditorOpacity{0.75};
    QString applicationStyle;
    QString applicationTheme;
    QString applicationLanguage;
    TextDiffCleanupMode textDiffCleanupMode{TextDiffCleanupMode::Auto};
    bool showAutomationRunLog{false};
    QString automationPythonCommand;
    QStringList automationPythonArguments;
    bool automationAutoInstallRequirements{false};
    QString automationPipIndexUrl;
    QMap<QString, QKeySequence> automationShortcuts;
    Qt::KeyboardModifiers moveLabelModifiers{Qt::ControlModifier};
    Qt::KeyboardModifiers previousLabelModifiers{Qt::ControlModifier};
    QKeySequence undoShortcut{QStringLiteral("Ctrl+Z")};
    QKeySequence redoShortcut{QStringLiteral("Ctrl+Shift+Z")};
    QKeySequence nextLabelShortcut{QStringLiteral("Tab")};
    QKeySequence alternatePreviousLabelShortcut{QStringLiteral("Ctrl+Up")};
    QKeySequence alternateNextLabelShortcut{QStringLiteral("Ctrl+Down")};
    QKeySequence previousPageShortcut{QStringLiteral("Ctrl+Left")};
    QKeySequence nextPageShortcut{QStringLiteral("Ctrl+Right")};
    QKeySequence editLabelTextShortcut{QStringLiteral("Return")};
    QKeySequence commitLabelTextShortcut{QStringLiteral("Ctrl+Return")};
    QKeySequence deleteLabelShortcut{QStringLiteral("Del")};
    QKeySequence findProjectShortcut{QStringLiteral("Ctrl+F")};
    QKeySequence nextSearchMatchShortcut{QStringLiteral("F3")};
    QKeySequence previousSearchMatchShortcut{QStringLiteral("Shift+F3")};
    QString backupPath{QStringLiteral("bak")};
    int backupIntervalSeconds{200};
    int backupMaximumCount{0};
    QVector<LabelGroupStyle> groupStyles{defaultLabelGroupStyles()};
};

class AppPreferences {
public:
    static QString defaultFilePath();
    static AppPreferences load();
    static AppPreferencesLoadResult loadWithDiagnostics();
    static AppPreferencesLoadResult loadFromFile(const QString& path);
    static AppPreferencesLoadResult loadFromJson(const QByteArray& json);
    static AppPreferences fromValues(AppPreferencesValues values);

    QJsonDocument toJsonDocument() const;
    AppPreferencesValues values() const;

    double labelMarkerDiameterPixels() const noexcept;
    double labelMarkerFontPointSize() const noexcept;
    int labelTableMaxTextRows() const noexcept;
    QString labelTableFontFamily() const;
    double labelTableFontPointSize() const noexcept;
    QString labelTextEditorFontFamily() const;
    double labelTextEditorFontPointSize() const noexcept;
    QString markerTextBubbleFontFamily() const;
    double markerTextBubbleFontPointSize() const noexcept;
    double markerTextBubbleOpacity() const noexcept;
    int markerTextBubbleMaxLineCharacters() const noexcept;
    double canvasLabelTextEditorOpacity() const noexcept;
    QString applicationStyle() const;
    QString applicationTheme() const;
    QString applicationLanguage() const;
    TextDiffCleanupMode textDiffCleanupMode() const noexcept;
    bool showAutomationRunLog() const noexcept;
    QString automationPythonCommand() const;
    QStringList automationPythonArguments() const;
    bool automationAutoInstallRequirements() const noexcept;
    QString automationPipIndexUrl() const;
    const QMap<QString, QKeySequence>& automationShortcuts() const noexcept;
    Qt::KeyboardModifiers moveLabelModifiers() const noexcept;
    Qt::KeyboardModifiers previousLabelModifiers() const noexcept;
    QKeySequence undoShortcut() const;
    QKeySequence redoShortcut() const;
    QKeySequence nextLabelShortcut() const;
    QKeySequence alternatePreviousLabelShortcut() const;
    QKeySequence alternateNextLabelShortcut() const;
    QKeySequence previousPageShortcut() const;
    QKeySequence nextPageShortcut() const;
    QKeySequence editLabelTextShortcut() const;
    QKeySequence commitLabelTextShortcut() const;
    QKeySequence deleteLabelShortcut() const;
    QKeySequence findProjectShortcut() const;
    QKeySequence nextSearchMatchShortcut() const;
    QKeySequence previousSearchMatchShortcut() const;
    QStringList conflictingSearchShortcuts() const;
    QString backupPath() const;
    int backupIntervalSeconds() const noexcept;
    int backupMaximumCount() const noexcept;
    const QVector<LabelGroupStyle>& groupStyles() const noexcept;

private:
    static AppPreferencesLoadResult loadFromDocument(const QJsonDocument& document, const QJsonParseError* parseError);

    AppPreferencesValues m_values;
};

struct AppPreferencesLoadResult {
    AppPreferences preferences;
    QVector<AppPreferenceWarning> warnings;
};

} // namespace labelqt::core
