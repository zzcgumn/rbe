#include <belief_evaluation/high_in_third.hpp>

#include <api/dds_constants.hpp>

namespace dds::belief_evaluation
{

namespace
{
    // The rank of the card at trick index 1. In third seat after partner's
    // lead, that player is declarer or dummy, depending on the deal. So read
    // index 1 directly, not a derived seat.
    //
    // The effective rank, because the second player may not have followed: a
    // ruff beats every candidate, and a discard of a third suit beats none.
    auto second_hand_effective_rank(DefenderHeuristicContext const& ctx) -> int
    {
        constexpr int kBeatsNothing = 0;    // lower than any real rank (2..14)
        constexpr int kBeatsEverything = 100;  // higher than any real rank

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
}

auto high_in_third() -> DefenderHeuristic
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
        if (highest.rank <= second_hand_effective_rank(ctx))
        {
            return std::nullopt;
        }
        return highest;
    };
}

}  // namespace dds::belief_evaluation
