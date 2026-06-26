#include "services/AutomationPythonResolver.h"

#include "core/CommandLineUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

#include <algorithm>

namespace labelqt::services::AutomationPythonResolver {
namespace {
QString commandDisplayText(const QString& program, const QStringList& arguments)
{
    QStringList command = {program};
    command.append(arguments);
    return labelqt::core::joinCommandLine(command);
}

void appendPythonCommand(QVector<AutomationPythonCommand>* commands, const QString& program,
                         const QStringList& arguments)
{
    if (commands == nullptr || program.trimmed().isEmpty()) {
        return;
    }

    AutomationPythonCommand command;
    command.program = program.trimmed();
    command.arguments = arguments;
    command.displayText = commandDisplayText(command.program, command.arguments);
    const auto duplicate = std::ranges::find_if(*commands, [&command](const auto& candidate) {
        return candidate.program == command.program && candidate.arguments == command.arguments;
    });
    if (duplicate == commands->cend()) {
        commands->append(command);
    }
}

void appendPythonCommandString(QVector<AutomationPythonCommand>* commands, const QString& commandText,
                               const QStringList& extraArguments = {})
{
    const QString trimmedCommand = commandText.trimmed();
    if (trimmedCommand.isEmpty()) {
        return;
    }

    if (QFileInfo::exists(trimmedCommand)) {
        appendPythonCommand(commands, trimmedCommand, extraArguments);
        return;
    }

    QStringList parts = QProcess::splitCommand(trimmedCommand);
    if (parts.isEmpty()) {
        return;
    }

    const QString program = parts.takeFirst();
    parts.append(extraArguments);
    appendPythonCommand(commands, program, parts);
}
} // namespace

QVector<AutomationPythonCommand> candidates(const AutomationPythonSettings& settings)
{
    QVector<AutomationPythonCommand> commands;
    if (!settings.command.trimmed().isEmpty()) {
        appendPythonCommandString(&commands, settings.command, settings.arguments);
        return commands;
    }

    const QByteArray configuredPython = qgetenv("LABELQT_PYTHON");
    if (!configuredPython.trimmed().isEmpty()) {
        appendPythonCommandString(&commands, QString::fromLocal8Bit(configuredPython));
    }

    const QString bundledPython = QDir(QCoreApplication::applicationDirPath())
                                      .filePath(
#ifdef Q_OS_WIN
                                          QStringLiteral("python/python.exe")
#else
                                          QStringLiteral("python/bin/python3")
#endif
                                      );
    if (QFileInfo::exists(bundledPython)) {
        appendPythonCommand(&commands, bundledPython, {});
    }

#ifdef Q_OS_WIN
    appendPythonCommand(&commands, QStringLiteral("python"), {});
    appendPythonCommand(&commands, QStringLiteral("py"), {});
#else
    appendPythonCommand(&commands, QStringLiteral("python3"), {});
    appendPythonCommand(&commands, QStringLiteral("python"), {});
#endif
    return commands;
}

QString unavailableError(const QVector<AutomationPythonCommand>& candidates, const QString& lastError)
{
    QStringList triedPrograms;
    triedPrograms.reserve(candidates.size());
    for (const AutomationPythonCommand& command : candidates) {
        triedPrograms.append(command.displayText);
    }
    // clang-format off
    return QCoreApplication::translate("AutomationService", "Python was not found. Install Python 3, configure the Python command in Preferences, or place a portable Python runtime next to LabelQt.\n\nTried: %1\nLast error: %2")
        .arg(triedPrograms.join(QStringLiteral(", ")), lastError);
    // clang-format on
}

} // namespace labelqt::services::AutomationPythonResolver
