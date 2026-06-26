#include "services/SaveChangesDecision.h"

namespace labelqt::services {

SaveChangesAction SaveChangesDecision::actionForChoice(SaveChangesChoice choice) noexcept
{
    switch (choice) {
    case SaveChangesChoice::Save:
        return SaveChangesAction::SaveThenContinue;
    case SaveChangesChoice::Discard:
        return SaveChangesAction::ContinueWithoutSaving;
    case SaveChangesChoice::Cancel:
        return SaveChangesAction::Cancel;
    }
    return SaveChangesAction::Cancel;
}

} // namespace labelqt::services
