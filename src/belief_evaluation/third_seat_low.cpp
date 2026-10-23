#include <belief_evaluation/third_seat_low.hpp>

#include <api/dds_constants.hpp>

#include <belief_evaluation/touching_group.hpp>

namespace dds::belief_evaluation
{

namespace
{
    // A copy of the function in high_in_third.cpp: each rule is a small,
    // separate file. Reads trick index 1 directly, and uses the effective
    // rank. See high_in_third.cpp.
    auto second_hand_effective_rank(DefenderHeuristicContext const& ctx) -> int
    {
        constexpr int kBeatsNothing = 0;
        constexpr int kBeatsEverything = 100;

        int const led_suit = ctx.layout.currentTrickSuit[0];
        int const second_hand_suit = ctx.layout.currentTrickSuit[1];
        if (second_hand_suit == led_suit)
        {
            return ctx.layout.currentTrickRank[1];
        }
        if (ctx.trump != DDS_NOTRUMP && second_hand_suit == ctx.trump)
        {
            return kBeatsEverything;
        }
        return kBeatsNothing;
    }

    auto highest_candidate_index(std::vector<Card> const& best_cards) -> std::size_t
    {
        std::size_t highest = 0;
        for (std::size_t i = 1; i < best_cards.size(); ++i)
        {
            if (best_cards[i].rank > best_cards[highest].rank)
            {
                highest = i;
            }
        }
        return highest;
    }

    // All candidates are in the led suit, so raw rank finds the right entry.
    // See true_lowest_rank.
    auto lowest_candidate_index(std::vector<Card> const& best_cards) -> std::size_t
    {
        std::size_t lowest = 0;
        for (std::size_t i = 1; i < best_cards.size(); ++i)
        {
            if (best_cards[i].rank < best_cards[lowest].rank)
            {
                lowest = i;
            }
        }
        return lowest;
    }
}

auto third_seat_low() -> DefenderHeuristic
{
    return [](DefenderHeuristicContext const& ctx, std::vector<Card> const& best_cards) -> std::optional<Card>
    {
        int const partner = (ctx.seat + 2) % DDS_HANDS;
        if (ctx.position_in_trick != 2 || !ctx.defending_side || !ctx.can_follow_led_suit
            || ctx.on_lead_to_trick != partner)
        {
            return std::nullopt;
        }
        if (best_cards.empty())
        {
            return std::nullopt;
        }

        Card const& highest = best_cards[highest_candidate_index(best_cards)];
        if (highest.rank > second_hand_effective_rank(ctx))
        {
            return std::nullopt;  // the high_in_third case: defer to it
        }
        std::size_t const lowest = lowest_candidate_index(best_cards);
        return Card{best_cards[lowest].suit, true_lowest_rank(best_cards, ctx.fut, lowest)};
    };
}

}  // namespace dds::belief_evaluation
