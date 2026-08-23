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
