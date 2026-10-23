#include <belief_evaluation/fourth_seat_low.hpp>

#include <belief_evaluation/touching_group.hpp>

namespace dds::belief_evaluation
{

auto fourth_seat_low() -> DefenderHeuristic
{
    return [](DefenderHeuristicContext const& ctx, std::vector<Card> const& best_cards) -> std::optional<Card>
    {
        if (ctx.position_in_trick != 3 || !ctx.can_follow_led_suit || !ctx.defending_side)
        {
            return std::nullopt;
        }
        if (best_cards.empty())
        {
            return std::nullopt;
        }

        // All candidates are in the led suit, so raw rank finds the right entry.
        // Only the rank returned needs resolving (see true_lowest_rank).
        std::size_t lowest = 0;
        for (std::size_t i = 1; i < best_cards.size(); ++i)
        {
            if (best_cards[i].rank < best_cards[lowest].rank)
            {
                lowest = i;
            }
        }
        return Card{best_cards[lowest].suit, true_lowest_rank(best_cards, ctx.fut, lowest)};
    };
}

}  // namespace dds::belief_evaluation
