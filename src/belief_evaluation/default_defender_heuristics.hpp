#pragma once

#include <belief_evaluation/defender_heuristic_chain.hpp>

namespace dds::belief_evaluation
{

/// A convenience. Equivalent to calling `add()` for, in order:
/// `second_seat_low(randomize_touching_honors)`, `high_in_third()`,
/// `third_seat_low()`, `fourth_seat_low()`, `ruff_small()` (only when
/// `trump != DDS_NOTRUMP`), and `discard_keep_winners()`. For another
/// order, a subset or your own rules, build a `DefenderHeuristicChain`
/// directly.
auto make_default_defender_heuristics(int trump, bool randomize_touching_honors = true) -> DefenderHeuristicChain;

}  // namespace dds::belief_evaluation
