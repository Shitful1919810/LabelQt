#pragma once

#include "core/Project.h"
#include <QCoreApplication>
#include <expected>
#include <functional>

namespace labelqt::services {
class ProofreadReportShareService final {
    Q_DECLARE_TR_FUNCTIONS(ProofreadReportShareService)
public:
    // Input is an immutable local workspace snapshot, never an external content URI.
    static std::expected<QString, QString> exportPdf(const QString& root, const core::Project& snapshot,
                                                     const QString& name, const QUuid& page = {},
                                                     const std::function<bool()>& canceled = {});
};
} // namespace labelqt::services
