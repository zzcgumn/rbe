#include <belief_evaluation/second_seat_low.hpp>

#include <belief_evaluation/spread.hpp>
#include <belief_evaluation/touching_group.hpp>

namespace dds::belief_evaluation
{

namespace
{
    // Whether best_cards[lowest_index] touches another card of its suit, by
    // ctx.fut.equals. Two cases:
    //
    // 1. Its own equals entry is non-empty. solve_board reports a touching
    //    run as one entry (the highest card) with the rest in equals. So
    //    best_cards may hold only that entry. true_lowest_rank resolves its
    //    rank.
    // 2. A caller lists the group as separate best_cards entries. Checked in
    //    both directions, because dds may record the group on any member.
    //    Here best_cards[lowest_index].rank is already the lowest rank.
    struct TouchingResult
    {
        bool touches;
        int lowest_rank;
    };

    auto touching_group_of(
        std::vector<Card> const& best_cards, FutureTricks const& fut, std::size_t lowest_index) -> TouchingResult
    {
        Card const& lowest = best_cards[lowest_index];

        if (lowest_index < static_cast<std::size_t>(fut.cards) && fut.equals[lowest_index] != 0)
        {
            return TouchingResult{true, true_lowest_rank(best_cards, fut, lowest_index)};
        }

        for (std::size_t j = 0; j < best_cards.size(); ++j)
        {
            if (j == lowest_index || best_cards[j].suit != lowest.suit)
            {
                continue;
            }
            bool const lowest_names_other =
                j < static_cast<std::size_t>(fut.cards) && (fut.equals[lowest_index] & (1u << best_cards[j].rank)) != 0;
            bool const other_names_lowest =
                j < static_cast<std::size_t>(fut.cards) && (fut.equals[j] & (1u << lowest.rank)) != 0;
            if (lowest_names_other || other_names_lowest)
            {
                return TouchingResult{true, lowest.rank};
            }
        }
        return TouchingResult{false, lowest.rank};
    }
}

auto second_seat_low(bool randomize_touching_honors) -> DefenderHeuristic
{
    return [randomize_touching_honors](
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

        TouchingResult const touching = touching_group_of(best_cards, ctx.fut, lowest_index);

        // Defer only if spread(fut, TouchingSequence) will pick this group.
        // fut may hold several tied groups (say A-K and a separate Q-J), and
        // spread() picks canonical_best(fut). Otherwise deferring would randomise
        // the wrong pair, or nothing. Then return the card that
        // randomize_touching_honors=false gives.
        bool const defer_reaches_this_group =
            canonical_best(ctx.fut) == static_cast<int>(lowest_index);

        // And only if the fallback is TouchingSequence. Under AllOptimal it
        // spreads over every tied card in every suit, which is not restricted
        // choice.
        bool const fallback_is_touching_sequence =
            ctx.fallback_policy == SpreadPolicy::TouchingSequence;

        if (randomize_touching_honors && touching.touches && defer_reaches_this_group
            && fallback_is_touching_sequence)
        {
            return std::nullopt;
        }

        return Card{best_cards[lowest_index].suit, touching.lowest_rank};
    };
}

}  // namespace dds::belief_evaluation
