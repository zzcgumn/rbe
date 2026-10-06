#include <belief_evaluation/high_in_third.hpp>

#include <api/dds_constants.hpp>

namespace dds::belief_evaluation
{

namespace
{
    // dummy's played card to this trick, read by position rather than by
    // currentTrick's play-order index directly, since dummy's seat -- not
    // its position in this particular trick -- is what the caller knows.
    auto dummy_played_rank(DefenderHeuristicContext const& ctx) -> int
    {
        int const dummy_seat = (ctx.state.declarer + 2) % DDS_HANDS;
        int const dummy_position = (dummy_seat - ctx.on_lead_to_trick + DDS_HANDS) % DDS_HANDS;
        return ctx.layout.currentTrickRank[dummy_position];
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
        if (highest.rank <= dummy_played_rank(ctx))
        {
            return std::nullopt;
        }
        return highest;
    };
}

}  // namespace dds::belief_evaluation
