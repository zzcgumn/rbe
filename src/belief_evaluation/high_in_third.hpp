#pragma once

#include <belief_evaluation/defender_heuristic.hpp>

namespace dds::belief_evaluation
{

/// delta rule: in third seat, following suit, as a defender, after
/// partner's lead and dummy's second-hand card -- play the highest
/// double-dummy-optimal card, when it beats dummy's played card. Defers
/// whenever dummy's card already beats every candidate (see
/// `third_seat_low`, which fires in exactly that case) or any other gate
/// condition fails.
///
/// "Partner led" is checked explicitly even though it is, in this
/// module's fixed N/E/S/W rotation, always true whenever
/// `position_in_trick == 2` (two seats around a four-seat cyclic rotation
/// always lands on the seat's own partner) -- kept for the same clarity
/// BridgeLibraries' own `HighInThird.cpp` keeps it for, not because it can
/// fail here.
auto high_in_third() -> DefenderHeuristic;

}  // namespace dds::belief_evaluation
