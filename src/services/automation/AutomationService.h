#pragma once

#include "core/Project.h"

#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <expected>
#include <memory>

class QTemporaryDir;
class QTimer;

namespace labelqt::services {

struct AutomationParameter {
    QString key;
    QString label;
    QString type;
    QString defaultValue;
    QStringList options;
    QString secretKey;
    QString secretService;
    QString secretAccount;
    QString secretEnvironment;
};

struct AutomationSecret {
    QString key;
    QString label;
    QString service;
    QString account;
    QString environment;
    bool required{true};
};

struct AutomationOperation {
    QString type;
    QString page;
    int labelIndex{-1};
    QString group;
    QString text;
    double x{0.0};
    double y{0.0};
};

struct AutomationSelection {
    bool hasSelection{false};
    QRectF normalizedRect;
};

struct AutomationContext {
    int currentImageIndex{-1};
    QVector<int> selectedLabelIndexes;
};

struct AutomationPythonSettings {
    QString command;
    QStringList arguments;
    bool autoInstallRequirements{false};
    QString pipIndexUrl;
};

struct AutomationPythonCommand {
    QString program;
    QStringList arguments;
    QString displayText;
};

struct AutomationScript {
    QString id;
    QString name;
    QString description;
    QString directoryName;
    QString directoryPath;
    QString entryPath;
    QVector<AutomationParameter> parameters;
    QVector<AutomationSecret> secrets;
    QMap<QString, QString> environment;
    bool official{false};
    int directoryOrder{0};
    int scriptOrder{0};
};

struct AutomationRunResult {
    bool success{false};
    QString summary;
    QString resultTitle;
    QString resultText;
    QString standardOutput;
    QString standardError;
    QString error;
    QVector<AutomationOperation> operations;
    bool quiet{false};
};

class AutomationService final {
public:
    static QVector<AutomationScript> discoverScripts(QStringList* warnings = nullptr);
    static std::expected<void, QString> tryStoreParameterSecrets(const AutomationScript& script,
                                                                 const QMap<QString, QString>& secrets);
    static bool storeParameterSecrets(const AutomationScript& script, const QMap<QString, QString>& secrets,
                                      QString* error);
    static std::expected<QMap<QString, QString>, QString> trySecretEnvironment(const AutomationScript& script);
    static bool secretEnvironment(const AutomationScript& script, QMap<QString, QString>* environment, QString* error);

private:
    static QStringList scriptsRootCandidates();
};

class AutomationRunner final : public QObject {
    Q_OBJECT

public:
    explicit AutomationRunner(QObject* parent = nullptr);
    ~AutomationRunner() override;

    void start(const AutomationScript& script, const labelqt::core::Project& project, int currentImageIndex,
               const QJsonObject& parameters = {}, AutomationSelection selection = {}, AutomationContext context = {},
               const QMap<QString, QString>& environmentOverrides = {},
               AutomationPythonSettings pythonSettings = {});
    void cancel();
    bool isRunning() const noexcept;

signals:
    void standardOutputReceived(const QString& text);
    void standardErrorReceived(const QString& text);
    void finished(const labelqt::services::AutomationRunResult& result);

private:
    enum class RunPhase {
        InstallRequirements,
        RunScript,
    };

    void startNextCandidate();
    void startProcess(const AutomationPythonCommand& command, QStringList arguments, RunPhase phase);
    void handleStarted();
    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleError();
    void handleTimeout();
    void appendProcessOutput();
    void finishWithResult(AutomationRunResult result);

    AutomationScript m_script;
    QMap<QString, QString> m_environmentOverrides;
    AutomationPythonSettings m_pythonSettings;
    QVector<AutomationPythonCommand> m_pythonCandidates;
    AutomationPythonCommand m_currentPythonCommand;
    RunPhase m_phase{RunPhase::RunScript};
    int m_candidateIndex{0};
    QStringList m_scriptArguments;
    QString m_outputPath;
    QString m_requirementsPath;
    AutomationRunResult m_lastFailure;
    std::unique_ptr<QTemporaryDir> m_temporaryDirectory;
    QProcess* m_process{nullptr};
    QTimer* m_timeoutTimer{nullptr};
    bool m_running{false};
    bool m_cancelRequested{false};
};

} // namespace labelqt::services
