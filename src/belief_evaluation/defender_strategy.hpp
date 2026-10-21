#pragma once

#include <functional>
#include <vector>

#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// One card a defender might play, and its probability in this layout.
struct WeightedCard
{
    Card card;
    Probability probability;
};

/// The input to one defender strategy call. `layout` is the actual
/// layout: defenders have perfect information.
struct DefenderQuery
{
    Deal const& layout;
    int seat;                       ///< which defender is being asked
    ObservationState const& state;  ///< what is commonly known
};

/// The defender model. Both defenders use the same strategy, with a
/// different `DefenderQuery::seat`.
///
/// Returns a distribution, not a single card, so that it can randomise
/// between equivalent cards (restricted choice). The contract:
/// - `seat` holds every card in `layout`, and each is legal there;
/// - every probability is strictly positive;
/// - the probabilities sum to one within tolerance.
///
/// **Omit a card the strategy never plays.** Do not give it zero
/// probability: a layout survives only through cards with probability
/// above zero.
///
/// It does not see declarer's strategy or belief space. That is what lets
/// the evaluator treat each node on its own.
using DefenderStrategy =
    std::function<std::vector<WeightedCard>(DefenderQuery const&)>;

}  // namespace dds::belief_evaluation
