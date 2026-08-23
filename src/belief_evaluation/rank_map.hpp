#pragma once

#include <api/dll.h>

#include <belief_evaluation/types.hpp>

/// Builds a RankMap from a Deal: aggr[s] is the OR of remainCards[h][s]
/// across all four hands, for each suit s, converted from Deal's public bit
/// convention (bit r = absolute rank r) to the compacted convention the
/// lookup tables use (bit r-2 = absolute rank r), masked to 13 significant
/// bits so a malformed Deal with a stray bit set above rank 14 cannot push
/// an aggr entry out of the lookup tables' valid index range.
auto make_rank_map(Deal const& deal) -> RankMap;
