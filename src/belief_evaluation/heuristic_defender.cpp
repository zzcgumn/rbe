#include <belief_evaluation/heuristic_defender.hpp>

#include <api/solve_board.hpp>
#include <solver_context/solver_context.hpp>

namespace dds::belief_evaluation
{

namespace
{
    // Whether `card` is actually among the double-dummy-optimal
    // candidates `fut` reports -- either a fut entry's own
    // representative, or a lower card folded into that entry's own
    // `equals` field (dds's representative convention; see
    // touching_group.hpp). A chain rule is a plain callable, not a type
    // this class controls, so nothing stops a caller-authored one from
    // returning an arbitrary Card -- this is the one place
    // HeuristicDefender itself upholds the "never invents a candidate
    // outside that set" claim specs/replenished-belief-evaluation.md
    // makes for it, rather than trusting every rule in the chain to.
    auto is_double_dummy_optimal(Card const& card, FutureTricks const& fut) -> bool
    {
        // card comes from a caller-authored chain rule, not from fut
        // itself -- rank must be checked against the documented 2..14
        // range before it is ever used to shift equals's bits; a
        // negative or oversized rank (reachable from Python with no
        // range check of its own) is undefined behaviour for `1u <<
        // card.rank`, not merely a card this function can correctly
        // recognise as non-optimal.
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
        // Same call, same solutions = 2 justification, as
        // DoubleDummyDefender::as_strategy() -- see that function's own
        // comment.
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
            make_defender_heuristic_context(query.layout, query.state, query.seat, fut);

        if (std::optional<Card> const card = chain_.select_card(ctx, best_cards))
        {
            if (is_double_dummy_optimal(*card, fut))
            {
                return {WeightedCard{*card, 1.0}};
            }
            // A misbehaving chain rule returned a card outside the
            // solved optimal set -- fall through to spread() rather
            // than ever returning it, so this guarantee never depends
            // on every rule in the chain behaving itself.
        }

        return spread(fut, fallback_policy_);
    };
}

}  // namespace dds::belief_evaluation
