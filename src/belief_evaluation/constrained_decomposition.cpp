#include <belief_evaluation/constrained_decomposition.hpp>

namespace dds::belief_evaluation
{

auto decompose_constrained(
    DefenderPool const& pool,
    std::array<bool, DDS_SUITS> const& fixed_seat_voids,
    std::array<bool, DDS_SUITS> const& other_seat_voids) -> ConstrainedDecomposition
{
    ConstrainedDecomposition result;

    // One pass over pool.cards, in its own canonical order -- so
    // free_cards is a filter of that order, never a re-sort, and stays
    // stable across calls with the same inputs.
    for (Card const& card : pool.cards)
    {
        bool const fixed_void = fixed_seat_voids[static_cast<std::size_t>(card.suit)];
        bool const other_void = other_seat_voids[static_cast<std::size_t>(card.suit)];

        if (fixed_void && other_void)
        {
            // Both defenders void in this card's suit, and the pool still
            // contains it: unresolvable, whichever of the two rules below
            // would otherwise apply. No well-defined side for this card
            // to be on, so nothing partial is reported either.
            return ConstrainedDecomposition{.status = ConstrainedSpaceStatus::ContradictoryVoid};
        }
        if (fixed_void)
        {
            // The fixed seat cannot hold it -- forced to the other defender.
            result.forced_to_other_seat.push_back(card);
        }
        else if (other_void)
        {
            // The other defender cannot hold it -- forced to the fixed seat.
            result.forced_to_fixed_seat.push_back(card);
        }
        else
        {
            result.free_cards.push_back(card);
        }
    }

    int const forced_to_fixed_seat = static_cast<int>(result.forced_to_fixed_seat.size());
    result.fixed_seat_needed = pool.fixed_seat_count - forced_to_fixed_seat;

    if (forced_to_fixed_seat > pool.fixed_seat_count)
    {
        result.status = ConstrainedSpaceStatus::ForcedExceedsFixedSeatCount;
        return result;
    }
    if (result.fixed_seat_needed > static_cast<int>(result.free_cards.size()))
    {
        result.status = ConstrainedSpaceStatus::InsufficientFreeCards;
        return result;
    }

    result.status = ConstrainedSpaceStatus::Ok;
    return result;
}

auto constrained_space_size(ConstrainedDecomposition const& decomposition) -> std::uint64_t
{
    if (decomposition.status != ConstrainedSpaceStatus::Ok)
    {
        // The three empty-space causes are kept apart on decomposition.status
        // itself (see ConstrainedSpaceStatus's own doxygen) -- collapsing
        // them all to the same 0 here would throw away exactly the
        // distinction that exists for.
        return 0;
    }
    return binomial_coefficient(
        static_cast<int>(decomposition.free_cards.size()), decomposition.fixed_seat_needed);
}

}  // namespace dds::belief_evaluation
