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
/// own played sequence, accumulating `p_j` through `delta` at every
/// defender ply. Three outcomes, and they must not be confused:
///
/// - **an ordinary rejection**: `layout` is `std::nullopt`, `error` is
///   `ValidationError::None`. The candidate does not follow this line --
///   the seat on play at some ply does not hold the recorded card, or
///   `delta` assigns that card zero probability (or omits it) in this
///   candidate. This is the "matches the played sequence" filter itself,
///   not a failure of anything;
/// - **a `delta` contract violation encountered during replay**: `layout`
///   is `std::nullopt`, `error` is the specific `ValidationError`, `seat`
///   is which defender was asked. Reported exactly the way
///   `expand_defender_node` reports one, through the same vocabulary --
///   see that function's own doxygen;
/// - **success**: `layout` holds the candidate played forward to the
///   node's own depth, `p_j` is the probability the defenders would have
///   played the observed sequence in this candidate, `error` is `None`.
struct ReplayResult
{
    std::optional<Deal> layout;
    Probability p_j = 0.0;                          ///< meaningful only when layout has a value
    ValidationError error = ValidationError::None;  ///< a delta contract violation; None otherwise
    int seat = -1;                                   ///< meaningful only when error is not None
};

/// Plays a root-space `candidate` forward along `node_state`'s own played
/// sequence, or rejects it, accumulating `p_j` at every defender ply along
/// the way. See `ReplayResult`'s own doxygen for the three outcomes.
///
/// The first `history_for(root_layout).number` entries of
/// `node_state.history` are skipped: those cards are already reflected in
/// `candidate`'s own `currentTrick*` fields (a candidate that reached this
/// point already passed `make_root`'s consistency filter, which requires
/// sharing `root_layout`'s current-trick state exactly), so replaying them
/// from index 0 would play them a second time — producing a `Deal` that
/// looks like a legal position and is wrong. This is the same prefix
/// `make_root` itself skips when seeding `ObservationState::history`, and
/// both go through `history_for` so the two can never disagree about where
/// it ends. Those pre-root defender cards contribute nothing to `p_j`
/// either — `make_root` sets every drawn layout's `p_i = 1` regardless of
/// what was already in the root's trick — so skipping them keeps a
/// replenished layout's weight on exactly the same footing as a drawn
/// one's.
///
/// At each remaining entry, the seat is derived via `seat_on_play` on the
/// candidate *as replayed so far* — never read from `node_state.first`,
/// which is the root's own leader and is unrelated once play has moved on
/// (see `ObservationState`'s own doxygen on `tricks_remaining` for the same
/// point made about a different field).
///
/// A declarer or dummy card is always legal in `candidate` — those
/// holdings are common knowledge and bit-identical across every consistent
/// layout — and is asserted rather than checked; it contributes nothing to
/// `p_j`, matching how `p` is built during ordinary expansion (declarer's
/// play neither filters nor reweights). A **defender** ply calls `delta`
/// in the intermediate state replayed alongside the layout (via
/// `advance_state`, from `node_state`'s own root — the recursion retains no
/// intermediate states, so this is the only way to recover the exact query
/// the original expansion made). `delta`'s probability for the card
/// actually played is multiplied into `p_j`; an omitted card and a
/// zero-probability card mean the same thing here (an ordinary rejection),
/// and the raw probability is used as `delta` returned it, never
/// renormalised — exactly as `expand_defender_node` does.
auto replay_candidate(
    Deal const& candidate,
    Deal const& root_layout,
    ObservationState const& node_state,
    DefenderStrategy const& delta) -> ReplayResult;

/// One layout the node-local scan accepted: the replayed `Deal`, its
/// accumulated `p_j` (see `replay_candidate`), and its root-space key (see
/// `BeliefNode::root_keys`) -- carried alongside the layout rather than
/// recomputed later, since the scan already derived it to check the
/// exclusion set.
struct ScanCandidate
{
    Deal layout;
    Probability p_j;
    std::uint64_t root_key;
};

/// The result of a node-local replenishment scan: the accepted candidates,
/// or a `delta` contract violation encountered while replaying one of
/// them. No `RootFailure`-shaped failure exists here: a scan that finds
/// nothing is an ordinary outcome, not an error -- the node simply keeps
/// the layouts it already had. `candidates` and `outcome` are meaningful
/// only when `error` is `ValidationError::None`; `seat` only when it is
/// not, mirroring `ReplayResult`'s own shape for the same reason.
struct ScanResult
{
    std::vector<ScanCandidate> candidates;
    ScanOutcome outcome = ScanOutcome::SourceExhausted;
    ValidationError error = ValidationError::None;
    int seat = -1;
};

/// Scans `source` from index 0 for layouts consistent with `root_layout`
/// that satisfy `node`'s own played sequence (see `replay_candidate`) and
/// are not already among `node.root_keys`, up to `wanted` candidates and
/// `budget` calls to `source.at()`.
///
/// The filters apply in this order, for cost and not only correctness: `at()`,
/// then `is_consistent` (the same cheap check `make_root` itself applies —
/// a candidate outside the belief space at all is rejected before anything
/// else looks at it), then the root-space exclusion set (cheap, and
/// possible before any replay precisely because the key is root-space —
/// see `BeliefNode::root_keys`), then `replay_candidate` (which folds the
/// history-following check and `delta`'s own accumulation into one pass —
/// a candidate rejected on a card it does not hold costs no `delta` call).
/// A candidate already present in `node.root_keys` costs no `delta` call
/// either, for the same reason.
///
/// `budget` counts `source.at()` calls specifically, matching
/// `RootOptions::scan_budget`'s own definition exactly, so a node-local
/// scan-to-hit measurement is commensurable with the root's.
///
/// `ScanOutcome`'s tie-break is kept identical to `make_root`'s: reaching
/// `wanted` and `budget` at the same step reports `SampleFilled`, since
/// `wanted` is checked first at every step. Reaching the end of `source`
/// reports `SourceExhausted` even if nothing was accepted — the node holds
/// everything its own path admits, which is what licenses `is_sample` being
/// set to false at such a node (see `ScanOutcome`'s own doxygen, written for
/// exactly this reuse).
///
/// `node`'s own declarer (`node.state.declarer`) is used throughout, not a
/// separate parameter -- common knowledge, identical across every layout
/// the node holds. `source.size()` is assumed to have already been
/// validated: `node` could not exist at all unless some earlier
/// `make_root` call already required `source.size()` to be present.
auto scan_for_replenishment(
    BeliefNode const& node,
    Deal const& root_layout,
    LayoutSource const& source,
    DefenderStrategy const& delta,
    std::uint64_t wanted,
    std::optional<std::uint64_t> budget) -> ScanResult;

}  // namespace dds::belief_evaluation
