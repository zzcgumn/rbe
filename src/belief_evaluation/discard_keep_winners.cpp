#include <belief_evaluation/discard_keep_winners.hpp>

#include <belief_evaluation/suit_top_tricks.hpp>
#include <belief_evaluation/touching_group.hpp>

namespace dds::belief_evaluation
{

auto discard_keep_winners() -> DefenderHeuristic
{
    return [](DefenderHeuristicContext const& ctx, std::vector<Card> const& best_cards) -> std::optional<Card>
    {
        if (!ctx.defending_side || ctx.was_on_lead || ctx.can_follow_led_suit)
        {
            return std::nullopt;
        }
        if (best_cards.empty())
        {
            return std::nullopt;
        }

        std::array<int, 4> const tricks = suit_top_tricks(ctx.layout, ctx.seat, ctx.trump);

        // Unlike second/third/fourth-seat low, candidates here may come
        // from different suits, so two touching runs are not guaranteed
        // disjoint in rank the way they are within one suit -- the true
        // rank of each candidate must be resolved before comparing, not
        // only once a winning index is found (see true_lowest_rank's own
        // doxygen).
        std::size_t best_index = 0;
        int best_rank = true_lowest_rank(best_cards, ctx.fut, 0);
        for (std::size_t i = 1; i < best_cards.size(); ++i)
        {
            Card const& candidate = best_cards[i];
            Card const& current = best_cards[best_index];
            int const candidate_rank = true_lowest_rank(best_cards, ctx.fut, i);

            int const candidate_tricks = tricks[static_cast<std::size_t>(candidate.suit)];
            int const current_tricks = tricks[static_cast<std::size_t>(current.suit)];
            if (candidate_tricks != current_tricks)
            {
                if (candidate_tricks < current_tricks)
                {
                    best_index = i;
                    best_rank = candidate_rank;
                }
                continue;
            }

            if (candidate_rank != best_rank)
            {
                if (candidate_rank < best_rank)
                {
                    best_index = i;
                    best_rank = candidate_rank;
                }
                continue;
            }

            if (candidate.suit < current.suit)
            {
                best_index = i;
                best_rank = candidate_rank;
            }
        }

        return Card{best_cards[best_index].suit, best_rank};
    };
}

}  // namespace dds::belief_evaluation
