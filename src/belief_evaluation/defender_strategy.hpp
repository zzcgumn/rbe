#pragma once

#include <functional>
#include <vector>

#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// One card a defender might play, and the probability the defender model
/// assigns to it in this layout.
struct WeightedCard
{
    Card card;
    Probability probability;
};

/// Everything one call to a defender strategy needs. `layout` is the actual
/// layout — perfect information — because this contract currently models
/// perfect-information defenders only.
struct DefenderQuery
{
    Deal const& layout;
    int seat;                       ///< which defender is being asked
    ObservationState const& state;  ///< what is commonly known
};

/// The defender model. Both defenders use the same strategy but feed it
/// different information (`DefenderQuery::seat`) to reach a decision.
///
/// Returns a distribution rather than a single card, so that randomisation
/// between double-dummy-equivalent cards (restricted choice) is expressible.
/// Every returned card must be held by `seat` in `layout` and legal there;
/// probabilities must be strictly positive and sum to one within tolerance.
/// A card the strategy will never play must be omitted, not given zero
/// probability — the evaluator treats "probability > 0" as the survival
/// test for a layout.
///
/// Does not receive declarer's strategy or belief space. That independence
/// is what licenses evaluating each node of the search in isolation.
using DefenderStrategy =
    std::function<std::vector<WeightedCard>(DefenderQuery const&)>;

}  // namespace dds::belief_evaluation
