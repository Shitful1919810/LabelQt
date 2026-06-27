#include "services/AutomationService.h"

#include "core/CommandLineUtils.h"
#include "core/RuntimePaths.h"
#include "services/AutomationManifestParser.h"
#include "services/AutomationPythonResolver.h"
#include "services/SecretStore.h"

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
constexpr int automationApiVersion = 1;
constexpr int automationTimeoutMs = 300000;
constexpr int automationProcessStopWaitMs = 3000;
constexpr int automationLogMaxCharacters = 1024 * 1024;

QJsonObject labelToJson(const labelqt::core::Label& label, int labelIndex, int visibleIndex)
{
    const QPointF position = label.position();
    return {
        {QStringLiteral("labelIndex"), labelIndex}, {QStringLiteral("visibleIndex"), visibleIndex},
        {QStringLiteral("group"), label.group()},   {QStringLiteral("x"), position.x()},
        {QStringLiteral("y"), position.y()},        {QStringLiteral("text"), label.text()},
    };
}

int visibleIndexForLabel(const labelqt::core::ImageEntry& image, int targetLabelIndex)
{
    if (targetLabelIndex < 0 || targetLabelIndex >= image.labels.size() ||
        image.labels.at(targetLabelIndex).isDeleted()) {
        return 0;
    }

    int visibleIndex = 0;
    for (int labelIndex = 0; labelIndex <= targetLabelIndex; ++labelIndex) {
        if (!image.labels.at(labelIndex).isDeleted()) {
            ++visibleIndex;
        }
    }
    return visibleIndex;
}

QJsonObject projectToJson(const labelqt::core::Project& project, int currentImageIndex)
{
    QJsonArray groups;
    for (const QString& group : project.groups()) {
        groups.append(group);
    }

    QJsonArray imagePaths;
    QJsonArray pages;
    for (int imageIndex = 0; imageIndex < project.images().size(); ++imageIndex) {
        const labelqt::core::ImageEntry& image = project.images().at(imageIndex);
        imagePaths.append(image.path);
        QJsonArray labels;
        int visibleIndex = 1;
        for (int labelIndex = 0; labelIndex < image.labels.size(); ++labelIndex) {
            const labelqt::core::Label& label = image.labels.at(labelIndex);
            if (label.isDeleted()) {
                continue;
            }
            labels.append(labelToJson(label, labelIndex, visibleIndex));
            ++visibleIndex;
        }

        pages.append(QJsonObject{
            {QStringLiteral("index"), imageIndex},
            {QStringLiteral("name"), image.name},
            {QStringLiteral("imagePath"), image.path},
            {QStringLiteral("labels"), labels},
        });
    }

    const QString currentPage = currentImageIndex >= 0 && currentImageIndex < project.images().size()
                                    ? project.images().at(currentImageIndex).name
                                    : QString();
    return {
        {QStringLiteral("path"), project.filePath()}, {QStringLiteral("sourceName"), project.sourceName()},
        {QStringLiteral("groups"), groups},           {QStringLiteral("currentPage"), currentPage},
        {QStringLiteral("imagePaths"), imagePaths},   {QStringLiteral("pages"), pages},
    };
}

QJsonObject selectionToJson(const labelqt::core::Project& project, int currentImageIndex, AutomationSelection selection)
{
    const bool hasSelection = selection.hasSelection && currentImageIndex >= 0 &&
                              currentImageIndex < project.images().size() && selection.normalizedRect.width() > 0.0 &&
                              selection.normalizedRect.height() > 0.0;
    if (!hasSelection) {
        return {{QStringLiteral("hasSelection"), false}};
    }

    QRectF rect = selection.normalizedRect.normalized();
    const double left = std::clamp(rect.left(), 0.0, 1.0);
    const double top = std::clamp(rect.top(), 0.0, 1.0);
    const double right = std::clamp(rect.left() + rect.width(), 0.0, 1.0);
    const double bottom = std::clamp(rect.top() + rect.height(), 0.0, 1.0);
    rect = QRectF(QPointF(left, top), QPointF(right, bottom)).normalized();

    const labelqt::core::ImageEntry& image = project.images().at(currentImageIndex);
    return {
        {QStringLiteral("hasSelection"), true},
        {QStringLiteral("imageIndex"), currentImageIndex},
        {QStringLiteral("page"), image.name},
        {QStringLiteral("imagePath"), image.path},
        {QStringLiteral("rect"),
         QJsonObject{
             {QStringLiteral("x"), rect.left()},
             {QStringLiteral("y"), rect.top()},
             {QStringLiteral("width"), rect.width()},
             {QStringLiteral("height"), rect.height()},
             {QStringLiteral("left"), rect.left()},
             {QStringLiteral("top"), rect.top()},
             {QStringLiteral("right"), rect.left() + rect.width()},
             {QStringLiteral("bottom"), rect.top() + rect.height()},
         }},
    };
}

QJsonObject contextToJson(const labelqt::core::Project& project, AutomationContext context)
{
    const int currentImageIndex = context.currentImageIndex;
    QJsonObject currentPage{{QStringLiteral("hasPage"), false}};
    if (currentImageIndex >= 0 && currentImageIndex < project.images().size()) {
        const labelqt::core::ImageEntry& image = project.images().at(currentImageIndex);
        currentPage = {
            {QStringLiteral("hasPage"), true},
            {QStringLiteral("index"), currentImageIndex},
            {QStringLiteral("number"), currentImageIndex + 1},
            {QStringLiteral("name"), image.name},
            {QStringLiteral("imagePath"), image.path},
        };
    }

    QJsonArray selectedLabelIndexes;
    QJsonArray selectedLabels;
    if (currentImageIndex >= 0 && currentImageIndex < project.images().size()) {
        const labelqt::core::ImageEntry& image = project.images().at(currentImageIndex);
        for (int labelIndex : context.selectedLabelIndexes) {
            if (labelIndex < 0 || labelIndex >= image.labels.size() || image.labels.at(labelIndex).isDeleted()) {
                continue;
            }
            selectedLabelIndexes.append(labelIndex);
            selectedLabels.append(
                labelToJson(image.labels.at(labelIndex), labelIndex, visibleIndexForLabel(image, labelIndex)));
        }
    }

    return {
        {QStringLiteral("currentPage"), currentPage},
        {QStringLiteral("selectedLabelIndexes"), selectedLabelIndexes},
        {QStringLiteral("selectedLabels"), selectedLabels},
    };
}

QJsonObject inputPayload(const labelqt::core::Project& project, int currentImageIndex, const QJsonObject& parameters,
                         AutomationSelection selection, AutomationContext context)
{
    return {
        {QStringLiteral("apiVersion"), automationApiVersion},
        {QStringLiteral("scope"), QStringLiteral("project")},
        {QStringLiteral("options"), QJsonObject{{QStringLiteral("includeDeletedLabels"), false}}},
        {QStringLiteral("parameters"), parameters},
        {QStringLiteral("context"), contextToJson(project, context)},
        {QStringLiteral("selection"), selectionToJson(project, currentImageIndex, selection)},
        {QStringLiteral("project"), projectToJson(project, currentImageIndex)},
    };
}

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
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
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

    AutomationScript script;
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

        const std::expected<void, QString> result = SecretStore::tryWriteText(parameter.secretService,
                                                                             parameter.secretAccount,
                                                                             secrets.value(parameter.secretKey));
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
                return std::unexpected(QCoreApplication::translate(
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
    if (m_process != nullptr) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(automationProcessStopWaitMs);
        }
    }
}

void AutomationRunner::start(const AutomationScript& script, const labelqt::core::Project& project,
                             int currentImageIndex, const QJsonObject& parameters, AutomationSelection selection,
                             AutomationContext context, const QMap<QString, QString>& environmentOverrides,
                             AutomationPythonSettings pythonSettings)
{
    if (m_running) {
        AutomationRunResult result;
        result.error = QStringLiteral("Automation script is already running.");
        finishWithResult(result);
        return;
    }

    m_script = script;
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
    if (!writeJsonFile(inputPath, inputPayload(project, currentImageIndex, parameters, selection, context), &error)) {
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
    while (m_candidateIndex < m_pythonCandidates.size()) {
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
        if (m_process != nullptr && m_process->state() != QProcess::NotRunning) {
            return;
        }
    }

    m_lastFailure.error = AutomationPythonResolver::unavailableError(m_pythonCandidates, m_lastFailure.error);
    finishWithResult(m_lastFailure);
}

void AutomationRunner::startProcess(const AutomationPythonCommand& command, QStringList arguments, RunPhase phase)
{
    if (m_process != nullptr) {
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

    m_process->start(command.program, arguments);
    if (m_process->waitForStarted(1000)) {
        m_timeoutTimer->start(automationTimeoutMs);
        return;
    }

    m_lastFailure.error = QStringLiteral("Failed to start %1: %2").arg(command.displayText, m_process->errorString());
    emit standardErrorReceived(m_lastFailure.error + QLatin1Char('\n'));
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

    std::expected<AutomationRunResult, QString> parsedResult = AutomationManifestParser::tryResultFromOutput(output);
    if (!parsedResult.has_value()) {
        m_lastFailure.error = parsedResult.error();
        finishWithResult(m_lastFailure);
        return;
    }

    AutomationRunResult result = *parsedResult;
    result.standardOutput = m_lastFailure.standardOutput;
    result.standardError = m_lastFailure.standardError;
    finishWithResult(result);
}

void AutomationRunner::handleError()
{
    if (m_process == nullptr || m_process->state() != QProcess::NotRunning) {
        return;
    }
    m_lastFailure.error = m_process->errorString();
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
    emit finished(result);
    m_temporaryDirectory.reset();
}

QStringList AutomationService::scriptsRootCandidates()
{
    return labelqt::core::automationScriptsRootCandidates();
}

} // namespace labelqt::services
