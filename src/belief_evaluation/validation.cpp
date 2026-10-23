#include <belief_evaluation/validation.hpp>

#include <cmath>

namespace dds::belief_evaluation
{

namespace
{
    auto is_held(Deal const& deal, int seat, Card const& card) -> bool
    {
        // A malformed seat, suit or rank would index remainCards out of bounds
        // and shift by an invalid amount. Reject it as CardNotHeld: no holding
        // could contain it.
        if (seat < 0 || seat >= DDS_HANDS || card.suit < 0 || card.suit >= DDS_SUITS
            || card.rank < 2 || card.rank > 14)
        {
            return false;
        }
        unsigned const holding = deal.remainCards[seat][card.suit];
        return (holding & (1u << card.rank)) != 0;
    }

    /// -1 if no card has been played to the current trick, else the suit of
    /// the first card. Also -1 for a suit outside 0..DDS_SUITS, so nothing
    /// indexes remainCards with it.
    auto led_suit(Deal const& deal) -> int
    {
        if (deal.currentTrickRank[0] == 0)
        {
            return -1;
        }
        int const suit = deal.currentTrickSuit[0];
        if (suit < 0 || suit >= DDS_SUITS)
        {
            return -1;
        }
        return suit;
    }

    /// Whether `card`, held by `seat`, follows suit in the current trick.
    /// Used by declarer and defender validation.
    auto follows_suit(Deal const& deal, int seat, Card const& card) -> bool
    {
        int const led = led_suit(deal);
        return led == -1 || led == card.suit || deal.remainCards[seat][led] == 0;
    }
}

auto validate_declarer_card(Deal const& deal, int seat, Card const& card) -> ValidationError
{
    if (! is_held(deal, seat, card))
    {
        return ValidationError::CardNotHeld;
    }

    if (! follows_suit(deal, seat, card))
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
    // An empty distribution never reaches the is_held() loop. Without this,
    // a bad seat would be reported as ProbabilitiesDoNotSumToOne.
    // validate_declarer_card reports a bad seat as CardNotHeld; so does this.
    if (seat < 0 || seat >= DDS_HANDS)
    {
        return ValidationError::CardNotHeld;
    }

    double total = 0.0;
    for (auto const& entry : distribution)
    {
        if (! is_held(layout, seat, entry.card))
        {
            return ValidationError::CardNotHeld;
        }
        if (! follows_suit(layout, seat, entry.card))
        {
            return ValidationError::CardIllegalForTrick;
        }
        // NaN fails every comparison, so `<= 0.0` lets it through, and it would
        // poison the sum. +-Inf is not a probability either. Reject every
        // non-finite value as ProbabilityNonPositive.
        if (! std::isfinite(entry.probability) || entry.probability <= 0.0)
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

}  // namespace dds::belief_evaluation
