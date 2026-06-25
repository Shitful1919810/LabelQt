#include "core/CommandLineUtils.h"

namespace labelqt::core {

QString joinCommandLine(QStringList arguments)
{
    for (QString& argument : arguments) {
        if (argument.isEmpty()) {
            argument = QStringLiteral("\"\"");
            continue;
        }

        if (!argument.contains(QLatin1Char(' ')) && !argument.contains(QLatin1Char('\t')) &&
            !argument.contains(QLatin1Char('"'))) {
            continue;
        }

        argument.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        argument.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        argument = QStringLiteral("\"%1\"").arg(argument);
    }
    return arguments.join(QLatin1Char(' '));
}

} // namespace labelqt::core
