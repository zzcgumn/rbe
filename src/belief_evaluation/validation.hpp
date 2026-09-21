#pragma once

#include <vector>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/types.hpp>
#include <belief_evaluation/defender_strategy.hpp>

namespace dds::belief_evaluation
{

/// How far a defender distribution's probabilities may sum from exactly
/// one and still be accepted by `validate_defender_distribution`. Shared
/// (not duplicated) with anything that needs to reason about the same
/// tolerance in aggregate — e.g. a check that sums a quantity derived from
/// several distributions' probabilities, where the worst-case discrepancy
/// scales with how many distributions are summed, not with this constant
/// alone.
constexpr double ProbabilitySumTolerance = 1e-6;

/// Distinct rejection reasons for a user-supplied callback return. A
/// user-supplied callback is an input, not an internal — violations are
/// reported, never asserted.
enum class ValidationError
{
    None,                        ///< the input is valid
    CardNotHeld,                 ///< the card is not in the seat's remaining holding
    CardIllegalForTrick,         ///< the seat holds the led suit but the card is of another suit
    ProbabilityNonPositive,      ///< a WeightedCard's probability is <= 0, NaN, or +-infinite
    ProbabilitiesDoNotSumToOne,  ///< the distribution's probabilities do not sum to 1 within tolerance
    /// The distribution is empty. `validate_defender_distribution` never
    /// reports this value itself -- an empty distribution fails its own
    /// sum-to-one check honestly, since zero genuinely is not one, so that
    /// function's answer for this input stays ProbabilitiesDoNotSumToOne.
    /// `expand_defender_node` (expand.hpp) draws this distinction before
    /// delegating to it: at the evaluator's boundary with a user callback
    /// the useful question is not "which invariant broke" but "what did
    /// the caller do wrong", and "you returned nothing" is more actionable
    /// than "your probabilities sum to 0". The two functions disagreeing
    /// on the same empty input is deliberate, not an inconsistency to
    /// reconcile.
    DistributionEmpty,
};

/// Validates a card a declarer strategy returned from `play`: held by `seat`
/// in `deal`, and legal for the trick in progress (must follow suit if
/// `seat` holds the led suit).
auto validate_declarer_card(Deal const& deal, int seat, Card const& card) -> ValidationError;

/// Validates a distribution a defender strategy returned: every card held by
/// `seat` in `layout` and legal for the trick in progress (must follow suit
/// if `seat` holds the led suit), every probability strictly positive, and
/// the probabilities summing to 1 within tolerance. A card the strategy will
/// never play must be omitted rather than given zero probability, so a
/// non-positive probability is a contract violation, not "never".
auto validate_defender_distribution(
    Deal const& layout,
    int seat,
    std::vector<WeightedCard> const& distribution) -> ValidationError;

}  // namespace dds::belief_evaluation
