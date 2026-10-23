#include <belief_evaluation/heuristic_defender.hpp>

#include <api/solve_board.hpp>
#include <solver_context/solver_context.hpp>

namespace dds::belief_evaluation
{

namespace
{
    // Whether `card` is among the optimal candidates in `fut`: an entry's
    // card, or a lower card in its `equals` (see touching_group.hpp). A chain
    // rule can return any card. This check upholds the spec's claim that
    // HeuristicDefender never plays outside the optimal set.
    auto is_double_dummy_optimal(Card const& card, FutureTricks const& fut) -> bool
    {
        // card comes from a chain rule, possibly from Python. Check the rank
        // before `1u << card.rank`, which is undefined for a bad rank.
        if (card.rank < 2 || card.rank > 14)
        {
            return false;
        }
        for (int i = 0; i < fut.cards; ++i)
        {
            if (fut.suit[i] != card.suit)
            {
                continue;
            }
            if (fut.rank[i] == card.rank || (static_cast<unsigned>(fut.equals[i]) & (1u << card.rank)) != 0)
            {
                return true;
            }
        }
        return false;
    }
}

HeuristicDefender::HeuristicDefender(
    SolverContext& ctx, DefenderHeuristicChain chain, SpreadPolicy fallback_policy)
    : ctx_(ctx), chain_(std::move(chain)), fallback_policy_(fallback_policy)
{
}

auto HeuristicDefender::as_strategy() -> DefenderStrategy
{
    return [this](DefenderQuery const& query) -> std::vector<WeightedCard>
    {
        FutureTricks fut{};
        // solutions = 2, as in DoubleDummyDefender::as_strategy().
        int const status =
            solve_board(ctx_, query.layout, /*target=*/-1, /*solutions=*/2, /*mode=*/0, &fut);
        if (status != RETURN_NO_FAULT)
        {
            return {};
        }

        std::vector<Card> best_cards;
        best_cards.reserve(static_cast<std::size_t>(fut.cards));
        for (int i = 0; i < fut.cards; ++i)
        {
            best_cards.push_back(Card{fut.suit[i], fut.rank[i]});
        }

        DefenderHeuristicContext const ctx =
            make_defender_heuristic_context(query.layout, query.state, query.seat, fut, fallback_policy_);

        if (std::optional<Card> const card = chain_.select_card(ctx, best_cards))
        {
            if (is_double_dummy_optimal(*card, fut))
            {
                return {WeightedCard{*card, 1.0}};
            }
            // A chain rule returned a card outside the optimal set. Use spread()
            // instead of returning it.
        }

        return spread(fut, fallback_policy_);
    };
}

}  // namespace dds::belief_evaluation
