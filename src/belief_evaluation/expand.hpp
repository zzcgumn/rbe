#pragma once

#include <optional>
#include <vector>

#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/validation.hpp>

/// The result of expanding one node: either the child, or the
/// ValidationError a callback's return violated. Provisional — task 08
/// unifies error propagation (and the extra context it carries: which
/// callback, seat and layout) across the whole evaluator; this exists so
/// that expansion reports a bad callback return rather than asserting it,
/// consistent with validation.hpp's own contract.
struct ExpandResult
{
    std::optional<BeliefNode> child;
    ValidationError error = ValidationError::None;  ///< meaningful only when child is nullopt
};

/// Expands a node where declarer or dummy is on play (`seat_on_play` on
/// `node.state.known_holdings`): calls `pi` once, validates the card it
/// returns through `validate_declarer_card`, and — if valid — builds the
/// single child via `make_declarer_children`.
auto expand_declarer_node(BeliefNode const& node, DeclarerStrategy const& pi) -> ExpandResult;

/// The mass-propagation step declarer children go through, isolated so it
/// can be pinned by a direct unit test rather than only by the evaluator
/// (which, in version one, only ever calls this with a single card and so
/// cannot distinguish giving each child the parent's full mass from
/// dividing it — `parent_mass / n_children` equals `parent_mass` whenever
/// `n_children == 1`). Every returned child receives `parent`'s full mass:
/// declarer's and dummy's cards are public, so declarer's play neither
/// filters nor reweights the belief space, unlike a defender's (see
/// defender-node expansion, which does partition mass across children).
///
/// Each child's `p` and `kappa` are exactly `parent`'s; each layout is
/// `parent`'s corresponding layout advanced by the given card via
/// `play()`, so the belief set's size and per-layout correspondence carry
/// over untouched even though the layouts themselves record one more
/// publicly-known card played.
auto make_declarer_children(BeliefNode const& parent, std::vector<Card> const& cards)
    -> std::vector<BeliefNode>;
