#pragma once

#include <array>

#include <api/dds_data_types.hpp>

namespace dds::belief_evaluation
{

/// For each suit, an approximate count of how many tricks the side
/// containing `defender_seat` (that seat and its partner) can cash by
/// leading that suit repeatedly, on an "optimal entry" assumption -- free
/// choice of which of the two defending hands leads each round, as if
/// entries were never a problem. `defender_seat`'s "opponents" are simply
/// the other two seats; this function has no notion of declarer or dummy,
/// since the trick-counting arithmetic does not depend on which one is
/// which.
///
/// **Never an overestimate; may still underestimate.** A deliberately
/// simplified single-suit top-tricks counter, not an exact double-dummy
/// suit-trick calculator (out of scope for this capability -- see the
/// plan's own non-goals). Takes the defending side's own `cap` highest
/// cards combined (`cap` bounded by whichever defending hand is longer
/// -- a round, for as long as both still hold the suit, consumes one
/// card from each simultaneously, so the two hands cannot cash more
/// separate tricks than that) and counts a leading run of those, highest
/// first: a card wins its round once the opposing side can no longer
/// contest at all (every non-void hand, theirs or ours, must contribute
/// one card every round the suit is run, whether or not that round is
/// won, so each opposing hand is provably exhausted after exactly as
/// many rounds as it started with cards, regardless of which rounds
/// those were), or, while they still can, beats the single highest card
/// either opposing hand holds. The run stops the moment one of our cards
/// does neither: this function counts tricks cashed *by leading this
/// suit*, and the instant an opponent wins one of them, the suit's lead
/// passes to them -- they are not obliged to lead it back, so nothing
/// beyond that point is a guaranteed top trick no matter how high it
/// ranks. (An earlier version kept matching our remaining cards against
/// the opponents' own remaining cards after the first stopper,
/// continuing to count every one of ours that still individually
/// outranked something; that overcounts for exactly this reason.)
///
/// Comparing every one of our cards, while the opposing side can still
/// contest, against a single static "highest card either opposing hand
/// holds" is still an approximation, not an exact simulation of which
/// specific opposing card survives to which specific round: a position
/// where an opposing hand's one dangerous card is not the one that
/// decides an early round, but still correctly resolves to beat a later
/// one of ours once the actual sequencing is worked out, can come out
/// lower here than the true optimal-defense answer. The documented
/// contract is the safe side of that: never higher.
///
/// `trump` uses this module's own convention (`defender_heuristic.hpp`'s
/// own doxygen): `DDS_NOTRUMP` for no trump suit. When `suit` is not
/// `trump` and an opponent holds any card of `trump`, that suit's count
/// is additionally capped at that opponent's own card count in `suit` --
/// once they run out of it, they may ruff instead of following, so no
/// further round of this suit is a guaranteed trick.
///
/// Reads `layout.remainCards` directly rather than through `RankMap`:
/// `RankMap` carries the deal's aggregate outstanding-card picture
/// (`rank_map.hpp`'s own doxygen), not any one seat's own per-suit
/// holding, which is what this function actually needs.
auto suit_top_tricks(Deal const& layout, int defender_seat, int trump) -> std::array<int, 4>;

}  // namespace dds::belief_evaluation
