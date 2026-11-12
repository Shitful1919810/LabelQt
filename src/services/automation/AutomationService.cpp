#include "services/automation/AutomationService.h"

#include "core/CommandLineUtils.h"
#include "core/RuntimePaths.h"
#include "services/automation/AutomationManifestParser.h"
#include "services/automation/AutomationPythonResolver.h"
#include "services/automation/SecretStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <expected>
#include <optional>
#include <utility>

namespace labelqt::services {

namespace {
constexpr int automationTimeoutMs = 300000;
constexpr int automationProcessStopWaitMs = 3000;
constexpr int automationLogMaxCharacters = 1024 * 1024;

QString commandDisplayText(const QString& program, const QStringList& arguments)
{
    QStringList command = {program};
    command.append(arguments);
    return labelqt::core::joinCommandLine(command);
}

bool writeJsonFile(const QString& path, const QJsonObject& object, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    const QByteArray data = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size()) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

bool readJsonFile(const QString& path, QJsonObject* object, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }

    QJsonParseError parseError;
    constexpr qint64 maxJsonBytes = 16 * 1024 * 1024;
    const QByteArray bytes = file.read(maxJsonBytes + 1);
    if (bytes.size() > maxJsonBytes || file.error() != QFileDevice::NoError) {
        if (error != nullptr)
            *error = QCoreApplication::translate(
                "AutomationController",
                "The script returned invalid or unauthorized changes. No changes were applied.");
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error != nullptr) {
            *error = parseError.errorString();
        }
        return false;
    }
    if (!document.isObject()) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("AutomationService", "JSON root must be an object.");
        }
        return false;
    }
    *object = document.object();
    return true;
}

QString scriptIdForDirectory(const QFileInfo& scriptDirectory, bool official)
{
    return QStringLiteral("%1:%2").arg(official ? QStringLiteral("official") : QStringLiteral("custom"),
                                       scriptDirectory.fileName());
}

QString scriptLocation(const QFileInfo& scriptDirectory, const QString& scriptName = {})
{
    if (scriptName.trimmed().isEmpty()) {
        return scriptDirectory.fileName();
    }
    return QStringLiteral("%1/%2").arg(scriptDirectory.fileName(), scriptName);
}

void appendLog(QString* log, const QString& text)
{
    if (log == nullptr || text.isEmpty()) {
        return;
    }
    log->append(text);
    if (log->size() > automationLogMaxCharacters) {
        const int keepCharacters = automationLogMaxCharacters / 2;
        *log = QCoreApplication::translate("AutomationService", "[Earlier automation log output was truncated.]\n") +
               log->right(keepCharacters);
    }
}

bool appendScriptFromManifest(QVector<AutomationScript>* scripts, const QFileInfo& scriptDirectory,
                              const QJsonObject& directoryManifest, const QJsonObject& scriptManifest, bool official,
                              int directoryIndex, int scriptIndex, QStringList* warnings)
{
    const QString entry = scriptManifest.value(QStringLiteral("entry")).toString();
    if (entry.trimmed().isEmpty()) {
        if (warnings != nullptr) {
            warnings->append(
                QCoreApplication::translate("AutomationService", "Skipped automation script %1: missing entry.")
                    .arg(scriptLocation(scriptDirectory, scriptManifest.value(QStringLiteral("name")).toString())));
        }
        return false;
    }

    const QString entryPath = QDir(scriptDirectory.absoluteFilePath()).filePath(entry);
    if (!QFileInfo::exists(entryPath)) {
        if (warnings != nullptr) {
            warnings->append(QCoreApplication::translate("AutomationService",
                                                         "Skipped automation script %1: entry file does not exist.")
                                 .arg(scriptLocation(scriptDirectory, entry)));
        }
        return false;
    }

    const auto task = AutomationProtocol::parseTaskSpec(scriptManifest.value("task").toObject());
    if (directoryManifest.value("apiVersion") != QJsonValue(AutomationProtocol::version) || !task) {
        if (warnings)
            warnings->append(
                QCoreApplication::translate("AutomationService", "Skipped automation script directory %1: %2")
                    .arg(scriptDirectory.fileName(),
                         QCoreApplication::translate("AutomationService",
                                                     "A version 2 manifest with a valid task scope is required.")));
        return false;
    }
    AutomationScript script;
    script.task = *task;
    const QString scriptId = scriptManifest.value(QStringLiteral("id")).toString(QString::number(scriptIndex));
    script.id = QStringLiteral("%1:%2").arg(
        scriptIdForDirectory(scriptDirectory, official),
        directoryManifest.contains(QStringLiteral("scripts")) ? scriptId : QStringLiteral("single"));
    script.name = scriptManifest.value(QStringLiteral("name")).toString(scriptDirectory.fileName());
    script.description = scriptManifest.value(QStringLiteral("description"))
                             .toString(directoryManifest.value(QStringLiteral("description")).toString());
    script.directoryName = directoryManifest.value(QStringLiteral("name")).toString(scriptDirectory.fileName());
    script.directoryPath = scriptDirectory.absoluteFilePath();
    script.entryPath = entryPath;
    script.parameters = AutomationManifestParser::parametersFromManifest(scriptManifest);
    script.secrets = AutomationManifestParser::secretsFromManifest(scriptManifest);
    script.environment = AutomationManifestParser::environmentFromManifest(scriptManifest);
    script.official = official;
    script.directoryOrder = directoryIndex;
    script.scriptOrder = scriptIndex;
    scripts->append(script);
    return true;
}

} // namespace

QVector<AutomationScript> AutomationService::discoverScripts(QStringList* warnings)
{
    QVector<AutomationScript> scripts;
    const QStringList roots = scriptsRootCandidates();
    for (const QString& rootPath : roots) {
        const QDir root(rootPath);
        if (!root.exists()) {
            continue;
        }

        const bool official = root.dirName() == QStringLiteral("official");
        const QFileInfoList scriptDirectories = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        int directoryIndex = 0;
        for (const QFileInfo& scriptDirectory : scriptDirectories) {
            const QString manifestPath =
                QDir(scriptDirectory.absoluteFilePath()).filePath(QStringLiteral("script.json"));
            if (!QFileInfo::exists(manifestPath))
                continue;
            QJsonObject manifest;
            QString error;
            if (!readJsonFile(manifestPath, &manifest, &error)) {
                if (warnings != nullptr) {
                    warnings->append(
                        QCoreApplication::translate("AutomationService", "Skipped automation script directory %1: %2")
                            .arg(scriptDirectory.fileName(), error));
                }
                ++directoryIndex;
                continue;
            }

            const QJsonArray scriptArray = manifest.value(QStringLiteral("scripts")).toArray();
            if (!scriptArray.isEmpty()) {
                int scriptIndex = 0;
                for (const QJsonValue& scriptValue : scriptArray) {
                    appendScriptFromManifest(&scripts, scriptDirectory, manifest, scriptValue.toObject(), official,
                                             directoryIndex, scriptIndex, warnings);
                    ++scriptIndex;
                }
            }
            else {
                appendScriptFromManifest(&scripts, scriptDirectory, manifest, manifest, official, directoryIndex, 0,
                                         warnings);
            }
            ++directoryIndex;
        }
    }

    std::sort(scripts.begin(), scripts.end(), [](const AutomationScript& lhs, const AutomationScript& rhs) {
        if (lhs.official != rhs.official) {
            return lhs.official;
        }
        const int directoryCompare = QString::localeAwareCompare(lhs.directoryName, rhs.directoryName);
        if (directoryCompare != 0) {
            return directoryCompare < 0;
        }
        return lhs.scriptOrder < rhs.scriptOrder;
    });
    return scripts;
}

std::expected<void, QString> AutomationService::tryStoreParameterSecrets(const AutomationScript& script,
                                                                         const QMap<QString, QString>& secrets)
{
    for (const AutomationParameter& parameter : script.parameters) {
        if (parameter.type.compare(QStringLiteral("secret"), Qt::CaseInsensitive) != 0 ||
            !secrets.contains(parameter.secretKey)) {
            continue;
        }

        const std::expected<void, QString> result = SecretStore::tryWriteText(
            parameter.secretService, parameter.secretAccount, secrets.value(parameter.secretKey));
        if (!result.has_value()) {
            return std::unexpected(
                QCoreApplication::translate("AutomationService", "Failed to store automation secret %1: %2")
                    .arg(parameter.label, result.error()));
        }
    }
    return {};
}

bool AutomationService::storeParameterSecrets(const AutomationScript& script, const QMap<QString, QString>& secrets,
                                              QString* error)
{
    const std::expected<void, QString> result = tryStoreParameterSecrets(script, secrets);
    if (result.has_value()) {
        return true;
    }
    if (error != nullptr) {
        *error = result.error();
    }
    return false;
}

std::expected<QMap<QString, QString>, QString> AutomationService::trySecretEnvironment(const AutomationScript& script)
{
    QMap<QString, QString> environment;
    for (const AutomationSecret& secret : script.secrets) {
        const std::expected<std::optional<QString>, QString> result =
            SecretStore::tryReadText(secret.service, secret.account);
        if (!result.has_value()) {
            return std::unexpected(
                QCoreApplication::translate("AutomationService", "Failed to read automation secret %1: %2")
                    .arg(secret.label, result.error()));
        }
        if (!result->has_value() || result->value().isEmpty()) {
            if (secret.required) {
                return std::unexpected(
                    QCoreApplication::translate(
                        "AutomationService",
                        "Automation secret %1 is not configured. Run the script's configuration first.")
                        .arg(secret.label));
            }
            continue;
        }
        environment.insert(secret.environment, result->value());
    }
    return environment;
}

bool AutomationService::secretEnvironment(const AutomationScript& script, QMap<QString, QString>* environment,
                                          QString* error)
{
    if (environment != nullptr) {
        environment->clear();
    }

    const std::expected<QMap<QString, QString>, QString> result = trySecretEnvironment(script);
    if (!result.has_value()) {
        if (error != nullptr) {
            *error = result.error();
        }
        return false;
    }
    if (environment != nullptr) {
        *environment = *result;
    }
    return true;
}

AutomationRunner::AutomationRunner(QObject* parent) : QObject(parent)
{
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, &AutomationRunner::handleTimeout);
}

AutomationRunner::~AutomationRunner()
{
    m_timeoutTimer->stop();
    m_timeoutTimer->disconnect(this);
    if (m_process != nullptr) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(automationProcessStopWaitMs);
        }
    }
}

void AutomationRunner::start(const AutomationScript& script, const ProjectTaskSnapshot& snapshot,
                             const QJsonObject& parameters, const QJsonObject& selection,
                             const QMap<QString, QString>& environmentOverrides,
                             AutomationPythonSettings pythonSettings)
{
    if (m_running)
        return;

    m_script = script;
    m_taskId = snapshot.taskId;
    m_sessionId = snapshot.sessionId;
    m_environmentOverrides = environmentOverrides;
    m_pythonSettings = std::move(pythonSettings);
    m_pythonCandidates = AutomationPythonResolver::candidates(m_pythonSettings);
    m_candidateIndex = 0;
    m_lastFailure = {};
    m_cancelRequested = false;
    m_running = true;

    m_temporaryDirectory = std::make_unique<QTemporaryDir>();
    if (!m_temporaryDirectory->isValid()) {
        AutomationRunResult result;
        result.error = QStringLiteral("Failed to create a temporary directory.");
        finishWithResult(result);
        return;
    }

    const QString inputPath = m_temporaryDirectory->filePath(QStringLiteral("input.json"));
    m_outputPath = m_temporaryDirectory->filePath(QStringLiteral("output.json"));
    QString error;
    if (!writeJsonFile(inputPath, AutomationProtocol::input(snapshot, parameters, selection), &error)) {
        AutomationRunResult result;
        result.error = QStringLiteral("Failed to write automation input: %1").arg(error);
        finishWithResult(result);
        return;
    }

    m_scriptArguments = {script.entryPath, QStringLiteral("--input"), inputPath, QStringLiteral("--output"),
                         m_outputPath};
    m_requirementsPath = QDir(script.directoryPath).filePath(QStringLiteral("requirements.txt"));
    startNextCandidate();
}

void AutomationRunner::cancel()
{
    m_cancelRequested = true;
    if (m_process != nullptr && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
    }
}

bool AutomationRunner::isRunning() const noexcept
{
    return m_running;
}

void AutomationRunner::startNextCandidate()
{
    if (m_cancelRequested) {
        m_lastFailure.error = QStringLiteral("Automation script was canceled.");
        finishWithResult(m_lastFailure);
        return;
    }
    if (m_candidateIndex < m_pythonCandidates.size()) {
        const AutomationPythonCommand command = m_pythonCandidates.at(m_candidateIndex);
        ++m_candidateIndex;

        if (m_pythonSettings.autoInstallRequirements && QFileInfo::exists(m_requirementsPath)) {
            QStringList installArguments = {QStringLiteral("-m"), QStringLiteral("pip"), QStringLiteral("install"),
                                            QStringLiteral("-r"), m_requirementsPath};
            if (!m_pythonSettings.pipIndexUrl.trimmed().isEmpty()) {
                installArguments.append({QStringLiteral("-i"), m_pythonSettings.pipIndexUrl.trimmed()});
            }
            startProcess(command, installArguments, RunPhase::InstallRequirements);
        }
        else {
            startProcess(command, m_scriptArguments, RunPhase::RunScript);
        }
        return;
    }

    m_lastFailure.error = AutomationPythonResolver::unavailableError(m_pythonCandidates, m_lastFailure.error);
    finishWithResult(m_lastFailure);
}

void AutomationRunner::startProcess(AutomationPythonCommand command, QStringList arguments, RunPhase phase)
{
    if (m_process != nullptr) {
        m_process->disconnect(this);
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_currentPythonCommand = command;
    m_phase = phase;
    arguments = command.arguments + arguments;

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_script.directoryPath);
    QProcessEnvironment processEnvironment = QProcessEnvironment::systemEnvironment();
    processEnvironment.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    processEnvironment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    for (auto it = m_script.environment.constBegin(); it != m_script.environment.constEnd(); ++it) {
        processEnvironment.insert(it.key(), it.value());
    }
    for (auto it = m_environmentOverrides.constBegin(); it != m_environmentOverrides.constEnd(); ++it) {
        processEnvironment.insert(it.key(), it.value());
    }
    m_process->setProcessEnvironment(processEnvironment);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &AutomationRunner::appendProcessOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &AutomationRunner::appendProcessOutput);
    connect(m_process, &QProcess::started, this, &AutomationRunner::handleStarted);
    connect(m_process, &QProcess::errorOccurred, this, &AutomationRunner::handleError);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            &AutomationRunner::handleFinished);

    // Startup participates in the same asynchronous failure/timeout path as script execution.
    // In particular, FailedToStart has no finished() signal, including after requirements installation.
    m_timeoutTimer->start(automationTimeoutMs);
    m_process->start(command.program, arguments);
}

void AutomationRunner::handleStarted()
{
    if (m_process != nullptr) {
        const QString phaseText = m_phase == RunPhase::InstallRequirements
                                      ? QCoreApplication::translate("AutomationService", "Installing requirements")
                                      : QCoreApplication::translate("AutomationService", "Running script");
        emit standardOutputReceived(
            QStringLiteral("%1: %2\n")
                .arg(phaseText, commandDisplayText(m_process->program(), m_process->arguments())));
    }
}

void AutomationRunner::handleFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    appendProcessOutput();
    if (m_timeoutTimer != nullptr) {
        m_timeoutTimer->stop();
    }

    if (m_cancelRequested) {
        AutomationRunResult result;
        result.error = QStringLiteral("Automation script was canceled.");
        if (m_process != nullptr) {
            appendLog(&result.standardOutput, QString::fromUtf8(m_process->readAllStandardOutput()));
            appendLog(&result.standardError, QString::fromUtf8(m_process->readAllStandardError()));
        }
        finishWithResult(result);
        return;
    }

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        m_lastFailure.error =
            m_phase == RunPhase::InstallRequirements
                ? QCoreApplication::translate("AutomationService",
                                              "Failed to install automation requirements with exit code %1.")
                      .arg(exitCode)
                : QStringLiteral("Automation script failed with exit code %1.").arg(exitCode);
        finishWithResult(m_lastFailure);
        return;
    }

    if (m_phase == RunPhase::InstallRequirements) {
        startProcess(m_currentPythonCommand, m_scriptArguments, RunPhase::RunScript);
        return;
    }

    QJsonObject output;
    QString error;
    if (!readJsonFile(m_outputPath, &output, &error)) {
        m_lastFailure.error = QStringLiteral("Failed to read automation output: %1").arg(error);
        finishWithResult(m_lastFailure);
        return;
    }

    const auto parsedResult = AutomationProtocol::result(output);
    if (!parsedResult.has_value()) {
        m_lastFailure.error = parsedResult.error();
        finishWithResult(m_lastFailure);
        return;
    }

    if (parsedResult->taskId != m_taskId || parsedResult->sessionId != m_sessionId) {
        m_lastFailure.error = QCoreApplication::translate(
            "AutomationController", "The script returned invalid or unauthorized changes. No changes were applied.");
        finishWithResult(m_lastFailure);
        return;
    }
    AutomationRunResult result;
    result.success = true;
    result.output = *parsedResult;
    result.standardOutput = m_lastFailure.standardOutput;
    result.standardError = m_lastFailure.standardError;
    finishWithResult(result);
}

void AutomationRunner::handleError(QProcess::ProcessError error)
{
    if (m_process == nullptr || sender() != m_process || !m_running) {
        return;
    }
    m_lastFailure.error = m_process->errorString();
    if (error == QProcess::FailedToStart) {
        m_timeoutTimer->stop();
        emit standardErrorReceived(m_lastFailure.error + QLatin1Char('\n'));
        startNextCandidate();
    }
}

void AutomationRunner::handleTimeout()
{
    if (m_process != nullptr && m_process->state() != QProcess::NotRunning) {
        m_lastFailure.error = QStringLiteral("Automation script timed out.");
        emit standardErrorReceived(m_lastFailure.error + QLatin1Char('\n'));
        m_process->kill();
    }
}

void AutomationRunner::appendProcessOutput()
{
    if (m_process == nullptr) {
        return;
    }

    const QString standardOutput = QString::fromUtf8(m_process->readAllStandardOutput());
    if (!standardOutput.isEmpty()) {
        appendLog(&m_lastFailure.standardOutput, standardOutput);
        emit standardOutputReceived(standardOutput);
    }

    const QString standardError = QString::fromUtf8(m_process->readAllStandardError());
    if (!standardError.isEmpty()) {
        appendLog(&m_lastFailure.standardError, standardError);
        emit standardErrorReceived(standardError);
    }
}

void AutomationRunner::finishWithResult(AutomationRunResult result)
{
    if (!m_running && !result.success && result.error.isEmpty()) {
        return;
    }

    if (m_timeoutTimer != nullptr) {
        m_timeoutTimer->stop();
    }
    if (m_process != nullptr) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
        }
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_running = false;
    m_temporaryDirectory.reset();
    emit finished(result);
}

QStringList AutomationService::scriptsRootCandidates()
{
    return labelqt::core::automationScriptsRootCandidates();
}

} // namespace labelqt::services
