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

/// The callback, or the evaluator stage, that produced an EvaluationError.
enum class EvaluationCallback
{
    RootConstruction,  ///< make_root() rejected `source`. Not a callback, but reported the same way.
    DeclarerPlay,
    DefenderStrategy,
};

/// A callback's contract violation, or an unusable `LayoutSource`, with
/// the context needed to find the call.
///
/// `layout` depends on `callback`:
/// - `DeclarerPlay`: the node's `known_holdings`.
/// - `DefenderStrategy`: the one offending layout.
/// - `RootConstruction`: the root layout.
///
/// `validation` and `root_failure` are exclusive. A `RootConstruction`
/// error sets `root_failure` and leaves `validation` at `None`.
struct EvaluationError
{
    ValidationError validation;
    EvaluationCallback callback;
    int seat;
    Deal layout;
    RootFailure root_failure = RootFailure::None;
};

/// The value of one candidate card at the root. See
/// EvaluationValue::root_children for what "candidate" means.
struct RootChildValue
{
    Card card;
    double value;
};

/// A caller-supplied double-dummy upper bound on the tricks declarer can
/// take from a layout. Used by `tier2_dead()` through
/// EvaluateOptions::bound.
///
/// **Not validated.** Checking a bound means solving the position. A bound
/// that is too *low* is unsound: the cut fires on a layout declarer would
/// make, and that layout scores zero. A bound that is too high only loses
/// pruning. See also EvaluateOptions::delta_is_double_dummy_optimal.
using LayoutBound = std::function<int(Deal const&)>;

/// The three sampling options. Default-constructed means exhaustive
/// enumeration, no scan budget and no replenishment.
///
/// **The fields are coupled.** `scan_budget` applies whether or not
/// `sample_size` is set. `replenish_below` does nothing without
/// `sample_size`.
struct SamplingOptions
{
    /// The maximum number of layouts drawn for the root. Absent means
    /// exhaustive enumeration. Passed to `make_root` as
    /// `RootOptions::sample_size`. There is no seed here: randomness comes
    /// from the order of the source.
    std::optional<std::uint64_t> sample_size;

    /// The maximum number of `source.at()` calls in **one scan**. The root
    /// scan and each replenishment scan get a fresh budget. A budget that
    /// runs out gives a smaller draw, not an error. See
    /// `RootFailure::ScanBudgetExhausted` for the case that is an error.
    std::optional<std::uint64_t> scan_budget;

    /// Replenish a node whose `layouts.size()` is below this. The node is
    /// topped up towards `sample_size` from `source` before any cut runs
    /// and before any `BeliefView` is built there. Absent by default: no
    /// scan after the root.
    ///
    /// The trigger reads **only** `node.layouts.size()`. Anything else,
    /// such as `is_sample` or the number of made or dead layouts, biases
    /// the result (algorithm.md).
    ///
    /// **Needs `sample_size`.** Without it, this field is ignored. Each scan
    /// is capped by `scan_budget`.
    ///
    /// A value **above** `sample_size` is accepted but makes every node
    /// trigger, which costs scans:
    /// - A node with `no_more_available` set skips the scan and is not
    ///   recorded in `EvaluationCounters::replenishment_by_depth`.
    /// - A node already at `sample_size` records an attempt with
    ///   `at_calls == 0`.
    /// - Any other node pays for a real scan, even one that finds nothing.
    ///
    /// So `attempted` counts scans that ran, not times the trigger held.
    std::optional<std::uint64_t> replenish_below;
};

/// Options for `evaluate()`. All off by default.
///
/// `retain_root` keeps the root node in EvaluationValue::retained_root.
/// Without it, each node is released when its subtree completes.
struct EvaluateOptions
{
    bool retain_root = false;

    /// Fill EvaluationValue::counters. Off by default, at no cost.
    /// Counters never change `p_make` or `root_children`.
    bool collect_counters = false;

    /// See LayoutBound. Absent by default, which disables `tier2_dead()`.
    LayoutBound bound;

    /// The caller declares that `delta` holds declarer to the double-dummy
    /// trick count, whatever declarer does. It is separate from `bound`
    /// because the two claims differ: a caller may want a bound for
    /// counters while their δ does not qualify.
    ///
    /// The claim is about *trick count*. `DoubleDummyDefender` qualifies,
    /// even though it is not best defence against a *contract*.
    ///
    /// **Not validated. A wrong declaration is silently wrong:** a δ that
    /// gives away a trick lets `tier2_dead()` discard a layout declarer
    /// would make.
    bool delta_is_double_dummy_optimal = false;

    /// `sample_size`, `scan_budget` and `replenish_below`. See
    /// `SamplingOptions`.
    SamplingOptions sampling;

    /// Every card played before `root_layout`, seen by every seat. Absent
    /// by default. It differs from `history`: see
    /// `ObservationState::play_record`.
    std::optional<PlayRecord> play_record;
};

/// Layout counts for all nodes at one depth.
///
/// - Mean layouts per node: `layout_sum / nodes`.
/// - `layout_min` shows collapse. A node with one layout gives a strategy
///   false certainty.
/// - `layout_sum` is the total of surviving layouts. It hides collapse.
struct DepthSampleStats
{
    std::uint64_t nodes = 0;       ///< nodes reached at this depth
    std::uint64_t layout_sum = 0;  ///< sum of node.layouts.size() over those nodes
    std::uint64_t layout_min = 0;  ///< smallest node.layouts.size() at this depth; meaningless if nodes == 0
};

/// Totals for all replenishment scans at one depth.
///
/// `attempted` minus `succeeded` is the number of scans that found
/// nothing. That shows whether `BeliefNode::no_more_available` saves work.
/// `at_calls` counts like `RootOptions::scan_budget`. Store totals, not
/// ratios, so that runs and depths can be summed. Depth 0 is always zero:
/// the root is never replenished.
struct DepthReplenishmentStats
{
    std::uint64_t attempted = 0;      ///< replenishment scans that ran at this depth
    std::uint64_t succeeded = 0;      ///< of those, scans that added at least one layout
    std::uint64_t layouts_added = 0;  ///< layouts added by all scans at this depth
    std::uint64_t at_calls = 0;       ///< source.at() calls by all scans at this depth
};

/// Facts about the cost and shape of one `evaluate()` run. Filled only when
/// EvaluateOptions::collect_counters is set. Nothing here affects `p_make`.
struct EvaluationCounters
{
    /// Every BeliefNode evaluated, terminal or expanded, root included. A
    /// test proves a cut fired when this drops below the uncut run.
    std::uint64_t nodes_visited = 0;

    /// already_made() cuts that fired. The node returns its mass without
    /// expanding.
    std::uint64_t tier1_made_cuts = 0;

    /// is_dead() cuts that fired. The node returns 0.0 without expanding.
    std::uint64_t tier1_dead_cuts = 0;

    /// tier2_dead() cuts that fired. Always 0 when the root, or an
    /// ancestor, was sampled.
    std::uint64_t tier2_cuts = 0;

    /// Entry i is depth i, root at 0, as in p_make()'s depth parameter.
    /// The vector grows as depths are reached. An index past the end means
    /// no node was visited at that depth. Every entry has `nodes >= 1`.
    /// Filled on the exhaustive path too.
    std::vector<DepthSampleStats> sample_size_by_depth;

    /// Entry i is depth i, indexed like `sample_size_by_depth`. An index
    /// past the end means no scan was attempted at that depth. That
    /// differs from a scan that found nothing.
    std::vector<DepthReplenishmentStats> replenishment_by_depth;
};

/// `P_make` for one declarer strategy against one defender strategy, plus
/// a value per root card. A search layer can then compare root cards
/// without rerunning the search.
///
/// - Declarer (or dummy) on play: one entry per legal first card. Each is
///   `P_make` for playing that card and then following `pi`. **These are
///   alternatives, not a partition.** `p_make` is the entry `pi` chose.
/// - Defender on play: one entry per card `delta` plays with positive
///   probability somewhere in the belief space. These **do** sum to
///   `p_make`.
/// - Empty at a terminal root, and when declarer already has the tricks
///   needed.
struct EvaluationValue
{
    double p_make = 0.0;
    std::vector<RootChildValue> root_children;

    /// True when declarer **or dummy** is on lead at the root
    /// (alternatives). False when a defender is (a partition).
    ///
    /// Do not test `root.first == declarer` instead: it is wrong when dummy
    /// is on lead. Set on every path, including when `root_children` is
    /// empty.
    bool root_is_declaring_side = false;

    /// The root BeliefNode, only when EvaluateOptions::retain_root is set.
    /// The root alone, never a tree.
    ///
    /// Its ObservationState::play_record is always null, even when
    /// EvaluateOptions::play_record was supplied. That pointer would
    /// dangle once the caller's options go out of scope.
    std::optional<BeliefNode> retained_root;

    /// The counters, only when EvaluateOptions::collect_counters is set.
    std::optional<EvaluationCounters> counters;
};

/// Either a value or an EvaluationError, never both. A contract violation
/// by a callback or by `source` is reported here, not thrown.
struct EvaluationResult
{
    /// One entry, keyed by `pi.id`.
    std::map<StrategyId, EvaluationValue> by_strategy;
    std::optional<EvaluationError> error;  ///< meaningful only when by_strategy is empty
};

/// Tier 1 made cut: true once declarer has every trick the contract
/// needs. Always sound: trick counts are the same in every layout.
///
/// Public so that tests can check it on a hand-built node.
auto already_made(ObservationState const& state) -> bool;

/// Tier 1 dead cut: true once declarer cannot reach `tricks_needed` even
/// by winning every remaining trick. Always sound, as above.
auto is_dead(ObservationState const& state) -> bool;

/// Tier 2 cut: true only when the caller sets
/// EvaluateOptions::delta_is_double_dummy_optimal *and* every layout at
/// `node` is dead by EvaluateOptions::bound. Otherwise it never fires.
///
/// **Gated on `! node.is_sample`.** On a sample, "every layout is dead"
/// only means "every layout drawn is dead". A sampled root passes
/// `is_sample` down, which turns the cut off. A replenishment scan that
/// exhausts the source sets `is_sample` back to false, and the cut can
/// fire again there.
///
/// Stops at the first live layout: each `bound` call is a double-dummy
/// solve.
///
/// **Never a make cut.** `DD >= rho` says nothing about `R`, because `pi`
/// may play worse than double dummy. The cut is per node, not per layout:
/// dropping one dead layout would change the posteriors `pi` sees. It
/// returns before any view is built at or below `node`.
auto tier2_dead(BeliefNode const& node, EvaluateOptions const& options) -> bool;

/// `P_make` for `pi` against `delta` over the belief space `make_root`
/// builds from `source`. That is every consistent layout, or a sample when
/// `options.sampling.sample_size` is set. With
/// `options.sampling.replenish_below` set, a node below it is topped up
/// from `source`. Without it, a sampled node only shrinks. The cuts above
/// skip subtrees worth exactly zero and change no answer.
///
/// `state_key` is never called: there is no cache.
auto evaluate(
    Deal const& root_layout,
    int declarer,
    int tricks_needed,
    LayoutSource const& source,
    DeclarerStrategy const& pi,
    DefenderStrategy const& delta,
    EvaluateOptions const& options = {}) -> EvaluationResult;

}  // namespace dds::belief_evaluation
