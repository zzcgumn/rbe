#include <belief_evaluation/third_seat_low.hpp>

#include <api/dds_constants.hpp>

#include <belief_evaluation/touching_group.hpp>

namespace dds::belief_evaluation
{

namespace
{
    // Duplicated from high_in_third.cpp rather than shared: each concrete
    // rule in this module is a small, independently-readable .cpp, kept
    // separate from its neighbours despite some overlap between them.
    //
    // The card played at the trick's second position -- always index 1,
    // never "dummy's card" specifically; see high_in_third.cpp's own
    // comment on this same function for why (a prior version derived
    // dummy's seat and assumed its position, which could read past
    // currentTrickRank's bound). Effective rank, not the raw one, since
    // the second player may have ruffed (beats every candidate) or
    // discarded a third suit (beats none) instead of following suit.
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

    // All candidates follow the same led suit here, so comparing by raw
    // (representative) rank already finds the right entry -- see
    // true_lowest_rank's own doxygen for why the search itself needs no
    // resolving, only the value ultimately returned for it.
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
            return std::nullopt;  // high_in_third's own case: defer to it
        }
        std::size_t const lowest = lowest_candidate_index(best_cards);
        return Card{best_cards[lowest].suit, true_lowest_rank(best_cards, ctx.fut, lowest)};
    };
}

}  // namespace dds::belief_evaluation
