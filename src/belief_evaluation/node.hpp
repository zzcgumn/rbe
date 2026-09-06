#pragma once

#include <optional>
#include <vector>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/layout_source.hpp>
#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// One node of the belief-evaluation recursion: the state every layout in
/// the node shares (see `ObservationState`), the surviving layouts, and each
/// layout's un-normalised probability `p`. `kappa` is the node's own sample
/// weight, not any one layout's — it is never combined with `p` into a
/// single stored `w = kappa * p_i`, so that rescaling `kappa` at a node
/// moves every layout's weight at once rather than requiring `p` to be
/// rewritten. `layouts` and `p` stay the same length; that invariant is
/// relied on wherever the node is read.
struct BeliefNode
{
    ObservationState state;
    std::vector<Deal> layouts;   ///< never grows after construction; stable while any BeliefView over it is live
    std::vector<Probability> p;  ///< parallel to layouts
    SampleWeight kappa = 0.0;

    /// Whether this node's layouts are a sample of a larger space rather
    /// than the whole of it. Set at construction (`make_root`'s own
    /// doxygen has the exact condition) and propagated to every child in
    /// both expansion paths — once true anywhere on the path to a node, it
    /// stays true for that node and everything below it, since a child
    /// built from a sample can never be more complete than its parent. See
    /// BeliefView::is_sample, which this feeds, and tier2_dead(), which
    /// gates on it.
    bool is_sample = false;
};

/// Why `make_root` could not build a node — `None` when it could.
/// Deliberately not `ValidationError`: that enum is documented as distinct
/// rejection reasons for a user-supplied *callback* return, and `source` is
/// not a callback; reusing it here would make `ValidationError::None` mean
/// two unrelated things.
enum class RootFailure
{
    None,
    SourceNotEnumerable,   ///< source.size() is std::nullopt
    NoLayoutSurvived,      ///< the whole source was scanned, but no candidate passed the consistency filter
    /// `RootOptions::scan_budget` ran out before a single consistent layout
    /// was found, with the source not yet exhausted -- distinct from
    /// `NoLayoutSurvived`, which means the *whole* source was checked and
    /// rejected everything. A caller seeing `NoLayoutSurvived` should fix
    /// their source; a caller seeing this should raise the budget instead
    /// -- collapsing the two would send them to debug the wrong thing.
    ScanBudgetExhausted,
};

/// Why a scan (of `make_root`'s own root-level draw, or -- reusing this
/// same signal -- a future node-local one at depth) stopped where it did.
/// `SourceExhausted` is the *exhaustive* case too: with no sample size and
/// no budget, a scan always ends this way, so this enum has no separate
/// "not sampling" value.
enum class ScanOutcome
{
    /// The whole source was scanned (index reached `source.size()`). The
    /// node holds every consistent layout that exists -- whatever
    /// `sample_size` or `scan_budget` were, they did not need to bind.
    /// `is_sample` is false.
    SourceExhausted,
    /// `sample_size` was reached before the source was exhausted. More
    /// consistent layouts may exist beyond where scanning stopped.
    /// `is_sample` is true.
    SampleFilled,
    /// `scan_budget` was reached before `sample_size` (or with no
    /// `sample_size` set) and before the source was exhausted -- a
    /// degraded draw, not a failure, provided at least one layout survived
    /// (see `RootFailure::ScanBudgetExhausted` for when none did).
    /// `is_sample` is true.
    BudgetExhausted,
};

/// `make_root`'s own result: the node on success, or `std::nullopt` paired
/// with the specific reason it could not be built. `node` and `failure`
/// disagree only in the way `EvaluationResult::by_strategy` and `error` do —
/// `node.has_value()` and `failure == RootFailure::None` always agree.
/// `outcome` is meaningful only on success (`node.has_value()`) — why the
/// scan stopped where it did, not just whether it succeeded.
struct RootConstructionResult
{
    std::optional<BeliefNode> node;
    RootFailure failure = RootFailure::None;
    ScanOutcome outcome = ScanOutcome::SourceExhausted;
};

/// `make_root`'s optional behaviour, distinct from `EvaluateOptions`
/// (`evaluate()`'s own parameter): `make_root` is usable standalone
/// (`node_test.cpp` depends on this), and most of `EvaluateOptions` —
/// `bound`, `delta_is_double_dummy_optimal`, `retain_root` — means nothing
/// at this layer, so threading that whole type down here would couple
/// `make_root` to fields it never reads.
struct RootOptions
{
    /// Cap the number of layouts drawn from `source`, absent for exhaustive
    /// enumeration (every consistent layout). See `make_root`'s own
    /// doxygen for the exact scanning behaviour and what this does to
    /// `is_sample`.
    std::optional<std::uint64_t> sample_size;

    /// Cap the number of `source.at()` calls the scan may make, absent for
    /// an unbounded scan. Counted in `at()` calls specifically, not
    /// consistent layouts found and not loop iterations that happen to be
    /// cheap: `at()` is the expensive operation (virtual, user-implemented,
    /// in production likely to build a `Deal`), and it is what a future
    /// scan-to-hit measurement counts too, so counting anything else here
    /// would make the two incommensurable. Applied whether or not
    /// `sample_size` is set: `make_root` checks it at every step regardless,
    /// so a `scan_budget` narrower than `source.size()` binds on its own and
    /// degrades the root even with no `sample_size` — see
    /// `RootFailure::ScanBudgetExhausted` for the case where it binds before
    /// any layout survives. It only fails to matter when it is wide enough
    /// that the source would exhaust first regardless of what `sample_size`
    /// is, which is the same "wide enough not to bind" case `sample_size`
    /// itself has.
    std::optional<std::uint64_t> scan_budget;
};

/// Builds the root node over `source`: scans from index 0 and takes every
/// layout consistent with `root_layout` (see below), each getting `p_i = 1`,
/// up to `options.sample_size` if one is supplied and up to
/// `options.scan_budget` calls to `source.at()` if one is supplied — both
/// absent, every consistent layout is taken, the exhaustive case.
/// `kappa = 1 / node.layouts.size()` in every case: neither cap ever
/// reaches the weight, so a node's mass is always exactly 1 regardless of
/// how many layouts it actually holds or why the scan stopped drawing them.
///
/// `result.outcome` reports why the scan stopped — see `ScanOutcome` — and
/// `node.is_sample` is true exactly when `outcome != SourceExhausted`: the
/// scan stopped **because** a cap bound, not merely because one was
/// supplied. A `sample_size` of `M >= N` (N being however many layouts
/// actually survive filtering) takes the whole consistent set in source
/// order before either cap ever binds, so `outcome` is `SourceExhausted`,
/// `is_sample` is false, and the result is byte-for-byte the exhaustive
/// one: same layouts, same order, same `kappa`. This is why `is_sample`
/// cannot be `options.sample_size.has_value()` directly — that would
/// report a sample on a node that genuinely holds the whole space, silently
/// breaking `space_size` and licensing tier 2's cut to switch off somewhere
/// it is still sound. The same reasoning applies to `scan_budget`: reaching
/// it exactly as the source also runs out is `SourceExhausted`, not
/// `BudgetExhausted` — there was nothing left to find regardless.
///
/// If both `sample_size` and `scan_budget` would bind at the same point,
/// `sample_size` wins and `outcome` is `SampleFilled`: the scan is checked
/// against `sample_size` first at each step, so a layout that fills the
/// sample is never charged against the budget.
///
/// No seed anywhere in this function or `RootOptions`: the caller's
/// `source` is the only source of randomness a sampled draw can have (see
/// `LayoutSource::at`'s own doxygen) — scanning from index 0 is a
/// deterministic prefix of whatever order `source` already presents.
///
/// A candidate layout is consistent with `root_layout` when it shares
/// `root_layout`'s trump, `first`, and current-trick state exactly; shares
/// `declarer`'s and dummy's (`(declarer + 2) % DDS_HANDS`) remaining
/// holdings exactly; and its two defenders' holdings partition the same
/// outstanding pool, suit by suit, as `root_layout`'s defenders do — the two
/// defenders may differ in *how* the pool is split between them, but not in
/// what the pool is. That last part is what keeps the node's aggregate
/// (`RankMap::aggr`) invariant across every layout, which is what licenses
/// one renumbering for the whole node.
///
/// The result carries no node, with a specific `RootFailure`, rather than
/// asserting — `source` is user-supplied — when `source.size()` is
/// `std::nullopt`; when no layout survives filtering after the whole source
/// was scanned (`NoLayoutSurvived`); or when `scan_budget` ran out before a
/// single consistent layout was found, with the source not yet exhausted
/// (`ScanBudgetExhausted`, distinct from `NoLayoutSurvived` — see that
/// value's own doxygen). A budget that ran out but still found at least one
/// layout is not a failure at all: it returns a node, degraded, with
/// `outcome == BudgetExhausted`.
auto make_root(
    Deal const& root_layout,
    int declarer,
    int tricks_needed,
    LayoutSource const& source,
    RootOptions const& options = {}) -> RootConstructionResult;

/// kappa * Sigma_i p_i, accumulated through KahanAccumulator. The node's
/// total probability mass, independent of what tricks_won_by_declarer says
/// about it.
auto node_mass(BeliefNode const& node) -> double;

/// The value of a node with no cards left to play: node_mass(node) if
/// declarer has already banked tricks_needed, else 0. tricks_won_by_declarer
/// lives only on node.state — every card played is observed, so it is
/// identical across every layout in the node (the same fact
/// algorithm.md notes about the indicator being constant on A_tau) — so
/// there is no per-layout trick count to read here even by mistake; the
/// per-layout indicator form this guards against cannot compile.
auto terminal_value(BeliefNode const& node) -> double;

/// True when no hand in the node has a card left. node.layouts is never
/// empty (make_root and every child-construction function guarantee at
/// least one layout survives), and every layout in a node shares the same
/// outstanding pool per suit (the aggr invariant), so an empty pool leaves
/// every layout — not just the first — with every hand empty; checking one
/// representative layout is sufficient.
auto is_terminal(BeliefNode const& node) -> bool;

/// How many more tricks remain to be played out from `state`, common
/// knowledge and identical across every layout the node holds (the
/// outstanding pool is layout-invariant, same as `RankMap::aggr`).
///
/// Derived from declarer's own holding in `state.known_holdings` — exact,
/// unlike a defender's entry, which is the union pool the two defenders
/// share (see `ObservationState::known_holdings`'s own doxygen); summing
/// across all four entries would double-count every outstanding card.
/// Declarer's card count equals tricks remaining exactly at a trick
/// boundary, but is one short of it mid-trick if declarer has *already*
/// played to the trick in progress — determined from `state.known_holdings`'s
/// own `first` (the current trick's leader; see `trick.hpp`) and how many
/// cards are already in it, not from `state.first` (the root's leader,
/// unrelated once play has moved on).
auto tricks_remaining(ObservationState const& state) -> int;

}  // namespace dds::belief_evaluation
