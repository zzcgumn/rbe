#include <belief_evaluation/validation.hpp>

#include <cmath>

namespace dds::belief_evaluation
{

namespace
{
    auto is_held(Deal const& deal, int seat, Card const& card) -> bool
    {
        // A malformed seat/suit/rank cannot possibly be held — but without
        // this guard it would index deal.remainCards out of bounds and shift
        // by an out-of-range amount, both undefined behaviour, before ever
        // reaching a "not held" answer. An invalid card is rejected the same
        // way an absent one is: CardNotHeld, since no valid holding could
        // ever contain it.
        if (seat < 0 || seat >= DDS_HANDS || card.suit < 0 || card.suit >= DDS_SUITS
            || card.rank < 2 || card.rank > 14)
        {
            return false;
        }
        unsigned const holding = deal.remainCards[seat][card.suit];
        return (holding & (1u << card.rank)) != 0;
    }

    /// -1 if no card has yet been played to the trick in progress, else the
    /// suit of the first card played (currentTrickSuit[0]). Also -1 for a
    /// malformed currentTrickSuit[0] (outside 0..DDS_SUITS): deal is not a
    /// user-supplied value validated here, but nothing downstream should
    /// index remainCards by an unguarded suit either.
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

    /// Whether `card` is legal for the trick currently in progress in `deal`,
    /// for a seat that holds `card`: must follow the led suit if `seat`
    /// holds any card of it. Shared by declarer and defender validation.
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
    // Every entry is checked against `seat` via is_held() below, but an
    // empty distribution never enters that loop — without this upfront
    // check an invalid seat with no cards to report would fall through to
    // ProbabilitiesDoNotSumToOne, misleadingly naming the wrong problem and
    // disagreeing with validate_declarer_card, which always reports an
    // invalid seat as CardNotHeld regardless of the card.
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
        // NaN fails every comparison, so an unguarded `<= 0.0` check lets it
        // through, and the sum-tolerance check below would too (NaN also
        // fails `>`) once it has poisoned `total`. +-Inf is a positive
        // number by that same `<= 0.0` test but is not a legitimate
        // probability either. Reject all non-finite values up front,
        // reusing ProbabilityNonPositive since none of them is one.
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
