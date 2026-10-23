#pragma once

#include <belief_evaluation/defender_heuristic.hpp>

namespace dds::belief_evaluation
{

/// delta rule: in third seat, following suit, as a defender, after
/// partner's lead: play the highest double-dummy-optimal card, when it
/// beats the second player's card. Defers otherwise, including when the
/// second card already beats every candidate (then `third_seat_low`
/// fires).
///
/// The second player is declarer or dummy, depending on the deal. So the
/// rule reads trick index 1 directly, not a derived seat. A ruff there
/// beats every candidate. A discard of a third suit beats none.
///
/// "Partner led" is always true in third seat. It is checked anyway, for
/// clarity.
auto high_in_third() -> DefenderHeuristic;

}  // namespace dds::belief_evaluation
