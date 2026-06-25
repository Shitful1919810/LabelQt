#pragma once

#include "core/Project.h"

#include <QString>

namespace labelqt::core {

class LabelPlusDocument {
public:
    static Project loadFromFile(const QString& filePath);
    static void saveToFile(const Project& project, const QString& filePath);

private:
    static Project parse(const QString& content, const QString& filePath);
    static QString serialize(const Project& project);
};

} // namespace labelqt::core
