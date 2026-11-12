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
    enum class CommentPolicy { Preserve, Omit };
    static bool isValidGroupName(const QString& group);
    static Project loadFromFile(const QString& filePath);
    static LoadedProjectDocument readFromFile(const QString& filePath);
    // Decode and hash the same byte snapshot, without reopening its source.
    static LoadedProjectDocument readFromBytes(const QByteArray& bytes, const QString& filePath);
    static void saveToFile(const Project& project, const QString& filePath);

    static Project parse(const QString& content, const QString& filePath);
    // Omit writes an empty comment section, including sourceName and generated LabelQt metadata.
    // Normal persistence must preserve comments; Omit is for explicit classic-body exports only.
    static QString serialize(const Project& project, CommentPolicy comments = CommentPolicy::Preserve);
};

} // namespace labelqt::core
