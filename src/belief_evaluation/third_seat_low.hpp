#pragma once

#include <belief_evaluation/defender_heuristic.hpp>

namespace dds::belief_evaluation
{

/// delta rule: in third seat, following suit, as a defender, after
/// partner's lead -- play the lowest double-dummy-optimal card, exactly
/// when `high_in_third` defers (dummy's played card already beats every
/// candidate, so there is nothing to gain by rising). The two rules
/// partition the same gate; see `high_in_third`'s own doxygen for the
/// shared conditions.
auto third_seat_low() -> DefenderHeuristic;

}  // namespace dds::belief_evaluation
