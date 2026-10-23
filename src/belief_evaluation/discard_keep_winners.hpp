#pragma once

#include <belief_evaluation/defender_heuristic.hpp>

namespace dds::belief_evaluation
{

/// delta rule: on a discard (not on lead, void in the led suit), as a
/// defender: discard from the suit with the fewest tricks for the
/// defending side (`suit_top_tricks`), keeping winners elsewhere. Ties go
/// to the lower rank, then to suit order. Suit order only makes the
/// choice deterministic.
auto discard_keep_winners() -> DefenderHeuristic;

}  // namespace dds::belief_evaluation
