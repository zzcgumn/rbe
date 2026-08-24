#pragma once

#include <optional>
#include <vector>

#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/defender_strategy.hpp>
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

/// The result of expanding a defender node: either the children (one per
/// card delta assigned positive probability to, across every layout), or
/// the ValidationError a callback's return violated. Provisional, as
/// ExpandResult above.
struct ExpandDefenderResult
{
    std::optional<std::vector<BeliefNode>> children;
    ValidationError error = ValidationError::None;  ///< meaningful only when children is nullopt
};

/// Expands a node where a defender is on play (`seat_on_play` on
/// `node.state.known_holdings`): calls `delta` once per layout, and groups
/// the results by card. Child `C_a` holds `{B_i : delta(C_a | B_i) > 0}`
/// with `p_i' = p_i * delta(C_a | B_i)` — a layout `delta` gives no
/// probability to a card is absent from that card's child, not present
/// with `p = 0`, and a layout may legitimately appear in more than one
/// child. `kappa` is unchanged in every child; defender children partition
/// `p`, not `kappa`. Every distribution `delta` returns is checked through
/// `validate_defender_distribution`; a violation aborts expansion and is
/// reported via the result rather than asserted.
auto expand_defender_node(BeliefNode const& node, DefenderStrategy const& delta)
    -> ExpandDefenderResult;
