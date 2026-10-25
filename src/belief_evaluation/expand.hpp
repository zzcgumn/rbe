#pragma once

#include <optional>
#include <vector>

#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/validation.hpp>

namespace dds::belief_evaluation
{

/// `state` after the seat on play (`seat_on_play(state.known_holdings)`)
/// plays `card`:
/// - `known_holdings` and `ranks` updated by `play()`;
/// - the card appended to `history`;
/// - `tricks_won_by_declarer` incremented if declarer or dummy won the
///   trick.
///
/// `trump`, `declarer`, `tricks_needed` and `first` do not change.
///
/// Public so that a node-local replay can rebuild the states δ saw. The
/// recursion keeps none of them.
auto advance_state(ObservationState const& state, Card const& card) -> ObservationState;

/// The result of expanding a declarer node: the child, or the
/// `ValidationError` that a callback's return broke. `EvaluationError`
/// carries the full context.
struct ExpandResult
{
    std::optional<BeliefNode> child;
    Card card{};                                     ///< the card pi returned; meaningful only when child has a value
    ValidationError error = ValidationError::None;  ///< meaningful only when child is nullopt

    /// Set only when `pi` threw `DeclarerStrategyContractViolation`. The
    /// seat and layout of *the delegate* that broke its contract.
    /// `evaluate.cpp` reports these instead of the node's, with
    /// `EvaluationCallback::DefenderStrategy`: the delegate is
    /// defender-shaped, though it is not the outer `delta`.
    std::optional<int> nested_seat;
    std::optional<Deal> nested_layout;
};

/// Expands a node where declarer or dummy is on play. Calls `pi` once,
/// checks the card with `validate_declarer_card`, and builds the one child
/// with `make_declarer_children`.
auto expand_declarer_node(BeliefNode const& node, DeclarerStrategy const& pi) -> ExpandResult;

/// Builds declarer children. Public so that a unit test can check the mass
/// rule: the evaluator always passes one card.
///
/// Every child gets the full mass of `parent`. Declarer's and dummy's
/// cards are public, so declarer's play does not filter or reweight the
/// belief space. Each child has the `p` and `kappa` of `parent`, with each
/// layout advanced by the card.
auto make_declarer_children(BeliefNode const& parent, std::vector<Card> const& cards)
    -> std::vector<BeliefNode>;

/// The result of expanding a defender node: the children, or the
/// `ValidationError` that a callback's return broke.
struct ExpandDefenderResult
{
    std::optional<std::vector<BeliefNode>> children;
    ValidationError error = ValidationError::None;  ///< meaningful only when children is nullopt
    Deal offending_layout{};  ///< the layout whose distribution broke the contract; meaningful only on error
};

/// Expands a node where a defender is on play. Calls `delta` once per
/// layout and groups the results by card. Child `C_a` holds
/// `{B_i : delta(C_a | B_i) > 0}`, with `p_i' = p_i * delta(C_a | B_i)`.
/// A layout with zero probability for a card is absent from that child.
/// One layout can be in several children. `kappa` is the same in every
/// child: defender children split `p`, not `kappa`.
///
/// An empty distribution is reported as
/// `ValidationError::DistributionEmpty`. Every other distribution goes
/// through `validate_defender_distribution`. A violation stops the
/// expansion and is reported, not asserted.
auto expand_defender_node(BeliefNode const& node, DefenderStrategy const& delta)
    -> ExpandDefenderResult;

}  // namespace dds::belief_evaluation
