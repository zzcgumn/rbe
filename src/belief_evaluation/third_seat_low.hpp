#pragma once

#include <belief_evaluation/defender_heuristic.hpp>

namespace dds::belief_evaluation
{

/// delta rule: in third seat, following suit, as a defender, after
/// partner's lead: play the lowest double-dummy-optimal card, exactly when
/// `high_in_third` defers because the second card beats every candidate.
/// The two rules share one gate. See `high_in_third`.
auto third_seat_low() -> DefenderHeuristic;

}  // namespace dds::belief_evaluation
