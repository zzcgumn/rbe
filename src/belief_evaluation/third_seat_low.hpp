#pragma once

#include <belief_evaluation/defender_heuristic.hpp>

namespace dds::belief_evaluation
{

/// delta rule: in third seat, following suit, as a defender, after
/// partner's lead -- play the lowest double-dummy-optimal card, exactly
/// when `high_in_third` defers (the second player's card already beats
/// every candidate, so there is nothing to gain by rising). The two
/// rules partition the same gate; see `high_in_third`'s own doxygen for
/// the shared conditions, including why "the second player" and not
/// "dummy" is the right way to describe trick index 1 here.
auto third_seat_low() -> DefenderHeuristic;

}  // namespace dds::belief_evaluation
