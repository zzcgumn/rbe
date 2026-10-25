#pragma once

#include <belief_evaluation/defender_heuristic.hpp>

namespace dds::belief_evaluation
{

/// delta rule: void in the led suit, in a trump contract, as a defender:
/// ruff with the smallest double-dummy-optimal trump. Defers when a
/// declaring-side seat still to play is void in the led suit and holds a
/// higher trump (an overruff). Partner's overruff is not considered.
auto ruff_small() -> DefenderHeuristic;

}  // namespace dds::belief_evaluation
