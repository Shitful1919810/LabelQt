#pragma once

namespace labelqt::services {

enum class SaveChangesChoice {
    Save,
    Discard,
    Cancel,
};

enum class SaveChangesAction {
    SaveThenContinue,
    ContinueWithoutSaving,
    Cancel,
};

class SaveChangesDecision final {
public:
    static SaveChangesAction actionForChoice(SaveChangesChoice choice) noexcept;
};

} // namespace labelqt::services
