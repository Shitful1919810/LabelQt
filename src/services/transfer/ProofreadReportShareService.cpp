#include "ProofreadReportShareService.h"
#include "WorkspaceStorageService.h"
#include "services/proofread/ProofreadReportContent.h"
#include "services/proofread/ProofreadReportService.h"
#include "services/proofread/ReviewRoundService.h"

namespace labelqt::services {
std::expected<QString, QString> ProofreadReportShareService::exportPdf(const QString& root,
                                                                       const core::Project& snapshot,
                                                                       const QString& name, const QUuid& page,
                                                                       const std::function<bool()>& canceled)
{
    try {
        if (canceled && canceled())
            return std::unexpected(WorkspaceStorageService::tr("Export canceled."));
        const auto fileName = WorkspaceStorageService::pdfFileName(name);
        if (!fileName)
            return std::unexpected(fileName.error());
        const auto round = ReviewRoundService::read(snapshot);
        if (!round)
            return std::unexpected(round.error());
        if (!*round)
            return std::unexpected(tr("No proofreading round exists."));
        std::optional<ReviewRoundPresentation> presentation;
        const auto session = ReviewSessionService::decode((**round).session);
        if (!session)
            return std::unexpected(session.error());
        const auto texts = ProofreadReportService::defaultTexts(
            tr("Original draft"), (**round).after ? tr("Completed draft") : tr("Current draft"));
        const QString filters = page.isNull() ? tr("All pages") : tr("Current page");
        ProofreadReportOptions options;
        options.includeReview = true;
        options.annotations = session->annotations;
        return WorkspaceStorageService(root).createSharedFile(
            *fileName,
            [&](const QString& path) {
                return ProofreadReportService::savePdfReport(path, presentation->changes, presentation->before,
                                                             presentation->after, texts, filters, options, canceled);
            },
            canceled,
            [&] {
                // Run comparison only after the shared cache/space gate. The estimate allows two
                // raw previews per pair plus text/layout overhead; it is not a streaming PDF quota.
                presentation = ReviewRoundService::present(**round, snapshot);
                if (!page.isNull())
                    presentation->changes.removeIf([&](const auto& change) { return change.pageId != page; });
                qint64 bytes = 16LL * 1024 * 1024;
                bytes += qint64(report::groupedChanges(presentation->changes).size()) * options.maxImageWidth *
                         options.maxImageWidth * 8;
                for (const auto& change : presentation->changes) {
                    const auto annotation = options.annotations.value(ReviewSessionService::fingerprint(change));
                    bytes += 64LL * (change.baseline.text.size() + change.current.text.size() + change.noteText.size() +
                                     annotation.note.size());
                }
                return bytes;
            });
    }
    catch (const std::exception& error) {
        return std::unexpected(QString::fromUtf8(error.what()));
    }
}
} // namespace labelqt::services
