#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/layout_source.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/types.hpp>
#include <belief_evaluation/validation.hpp>

namespace dds::belief_evaluation
{

/// The result of replaying a root-space candidate along a node's played
/// cards. Three outcomes:
///
/// - **Rejection:** no `layout`, `error` is `None`. The seat on play does
///   not hold the recorded card, or `delta` gives it no probability. This
///   is the filter working, not a failure.
/// - **A `delta` contract violation:** no `layout`. `error` names the
///   cause; `seat` and `offending_layout` locate it, as in
///   `expand_defender_node`.
/// - **Success:** `layout` is the candidate played forward to the node.
///   `p_j` is the probability that the defenders played the observed
///   cards in it.
struct ReplayResult
{
    std::optional<Deal> layout;
    Probability p_j = 0.0;                          ///< meaningful only when layout has a value
    ValidationError error = ValidationError::None;  ///< a delta contract violation; None otherwise
    int seat = -1;                                   ///< meaningful only when error is not None
    Deal offending_layout{};                        ///< meaningful only when error is not None
};

/// Plays a root-space `candidate` forward along the played cards of
/// `node_state`, or rejects it. Multiplies `p_j` at every defender ply.
/// See `ReplayResult`.
///
/// Skips the first `history_for(root_layout).number` cards. They are
/// already in the `currentTrick*` fields of `candidate`, so replaying them
/// would give a wrong `Deal` that looks legal. They add nothing to `p_j`,
/// just as `make_root` gives every layout `p_i = 1`.
///
/// The seat for each card comes from `seat_on_play` on the replayed
/// candidate, never from `node_state.first`.
///
/// Declarer and dummy cards are common knowledge, so their legality is
/// asserted, and they add nothing to `p_j`. At a **defender** ply, `delta`
/// is called in the state built by `advance_state`. Its probability for
/// the played card is multiplied into `p_j`, raw, as in
/// `expand_defender_node`. An omitted card and a zero-probability card both
/// reject.
auto replay_candidate(
    Deal const& candidate,
    Deal const& root_layout,
    ObservationState const& node_state,
    DefenderStrategy const& delta) -> ReplayResult;

/// One layout the scan accepted: the replayed `Deal`, its `p_j`, and its
/// root-space key.
struct ScanCandidate
{
    Deal layout;
    Probability p_j;
    std::uint64_t root_key;
};

/// The result of a replenishment scan. Finding nothing is not a failure:
/// the node keeps its layouts. `candidates` and `outcome` are meaningful
/// only when `error` is `None`. `seat` and `offending_layout` are
/// meaningful only when it is not. `at_calls` is always meaningful.
struct ScanResult
{
    std::vector<ScanCandidate> candidates;
    ScanOutcome outcome = ScanOutcome::SourceExhausted;
    ValidationError error = ValidationError::None;
    int seat = -1;
    Deal offending_layout{};
    std::uint64_t at_calls = 0;
};

/// Scans `source` from index 0 for layouts that are consistent with
/// `root_layout`, fit the played cards of `node`, and are not in
/// `node.root_keys`. Stops at `wanted` candidates or `budget` `at()` calls.
///
/// Filters run cheapest first: `at()`, `is_consistent`, the root-key
/// exclusion set, then `replay_candidate`. Only the last calls `delta`.
///
/// The exclusion set does not grow during the scan. A source that returns
/// the same layout at two indices gives two entries, as in `make_root`.
///
/// `budget` counts `source.at()` calls, like `RootOptions::scan_budget`.
/// `wanted` is checked before the budget, as in `make_root`. Reaching the
/// end of `source` is `SourceExhausted` even when nothing was accepted.
/// That is what allows `is_sample` to be cleared at the node.
///
/// Uses `node.state.declarer`. Assumes `source.size()` was validated by
/// `make_root`.
///
/// `wanted == 0` returns `SampleFilled` at once, with no `source.size()`
/// call and no exclusion set. This happens at every node when
/// `replenish_below` is above `sample_size`.
auto scan_for_replenishment(
    BeliefNode const& node,
    Deal const& root_layout,
    LayoutSource const& source,
    DefenderStrategy const& delta,
    std::uint64_t wanted,
    std::optional<std::uint64_t> budget) -> ScanResult;

}  // namespace dds::belief_evaluation
