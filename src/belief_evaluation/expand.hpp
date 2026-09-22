#pragma once

#include <optional>
#include <vector>

#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/validation.hpp>

namespace dds::belief_evaluation
{

/// `state` advanced by `seat_on_play(state.known_holdings)` playing `card`:
/// `known_holdings` and `ranks` updated via `play()`, the card appended to
/// `history`, and `tricks_won_by_declarer` incremented if the play resolved
/// a trick declarer's or dummy's way. `trump`, `declarer`, `tricks_needed`
/// and `first` are unaffected by a single card.
///
/// Exposed so a node-local replay can rebuild the intermediate states δ was
/// asked about during the original expansion. The recursion retains none of
/// them, so replaying from the root's state is the only way to recover
/// them.
auto advance_state(ObservationState const& state, Card const& card) -> ObservationState;

/// The result of expanding one node: either the child, or the
/// `ValidationError` a callback's return violated. A local, minimal shape —
/// `EvaluationError` is what carries the fuller context across the whole
/// evaluator.
struct ExpandResult
{
    std::optional<BeliefNode> child;
    Card card{};                                     ///< the card pi returned; meaningful only when child has a value
    ValidationError error = ValidationError::None;  ///< meaningful only when child is nullopt
};

/// Expands a node where declarer or dummy is on play: calls `pi` once,
/// validates the card through `validate_declarer_card`, and builds the
/// single child via `make_declarer_children`.
auto expand_declarer_node(BeliefNode const& node, DeclarerStrategy const& pi) -> ExpandResult;

/// The mass-propagation step declarer children go through, isolated so a
/// unit test can pin it: the evaluator only ever calls it with one card, so
/// it cannot distinguish giving each child the parent's full mass from
/// dividing it.
///
/// Every child receives `parent`'s full mass. Declarer's and dummy's cards
/// are public, so declarer's play neither filters nor reweights the belief
/// space — unlike a defender's. Each child's `p` and `kappa` are exactly
/// `parent`'s, and each layout is the corresponding parent layout advanced
/// by the card.
auto make_declarer_children(BeliefNode const& parent, std::vector<Card> const& cards)
    -> std::vector<BeliefNode>;

/// The result of expanding a defender node: either the children, or the
/// `ValidationError` a callback's return violated.
struct ExpandDefenderResult
{
    std::optional<std::vector<BeliefNode>> children;
    ValidationError error = ValidationError::None;  ///< meaningful only when children is nullopt
    Deal offending_layout{};  ///< the layout whose distribution violated the contract; meaningful only on error
};

/// Expands a node where a defender is on play: calls `delta` once per
/// layout and groups the results by card. Child `C_a` holds
/// `{B_i : delta(C_a | B_i) > 0}` with `p_i' = p_i * delta(C_a | B_i)` — a
/// layout `delta` gives no probability to is absent from that child rather
/// than present at `p = 0`, and one layout may appear in several children.
/// `kappa` is unchanged in every child: defender children partition `p`,
/// not `kappa`.
///
/// An empty distribution is reported as `ValidationError::DistributionEmpty`
/// before `validate_defender_distribution` is called — see that enum value.
/// Every other distribution goes through that function, and a violation
/// aborts expansion and is reported rather than asserted.
auto expand_defender_node(BeliefNode const& node, DefenderStrategy const& delta)
    -> ExpandDefenderResult;

}  // namespace dds::belief_evaluation
