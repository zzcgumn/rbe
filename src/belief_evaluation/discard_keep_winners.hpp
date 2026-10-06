#pragma once

#include <belief_evaluation/defender_heuristic.hpp>

namespace dds::belief_evaluation
{

/// delta rule: on a genuine discard (not on lead, void in the led suit),
/// as a defender -- discard from the suit with the fewest remaining tricks
/// for the defending side (`suit_top_tricks`), keeping winners in the
/// other suits for later. Ties broken by rank, then by suit -- suit is
/// the final tie-break only to give a deterministic total order, not a
/// discard policy choice in its own right.
auto discard_keep_winners() -> DefenderHeuristic;

}  // namespace dds::belief_evaluation
