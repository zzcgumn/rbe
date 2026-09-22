#pragma once

#include <array>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

namespace dds::belief_evaluation
{

/// For each seat (0..3), which suits that seat has shown void in by
/// `history`: `[seat][suit]` is true once that seat is known, from the play
/// alone, to hold no more cards of `suit`.
using VoidsBySeat = std::array<std::array<bool, DDS_SUITS>, DDS_HANDS>;

/// Derives `VoidsBySeat` from `history` alone — no holdings needed: a seat
/// that plays off the suit led has proven it holds none of the led suit,
/// and that is the whole rule. A discard and a ruff are both off-suit plays
/// and are treated identically; the void follows from the suit played,
/// never from whether the card won. Trick winners, needed only to know
/// which seat a later card belongs to, follow from `trump`,
/// `opening_leader` and the cards, exactly as `trick_winner` computes them
/// one trick at a time.
///
/// `history` may end mid-trick — `ObservationState::history` always does
/// once the root is mid-trick — and the trailing cards establish voids just
/// as a complete trick's do.
///
/// **Total: never rejects `history`, however it was built.** A history that
/// does not belong to a root simply produces voids that are wrong about
/// that root; catching that is `verify_history`'s job, and this function
/// cannot depend on callers routing through it first.
///
/// Totality covers malformed input too, in two different ways. A
/// `history.number` outside `[0, 52]` or an `opening_leader` outside
/// `[0, DDS_HANDS)` is asserted and clamped — checked once per call. A
/// malformed card *within* a well-formed history is silently substituted
/// with a safe value (suit 0, rank 2) before being used to index anything:
/// garbage in, garbage out, never undefined behaviour. Not asserted,
/// because that check runs up to 52 times per call, and because silent
/// substitution is `RankMap::to_relative`'s own precedent for exactly this
/// shape of per-element check.
///
/// Declarer's and dummy's entries are derived like the defenders' even
/// though only defenders' voids narrow an enumeration: it is a fact about
/// the play, and `verify_history`'s cross-check gets to lean on all four
/// for free.
auto derive_voids(PlayTraceBin const& history, int opening_leader, int trump) -> VoidsBySeat;

}  // namespace dds::belief_evaluation
