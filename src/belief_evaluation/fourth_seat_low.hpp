#pragma once

#include <belief_evaluation/defender_heuristic.hpp>

namespace dds::belief_evaluation
{

/// delta rule: in fourth seat, following suit, as a defender: play the
/// lowest double-dummy-optimal card. Every candidate is already
/// trick-optimal. The rule only breaks the tie, low first, as the other
/// seat rules do.
auto fourth_seat_low() -> DefenderHeuristic;

}  // namespace dds::belief_evaluation
