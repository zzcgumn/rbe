#pragma once

#include <belief_evaluation/defender_heuristic_chain.hpp>

namespace dds::belief_evaluation
{

/// Convenience only -- not the only way to build a chain. Equivalent to
/// calling `add()` for `second_seat_low(randomise_touching_honours)`,
/// `high_in_third()`, `third_seat_low()`, `fourth_seat_low()`,
/// `ruff_small()` (only for a trump denomination -- `trump != DDS_NOTRUMP`),
/// and `discard_keep_winners()`, in that order. A caller wanting a
/// different order, a subset, or their own rule spliced in builds a
/// `DefenderHeuristicChain` directly instead of calling this.
auto make_default_defender_heuristics(int trump, bool randomise_touching_honours = true) -> DefenderHeuristicChain;

}  // namespace dds::belief_evaluation
