#include "core/LabelPlusDocument.h"

#include "core/ProjectMetadataService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>
#include <QVector>

#include <stdexcept>
#include <utility>

namespace labelqt::core {

namespace {
const QRegularExpression imageLineRegex(R"(>>>>>>>>\[(.*?)\]<<<<<<<<)");
const QRegularExpression labelLineRegex(R"(----------------\[(\d+)\]----------------\[([0-9.]+),([0-9.]+),(\d+)\])");

QString readTextFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(QStringLiteral("Cannot open %1").arg(filePath).toStdString());
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    return stream.readAll();
}

void writeTextFile(const QString& filePath, const QString& content)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        throw std::runtime_error(QStringLiteral("Cannot write %1").arg(filePath).toStdString());
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << content;
}

QString normalizeLine(QString line)
{
    if (line.startsWith(QChar::ByteOrderMark)) {
        line.remove(0, 1);
    }
    return line.trimmed();
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

QHash<QString, QString> labelIdsFromCommentLines(const QStringList& commentLines)
{
    QHash<QString, QString> labelIds;
    const QJsonObject metadata = labelqt::core::ProjectMetadataService::metadataObject(commentLines);
    const QJsonArray labels = metadata.value(QStringLiteral("labelIds")).toArray();
    for (const QJsonValue& value : labels) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        const QString page = object.value(QStringLiteral("page")).toString();
        const int labelIndex = object.value(QStringLiteral("labelIndex")).toInt(-1);
        const QString id = object.value(QStringLiteral("id")).toString();
        if (!page.isEmpty() && labelIndex >= 0 && !id.isEmpty()) {
            labelIds.insert(QStringLiteral("%1#%2").arg(page, QString::number(labelIndex)), id);
        }
    }
    return labelIds;
}

void applyLabelIds(Project& project)
{
    const QHash<QString, QString> labelIds = labelIdsFromCommentLines(project.commentLines());
    for (ImageEntry& image : project.images()) {
        for (int labelIndex = 0; labelIndex < image.labels.size(); ++labelIndex) {
            Label& label = image.labels[labelIndex];
            const QString key = QStringLiteral("%1#%2").arg(image.name, QString::number(labelIndex));
            if (labelIds.contains(key)) {
                label.setStableId(labelIds.value(key));
            }
            label.ensureStableId();
        }
    }
}

QJsonArray labelIdsArray(const Project& project)
{
    QJsonArray labels;
    for (const ImageEntry& image : project.images()) {
        for (int labelIndex = 0; labelIndex < image.labels.size(); ++labelIndex) {
            const Label& label = image.labels.at(labelIndex);
            if (label.isDeleted()) {
                continue;
            }
            QJsonObject object;
            object.insert(QStringLiteral("page"), image.name);
            object.insert(QStringLiteral("labelIndex"), labelIndex);
            object.insert(QStringLiteral("id"), label.stableId());
            labels.append(object);
        }
    }
    return labels;
}

QStringList commentLinesWithMetadata(const Project& project)
{
    QJsonObject metadata = labelqt::core::ProjectMetadataService::metadataObject(project.commentLines());
    if (metadata.contains(QStringLiteral("review"))) {
        const QJsonArray labelIds = labelIdsArray(project);
        if (labelIds.isEmpty()) {
            metadata.remove(QStringLiteral("labelIds"));
        } else {
            metadata.insert(QStringLiteral("labelIds"), labelIds);
        }
    } else {
        metadata.remove(QStringLiteral("labelIds"));
    }
    return labelqt::core::ProjectMetadataService::rewriteCommentLines(project.commentLines(), metadata);
}
} // namespace

Project LabelPlusDocument::loadFromFile(const QString& filePath)
{
    return parse(readTextFile(filePath), filePath);
}

void LabelPlusDocument::saveToFile(const Project& project, const QString& filePath)
{
    writeTextFile(filePath, serialize(project));
}

Project LabelPlusDocument::parse(const QString& content, const QString& filePath)
{
    Project project;
    project.setFilePath(filePath);

    const QFileInfo txtInfo(filePath);
    const QString baseFolder = txtInfo.absolutePath();
    const QStringList lines = content.split(QRegularExpression(R"(\r\n|\r|\n)"));

    int sectionSeparatorCount = 0;
    ImageEntry* currentImage = nullptr;
    Label* currentLabel = nullptr;
    QString pendingText;

    auto commitText = [&]() {
        if (currentLabel == nullptr) {
            return;
        }
        currentLabel->setText(pendingText.trimmed());
        pendingText.clear();
        currentLabel = nullptr;
    };

    for (const QString& rawLine : lines) {
        const QString line = normalizeLine(rawLine);
        if (line.isEmpty()) {
            continue;
        }

        if (line == "-") {
            ++sectionSeparatorCount;
            continue;
        }

        if (sectionSeparatorCount == 1) {
            project.groups().append(line);
            continue;
        }

        if (sectionSeparatorCount >= 2 && line.startsWith(QStringLiteral("关联文件:"))) {
            project.setSourceName(line.mid(QStringLiteral("关联文件:").size()).trimmed());
            continue;
        }

        const QRegularExpressionMatch imageMatch = imageLineRegex.match(line);
        if (imageMatch.hasMatch()) {
            commitText();

            const QString imageName = imageMatch.captured(1).trimmed();
            ImageEntry entry;
            entry.name = imageName;
            entry.path = imagePathFor(baseFolder, imageName);
            project.images().append(std::move(entry));
            currentImage = &project.images().last();
            continue;
        }

        if (sectionSeparatorCount >= 2 && currentImage == nullptr && currentLabel == nullptr) {
            project.commentLines().append(line);
            continue;
        }

        const QRegularExpressionMatch labelMatch = labelLineRegex.match(line);
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
            if (!pendingText.isEmpty()) {
                pendingText.append('\n');
            }
            pendingText.append(line);
        }
    }

    commitText();

    if (project.groups().isEmpty()) {
        project.setGroups({QStringLiteral("框内"), QStringLiteral("框外")});
    }
    applyLabelIds(project);

    return project;
}

QString LabelPlusDocument::serialize(const Project& project)
{
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
        const QString imageName = image.name.isEmpty() ? QFileInfo(image.path).fileName() : image.name;
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
            stream << label.text() << "\n\n";
        }

        stream << '\n';
    }

    return output;
}

} // namespace labelqt::core
