#include <belief_evaluation/second_seat_low.hpp>

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
    //    silent, not a crash.
    // 2. The cross-reference loop below, for the case a caller lists a
    //    touching group as separate best_cards entries explicitly instead
    //    (checked in both directions, since dds may record a group's
    //    membership on whichever entry it treats as the representative,
    //    not necessarily the lowest-ranked one).
    //
    // Returns the group's own true lowest rank alongside the verdict,
    // since that rank may itself be one folded into lowest_index's equals
    // rather than best_cards[lowest_index].rank -- dds's own representative
    // is the *highest* card of a touching run, not the lowest.
    struct TouchingResult
    {
        bool touches;
        int lowest_rank;
    };

    auto touching_group_of(
        std::vector<Card> const& best_cards, FutureTricks const& fut, std::size_t lowest_index) -> TouchingResult
    {
        Card const& lowest = best_cards[lowest_index];
        int lowest_rank = lowest.rank;

        if (lowest_index < static_cast<std::size_t>(fut.cards))
        {
            unsigned const equals = static_cast<unsigned>(fut.equals[lowest_index]);
            if (equals != 0)
            {
                for (int rank = 2; rank < lowest_rank; ++rank)
                {
                    if ((equals & (1u << rank)) != 0)
                    {
                        lowest_rank = rank;
                        break;
                    }
                }
                return TouchingResult{true, lowest_rank};
            }
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
                return TouchingResult{true, lowest_rank};
            }
        }
        return TouchingResult{false, lowest_rank};
    }
}

auto second_seat_low(bool randomise_touching_honours) -> DefenderHeuristic
{
    return [randomise_touching_honours](
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
        if (randomise_touching_honours && touching.touches)
        {
            return std::nullopt;
        }

        return Card{best_cards[lowest_index].suit, touching.lowest_rank};
    };
}

}  // namespace dds::belief_evaluation
