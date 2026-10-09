#include <belief_evaluation/second_seat_low.hpp>

#include <belief_evaluation/spread.hpp>
#include <belief_evaluation/touching_group.hpp>

namespace dds::belief_evaluation
{

namespace
{
    // Whether best_cards[lowest_index] touches another same-suit card, per
    // ctx.fut.equals -- checked both ways:
    //
    // 1. lowest_index's *own* equals entry, non-empty -- the case
    //    solve_board actually produces for a plain touching pair/run: it
    //    collapses the whole group to ONE canonical entry (the highest
    //    card) and folds every lower member into that one entry's own
    //    equals, so best_cards may hold only that single entry with
    //    nothing else to compare it against. A loop that only ever
    //    compares *different* best_cards entries against each other -- the
    //    first version of this function -- can never see this case at all:
    //    with one entry there is nothing to loop over, and the bug is
    //    silent, not a crash. true_lowest_rank (shared with the other
    //    rules that compare candidates by raw rank) resolves the rank
    //    half of this; only the touching verdict itself is local here.
    // 2. The cross-reference loop below, for the case a caller lists a
    //    touching group as separate best_cards entries explicitly instead
    //    (checked in both directions, since dds may record a group's
    //    membership on whichever entry it treats as the representative,
    //    not necessarily the lowest-ranked one). The group is not
    //    collapsed in this shape, so best_cards[lowest_index].rank is
    //    already the true lowest rank -- nothing to resolve.
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

        // Deferring only actually randomises the lowest candidate's own
        // group if that group is the one spread(fut,
        // SpreadPolicy::TouchingSequence) itself goes on to pick --
        // fut may hold more than one disjoint touching group tied for
        // best score (e.g. an untouched A-K alongside a separate Q-J),
        // and spread() always resolves to canonical_best(fut)'s own
        // group, not necessarily this one. Deferring when the two
        // diverge would silently randomise the wrong pair -- or, if
        // canonical_best's own candidate does not touch anything at
        // all, would not randomise at all -- so this rule only defers
        // when it knows the fallback actually lands on its own group;
        // otherwise it still returns its own answer directly, the same
        // card randomize_touching_honors=false would give.
        bool const defer_reaches_this_group =
            canonical_best(ctx.fut) == static_cast<int>(lowest_index);

        // And only if the fallback it defers to is actually the policy
        // that distributes over just this one touching group --
        // ctx.fallback_policy may be SpreadPolicy::AllOptimal instead
        // (a HeuristicDefender constructed with that policy), which
        // spreads over every tied candidate across every suit rather
        // than restricting to this group alone. Deferring there would
        // not produce restricted choice at all, so this rule falls
        // back to returning its own answer directly in that case too.
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
