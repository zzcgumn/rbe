#pragma once

#include <vector>

#include <api/dll.h>

#include <belief_evaluation/types.hpp>
#include <belief_evaluation/defender_strategy.hpp>

/// Distinct rejection reasons for a user-supplied callback return. A
/// user-supplied callback is an input, not an internal — violations are
/// reported, never asserted.
enum class ValidationError
{
    None,                        ///< the input is valid
    CardNotHeld,                 ///< the card is not in the seat's remaining holding
    CardIllegalForTrick,         ///< the seat holds the led suit but the card is of another suit
    ProbabilityNonPositive,      ///< a WeightedCard's probability is <= 0
    ProbabilitiesDoNotSumToOne,  ///< the distribution's probabilities do not sum to 1 within tolerance
};

/// Validates a card a declarer strategy returned from `play`: held by `seat`
/// in `deal`, and legal for the trick in progress (must follow suit if
/// `seat` holds the led suit).
auto validate_declarer_card(Deal const& deal, int seat, Card const& card) -> ValidationError;

/// Validates a distribution a defender strategy returned: every card held by
/// `seat` in `layout`, every probability strictly positive, and the
/// probabilities summing to 1 within tolerance. A card the strategy will
/// never play must be omitted rather than given zero probability, so a
/// non-positive probability is a contract violation, not "never".
auto validate_defender_distribution(
    Deal const& layout,
    int seat,
    std::vector<WeightedCard> const& distribution) -> ValidationError;
