#include "services/transfer/ProjectTransferService.h"

#include "core/LabelPlusDocument.h"
#include "core/ProjectIdentityService.h"
#include "core/ProjectMetadataService.h"
#include "services/project/ProjectCreationService.h"
#include "services/project/ProjectLocationService.h"
#include "services/proofread/ReviewRoundService.h"
#include "services/transfer/TransferSupport.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>

namespace labelqt::services {
namespace {
using namespace transfer;
using core::Project;
using core::ProjectMetadataService;

QString metadataError()
{
    return ProjectTransferService::tr("Transfer cannot safely preserve unknown or unsupported project metadata.");
}

void keys(const QJsonObject& object, const QStringList& allowed)
{
    for (auto it = object.begin(); it != object.end(); ++it)
        require(allowed.contains(it.key()), metadataError());
}

// Owning readers validate meaning; their encoders define the fields they actually understand.
// Check structure, not values: UUID spelling and other legitimate normalization need not be identical.
// Missing optional fields (notably a new round's empty session) are allowed. Unknown fields are not.
void knownFields(const QJsonValue& input, const QJsonValue& encoded)
{
    require(input.type() == encoded.type(), metadataError());
    if (input.isObject()) {
        const auto object = input.toObject(), supported = encoded.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            require(supported.contains(it.key()), metadataError());
            knownFields(it.value(), supported.value(it.key()));
        }
    }
    else if (input.isArray()) {
        const auto array = input.toArray(), supported = encoded.toArray();
        require(array.size() == supported.size(), metadataError());
        for (qsizetype i = 0; i < array.size(); ++i)
            knownFields(array.at(i), supported.at(i));
    }
}

bool nonnegativeInteger(const QJsonValue& value)
{
    return value.isDouble() && value.toInt(-1) >= 0 && value.toDouble() == value.toInt(-1);
}

void identities(const QJsonObject& object)
{
    // restore() deliberately skips stale page/group entries after external edits. Its encoder also
    // replaces them, so a round-trip structure check would reject supported stale identities.
    // Retain only the closed shape guard here, not a second UUID/signature/identity validator.
    keys(object, {"version", "project", "groups", "pages"});
    require(object.value("version") == QJsonValue(1) && object.value("project").isString() &&
                object.value("groups").isObject() && object.value("pages").isObject(),
            metadataError());
    const auto groups = object.value("groups").toObject();
    for (const auto& value : groups)
        require(value.isString(), metadataError());
    const auto pages = object.value("pages").toObject();
    for (const auto& value : pages) {
        require(value.isObject(), metadataError());
        const auto page = value.toObject();
        keys(page, {"id", "labels", "signature", "labelGeneration"});
        require(page.value("id").isString() && page.value("labels").isArray() && page.value("signature").isString() &&
                    (!page.contains("labelGeneration") || page.value("labelGeneration").isString()),
                metadataError());
        for (const auto& label : page.value("labels").toArray())
            require(label.isString(), metadataError());
    }
}

void session(const QJsonObject& object)
{
    const auto decoded = ReviewSessionService::decode(object);
    require(decoded.has_value(), decoded ? QString() : decoded.error());
    knownFields(object, ReviewSessionService::encode(*decoded));
}

void consumePageLabels(qsizetype count, qsizetype& remaining, const TransferLimits& limits)
{
    require(count <= limits.pageLabels && count <= remaining,
            ProjectTransferService::tr("Transfer size limit exceeded."));
    remaining -= count;
}

void checkSnapshotLabelLimits(const QJsonObject& metadata, const TransferLimits& limits)
{
    // Check raw array sizes before the owning review decoder allocates Project/Label snapshots.
    const auto round = metadata.value("proofreading").toObject();
    for (const auto& key : {"before", "after"}) {
        qsizetype remaining = limits.projectLabels;
        for (const auto& page : round.value(key).toObject().value("pages").toArray())
            consumePageLabels(page.toObject().value("labels").toArray().size(), remaining, limits);
    }
    const auto legacy = metadata.value("review").toObject().value("labels").toArray();
    require(legacy.size() <= limits.projectLabels, ProjectTransferService::tr("Transfer size limit exceeded."));
    QHash<QString, qsizetype> perPage;
    for (const auto& label : legacy)
        require(++perPage[label.toObject().value("page").toString()] <= limits.pageLabels,
                ProjectTransferService::tr("Transfer size limit exceeded."));
}

QJsonObject checkedMetadata(const Project& project, const TransferLimits& limits)
{
    for (const auto& line : project.commentLines()) {
        const auto trimmed = line.trimmed();
        require(!trimmed.startsWith("# LabelQtMergeSources") && !trimmed.startsWith("# LabelQtReview"),
                metadataError());
    }
    const auto parsed = ProjectMetadataService::tryMetadataObject(project.commentLines());
    require(parsed.has_value(), parsed ? QString() : parsed.error());
    auto metadata = *parsed;
    keys(metadata,
         {"identities", "imagePaths", "labelText", "proofreading", "mergeSources", "review", "reviewSession"});
    for (auto it = metadata.begin(); it != metadata.end(); ++it)
        require(it.key() == "mergeSources" ? it->isArray() : it->isObject(), metadataError());
    checkSnapshotLabelLimits(metadata, limits);
    if (metadata.contains("identities")) {
        identities(metadata.value("identities").toObject());
        auto candidate = ReviewRoundService::snapshot(project);
        const auto restored = core::ProjectIdentityService::restore(candidate, metadata.value("identities").toObject());
        require(restored.has_value(), restored ? QString() : restored.error());
    }
    if (metadata.contains("imagePaths")) {
        const auto section = metadata.value("imagePaths").toObject();
        keys(section, {"version", "paths"});
        require(section.value("version") == QJsonValue(1) && section.value("paths").isObject(), metadataError());
        const auto paths = section.value("paths").toObject();
        // Check ALL entries, including stale mappings not selected by LabelPlusDocument::parse.
        for (auto it = paths.begin(); it != paths.end(); ++it) {
            relativePath(it.key(), limits);
            require(it->isString(), metadataError());
            relativePath(it->toString(), limits);
        }
    }
    if (metadata.contains("labelText")) {
        // LabelPlusDocument ignores stale overrides and has no strict section validator. Keep the
        // shape/version guard so unknown data cannot disappear when it regenerates this section.
        const auto section = metadata.value("labelText").toObject();
        keys(section, {"version", "entries"});
        require(section.value("version") == QJsonValue(1) && section.value("entries").isArray(), metadataError());
        for (const auto& value : section.value("entries").toArray()) {
            require(value.isObject(), metadataError());
            const auto entry = value.toObject();
            keys(entry, {"image", "label", "bodySha256", "text"});
            require(entry.value("image").isString() && nonnegativeInteger(entry.value("label")) &&
                        entry.value("bodySha256").isString() && entry.value("text").isString(),
                    metadataError());
        }
    }
    if (metadata.contains("reviewSession"))
        session(metadata.value("reviewSession").toObject());
    if (metadata.contains("review")) {
        const auto section = metadata.value("review").toObject();
        // Validate the legacy section independently, even if a modern round takes precedence.
        auto legacy = project;
        legacy.setCommentLines(ProjectMetadataService::rewriteCommentLines({}, {{"review", section}}));
        const auto decoded = ReviewRoundService::read(legacy);
        require(decoded.has_value(), decoded ? QString() : decoded.error());
        const auto encoded = ProjectMetadataService::metadataObject(
            ReviewMetadataService::rewriteCommentLines({}, ReviewMetadataService::metadataForProject(legacy)));
        knownFields(section, encoded.value("review"));
        for (const auto& value : section.value("labels").toArray()) {
            relativePath(value.toObject().value("page").toString(), limits);
        }
    }
    // PageSourceInfoService has a tolerant display reader, not a validating decoder. These are the
    // current range fields plus its legacy single-image spelling; sourcePath is redacted on output.
    for (const auto& value : metadata.value("mergeSources").toArray()) {
        require(value.isObject(), metadataError());
        const auto source = value.toObject();
        keys(source, {"sourceIndex", "sourcePath", "pageCount", "labelCount", "firstImage", "lastImage", "image"});
        require(source.value("sourcePath").isString() && nonnegativeInteger(source.value("sourceIndex")) &&
                    nonnegativeInteger(source.value("pageCount")) && nonnegativeInteger(source.value("labelCount")),
                metadataError());
        for (const auto& key : {"firstImage", "lastImage", "image"})
            if (source.contains(key))
                relativePath(source.value(key).toString(), limits);
    }
    if (metadata.contains("proofreading")) {
        const auto review = ReviewRoundService::read(project);
        require(review.has_value(), review ? QString() : review.error());
        const auto section = metadata.value("proofreading").toObject();
        require(review->has_value(), metadataError());
        session(section.value("session").toObject());
        knownFields(section, ReviewRoundService::encode(**review, project.filePath()));
        for (const auto& key : {"before", "after"}) {
            // Inspect raw paths before the owner's decoder can clean away traversal components.
            for (const auto& pageValue : section.value(key).toObject().value("pages").toArray()) {
                const auto page = pageValue.toObject();
                relativePath(page.value("name").toString(), limits);
                if (!page.value("path").toString().isEmpty())
                    relativePath(page.value("path").toString(), limits);
            }
        }
    }
    return metadata;
}

QString portableBasename(QString path, const TransferLimits& limits)
{
    if (path.isEmpty())
        return {};
    path.replace('\\', '/');
    return relativePath(path.section('/', -1), limits);
}

QString containedReference(const QString& base, const QString& path, const TransferLimits& limits)
{
    require(!path.isEmpty(), ProjectTransferService::tr("An image has no transfer path."));
    if (QDir::isAbsolutePath(path))
        noLinks(path);
    const auto relative = QDir::isAbsolutePath(path) ? QDir(base).relativeFilePath(path) : path;
    relativePath(relative, limits);
    const auto absolute = QDir(base).absoluteFilePath(relative);
    noLinks(absolute);
    return absolute;
}

// Validate before touching any referenced source. Missing paths remain local references on import.
QJsonObject validateProject(const Project& project, const TransferLimits& limits)
{
    require(QDir::isAbsolutePath(project.filePath()),
            ProjectTransferService::tr("A local project location is required for transfer."));
    noLinks(project.filePath());
    qsizetype remainingLabels = limits.projectLabels;
    for (const auto& image : project.images())
        consumePageLabels(image.labels.size(), remainingLabels, limits);
    const auto metadata = checkedMetadata(project, limits);
    const auto base = QFileInfo(project.filePath()).absolutePath();
    Names names;
    names.add("project.txt");
    require(project.images().size() < limits.entries, ProjectTransferService::tr("Transfer entry limit exceeded."));
    for (const auto& image : project.images()) {
        const auto name = relativePath(image.name, limits);
        require(QFileInfo(name).suffix().compare("txt", Qt::CaseInsensitive) != 0,
                ProjectTransferService::tr("An image cannot use the project TXT extension."));
        names.add(name);
        containedReference(base, image.path, limits);
    }
    // Historical references must pass the same boundary even when no pixels will be copied.
    const auto round = metadata.value("proofreading").toObject();
    for (const auto& key : {"before", "after"}) {
        for (const auto& value : round.value(key).toObject().value("pages").toArray()) {
            const auto path = value.toObject().value("path").toString();
            if (!path.isEmpty())
                containedReference(base, path, limits);
        }
    }
    return metadata;
}

QStringList
materialize(const Project& source, const QString& destination, bool copyImages, bool exporting,
            const TransferLimits& limits,
            core::LabelPlusDocument::CommentPolicy comments = core::LabelPlusDocument::CommentPolicy::Preserve)
{
    auto output = source;
    if (comments == core::LabelPlusDocument::CommentPolicy::Omit) {
        output.setCommentLines({});
        output.setSourceName({});
    }
    // Discarded history is not part of this artifact. Validate and copy only its actual references.
    auto metadata = validateProject(output, limits);
    const auto base = QFileInfo(source.filePath()).absolutePath();
    const auto outputPath = QDir(destination).filePath("project.txt");
    output = ProjectLocationService::forOutput(output, outputPath);
    output.setSourceName(portableBasename(output.sourceName(), limits));
    Names names;
    names.add("project.txt");
    QStringList files;
    QHash<QString, QString> copiedPaths;
    qint64 remaining = limits.totalBytes;
    int plannedFiles = 1;
    const auto copy = [&](const QString& sourcePath, const QString& target) {
        names.add(relativePath(target, limits));
        require(QFileInfo(target).suffix().compare("txt", Qt::CaseInsensitive) != 0,
                ProjectTransferService::tr("An image cannot use the project TXT extension."));
        require(++plannedFiles <= limits.entries, ProjectTransferService::tr("Transfer entry limit exceeded."));
        if (!copyImages)
            return;
        if (!QFileInfo::exists(sourcePath)) {
            require(!exporting, ProjectTransferService::tr("A required image is missing: %1").arg(sourcePath));
            return;
        }
        copyFile(sourcePath, QDir(destination).filePath(target), limits.fileBytes, remaining);
        files.append(target);
    };
    for (auto& page : output.images()) {
        const auto path = containedReference(base, page.path, limits);
        copy(path, page.name);
        if (!copiedPaths.contains(path))
            copiedPaths.insert(path, page.name);
        page.path = QDir(destination).filePath(page.name);
    }
    auto round = metadata.value("proofreading").toObject();
    int historyIndex = 0;
    for (const auto& key : {"before", "after"}) {
        if (!round.contains(key))
            continue;
        auto snapshot = round.value(key).toObject();
        auto pages = snapshot.value("pages").toArray();
        for (qsizetype i = 0; i < pages.size(); ++i) {
            auto page = pages.at(i).toObject();
            const auto reference = page.value("path").toString();
            if (reference.isEmpty())
                continue;
            const auto path = containedReference(base, reference, limits);
            if (!copiedPaths.contains(path)) {
                const auto target =
                    QStringLiteral("_history/%1/%2").arg(++historyIndex).arg(QFileInfo(path).fileName());
                copy(path, target);
                copiedPaths.insert(path, target);
            }
            page.insert("path", copiedPaths.value(path));
            pages[i] = page;
        }
        snapshot.insert("pages", pages);
        round.insert(key, snapshot);
    }
    if (metadata.contains("proofreading"))
        metadata.insert("proofreading", round);
    auto sources = metadata.value("mergeSources").toArray();
    for (qsizetype i = 0; i < sources.size(); ++i) {
        auto item = sources.at(i).toObject();
        item.insert("sourcePath", portableBasename(item.value("sourcePath").toString(), limits));
        sources[i] = item;
    }
    if (metadata.contains("mergeSources"))
        metadata.insert("mergeSources", sources);
    metadata.remove("imagePaths"); // The serializer regenerates current paths from the relocated candidate.
    output.setCommentLines(ProjectMetadataService::rewriteCommentLines(output.commentLines(), metadata));
    const auto bytes = core::LabelPlusDocument::serialize(output, comments).toUtf8();
    require(bytes.size() <= limits.textBytes && bytes.size() <= limits.fileBytes && bytes.size() <= remaining,
            ProjectTransferService::tr("Transfer size limit exceeded."));
    writeFile(outputPath, bytes);
    files.prepend("project.txt");
    validateProject(readProject(outputPath, limits), limits);
    return files;
}

WorkspaceInfo readWorkspace(const QString& directory)
{
    auto info = readWorkspaceInfo(directory);
    try {
        // Bound optional list decoration without imposing transfer eligibility on local data.
        // In particular, editing past a label limit must not remove a saved workspace's entry.
        const TransferLimits inspection;
        require(QFileInfo(info.projectPath).size() <= inspection.textBytes,
                ProjectTransferService::tr("The project is too large to inspect in the workspace list."));
        const auto project = readProject(info.projectPath, inspection);
        const auto round = ReviewRoundService::read(project);
        require(round.has_value(), round ? QString{} : round.error());
        info.reviewActive = *round && !(**round).after;
    }
    catch (const std::exception& error) {
        info.diagnostic = QString::fromUtf8(error.what());
    }
    return info;
}

Project archiveProject(const QString& directory, const QStringList& files, const QString& title,
                       const TransferLimits& limits)
{
    QStringList projects, images;
    const auto filters = ProjectCreationService::supportedImageNameFilters();
    for (const auto& file : files) {
        // Finder resource forks are transport metadata, not images or LabelPlus documents.
        // This runs after extraction, so these entries still undergo all archive safety checks.
        const auto components = file.split('/');
        if (components.contains("__MACOSX") || components.last().startsWith("._"))
            continue;
        if (QFileInfo(file).suffix().compare("txt", Qt::CaseInsensitive) == 0)
            projects.append(file);
        else if (QDir::match(filters, QFileInfo(file).fileName()))
            images.append(file);
    }
    require(projects.size() <= 1,
            ProjectTransferService::tr(
                "The archive contains multiple TXT files. Select a single project before importing."));
    if (!projects.isEmpty())
        return readProject(QDir(directory).filePath(projects.first()), limits);
    require(!images.isEmpty(),
            ProjectTransferService::tr("The archive contains neither a project TXT nor supported images."));
    // Validate headers without decoding full images. Never silently omit a damaged page.
    for (const auto& image : images) {
        QImageReader reader(QDir(directory).filePath(image));
        require(reader.canRead() && !reader.size().isEmpty(),
                ProjectTransferService::tr("Cannot read an image in the archive: %1").arg(image));
    }
    ProjectCreationService::sortImageNames(images);
    return ProjectCreationService::fromImages(QDir(directory).filePath("project.txt"), images,
                                              ProjectCreationService::defaultGroups(), title);
}
} // namespace

qint64 transfer::estimateZipStagingBytes(const core::Project& project, const TransferLimits& limits,
                                         core::LabelPlusDocument::CommentPolicy comments)
{
    checkLimits(limits);
    auto candidate = project;
    if (comments == core::LabelPlusDocument::CommentPolicy::Omit)
        candidate.setCommentLines({});
    const auto metadata = validateProject(candidate, limits);
    const auto base = QFileInfo(project.filePath()).absolutePath();
    QSet<QString> copiedPaths;
    qint64 imageBytes = 0;
    int files = 1;
    const auto countFile = [&](const QString& path) {
        const QFileInfo info(path);
        require(info.exists(), ProjectTransferService::tr("A required image is missing: %1").arg(path));
        require(info.isFile(), ioError(path));
        require(info.size() <= limits.fileBytes && info.size() <= limits.totalBytes - imageBytes,
                ProjectTransferService::tr("Transfer size limit exceeded."));
        require(++files <= limits.entries, ProjectTransferService::tr("Transfer entry limit exceeded."));
        imageBytes += info.size();
        copiedPaths.insert(path);
    };
    // Live aliases each produce a file; history reuses the first existing source copy.
    for (const auto& image : candidate.images())
        countFile(containedReference(base, image.path, limits));
    const auto round = metadata.value("proofreading").toObject();
    for (const auto& key : {"before", "after"}) {
        for (const auto& value : round.value(key).toObject().value("pages").toArray()) {
            const auto reference = value.toObject().value("path").toString();
            if (reference.isEmpty())
                continue;
            const auto path = containedReference(base, reference, limits);
            if (!copiedPaths.contains(path))
                countFile(path);
        }
    }
    // The relocated TXT is bounded by materialize(). Include both full trees, a conservative
    // deflate/container allowance, and the archive itself at their simultaneous peak.
    const qint64 payload = std::min(limits.totalBytes, imageBytes + std::min(limits.textBytes, limits.fileBytes));
    const qint64 archive = std::min(limits.inputBytes, payload + payload / 100 + qint64(files) * 16384 + 65536);
    return 2 * payload + archive;
}

ProjectTransferService::ProjectTransferService(QString root, TransferLimits limits)
    : m_root(std::move(root)), m_limits(limits)
{
}

QVector<WorkspaceInfo> ProjectTransferService::listWorkspaces() const
{
    QVector<WorkspaceInfo> result;
    try {
        const auto directory = QDir(privateRoot(m_root, false)).filePath("workspaces");
        noLinks(directory);
        const QDir workspaces(directory);
        for (const auto& name :
             workspaces.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name)) {
            try {
                result.append(readWorkspace(workspaces.filePath(name)));
            }
            catch (const std::exception&) {
                // Invalid manifests/unsafe paths cannot define a workspace. Never delete their evidence.
            }
        }
    }
    catch (const std::exception&) {
    }
    return result;
}

std::expected<WorkspaceInfo, QString> ProjectTransferService::importFile(QString path) const
{
    return importFile(std::move(path), true);
}

std::expected<WorkspaceInfo, QString> ProjectTransferService::importFile(QString path, bool copyAdjacentImages,
                                                                         QString displayName) const
{
    try {
        checkLimits(m_limits);
        require(!path.contains("://") && !path.contains(QChar::Null) && !path.split('/').contains(".."),
                tr("Transfer accepts local files only."));
        path = QFileInfo(path).absoluteFilePath();
        noLinks(path);
        const auto root = privateRoot(m_root, true);
        const auto workspaces = QDir(root).filePath("workspaces");
        makeDirectory(workspaces);
        QTemporaryDir stage(QDir(root).filePath(".staging-import-XXXXXX"));
        require(stage.isValid(), tr("Cannot create transfer staging directory."));
        const auto input = stage.filePath("input");
        qint64 remaining = m_limits.inputBytes;
        copyFile(path, input, m_limits.inputBytes, remaining);
        QFile probe(input);
        require(probe.open(QIODevice::ReadOnly), tr("Cannot inspect transfer input."));
        const auto format = detectArchive(probe);
        probe.close();
        const auto sourceName = QFileInfo(displayName.isEmpty() ? path : displayName).fileName();
        QString title = QFileInfo(sourceName).completeBaseName();
        if (sourceName.endsWith(".tar.gz", Qt::CaseInsensitive))
            title.chop(4);
        Project project;
        if (format) {
            const auto extracted = stage.filePath("extracted");
            makeDirectory(extracted);
            const auto files = extractArchive(input, extracted, *format, m_limits);
            project = archiveProject(extracted, files, title, m_limits);
        }
        else {
            require(QFileInfo(sourceName).suffix().compare("txt", Qt::CaseInsensitive) == 0,
                    tr("Unsupported file content. Select a project TXT or a supported archive."));
            project = readProject(input, m_limits);
            // The staged bytes belong logically to the original TXT directory. Validate raw metadata
            // before relocating the loaded paths; do not serialize/reparse or rebase relative comments
            // back to the temporary directory. This preserves identities and exact decoded text.
            validateProject(project, m_limits);
            const QDir staged(QFileInfo(input).absolutePath()), source(QFileInfo(path).absolutePath());
            for (auto& image : project.images())
                image.path = source.filePath(relativePath(staged.relativeFilePath(image.path), m_limits));
            project.setFilePath(path);
        }
        const auto candidate = stage.filePath("candidate");
        const auto projectDir = QDir(candidate).filePath("project");
        makeDirectory(projectDir);
        materialize(project, projectDir, format.has_value() || copyAdjacentImages, false, m_limits);
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (title.isEmpty())
            title = "project";
        const auto archiveName = WorkspaceStorageService::suggestedArchiveName(
            sourceName.endsWith(".zip", Qt::CaseInsensitive) ? sourceName : title + ".zip");
        const auto manifest = workspaceManifest({id, title, {}, archiveName, {}});
        writeFile(QDir(candidate).filePath("workspace.json"), manifest);
        const auto published = QDir(workspaces).filePath(id);
        require(QDir().rename(candidate, published), tr("Cannot publish the imported workspace."));
        auto info = readWorkspaceInfo(published);
        const auto round = ReviewRoundService::read(project);
        info.reviewActive = round && *round && !(**round).after;
        return info;
    }
    catch (const std::exception& error) {
        return std::unexpected(QString::fromUtf8(error.what()));
    }
}

std::expected<QString, QString> ProjectTransferService::exportZip(const core::Project& project, QString fileName,
                                                                  core::LabelPlusDocument::CommentPolicy comments) const
{
    try {
        checkLimits(m_limits);
        const auto checkedName = WorkspaceStorageService::archiveFileName(std::move(fileName));
        require(checkedName.has_value(), checkedName ? QString{} : checkedName.error());
        return WorkspaceStorageService(m_root).createSharedFile(
            *checkedName,
            [&](const QString& archive) -> std::expected<void, QString> {
                const auto stagePath = QFileInfo(archive).absolutePath();
                const auto projectDir = QDir(stagePath).filePath("project");
                makeDirectory(projectDir);
                auto files = materialize(project, projectDir, true, true, m_limits, comments);
                for (auto& file : files)
                    file.prepend("project/");
                writeZip(stagePath, files, archive, m_limits);
                // Read back the complete artifact, including CRCs, before making it visible to sharing callers.
                const auto verification = QDir(stagePath).filePath("verification");
                makeDirectory(verification);
                auto verified = extractZip(archive, verification, m_limits);
                files.sort();
                verified.sort();
                require(files == verified, tr("ZIP verification failed."));
                validateProject(readProject(QDir(verification).filePath("project/project.txt"), m_limits), m_limits);
                return {};
            },
            {}, [&] { return estimateZipStagingBytes(project, m_limits, comments); });
    }
    catch (const std::exception& error) {
        return std::unexpected(QString::fromUtf8(error.what()));
    }
}

} // namespace labelqt::services
