#include <belief_evaluation/ruff_small.hpp>

#include <api/dds_constants.hpp>

namespace dds::belief_evaluation
{

auto ruff_small() -> DefenderHeuristic
{
    return [](DefenderHeuristicContext const& ctx, std::vector<Card> const& best_cards) -> std::optional<Card>
    {
        if (ctx.trump == DDS_NOTRUMP || ctx.was_on_lead || ctx.can_follow_led_suit)
        {
            return std::nullopt;
        }

        std::optional<Card> smallest_trump;
        for (Card const& card : best_cards)
        {
            if (card.suit != ctx.trump)
            {
                continue;
            }
            if (!smallest_trump || card.rank < smallest_trump->rank)
            {
                smallest_trump = card;
            }
        }
        if (!smallest_trump)
        {
            return std::nullopt;
        }

        int const led_suit = ctx.layout.currentTrickSuit[0];
        unsigned const higher_trump_mask = ~((1u << (smallest_trump->rank + 1)) - 1);

        for (int seat = 0; seat < DDS_HANDS; ++seat)
        {
            if (seat == ctx.seat)
            {
                continue;
            }
            int const position = (seat - ctx.on_lead_to_trick + DDS_HANDS) % DDS_HANDS;
            if (position <= ctx.position_in_trick)
            {
                continue;  // already played to this trick
            }
            bool const declaring_side = (seat % 2) == (ctx.state.declarer % 2);
            if (!declaring_side)
            {
                continue;
            }
            if (ctx.layout.remainCards[seat][led_suit] != 0)
            {
                continue;  // can follow suit, no overruff needed
            }
            if ((ctx.layout.remainCards[seat][ctx.trump] & higher_trump_mask) != 0)
            {
                return std::nullopt;  // this seat could overruff
            }
        }

        return smallest_trump;
    };
}

}  // namespace dds::belief_evaluation
