#pragma once

#include <optional>
#include <vector>

#include <api/dll.h>

#include <belief_evaluation/layout_source.hpp>
#include <belief_evaluation/types.hpp>

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
    std::vector<Deal> layouts;   ///< reserved up front; stable while any BeliefView over it is live
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

/// Builds the root node of an exhaustive evaluation over `source`: every
/// layout `source` enumerates that is consistent with `root_layout` (see
/// below) gets `p_i = 1`, and `kappa = 1 / N` where N is the number of
/// layouts that survive — not `source`'s raw size.
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
/// Returns `std::nullopt` rather than asserting — `source` is user-supplied
/// — when `source.size()` is `std::nullopt` (exhaustive evaluation has no
/// bound to enumerate without one), or when no layout survives filtering.
auto make_root(
    Deal const& root_layout,
    int declarer,
    int tricks_needed,
    LayoutSource const& source) -> std::optional<BeliefNode>;

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
