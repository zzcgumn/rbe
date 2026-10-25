#pragma once

#include <api/dds_data_types.hpp>

#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// Builds a RankMap from a Deal. aggr[s] is the OR of remainCards[h][s]
/// over the four hands, moved from bit r to bit r-2 for absolute rank r.
/// Masked to 13 bits, so a malformed Deal cannot index past the lookup
/// tables.
auto make_rank_map(Deal const& deal) -> RankMap;

}  // namespace dds::belief_evaluation
