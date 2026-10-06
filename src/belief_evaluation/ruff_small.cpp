#include <belief_evaluation/ruff_small.hpp>

#include <api/dds_constants.hpp>

#include <belief_evaluation/touching_group.hpp>

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

        // Every candidate considered here is confined to one suit
        // (ctx.trump), so two touching runs among them are disjoint,
        // ordered intervals -- comparing by raw (representative) rank
        // already finds the right entry; only the rank ultimately played
        // (used below for the card returned and the overruff check) needs
        // resolving against ctx.fut.equals (see true_lowest_rank's own
        // doxygen).
        std::optional<std::size_t> smallest_trump_index;
        for (std::size_t i = 0; i < best_cards.size(); ++i)
        {
            if (best_cards[i].suit != ctx.trump)
            {
                continue;
            }
            if (!smallest_trump_index || best_cards[i].rank < best_cards[*smallest_trump_index].rank)
            {
                smallest_trump_index = i;
            }
        }
        if (!smallest_trump_index)
        {
            return std::nullopt;
        }
        int const smallest_trump_rank = true_lowest_rank(best_cards, ctx.fut, *smallest_trump_index);

        int const led_suit = ctx.layout.currentTrickSuit[0];
        unsigned const higher_trump_mask = ~((1u << (smallest_trump_rank + 1)) - 1);

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

        return Card{best_cards[*smallest_trump_index].suit, smallest_trump_rank};
    };
}

}  // namespace dds::belief_evaluation
