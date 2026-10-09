#pragma once

#include <belief_evaluation/defender_heuristic.hpp>

namespace dds::belief_evaluation
{

/// delta rule: void in the led suit, in a trump contract, as a defender --
/// ruff with the smallest double-dummy-optimal trump, unless some
/// declaring-side seat that has not yet played to this trick is itself
/// void in the led suit and holds a higher trump (an overruff), in which
/// case this rule defers. A defending-side seat's own overruff potential
/// is not this rule's concern -- matching this technique's traditional
/// scope.
auto ruff_small() -> DefenderHeuristic;

}  // namespace dds::belief_evaluation
