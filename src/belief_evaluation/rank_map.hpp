#pragma once

#include <api/dll.h>

#include <belief_evaluation/types.hpp>

/// Builds a RankMap from a Deal: aggr[s] is the OR of remainCards[h][s]
/// across all four hands, for each suit s, converted from Deal's public bit
/// convention (bit r = absolute rank r) to the compacted convention the
/// lookup tables use (bit r-2 = absolute rank r).
auto make_rank_map(Deal const& deal) -> RankMap;
