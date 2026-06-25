#pragma once

#include <QColor>
#include <QKeySequence>
#include <QMap>
#include <QString>
#include <QVector>
#include <Qt>

class QJsonDocument;
class QJsonParseError;

namespace labelqt::core {

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
    BackupPathWrongType,
    BackupIntervalWrongType,
    BackupIntervalOutOfRange,
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

class AppPreferences {
public:
    static QString defaultFilePath();
    static AppPreferences load();
    static AppPreferencesLoadResult loadWithDiagnostics();
    static AppPreferencesLoadResult loadFromFile(const QString& path);
    static AppPreferencesLoadResult loadFromJson(const QByteArray& json);

    QJsonDocument toJsonDocument() const;

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
    double canvasLabelTextEditorOpacity() const noexcept;
    QString applicationStyle() const;
    QString applicationTheme() const;
    QString applicationLanguage() const;
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
    QString backupPath() const;
    int backupIntervalSeconds() const noexcept;
    const QVector<LabelGroupStyle>& groupStyles() const noexcept;

private:
    static QVector<LabelGroupStyle> defaultGroupStyles();
    static AppPreferencesLoadResult loadFromDocument(const QJsonDocument& document, const QJsonParseError* parseError);

    double m_labelMarkerDiameterPixels{20.0};
    double m_labelMarkerFontPointSize{10.0};
    int m_labelTableMaxTextRows{3};
    QString m_labelTableFontFamily;
    double m_labelTableFontPointSize{0.0};
    QString m_labelTextEditorFontFamily;
    double m_labelTextEditorFontPointSize{0.0};
    QString m_markerTextBubbleFontFamily;
    double m_markerTextBubbleFontPointSize{0.0};
    double m_markerTextBubbleOpacity{1.0};
    double m_canvasLabelTextEditorOpacity{1.0};
    QString m_applicationStyle;
    QString m_applicationTheme;
    QString m_applicationLanguage;
    bool m_showAutomationRunLog{false};
    QString m_automationPythonCommand;
    QStringList m_automationPythonArguments;
    bool m_automationAutoInstallRequirements{false};
    QString m_automationPipIndexUrl;
    QMap<QString, QKeySequence> m_automationShortcuts;
    Qt::KeyboardModifiers m_moveLabelModifiers{Qt::ControlModifier};
    Qt::KeyboardModifiers m_previousLabelModifiers{Qt::ControlModifier};
    QKeySequence m_undoShortcut{QStringLiteral("Ctrl+Z")};
    QKeySequence m_redoShortcut{QStringLiteral("Ctrl+Y")};
    QKeySequence m_nextLabelShortcut{QStringLiteral("Tab")};
    QKeySequence m_alternatePreviousLabelShortcut{QStringLiteral("Ctrl+Up")};
    QKeySequence m_alternateNextLabelShortcut{QStringLiteral("Ctrl+Down")};
    QKeySequence m_previousPageShortcut{QStringLiteral("Alt+Left")};
    QKeySequence m_nextPageShortcut{QStringLiteral("Alt+Right")};
    QKeySequence m_editLabelTextShortcut{QStringLiteral("Return")};
    QKeySequence m_commitLabelTextShortcut{QStringLiteral("Ctrl+Return")};
    QString m_backupPath{QStringLiteral("bak")};
    int m_backupIntervalSeconds{60};
    QVector<LabelGroupStyle> m_groupStyles{defaultGroupStyles()};
};

struct AppPreferencesLoadResult {
    AppPreferences preferences;
    QVector<AppPreferenceWarning> warnings;
};

} // namespace labelqt::core
