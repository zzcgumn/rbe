#pragma once

#include <array>

#include <api/dds_data_types.hpp>
#include <utility/constants.h>

namespace dds::belief_evaluation
{

/// For each seat (0..3), which suits that seat has shown void in by
/// `history`: `[seat][suit]` is true once that seat is known, from the play
/// alone, to hold no more cards of `suit`.
using VoidsBySeat = std::array<std::array<bool, DDS_SUITS>, DDS_HANDS>;

/// Derives `VoidsBySeat` from `history` alone -- no holdings anywhere in
/// sight, and none needed: a seat that plays a card of a suit other than the
/// one led to that trick has just proven it holds none of the led suit, and
/// that is the whole rule. Trick winners (needed only to know who leads the
/// next trick, and so which seat a later card in `history` belongs to) follow
/// from `trump`, `opening_leader` and the cards themselves, exactly as
/// `trick_winner` (position.hpp) computes them from a completed trick's four
/// plays -- this function is that same trick arithmetic run forward over a
/// whole history rather than one trick at a time.
///
/// A discard and a ruff are both off-suit plays and are treated identically:
/// the void follows from the suit played, never from whether the card won.
///
/// `history` may end mid-trick -- `ObservationState::history` always does,
/// once the root itself is mid-trick -- and the trailing one-to-three cards
/// still establish voids exactly as a complete trick's cards do; there is
/// simply no next leader to compute from them, and none is needed since
/// there is no further trick in `history` to derive one for.
///
/// Total: never rejects `history`, however it was built. A history that does
/// not actually belong to a root produces voids that are simply wrong about
/// that root -- catching that is a different, and separate, concern (see
/// history_verification.hpp).
///
/// Declarer's and dummy's entries are derived exactly like the defenders',
/// even though only the defenders' voids ever narrow an enumeration: it is a
/// fact about the play, not about what any one caller needs, and a cheap
/// consistency check elsewhere gets to lean on all four for free.
auto derive_voids(PlayTraceBin const& history, int opening_leader, int trump) -> VoidsBySeat;

}  // namespace dds::belief_evaluation
