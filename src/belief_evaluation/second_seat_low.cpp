#include <belief_evaluation/second_seat_low.hpp>

namespace dds::belief_evaluation
{

namespace
{
    // Whether best_cards[lowest_index] touches another same-suit entry,
    // per ctx.fut.equals -- checked in both directions (the lowest's own
    // equals naming the other, or the other's equals naming the lowest),
    // since dds reports a touching group's membership on whichever entry
    // it treats as that group's representative, not necessarily the
    // lowest-ranked one.
    auto touches_another_same_suit_candidate(
        std::vector<Card> const& best_cards, FutureTricks const& fut, std::size_t lowest_index) -> bool
    {
        Card const& lowest = best_cards[lowest_index];
        for (std::size_t j = 0; j < best_cards.size(); ++j)
        {
            if (j == lowest_index || best_cards[j].suit != lowest.suit)
            {
                continue;
            }
            bool const lowest_names_other = (fut.equals[lowest_index] & (1u << best_cards[j].rank)) != 0;
            bool const other_names_lowest = (fut.equals[j] & (1u << lowest.rank)) != 0;
            if (lowest_names_other || other_names_lowest)
            {
                return true;
            }
        }
        return false;
    }
}

auto second_seat_low(bool randomise_touching_honours) -> DefenderHeuristic
{
    return [randomise_touching_honours](
               DefenderHeuristicContext const& ctx, std::vector<Card> const& best_cards) -> std::optional<Card>
    {
        if (ctx.position_in_trick != 1 || !ctx.can_follow_led_suit || !ctx.defending_side)
        {
            return std::nullopt;
        }
        if (best_cards.empty())
        {
            return std::nullopt;
        }

        std::size_t lowest_index = 0;
        for (std::size_t i = 1; i < best_cards.size(); ++i)
        {
            if (best_cards[i].rank < best_cards[lowest_index].rank)
            {
                lowest_index = i;
            }
        }

        if (randomise_touching_honours && touches_another_same_suit_candidate(best_cards, ctx.fut, lowest_index))
        {
            return std::nullopt;
        }

        return best_cards[lowest_index];
    };
}

}  // namespace dds::belief_evaluation
