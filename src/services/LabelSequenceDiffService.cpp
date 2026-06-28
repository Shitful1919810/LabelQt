#include "services/LabelSequenceDiffService.h"

#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace labelqt::services {
namespace {

constexpr int gapPenalty = -24;
constexpr int minimumPairScore = 42;
constexpr int shortChangedTextMaxLength = 3;
constexpr int shortChangedTextMovedMinimumPairScore = 58;

QString normalizedText(QString text)
{
    return text.simplified();
}

int lcsLength(const QString& lhs, const QString& rhs)
{
    if (lhs.isEmpty() || rhs.isEmpty()) {
        return 0;
    }

    QVector<int> previous(rhs.size() + 1);
    QVector<int> current(rhs.size() + 1);
    for (int i = 1; i <= lhs.size(); ++i) {
        for (int j = 1; j <= rhs.size(); ++j) {
            if (lhs.at(i - 1) == rhs.at(j - 1)) {
                current[j] = previous.at(j - 1) + 1;
            } else {
                current[j] = std::max(previous.at(j), current.at(j - 1));
            }
        }
        std::swap(previous, current);
        std::fill(current.begin(), current.end(), 0);
    }
    return previous.last();
}

int textSimilarityScore(const QString& baselineText, const QString& currentText)
{
    const QString baseline = normalizedText(baselineText);
    const QString current = normalizedText(currentText);
    if (baseline == current) {
        return baseline.isEmpty() ? 70 : 100;
    }
    if (baseline.isEmpty() || current.isEmpty()) {
        return -20;
    }

    const int commonLength = lcsLength(baseline, current);
    const int maxLength = static_cast<int>(std::max(baseline.size(), current.size()));
    const int minLength = static_cast<int>(std::min(baseline.size(), current.size()));
    const double lengthConfidence = std::clamp(static_cast<double>(minLength) / 6.0, 0.35, 1.0);
    const int score = static_cast<int>(std::round(100.0 * static_cast<double>(commonLength) /
                                                  static_cast<double>(maxLength) * lengthConfidence));
    return score;
}

bool isShortChangedTextPair(const ReviewLabelSnapshot& baseline, const ReviewLabelSnapshot& current)
{
    const QString baselineText = normalizedText(baseline.text);
    const QString currentText = normalizedText(current.text);
    if (baselineText == currentText) {
        return false;
    }
    return std::max(baselineText.size(), currentText.size()) <= shortChangedTextMaxLength;
}

int minimumScoreForPair(const ReviewLabelSnapshot& baseline, const ReviewLabelSnapshot& current)
{
    if (baseline.labelIndex == current.labelIndex) {
        return minimumPairScore;
    }
    return isShortChangedTextPair(baseline, current) ? shortChangedTextMovedMinimumPairScore : minimumPairScore;
}

int positionTieBreakScore(const ReviewLabelSnapshot& baseline, const ReviewLabelSnapshot& current)
{
    const double dx = baseline.position.x() - current.position.x();
    const double dy = baseline.position.y() - current.position.y();
    const double distance = std::hypot(dx, dy);
    if (distance <= 0.03) {
        return 4;
    }
    if (distance <= 0.10) {
        return 2;
    }
    return 0;
}

int pairScore(const ReviewLabelSnapshot& baseline, const ReviewLabelSnapshot& current)
{
    int score = textSimilarityScore(baseline.text, current.text);
    if (baseline.group == current.group) {
        score += 3;
    }
    score += positionTieBreakScore(baseline, current);
    if (baseline.labelIndex == current.labelIndex) {
        score += 8;
    } else {
        score -= std::min(8, std::abs(baseline.labelIndex - current.labelIndex));
    }
    return score;
}

int orderedPairScore(const ReviewLabelSnapshot& baseline, const ReviewLabelSnapshot& current)
{
    if (normalizedText(baseline.text) != normalizedText(current.text)) {
        return -1000;
    }
    return pairScore(baseline, current);
}

void sortByLabelIndex(QVector<ReviewLabelSnapshot>& labels)
{
    std::sort(labels.begin(), labels.end(), [](const ReviewLabelSnapshot& lhs, const ReviewLabelSnapshot& rhs) {
        return lhs.labelIndex < rhs.labelIndex;
    });
}

QVector<LabelSequenceDiffEntry> orderedAlignment(const QVector<ReviewLabelSnapshot>& baselineLabels,
                                                 const QVector<ReviewLabelSnapshot>& currentLabels,
                                                 QSet<int>& matchedBaselineIndexes,
                                                 QSet<int>& matchedCurrentIndexes)
{
    const int baselineCount = static_cast<int>(baselineLabels.size());
    const int currentCount = static_cast<int>(currentLabels.size());
    QVector<QVector<int>> scores(baselineCount + 1, QVector<int>(currentCount + 1));
    for (int i = 1; i <= baselineCount; ++i) {
        scores[i][0] = scores.at(i - 1).at(0) + gapPenalty;
    }
    for (int j = 1; j <= currentCount; ++j) {
        scores[0][j] = scores.at(0).at(j - 1) + gapPenalty;
    }

    for (int i = 1; i <= baselineCount; ++i) {
        for (int j = 1; j <= currentCount; ++j) {
            const int diagonal =
                scores.at(i - 1).at(j - 1) + orderedPairScore(baselineLabels.at(i - 1), currentLabels.at(j - 1));
            const int deleted = scores.at(i - 1).at(j) + gapPenalty;
            const int added = scores.at(i).at(j - 1) + gapPenalty;
            scores[i][j] = std::max({diagonal, deleted, added});
        }
    }

    QVector<LabelSequenceDiffEntry> entries;
    int i = baselineCount;
    int j = currentCount;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0) {
            const int score = orderedPairScore(baselineLabels.at(i - 1), currentLabels.at(j - 1));
            if (score >= minimumScoreForPair(baselineLabels.at(i - 1), currentLabels.at(j - 1)) &&
                scores.at(i).at(j) == scores.at(i - 1).at(j - 1) + score) {
                const ReviewLabelSnapshot& baseline = baselineLabels.at(i - 1);
                const ReviewLabelSnapshot& current = currentLabels.at(j - 1);
                entries.append({baseline.labelIndex, current.labelIndex, baseline, current, false});
                matchedBaselineIndexes.insert(i - 1);
                matchedCurrentIndexes.insert(j - 1);
                --i;
                --j;
                continue;
            }
        }
        if (i > 0 && (j == 0 || scores.at(i).at(j) == scores.at(i - 1).at(j) + gapPenalty)) {
            --i;
            continue;
        }
        if (j > 0) {
            --j;
        }
    }

    std::reverse(entries.begin(), entries.end());
    return entries;
}

void pairMovedLabels(QVector<LabelSequenceDiffEntry>& entries, const QVector<ReviewLabelSnapshot>& baselineLabels,
                     const QVector<ReviewLabelSnapshot>& currentLabels, QSet<int>& matchedBaselineIndexes,
                     QSet<int>& matchedCurrentIndexes)
{
    while (true) {
        int bestBaselineIndex = -1;
        int bestCurrentIndex = -1;
        int bestScore = std::numeric_limits<int>::min();
        for (int baselineIndex = 0; baselineIndex < baselineLabels.size(); ++baselineIndex) {
            if (matchedBaselineIndexes.contains(baselineIndex)) {
                continue;
            }
            for (int currentIndex = 0; currentIndex < currentLabels.size(); ++currentIndex) {
                if (matchedCurrentIndexes.contains(currentIndex)) {
                    continue;
                }
                const int score = pairScore(baselineLabels.at(baselineIndex), currentLabels.at(currentIndex));
                if (score >= minimumScoreForPair(baselineLabels.at(baselineIndex), currentLabels.at(currentIndex)) &&
                    score > bestScore) {
                    bestScore = score;
                    bestBaselineIndex = baselineIndex;
                    bestCurrentIndex = currentIndex;
                }
            }
        }

        if (bestBaselineIndex < 0 || bestCurrentIndex < 0) {
            return;
        }

        const ReviewLabelSnapshot& baseline = baselineLabels.at(bestBaselineIndex);
        const ReviewLabelSnapshot& current = currentLabels.at(bestCurrentIndex);
        entries.append({baseline.labelIndex, current.labelIndex, baseline, current,
                        baseline.labelIndex != current.labelIndex});
        matchedBaselineIndexes.insert(bestBaselineIndex);
        matchedCurrentIndexes.insert(bestCurrentIndex);
    }
}

void appendUnmatchedLabels(QVector<LabelSequenceDiffEntry>& entries, const QVector<ReviewLabelSnapshot>& baselineLabels,
                           const QVector<ReviewLabelSnapshot>& currentLabels, const QSet<int>& matchedBaselineIndexes,
                           const QSet<int>& matchedCurrentIndexes)
{
    for (int baselineIndex = 0; baselineIndex < baselineLabels.size(); ++baselineIndex) {
        if (!matchedBaselineIndexes.contains(baselineIndex)) {
            const ReviewLabelSnapshot& baseline = baselineLabels.at(baselineIndex);
            entries.append({baseline.labelIndex, -1, baseline, {}, false});
        }
    }
    for (int currentIndex = 0; currentIndex < currentLabels.size(); ++currentIndex) {
        if (!matchedCurrentIndexes.contains(currentIndex)) {
            const ReviewLabelSnapshot& current = currentLabels.at(currentIndex);
            entries.append({-1, current.labelIndex, {}, current, false});
        }
    }
}

} // namespace

QVector<LabelSequenceDiffEntry> LabelSequenceDiffService::diff(QVector<ReviewLabelSnapshot> baselineLabels,
                                                               QVector<ReviewLabelSnapshot> currentLabels)
{
    sortByLabelIndex(baselineLabels);
    sortByLabelIndex(currentLabels);

    QSet<int> matchedBaselineIndexes;
    QSet<int> matchedCurrentIndexes;
    QVector<LabelSequenceDiffEntry> entries =
        orderedAlignment(baselineLabels, currentLabels, matchedBaselineIndexes, matchedCurrentIndexes);
    pairMovedLabels(entries, baselineLabels, currentLabels, matchedBaselineIndexes, matchedCurrentIndexes);
    appendUnmatchedLabels(entries, baselineLabels, currentLabels, matchedBaselineIndexes, matchedCurrentIndexes);

    std::sort(entries.begin(), entries.end(), [](const LabelSequenceDiffEntry& lhs, const LabelSequenceDiffEntry& rhs) {
        const int lhsIndex = lhs.currentLabelIndex >= 0 ? lhs.currentLabelIndex : lhs.baselineLabelIndex;
        const int rhsIndex = rhs.currentLabelIndex >= 0 ? rhs.currentLabelIndex : rhs.baselineLabelIndex;
        return lhsIndex < rhsIndex;
    });
    return entries;
}

} // namespace labelqt::services
