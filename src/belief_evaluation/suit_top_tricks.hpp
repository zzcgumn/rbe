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
/// **Never an overestimate.** Ported in spirit, not verbatim, from
/// BridgeLibraries-private's `TopTricksAnalyzer`: that algorithm
/// additionally conserves which specific defending card is played each
/// round, to avoid unblocking problems between two unevenly-long hands.
/// This version always leads the higher of the two defending hands'
/// current top cards instead, which can only ever equal or undercount
/// the entry-optimal result -- consistent with `TopTricksAnalyzer`'s own
/// doxygen: "more important that the calculation never overestimates...
/// might be lower than what is actually available."
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
