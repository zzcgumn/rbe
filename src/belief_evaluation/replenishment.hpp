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

/// The result of replaying a root-space candidate forward along a node's
/// played sequence. Three outcomes, which must not be confused:
///
/// - **rejection**: no `layout`, `error` is `None`. The candidate does not
///   follow this line — the seat on play does not hold the recorded card,
///   or `delta` gives it no probability. This is the filter working, not a
///   failure;
/// - **a `delta` contract violation**: no `layout`, `error` names the
///   cause, `seat` and `offending_layout` locate it. Reported exactly as
///   `expand_defender_node` reports one;
/// - **success**: `layout` is the candidate played forward to the node's
///   depth, `p_j` the probability the defenders would have played the
///   observed sequence in it.
struct ReplayResult
{
    std::optional<Deal> layout;
    Probability p_j = 0.0;                          ///< meaningful only when layout has a value
    ValidationError error = ValidationError::None;  ///< a delta contract violation; None otherwise
    int seat = -1;                                   ///< meaningful only when error is not None
    Deal offending_layout{};                        ///< meaningful only when error is not None
};

/// Plays a root-space `candidate` forward along `node_state`'s played
/// sequence, or rejects it, accumulating `p_j` at every defender ply. See
/// `ReplayResult` for the three outcomes.
///
/// The first `history_for(root_layout).number` entries are skipped: those
/// cards are already in `candidate`'s own `currentTrick*` fields, so
/// replaying them would play them twice and produce a legal-looking `Deal`
/// that is wrong. They contribute nothing to `p_j` either — `make_root`
/// gives every drawn layout `p_i = 1` regardless of what was in the root's
/// trick — so a replenished layout's weight stays on the same footing as a
/// drawn one's.
///
/// The seat at each remaining entry comes from `seat_on_play` against the
/// candidate as replayed so far, never from `node_state.first`.
///
/// A declarer or dummy card is always legal — those holdings are common
/// knowledge — so it is asserted rather than checked, and contributes
/// nothing to `p_j`. A **defender** ply calls `delta` in the intermediate
/// state replayed alongside the layout via `advance_state`; its probability
/// for the card actually played is multiplied into `p_j`, used raw and
/// never renormalised, exactly as `expand_defender_node` does. An omitted
/// card and a zero-probability card both mean rejection.
auto replay_candidate(
    Deal const& candidate,
    Deal const& root_layout,
    ObservationState const& node_state,
    DefenderStrategy const& delta) -> ReplayResult;

/// One layout the node-local scan accepted: the replayed `Deal`, its
/// accumulated `p_j`, and its root-space key — carried rather than
/// recomputed later, since the scan already derived it to check the
/// exclusion set.
struct ScanCandidate
{
    Deal layout;
    Probability p_j;
    std::uint64_t root_key;
};

/// The result of a node-local replenishment scan. There is no
/// `RootFailure`-shaped failure here: a scan that finds nothing is an
/// ordinary outcome, and the node keeps the layouts it had. `candidates`
/// and `outcome` are meaningful only when `error` is `None`, `seat` and
/// `offending_layout` only when it is not. `at_calls` is meaningful either
/// way — it is what scan-to-hit instrumentation reports on.
struct ScanResult
{
    std::vector<ScanCandidate> candidates;
    ScanOutcome outcome = ScanOutcome::SourceExhausted;
    ValidationError error = ValidationError::None;
    int seat = -1;
    Deal offending_layout{};
    std::uint64_t at_calls = 0;
};

/// Scans `source` from index 0 for layouts consistent with `root_layout`
/// that satisfy `node`'s played sequence and are not already among
/// `node.root_keys`, up to `wanted` candidates and `budget` `at()` calls.
///
/// Filters apply cheapest-first: `at()`, then `is_consistent`, then the
/// root-space exclusion set (possible before any replay precisely because
/// the key is root-space), then `replay_candidate`. A candidate rejected
/// earlier costs no `delta` call.
///
/// The exclusion set is built once and not grown as candidates are
/// accepted, so a source returning the same root-space layout at two
/// indices within one scan yields two entries. That is `make_root`'s
/// existing behaviour; de-duplicating within a scan would be a `make_root`
/// change first.
///
/// `budget` counts `source.at()` calls, matching `RootOptions::scan_budget`
/// exactly, so node-local and root scan-to-hit are commensurable.
/// `ScanOutcome`'s tie-break is `make_root`'s: `wanted` is checked first,
/// and reaching the end of `source` is `SourceExhausted` even if nothing
/// was accepted — which is what licenses clearing `is_sample` at such a
/// node.
///
/// `node.state.declarer` is used throughout rather than a separate
/// parameter. `source.size()` is assumed already validated: `node` could
/// not exist unless `make_root` had required it.
///
/// `wanted == 0` returns immediately — `SampleFilled`, no candidates, no
/// `source.size()` call and no exclusion set built. The loop would reach
/// the same outcome anyway, but not before paying for both, and this is
/// not a rare input: it is what
/// `EvaluateOptions::sampling.replenish_below` set above `sample_size`
/// reaches at every node.
auto scan_for_replenishment(
    BeliefNode const& node,
    Deal const& root_layout,
    LayoutSource const& source,
    DefenderStrategy const& delta,
    std::uint64_t wanted,
    std::optional<std::uint64_t> budget) -> ScanResult;

}  // namespace dds::belief_evaluation
