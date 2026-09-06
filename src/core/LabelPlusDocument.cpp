#include "core/LabelPlusDocument.h"

#include "core/AtomicFileWriter.h"
#include "core/ProjectIdentityService.h"
#include "core/ProjectMetadataService.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>
#include <QVector>

#include <stdexcept>
#include <utility>

namespace labelqt::core {

namespace {
const QRegularExpression imageLineRegex(R"(^>>>>>>>>\[(.*?)\]<<<<<<<<$)");
const QRegularExpression labelLineRegex(R"(^----------------\[(\d+)\]----------------\[([0-9.]+),([0-9.]+),(\d+)\]$)");

[[noreturn]] void invalidDocument()
{
    throw std::runtime_error(
        QCoreApplication::translate("LabelPlusDocument", "Invalid LabelPlus document structure.").toStdString());
}

QString normalizedText(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text;
}

QString bodyText(QString text)
{
    text = normalizedText(std::move(text));
    while (text.endsWith(QLatin1Char('\n'))) {
        text.chop(1);
    }
    return text;
}

QString textDigest(const QString& text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString imageNameFor(const ImageEntry& image)
{
    return image.name.isEmpty() ? QFileInfo(image.path).fileName() : image.name;
}

QJsonObject checkedMetadata(const Project& project)
{
    const auto result = ProjectMetadataService::tryMetadataObject(project.commentLines());
    if (!result)
        throw std::runtime_error(result.error().toStdString());
    return *result;
}

QByteArray readFileBytes(const QString& filePath)
{
    const QFileInfo info(filePath);
    if ((info.exists() && !info.isFile()) || (info.isSymLink() && !info.exists()))
        throw std::runtime_error(
            QCoreApplication::translate("AtomicFileWriter", "Not a regular file: %1").arg(filePath).toStdString());
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(QStringLiteral("Cannot open %1").arg(filePath).toStdString());
    }

    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        throw std::runtime_error(file.errorString().toStdString());
    }
    return bytes;
}

void writeTextFile(const QString& filePath, const QString& content)
{
    const auto result = AtomicFileWriter::write(filePath, content.toUtf8());
    if (!result) {
        throw std::runtime_error(result.error().toStdString());
    }
}

QString imagePathFor(const QString& baseFolder, const QString& imageName)
{
    return QDir(baseFolder).filePath(imageName);
}

int groupIndex(const QStringList& groups, const QString& group)
{
    const int index = static_cast<int>(groups.indexOf(group));
    return index >= 0 ? index + 1 : 1;
}

QStringList commentLinesWithMetadata(const Project& project)
{
    QJsonObject metadata = checkedMetadata(project);
    metadata.remove(QStringLiteral("labelIds"));
    metadata.insert(QStringLiteral("identities"), ProjectIdentityService::encode(project));
    QJsonObject imagePaths;
    const QDir directory(QFileInfo(project.filePath()).absolutePath());
    for (const ImageEntry& image : project.images()) {
        if (!image.path.isEmpty()) {
            const QString relative = directory.relativeFilePath(QFileInfo(image.path).absoluteFilePath());
            if (relative != imageNameFor(image))
                imagePaths.insert(imageNameFor(image), relative);
        }
    }
    metadata.remove(QStringLiteral("imagePaths"));
    if (!imagePaths.isEmpty())
        metadata.insert(QStringLiteral("imagePaths"),
                        QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("paths"), imagePaths}});
    QJsonArray textOverrides;
    for (const ImageEntry& image : project.images()) {
        int visibleIndex = 0;
        for (const Label& label : image.labels) {
            if (label.isDeleted())
                continue;
            const QString body = bodyText(label.text());
            if (body != label.text()) {
                textOverrides.append(QJsonObject{{QStringLiteral("image"), imageNameFor(image)},
                                                 {QStringLiteral("label"), visibleIndex},
                                                 {QStringLiteral("bodySha256"), textDigest(body)},
                                                 {QStringLiteral("text"), label.text()}});
            }
            ++visibleIndex;
        }
    }
    metadata.remove(QStringLiteral("labelText"));
    if (!textOverrides.isEmpty()) {
        metadata.insert(QStringLiteral("labelText"),
                        QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("entries"), textOverrides}});
    }
    return labelqt::core::ProjectMetadataService::rewriteCommentLines(project.commentLines(), metadata);
}

void restoreTextMetadata(Project& project)
{
    const QJsonObject section = checkedMetadata(project).value(QStringLiteral("labelText")).toObject();
    if (section.value(QStringLiteral("version")).toInt() != 1)
        return;
    for (const QJsonValue& value : section.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject item = value.toObject();
        ImageEntry* image = project.imageByName(item.value(QStringLiteral("image")).toString());
        const int labelIndex = item.value(QStringLiteral("label")).toInt(-1);
        if (image == nullptr || labelIndex < 0 || labelIndex >= image->labels.size() ||
            !item.value(QStringLiteral("text")).isString())
            continue;
        Label& label = image->labels[labelIndex];
        const QString original = item.value(QStringLiteral("text")).toString();
        if (textDigest(label.text()) == item.value(QStringLiteral("bodySha256")).toString() &&
            bodyText(original) == label.text()) {
            label.setText(original);
        }
    }
}

void validateSerializable(const Project& project)
{
    if (project.sourceName().contains(QLatin1Char('\n')) || project.sourceName().contains(QLatin1Char('\r')))
        invalidDocument();
    for (const QString& group : project.groups()) {
        if (group.trimmed().isEmpty() || group != group.trimmed() || group == QStringLiteral("-") ||
            group.contains(QLatin1Char('\n')) || group.contains(QLatin1Char('\r')))
            invalidDocument();
    }
    for (const ImageEntry& image : project.images()) {
        if (image.name.contains(QLatin1Char('\n')) || image.name.contains(QLatin1Char('\r')))
            invalidDocument();
        for (const Label& label : image.labels) {
            if (label.isDeleted())
                continue;
            for (const QString& line : normalizedText(label.text()).split(QLatin1Char('\n'))) {
                if (imageLineRegex.match(line.trimmed()).hasMatch() ||
                    labelLineRegex.match(line.trimmed()).hasMatch()) {
                    throw std::runtime_error(
                        QCoreApplication::translate("LabelPlusDocument",
                                                    "A label contains a reserved header line. Saving was canceled.")
                            .toStdString());
                }
            }
        }
    }
}
} // namespace

Project LabelPlusDocument::loadFromFile(const QString& filePath)
{
    return readFromFile(filePath).project;
}

LoadedProjectDocument LabelPlusDocument::readFromFile(const QString& filePath)
{
    const auto bytes = readFileBytes(filePath);
    QTextStream stream(bytes);
    stream.setEncoding(QStringConverter::Utf8);
    const auto text = stream.readAll();
    if (stream.status() != QTextStream::Ok)
        invalidDocument();
    // Hash the same raw bytes we parsed, including BOM and original line endings.
    return {parse(text, filePath), QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)};
}

void LabelPlusDocument::saveToFile(const Project& project, const QString& filePath)
{
    Project output = project;
    output.setFilePath(filePath);
    writeTextFile(filePath, serialize(output));
}

Project LabelPlusDocument::parse(const QString& content, const QString& filePath)
{
    Project project;
    project.setFilePath(filePath);

    const QFileInfo txtInfo(filePath);
    const QString baseFolder = txtInfo.absolutePath();
    QString normalized = normalizedText(content);
    if (normalized.startsWith(QChar::ByteOrderMark))
        normalized.remove(0, 1);
    const QStringList lines = normalized.split(QLatin1Char('\n'));

    int sectionSeparatorCount = 0;
    ImageEntry* currentImage = nullptr;
    Label* currentLabel = nullptr;
    QStringList pendingLines;
    bool hasVersion = false;

    auto commitText = [&]() {
        if (currentLabel == nullptr) {
            return;
        }
        while (!pendingLines.isEmpty() && pendingLines.last().isEmpty())
            pendingLines.removeLast();
        currentLabel->setText(pendingLines.join(QLatin1Char('\n')));
        pendingLines.clear();
        currentLabel = nullptr;
    };

    for (const QString& rawLine : lines) {
        const QString line = sectionSeparatorCount < 2 ? rawLine.trimmed() : rawLine;
        if (!hasVersion) {
            if (line.isEmpty())
                continue;
            if (line != QStringLiteral("1,0"))
                invalidDocument();
            hasVersion = true;
            continue;
        }
        if (sectionSeparatorCount < 2 && line == "-") {
            ++sectionSeparatorCount;
            continue;
        }

        if (sectionSeparatorCount == 0) {
            if (!line.isEmpty())
                invalidDocument();
            continue;
        }

        if (sectionSeparatorCount == 1) {
            if (!line.isEmpty()) {
                auto groups = project.groups();
                groups.append(line);
                project.setGroups(std::move(groups));
            }
            continue;
        }

        const QString structureLine = line.trimmed();
        if (currentImage == nullptr && structureLine.startsWith(QStringLiteral("关联文件:"))) {
            project.setSourceName(structureLine.mid(QStringLiteral("关联文件:").size()).trimmed());
            continue;
        }

        const QRegularExpressionMatch imageMatch = imageLineRegex.match(structureLine);
        if (imageMatch.hasMatch()) {
            commitText();

            const QString imageName = imageMatch.captured(1);
            ImageEntry entry;
            entry.name = imageName;
            entry.path = imagePathFor(baseFolder, imageName);
            project.images().append(std::move(entry));
            currentImage = &project.images().last();
            continue;
        }

        if (sectionSeparatorCount >= 2 && currentImage == nullptr && currentLabel == nullptr) {
            if (!line.isEmpty())
                project.commentLines().append(line);
            continue;
        }

        const QRegularExpressionMatch labelMatch = labelLineRegex.match(structureLine);
        if (labelMatch.hasMatch() && currentImage != nullptr) {
            commitText();

            const double x = labelMatch.captured(2).toDouble();
            const double y = labelMatch.captured(3).toDouble();
            const int rawGroupIndex = labelMatch.captured(4).toInt();
            const QString group = rawGroupIndex > 0 && rawGroupIndex <= project.groups().size()
                                      ? project.groups().at(rawGroupIndex - 1)
                                      : QStringLiteral("框内");

            currentImage->labels.append(Label(QString(), group, QPointF(x, y)));
            currentLabel = &currentImage->labels.last();
            continue;
        }

        if (currentLabel != nullptr && sectionSeparatorCount >= 2) {
            pendingLines.append(line);
        }
    }

    commitText();
    if (!hasVersion || sectionSeparatorCount != 2)
        invalidDocument();
    restoreTextMetadata(project);
    const QJsonObject paths = checkedMetadata(project).value(QStringLiteral("imagePaths")).toObject();
    if (!paths.isEmpty()) {
        if (paths.value(QStringLiteral("version")).toInt() != 1 || !paths.value(QStringLiteral("paths")).isObject())
            invalidDocument();
        const QJsonObject entries = paths.value(QStringLiteral("paths")).toObject();
        for (ImageEntry& image : project.images()) {
            if (entries.contains(image.name)) {
                const auto value = entries.value(image.name);
                if (!value.isString() || value.toString().isEmpty())
                    invalidDocument();
                image.path = QDir::cleanPath(QDir(baseFolder).absoluteFilePath(value.toString()));
            }
        }
    }

    if (project.groups().isEmpty()) {
        project.setGroups({QStringLiteral("框内"), QStringLiteral("框外")});
    }

    const auto identitySection = checkedMetadata(project).value("identities");
    if (!identitySection.isUndefined() && !identitySection.isObject())
        invalidDocument();
    const auto identities = ProjectIdentityService::restore(project, identitySection.toObject());
    if (!identities)
        throw std::runtime_error(identities.error().toStdString());
    return project;
}

QString LabelPlusDocument::serialize(const Project& project)
{
    validateSerializable(project);
    if (!ProjectIdentityService::valid(project))
        invalidDocument();
    QStringList groups = project.groups();
    if (groups.isEmpty()) {
        groups = {QStringLiteral("框内"), QStringLiteral("框外")};
    }

    QString output;
    QTextStream stream(&output);
    stream.setEncoding(QStringConverter::Utf8);

    stream << "1,0\n";
    stream << "-\n";
    for (const QString& group : groups) {
        stream << group << '\n';
    }
    stream << "-\n";
    if (!project.sourceName().isEmpty()) {
        stream << QStringLiteral("关联文件:") << project.sourceName() << "\n";
    }
    for (const QString& commentLine : commentLinesWithMetadata(project)) {
        stream << commentLine << '\n';
    }
    stream << '\n';

    for (const ImageEntry& image : project.images()) {
        const QString imageName = imageNameFor(image);
        stream << ">>>>>>>>[" << imageName << "]<<<<<<<<\n";

        int visibleIndex = 1;
        for (const Label& label : image.labels) {
            if (label.isDeleted()) {
                continue;
            }

            const QPointF position = label.position();
            stream << "----------------[" << visibleIndex++ << "]----------------["
                   << QString::number(position.x(), 'f', 3) << ',' << QString::number(position.y(), 'f', 3) << ','
                   << groupIndex(groups, label.group()) << "]\n";
            stream << bodyText(label.text()) << "\n\n";
        }

        stream << '\n';
    }

    return output;
}

} // namespace labelqt::core
