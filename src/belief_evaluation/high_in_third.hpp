#pragma once

#include <belief_evaluation/defender_heuristic.hpp>

namespace dds::belief_evaluation
{

/// delta rule: in third seat, following suit, as a defender, after
/// partner's lead and the second player's card -- play the highest
/// double-dummy-optimal card, when it beats that card. Defers whenever
/// the second player's card already beats every candidate (see
/// `third_seat_low`, which fires in exactly that case) or any other gate
/// condition fails.
///
/// The second player (trick index 1) is from the declaring side by
/// construction -- partner led, and this module's fixed N/E/S/W rotation
/// always alternates sides -- but *which* of declarer or dummy that is
/// varies by deal, so this rule compares against trick index 1 directly
/// rather than deriving a seat and assuming it lands there (an earlier
/// version did, and read past `currentTrickRank`'s bound whenever the
/// assumption failed). A trump ruff at that position beats every
/// candidate (nothing that merely follows suit beats a trump); a plain
/// discard of a third suit beats none (it does not contest the trick).
///
/// "Partner led" is checked explicitly even though it is, in this
/// module's fixed N/E/S/W rotation, always true whenever
/// `position_in_trick == 2` (two seats around a four-seat cyclic rotation
/// always lands on the seat's own partner) -- kept here for clarity, not
/// because it can actually fail in this module's rotation.
auto high_in_third() -> DefenderHeuristic;

}  // namespace dds::belief_evaluation
