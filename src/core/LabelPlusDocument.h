#pragma once

#include "core/Project.h"

#include <QByteArray>
#include <QString>

namespace labelqt::core {

struct LoadedProjectDocument {
    Project project;
    QByteArray sha256;
};

class LabelPlusDocument {
public:
    static Project loadFromFile(const QString& filePath);
    static LoadedProjectDocument readFromFile(const QString& filePath);
    static void saveToFile(const Project& project, const QString& filePath);

    static Project parse(const QString& content, const QString& filePath);
    static QString serialize(const Project& project);
};

} // namespace labelqt::core
