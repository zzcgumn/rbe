#pragma once

#include <vector>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/types.hpp>
#include <belief_evaluation/defender_strategy.hpp>

namespace dds::belief_evaluation
{

/// How far a defender distribution may sum from one and still pass
/// `validate_defender_distribution`. Code that sums many distributions
/// uses this constant too, scaled by the number summed.
constexpr double ProbabilitySumTolerance = 1e-6;

/// Why a callback's return was rejected. A callback is caller input, so
/// violations are reported, never asserted.
enum class ValidationError
{
    None,                        ///< the input is valid
    CardNotHeld,                 ///< the card is not in the seat's remaining holding
    CardIllegalForTrick,         ///< the seat holds the led suit but the card is of another suit
    ProbabilityNonPositive,      ///< a WeightedCard's probability is <= 0, NaN, or +-infinite
    ProbabilitiesDoNotSumToOne,  ///< the distribution's probabilities do not sum to 1 within tolerance
    /// The distribution is empty. Reported by `expand_defender_node`,
    /// which checks first: "you returned nothing" is clearer than "your
    /// probabilities sum to 0". `validate_defender_distribution` on its
    /// own reports ProbabilitiesDoNotSumToOne for an empty input.
    DistributionEmpty,
};

/// Checks a card returned by `DeclarerStrategy::play`: `seat` holds it in
/// `deal`, and it follows suit if `seat` can.
auto validate_declarer_card(Deal const& deal, int seat, Card const& card) -> ValidationError;

/// Checks a distribution returned by a defender strategy:
/// - `seat` holds every returned card in `layout`, and each follows suit
///   if `seat` can;
/// - every probability is strictly positive;
/// - the probabilities sum to 1 within `ProbabilitySumTolerance`.
///
/// Omit a card the strategy never plays. A zero probability is a contract
/// violation, not "never".
auto validate_defender_distribution(
    Deal const& layout,
    int seat,
    std::vector<WeightedCard> const& distribution) -> ValidationError;

}  // namespace dds::belief_evaluation
