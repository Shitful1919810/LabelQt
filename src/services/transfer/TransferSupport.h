#pragma once

#include "services/transfer/ProjectTransferService.h"

#include <QFile>
#include <QHash>
#include <QJsonObject>
#include <optional>
#include <stdexcept>

namespace labelqt::services::transfer {

WorkspaceInfo readWorkspaceInfo(const QString& directory);
QByteArray workspaceManifest(const WorkspaceInfo& info);
QString privateRoot(const QString& root, bool create);
qint64 shareCacheSize(const QString& root);
qint64 estimateZipStagingBytes(const core::Project& project, const TransferLimits& limits,
                              core::LabelPlusDocument::CommentPolicy comments =
                                  core::LabelPlusDocument::CommentPolicy::Preserve);

// Internal helpers throw only across the service's expected-returning boundary.
QString ioError(const QString& path);
[[noreturn]] void fail(const QString& message);
void require(bool condition, const QString& message);
void checkLimits(const TransferLimits& limits);
QString relativePath(const QString& path, const TransferLimits& limits);
void noLinks(const QString& absolutePath);
void makeDirectory(const QString& path);
QByteArray readBounded(const QString& path, qint64 limit);
QByteArray copyFile(const QString& from, const QString& to, qint64 limit, qint64& remaining);
void writeFile(const QString& path, const QByteArray& bytes);
core::Project readProject(const QString& path, const TransferLimits& limits);

class Names final {
public:
    void add(const QString& path, bool directory = false);

private:
    struct Name {
        QString spelling;
        bool directory;
        bool explicitEntry;
    };
    QHash<QString, Name> m_names;
};

// Archive bytes are decoded by libarchive. ZIP envelope preflight additionally checks length-delimited
// raw names (including NUL) and restricts methods/volumes before handing entries to its C-string API.
QStringList extractZip(const QString& input, const QString& destination, const TransferLimits& limits);
enum class ArchiveFormat { Zip, SevenZip, Rar, Rar5, Tar, TarGzip };
std::optional<ArchiveFormat> detectArchive(QFile& input);
QStringList extractArchive(const QString& input, const QString& destination, ArchiveFormat format,
                           const TransferLimits& limits);
void writeZip(const QString& directory, const QStringList& files, const QString& output, const TransferLimits& limits);

} // namespace labelqt::services::transfer
