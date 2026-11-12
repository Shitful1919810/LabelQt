#include "MobileSearchController.h"
#include "services/label/LabelVisibility.h"
#include <QtConcurrentRun>
#include <algorithm>

namespace labelqt::mobile {
MobileSearchController::MobileSearchController(MobileDocument& document, QObject* parent)
    : QObject(parent), m_document(document)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(180);
    connect(&m_timer, &QTimer::timeout, this, &MobileSearchController::start);
    connect(&document, &MobileDocument::changed, this, &MobileSearchController::observeDocument);
    connect(&document.controller(), &services::ProjectController::storageChanged, this,
            &MobileSearchController::observeDocument);
    observeDocument();
}

MobileSearchController::~MobileSearchController()
{
    disconnect(&m_worker, nullptr, this, nullptr);
    m_timer.stop();
}

void MobileSearchController::configure(QString text, bool matchCase, int scope, int first, int last, bool allGroups,
                                       QStringList groups)
{
    scope = std::clamp(scope, 0, 2);
    first = scope == 2 ? first : 0;
    last = scope == 2 ? last : -1;
    const QSet<QString> selectedGroups = allGroups ? QSet<QString>{} : QSet<QString>{groups.cbegin(), groups.cend()};
    if (m_query.text == text && m_query.caseSensitive == matchCase && m_scope == scope &&
        m_query.firstPage == first && m_query.lastPage == last && m_query.allGroups == allGroups &&
        m_query.groups == selectedGroups)
        return;
    m_query.text = std::move(text);
    m_query.caseSensitive = matchCase;
    m_scope = scope;
    m_query.firstPage = first;
    m_query.lastPage = last;
    m_query.allGroups = allGroups;
    m_query.groups = selectedGroups;
    m_query.maxMatches = 1000;
    m_page = m_document.pageIndex();
    invalidate();
}

void MobileSearchController::observeDocument()
{
    auto& controller = m_document.controller();
    const auto revision = controller.revisions().revision();
    const auto session = controller.documentSession();
    const int page = m_document.pageIndex();
    if (session == m_session && revision == m_revision && (m_scope != 1 || page == m_page))
        return;
    m_session = session;
    m_revision = revision;
    m_page = page;
    invalidate();
}

void MobileSearchController::invalidate()
{
    ++m_generation;
    m_results.clear();
    m_current = -1;
    m_truncated = false;
    m_error.clear();
    m_pending = m_document.opened() && !m_query.text.isEmpty();
    m_timer.stop();
    if (m_pending)
        m_timer.start();
    emit changed();
}

void MobileSearchController::start()
{
    if (m_running || !m_pending)
        return;
    m_pending = false;
    m_running = true;
    auto query = m_query;
    if (m_scope == 1)
        query.firstPage = query.lastPage = m_document.pageIndex();
    const auto generation = m_generation;
    disconnect(&m_worker, nullptr, this, nullptr);
    connect(&m_worker, &QFutureWatcher<Result>::finished, this, [this, generation] {
        m_running = false;
        observeDocument();
        if (generation == m_generation) {
            try {
                const auto result = m_worker.result();
                m_results = result.rows;
                m_truncated = result.truncated;
                m_error = result.error;
            }
            catch (...) {
                m_error = tr("Search failed.");
            }
        }
        if (m_pending && !m_timer.isActive())
            m_timer.start();
        emit changed();
    });
    m_worker.setFuture(QtConcurrent::run([project = m_document.controller().project(), query] {
        Result result;
        const auto matches = services::ProjectTextSearchService::search(project, query);
        result.truncated = matches.truncated;
        QSet<QString> groups(project.groups().cbegin(), project.groups().cend());
        int previousPage = -1;
        QVector<int> numbers;
        for (const auto& match : matches.matches) {
            const auto& page = project.images().at(match.imageIndex);
            if (previousPage != match.imageIndex) {
                previousPage = match.imageIndex;
                numbers.fill(0, page.labels.size());
                const auto visible = services::LabelVisibility::sourceIndexes(page.labels, groups);
                for (int row = 0; row < visible.size(); ++row)
                    numbers[visible.at(row)] = row + 1;
            }
            const auto& label = page.labels.at(match.labelIndex);
            const auto from = std::max<qsizetype>(0, match.start - 40);
            const auto snippet = match.text.mid(from, 160);
            const auto offset = match.start - from;
            const auto length = std::min(match.length, snippet.size() - offset);
            const auto html = snippet.left(offset).toHtmlEscaped() + "<b><u>" +
                              snippet.mid(offset, length).toHtmlEscaped() + "</u></b>" +
                              snippet.mid(offset + length).toHtmlEscaped();
            result.rows.append(QVariantMap{{"pageKey", page.id.toString(QUuid::WithoutBraces)},
                                           {"labelKey", label.id().toString(QUuid::WithoutBraces)},
                                           {"pageName", page.name},
                                           {"number", numbers.at(match.labelIndex)},
                                           {"start", match.start},
                                           {"length", match.length},
                                           {"snippet", html},
                                           {"group", label.group()}});
        }
        return result;
    }));
    emit changed();
}

bool MobileSearchController::navigate(int row)
{
    if (!m_document.commitText())
        return false;
    observeDocument();
    if (busy() || row < 0 || row >= m_results.size())
        return false;
    const auto match = m_results.at(row).toMap();
    if (!m_document.navigateToLabel(match.value("pageKey").toString(), match.value("labelKey").toString()))
        return false;
    m_current = row;
    emit changed();
    return true;
}
} // namespace labelqt::mobile
