#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/layout_source.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/validation.hpp>

namespace dds::belief_evaluation
{

/// Which callback — or which stage of the evaluator itself — produced a
/// reported EvaluationError.
enum class EvaluationCallback
{
    RootConstruction,  ///< make_root() rejected `source`; not a callback, but reported the same way
    DeclarerPlay,
    DefenderStrategy,
};

/// A callback's contract violation, or an unusable `LayoutSource`, with
/// enough context to locate the offending call.
///
/// `seat` and `layout` mean something different per `callback`: `layout` is
/// the node's `known_holdings` for `DeclarerPlay`, the one offending layout
/// for `DefenderStrategy`, and the root layout for `RootConstruction`.
/// `validation` and `root_failure` are likewise exclusive — a
/// `RootConstruction` error carries the cause in `root_failure` and leaves
/// `validation` at `None`, since there is no callback return to validate.
struct EvaluationError
{
    ValidationError validation;
    EvaluationCallback callback;
    int seat;
    Deal layout;
    RootFailure root_failure = RootFailure::None;
};

/// The value of one candidate action at the root, and the card that leads
/// to it. See EvaluationValue::root_children for what "candidate" means
/// when the root is a declarer node versus a defender node.
struct RootChildValue
{
    Card card;
    double value;
};

/// A caller-supplied double-dummy upper bound on the tricks declarer can
/// take from a given layout, for EvaluateOptions::bound. Consumed by
/// `tier2_dead()`, and kept separate from the cut so the cut is testable
/// with a scripted bound and no solver in the loop.
///
/// **Not validated, and cannot be** — checking a claimed trick bound means
/// solving the position, which is the work the bound exists to avoid. Too
/// *low* is the unsound direction: it makes the cut fire on a node holding
/// a layout declarer would have made, which then contributes zero. Too high
/// only loses pruning. This is one of two unvalidatable obligations a bound
/// brings with it; see EvaluateOptions::delta_is_double_dummy_optimal.
using LayoutBound = std::function<int(Deal const&)>;

/// The three sampling-related fields, grouped because they are coupled.
/// Default-constructed means exhaustive enumeration, an unbounded scan, and
/// no replenishment.
///
/// **Grouping is not composing.** `scan_budget` binds whether or not
/// `sample_size` is set; `replenish_below` does nothing at all without
/// `sample_size`, since there is no separate top-up target.
struct SamplingOptions
{
    /// Cap the number of layouts drawn for the root, absent for exhaustive
    /// enumeration. Threaded straight through to `make_root` as
    /// `RootOptions::sample_size`; see that field for the scanning and
    /// `is_sample` semantics. No seed here or anywhere in the evaluator —
    /// randomness lives in the source's own ordering.
    std::optional<std::uint64_t> sample_size;

    /// Cap the number of `source.at()` calls a **single scan** may make —
    /// the root's own, and, identically, each node-local replenishment
    /// scan, which starts a fresh budget rather than sharing what the root
    /// spent. Applies whether or not `sample_size` is set. A budget that
    /// binds is a degraded draw, not an error; see
    /// `RootFailure::ScanBudgetExhausted` for the case that is.
    std::optional<std::uint64_t> scan_budget;

    /// Replenish a node whose `layouts.size()` is below this, topping it
    /// back up towards `sample_size` from `source` before any cut is
    /// evaluated and before any `BeliefView` is built there. Absent by
    /// default, which leaves the recursion never scanning past the root.
    ///
    /// The trigger reads **only** `node.layouts.size()` — not `is_sample`,
    /// not how many layouts are made or dead. algorithm.md requires the
    /// rule to depend only on sample size or probability mass; anything
    /// else biases the result.
    ///
    /// **Coupled with `sample_size`: there is no separate top-up target.**
    /// With `sample_size` absent this field is treated as absent too, and
    /// the scan never runs. Each scan is capped by `scan_budget`.
    ///
    /// A value **above** `sample_size` is accepted but degenerate: the
    /// threshold is then met at every node. That is not free. Only a node
    /// with `no_more_available` already set short-circuits at no cost, and
    /// it is not recorded in `EvaluationCounters::replenishment_by_depth`
    /// at all. A node already at `sample_size` records an attempt costing
    /// nothing (`at_calls == 0`); anything below it pays for a real scan,
    /// including one that reaches the end of `source` having found nothing.
    /// So `attempted` counts scans that ran, never times the trigger held.
    std::optional<std::uint64_t> replenish_below;
};

/// Opt-in behaviour for `evaluate()`. Off by default: nodes are built on
/// the recursion stack and released as each subtree completes, so a
/// retained tree — a belief set kept at every node — is affordable only
/// when asked for; see EvaluationValue::retained_root.
struct EvaluateOptions
{
    bool retain_root = false;

    /// Populate EvaluationValue::counters. Off by default, so the ordinary
    /// path pays nothing. Collecting counters never changes `p_make` or
    /// `root_children` — counters_test.cpp pins that in both directions.
    bool collect_counters = false;

    /// See LayoutBound. Absent by default, which leaves `tier2_dead()`
    /// disabled just as an unset `delta_is_double_dummy_optimal` does.
    LayoutBound bound;

    /// The caller's declaration that `delta` holds declarer to the
    /// double-dummy trick count whatever declarer does. **Separate** from
    /// `bound` on purpose: the two assert different things, and a caller
    /// may want a bound for instrumentation while their δ does not qualify.
    ///
    /// This is about *trick count*, so a trick-maximising δ satisfies it —
    /// including `DoubleDummyDefender`, which is separately documented as
    /// not being best defence against a *contract*. The two claims do not
    /// conflict.
    ///
    /// **Cannot be validated**, and a wrong declaration is silently wrong
    /// in one direction: a δ that ducks a trick it need not lose lets
    /// `tier2_dead()` discard a layout declarer would have made. The bound
    /// is an upper bound on double-dummy play; nothing checks that δ
    /// delivers double-dummy play.
    bool delta_is_double_dummy_optimal = false;

    /// `sample_size`, `scan_budget` and `replenish_below`, grouped — see
    /// `SamplingOptions`. Default-constructed: exhaustive enumeration, no
    /// scan budget, no replenishment.
    SamplingOptions sampling;
};

/// Per-depth aggregate of `node.layouts.size()` across every node reached
/// at that depth. A depth has no single sample size — defender expansion
/// splits layouts across children — so this carries enough for the three
/// statistics that matter: `layout_sum / nodes` for the mean, `layout_min`
/// for the alarming case (a node down to one layout is where a strategy
/// gets false certainty), and `layout_sum` for total surviving layouts,
/// which *hides* collapse since many tiny nodes sum as one large one does.
struct DepthSampleStats
{
    std::uint64_t nodes = 0;       ///< nodes reached at this depth
    std::uint64_t layout_sum = 0;  ///< sum of node.layouts.size() across those nodes
    std::uint64_t layout_min = 0;  ///< the smallest node.layouts.size() seen at this depth; meaningless if nodes == 0
};

/// Per-depth aggregate of every node-local replenishment scan attempted at
/// that depth.
///
/// `attempted` and `succeeded` are kept apart because their difference is
/// how often a scan ran and found nothing — which is what says whether
/// `BeliefNode::no_more_available` is doing real work. `at_calls` counts
/// the same way `RootOptions::scan_budget` does, so a scan-to-hit ratio is
/// derivable; it is not stored as a ratio, since ratios cannot be summed
/// across depths or runs. Never incremented at the root: `make_root` built
/// it from a fresh scan, so depth 0's entry is structurally all zero.
struct DepthReplenishmentStats
{
    std::uint64_t attempted = 0;      ///< replenishment scans that actually ran at this depth
    std::uint64_t succeeded = 0;      ///< of those, how many added at least one layout
    std::uint64_t layouts_added = 0;  ///< total layouts added across every scan at this depth
    std::uint64_t at_calls = 0;       ///< total source.at() calls across every scan at this depth
};

/// Instrumentation `evaluate()` reports about its own run, populated only
/// when EvaluateOptions::collect_counters is set. Nothing here is read back
/// into `p_make`; every field is a fact about this run's shape or cost.
///
/// This is the module's shared instrumentation type. A later counter
/// arrives as one more member here rather than reshaping what came before.
struct EvaluationCounters
{
    /// Every BeliefNode reached and evaluated for a value — terminal or
    /// expanded, including the root. This is how a cut's tests prove it
    /// fired rather than merely computing the right number: a firing cut
    /// drops this below the same fixture's uncut run.
    std::uint64_t nodes_visited = 0;

    /// Every already_made() cut that fired: the contract is already made in
    /// every layout the node holds, so the node's mass is returned directly
    /// without expanding it.
    std::uint64_t tier1_made_cuts = 0;

    /// Every is_dead() cut that fired: the contract cannot be made from the
    /// node in even one layout, so 0.0 is returned directly without
    /// expanding it.
    std::uint64_t tier1_dead_cuts = 0;

    /// Every tier2_dead() cut that fired: every layout the node holds is
    /// dead under the caller's injected bound and delta_is_double_dummy_optimal
    /// declaration. Gated on !is_sample, so this stays 0 on any run where
    /// the root (or an ancestor) was sampled.
    std::uint64_t tier2_cuts = 0;

    /// Index i is depth i's own DepthSampleStats, root at depth 0 — the
    /// same indexing p_make()'s depth parameter uses. Grown as depth is
    /// reached rather than pre-sized, so that an index beyond `size() - 1`
    /// is what means "no node was ever visited at this depth"; a trailing
    /// zero-entry would instead read as a sample collapsed to nothing.
    /// Every populated entry has `nodes >= 1`. Populated on the exhaustive
    /// path too, where it describes the tree's shape rather than a sample.
    std::vector<DepthSampleStats> sample_size_by_depth;

    /// Index i is depth i's own DepthReplenishmentStats, indexed and grown
    /// exactly as `sample_size_by_depth` is, and read the same way: an
    /// index beyond `size() - 1` means replenishment was never attempted
    /// at that depth, which is not the same as attempting and finding
    /// nothing.
    std::vector<DepthReplenishmentStats> replenishment_by_depth;
};

/// `P_make` for one declarer strategy against one defender strategy, plus
/// root-child values, so that a search layer above can compare candidate
/// root actions without re-running the whole search per candidate.
///
/// - Declarer (or dummy) root: one entry per card that may legally be
///   played first, each the `P_make` of committing to it and following `pi`
///   thereafter. **Alternatives, not a partition** — `p_make` is whichever
///   entry `pi` actually chose, not the sum.
/// - Defender root: one entry per card `delta` gives positive probability
///   to somewhere in the belief space. These **do** sum to `p_make`, since
///   defender children partition mass by construction.
/// - Empty at a terminal root, and at a root where declarer has already
///   banked every trick needed: no first-card decision remains.
struct EvaluationValue
{
    double p_make = 0.0;
    std::vector<RootChildValue> root_children;

    /// The root BeliefNode, populated only when EvaluateOptions::retain_root
    /// is set. The root alone, never a tree.
    std::optional<BeliefNode> retained_root;

    /// This run's instrumentation, populated only when
    /// EvaluateOptions::collect_counters is set.
    std::optional<EvaluationCounters> counters;
};

/// Either the value, or the EvaluationError a callback's return (or
/// `source`) violated — never both. A callback is caller input, so a
/// contract violation is reported here rather than thrown; an internal
/// invariant failure asserts instead.
struct EvaluationResult
{
    /// `pi.id`'s entry. Exactly one today — `evaluate()` takes a single
    /// DeclarerStrategy — but keyed by the caller's own StrategyId so that
    /// a caller can look its own strategy up, and so the shape survives a
    /// future multi-strategy comparison.
    std::map<StrategyId, EvaluationValue> by_strategy;
    std::optional<EvaluationError> error;  ///< meaningful only when by_strategy is empty
};

/// Tier 1's already-made cut: true once declarer has banked every trick the
/// contract needs, whatever is left to play. Sound with no precondition —
/// both trick counts are common knowledge, identical across every layout in
/// the node — so there is no gate.
///
/// Exposed, rather than kept private to evaluate.cpp, so a test can build a
/// node by hand and check the condition without the recursion.
auto already_made(ObservationState const& state) -> bool;

/// Tier 1's dead cut, the mirror of already_made(): true once declarer
/// cannot reach tricks_needed even by winning every remaining trick. Sound
/// with no precondition, same argument, no gate.
auto is_dead(ObservationState const& state) -> bool;

/// Tier 2's node-level cut: true only when the caller has made the
/// EvaluateOptions::delta_is_double_dummy_optimal declaration *and* every
/// layout at `node` is dead by EvaluateOptions::bound. Absent either, this
/// never fires — the declaration is not a performance switch.
///
/// **Gated on `! node.is_sample`**, unlike the tier-1 cuts, which read only
/// common knowledge. This one concludes "every layout in this node is dead"
/// from the layouts the node happens to hold, which on a sample is only
/// "every layout drawn is dead". A sampled root propagates `is_sample` down
/// both expansion paths and switches the gate off — except at a node whose
/// own replenishment scan exhausted the source, which sets `is_sample` back
/// to false there and so genuinely holds the whole of its remaining space.
/// The gate fires again there through this same unchanged condition; it
/// tracks no replenishment floor.
///
/// Stops at the first live layout rather than calling `bound` for every
/// one: each call is a double-dummy solve in production.
///
/// **Never a make-cut.** `DD >= rho` implies nothing about `R` — `pi` may
/// play worse than double dummy — so the solve behind a bound must never be
/// reused to conclude the contract makes. Node-level, not per-layout:
/// dropping a dead layout would renormalise every survivor's posterior and
/// change what an arbitrary `pi` does with the view it is given. This
/// returns before any view is built at or below `node`.
auto tier2_dead(BeliefNode const& node, EvaluateOptions const& options) -> bool;

/// Evaluates `P_make` for `pi` against `delta` over the root belief space
/// `make_root` builds from `source`: every consistent layout by default, or
/// a bounded prefix under `options.sampling.sample_size`. With
/// `options.sampling.replenish_below` also set, a node whose layout count
/// falls below it is topped back up from `source` before being evaluated
/// further; absent, a sampled root's count only ever shrinks as defenders
/// play. The early cuts above skip subtrees that contribute exactly zero
/// and change no answer.
///
/// `state_key` is never called: there is no cache yet.
auto evaluate(
    Deal const& root_layout,
    int declarer,
    int tricks_needed,
    LayoutSource const& source,
    DeclarerStrategy const& pi,
    DefenderStrategy const& delta,
    EvaluateOptions const& options = {}) -> EvaluationResult;

}  // namespace dds::belief_evaluation
