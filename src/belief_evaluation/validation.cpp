#include <belief_evaluation/validation.hpp>

#include <cmath>

namespace
{
    constexpr double ProbabilitySumTolerance = 1e-6;

    auto is_held(Deal const& deal, int seat, Card const& card) -> bool
    {
        unsigned const holding = deal.remainCards[seat][card.suit];
        return (holding & (1u << card.rank)) != 0;
    }

    /// -1 if no card has yet been played to the trick in progress, else the
    /// suit of the first card played (currentTrickSuit[0]).
    auto led_suit(Deal const& deal) -> int
    {
        if (deal.currentTrickRank[0] == 0)
        {
            return -1;
        }
        return deal.currentTrickSuit[0];
    }
}

auto validate_declarer_card(Deal const& deal, int seat, Card const& card) -> ValidationError
{
    if (! is_held(deal, seat, card))
    {
        return ValidationError::CardNotHeld;
    }

    int const led = led_suit(deal);
    if (led != -1 && led != card.suit && deal.remainCards[seat][led] != 0)
    {
        return ValidationError::CardIllegalForTrick;
    }

    return ValidationError::None;
}

auto validate_defender_distribution(
    Deal const& layout,
    int seat,
    std::vector<WeightedCard> const& distribution) -> ValidationError
{
    double total = 0.0;
    for (auto const& entry : distribution)
    {
        if (! is_held(layout, seat, entry.card))
        {
            return ValidationError::CardNotHeld;
        }
        if (entry.probability <= 0.0)
        {
            return ValidationError::ProbabilityNonPositive;
        }
        total += entry.probability;
    }

    if (std::abs(total - 1.0) > ProbabilitySumTolerance)
    {
        return ValidationError::ProbabilitiesDoNotSumToOne;
    }

    return ValidationError::None;
}
