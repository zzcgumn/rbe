#include <belief_evaluation/discard_keep_winners.hpp>

#include <belief_evaluation/suit_top_tricks.hpp>

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

        std::size_t best_index = 0;
        for (std::size_t i = 1; i < best_cards.size(); ++i)
        {
            Card const& candidate = best_cards[i];
            Card const& current = best_cards[best_index];

            int const candidate_tricks = tricks[static_cast<std::size_t>(candidate.suit)];
            int const current_tricks = tricks[static_cast<std::size_t>(current.suit)];
            if (candidate_tricks != current_tricks)
            {
                if (candidate_tricks < current_tricks)
                {
                    best_index = i;
                }
                continue;
            }

            if (candidate.rank != current.rank)
            {
                if (candidate.rank < current.rank)
                {
                    best_index = i;
                }
                continue;
            }

            if (candidate.suit < current.suit)
            {
                best_index = i;
            }
        }

        return best_cards[best_index];
    };
}

}  // namespace dds::belief_evaluation
