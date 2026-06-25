#pragma once

#include "core/Project.h"

#include <QStringList>

namespace labelqt::services {

struct LabelNavigationState {
    int imageIndex{-1};
    int labelIndex{-1};
    QStringList visibleGroups;
};

struct LabelNavigationTarget {
    int imageIndex{-1};
    int labelIndex{-1};

    bool isValid() const noexcept;
};

class LabelNavigator final {
public:
    static LabelNavigationTarget nextVisibleLabel(const labelqt::core::Project& project,
                                                  const LabelNavigationState& state);
    static LabelNavigationTarget previousVisibleLabel(const labelqt::core::Project& project,
                                                      const LabelNavigationState& state);
};

} // namespace labelqt::services
