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
    /// than the whole of it. Currently always false — nothing in this
    /// exhaustive evaluator samples — but a stored, propagated field rather
    /// than a literal at each call site, so a future sampling evaluator has
    /// one place to set it true instead of every construction site to hunt
    /// down. See BeliefView::is_sample, which this feeds.
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
    SourceNotEnumerable,  ///< source.size() is std::nullopt
    NoLayoutSurvived,      ///< source.size() had a value, but no candidate passed the consistency filter
};

/// `make_root`'s own result: the node on success, or `std::nullopt` paired
/// with the specific reason it could not be built. `node` and `failure`
/// disagree only in the way `EvaluationResult::by_strategy` and `error` do —
/// `node.has_value()` and `failure == RootFailure::None` always agree.
struct RootConstructionResult
{
    std::optional<BeliefNode> node;
    RootFailure failure = RootFailure::None;
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
};

/// Builds the root node over `source`: scans from index 0 and takes every
/// layout consistent with `root_layout` (see below), each getting `p_i = 1`,
/// up to `options.sample_size` if one is supplied — absent, every consistent
/// layout is taken, the exhaustive case. `kappa = 1 / node.layouts.size()`
/// either way: M caps the loop, it never reaches the weight, so a node's
/// mass is always exactly 1 regardless of how many layouts it actually
/// holds.
///
/// `node.is_sample` is true exactly when the scan stopped **because** the
/// cap was reached, not merely because a cap was supplied — reaching
/// `options.sample_size` with the scan not yet at `source`'s end. A sample
/// size of `M >= N` (N being however many layouts actually survive
/// filtering) takes the whole consistent set in source order before the cap
/// ever binds, so `is_sample` is false and the result is byte-for-byte the
/// exhaustive one: same layouts, same order, same `kappa`. This is why
/// `is_sample` cannot be `options.sample_size.has_value()` directly — that
/// would report a sample on a node that genuinely holds the whole space,
/// silently breaking `space_size` and licensing tier 2's cut to switch off
/// somewhere it is still sound.
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
/// `std::nullopt` (no bound to enumerate, or to scan a prefix of, without
/// one), or when no layout survives filtering (whether or not a sample size
/// was requested — an empty result is an empty result either way).
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
