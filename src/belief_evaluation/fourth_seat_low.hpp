#pragma once

#include <belief_evaluation/defender_heuristic.hpp>

namespace dds::belief_evaluation
{

/// delta rule: in fourth seat, following suit, as a defender -- play the
/// lowest double-dummy-optimal card. Every candidate is already
/// trick-optimal by construction (`best_cards` comes from the solve);
/// this rule exists only to break the tie among them the same low-first
/// way `second_seat_low`/`third_seat_low` do, rather than leaving it to
/// the chain's fallback spread.
auto fourth_seat_low() -> DefenderHeuristic;

}  // namespace dds::belief_evaluation
