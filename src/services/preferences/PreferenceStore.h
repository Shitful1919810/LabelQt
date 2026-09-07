#pragma once

#include "services/io/BoundedJsonIo.h"
#include "services/io/FileRevision.h"

#include <QJsonDocument>

#include <expected>
#include <optional>

namespace labelqt::services {

// One instance per settings draft. Only successful loads/commits advance its disk baseline.
class PreferenceStore final {
public:
    static constexpr BoundedJsonIo::Limits StorageLimits{4 * 1024 * 1024, 10000, 32};
    enum class ErrorCode { MissingBaseline, ReadFailed, InvalidDocument, Locked, ExternalChanges, WriteFailed };
    struct Error {
        ErrorCode code;
        QString detail;
    };
    struct Snapshot {
        QJsonDocument document;
        bool exists{false};
    };

    explicit PreferenceStore(QString path);
    const QString& path() const;
    QString lockPath() const;
    bool hasBaseline() const;
    std::expected<Snapshot, Error> load();
    std::expected<bool, Error> compareExternalChanges() const;
    std::expected<void, Error> save(const QJsonDocument& document);

private:
    QString m_path;
    std::optional<FileRevision> m_baseline;
};

} // namespace labelqt::services
