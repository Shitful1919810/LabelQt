#pragma once

#include "WorkspaceStorageService.h"
#include "core/Project.h"

#include <QCoreApplication>
#include <QString>
#include <QVector>
#include <expected>

namespace labelqt::services {

struct TransferLimits {
    // Defaults are hard ceilings; callers may lower them, not raise them.
    qint64 inputBytes{2LL * 1024 * 1024 * 1024};
    qint64 fileBytes{512LL * 1024 * 1024};
    qint64 totalBytes{8LL * 1024 * 1024 * 1024};
    qint64 textBytes{32LL * 1024 * 1024};
    int entries{10000};
    int depth{32};
    // Applied independently to the live project, each before/after snapshot, and the legacy baseline.
    // Count all stored labels (including live undo tombstones); exceeding a cap fails without truncation.
    int projectLabels{100000};
    int pageLabels{2000};
};

// Synchronous, value-only preview service. root must be an exclusively app-owned local directory.
// Resolve trusted OS directory aliases at application startup, before passing root here. This
// service deliberately rejects symlinks in all supplied paths, including root and its ancestors.
// Call on a worker with a Project value captured on its owner thread, never with the active document.
// No JNI, URI access, active-document save, source rename, recovery, or outgoing cleanup is performed.
// Source files must remain stable during transfer; observable changes are rejected, but hostile concurrent
// filesystem writers are outside this boundary. Callers must coordinate file mutations with the worker.
// Import accepts ordinary single-volume ZIP, 7z, RAR/RAR5, POSIX tar and tar.gz (no encryption).
// Content signatures, not extensions, select the decoder. ZIP remains store/deflate, UTF-8, non-ZIP64;
// its strict envelope rejects prefixes, trailing payloads and alternate-name Unicode extras.
// One TXT imports an existing project. No TXT creates a new project from supported image files,
// naturally ordered by relative path, preserving subdirectories. Multiple/invalid TXT files fail closed.
// Finder resource files are excluded from discovery, not from archive safety checks. Gzip CRC/ISIZE are
// independently checked with bounded streaming expansion; concatenated gzip members are unsupported.
// Output is always ZIP. Decoder dictionary/solid-block allocations are not bounded by output byte limits.
// Unsafe references and unknown metadata are rejected, not stripped. Missing images may be imported;
// export requires every nonempty current/historical image reference to exist. Empty historical paths stay empty.
// References must remain below the TXT's own directory (not a sibling ZIP directory). Image payloads are
// transported as bytes, not decoded/re-encoded. Name conflicts are errors; there is no automatic rename plan.
// Publication is a same-filesystem directory rename, not a power-loss durability guarantee. Interrupted
// .staging-* directories are never listed; WorkspaceStorageService expires only abandoned transfer staging,
// not project recovery evidence. Coordinate its maintenance with all transfers on this root.
// libarchive is an external BSD-licensed dependency; no SDK/library binaries are bundled here.
class ProjectTransferService final {
    Q_DECLARE_TR_FUNCTIONS(ProjectTransferService)

public:
    explicit ProjectTransferService(QString root, TransferLimits limits = {});

    // Invalid directories are skipped; an inaccessible/unsafe root yields an empty list. No writes.
    QVector<WorkspaceInfo> listWorkspaces() const;
    // The one-argument form copies referenced adjacent local images when present. Android callers
    // staging only a TXT without sibling authorization must use the false overload.
    std::expected<WorkspaceInfo, QString> importFile(QString localPath) const;
    // displayName is presentation metadata only, never used to construct filesystem paths.
    std::expected<WorkspaceInfo, QString> importFile(QString localPath, bool copyAdjacentImages,
                                                     QString displayName = {}) const;
    // Unique read-only outgoing ZIP; subsequent exports never overwrite it. Does not save the source.
    // Managed provenance paths are reduced to basenames; user text/comments are deliberately untouched.
    std::expected<QString, QString> exportZip(const core::Project& project, QString fileName = "project.zip") const;

private:
    QString m_root;
    TransferLimits m_limits;
};

} // namespace labelqt::services
