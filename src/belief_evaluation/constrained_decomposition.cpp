#include <belief_evaluation/constrained_decomposition.hpp>

namespace dds::belief_evaluation
{

auto decompose_constrained(
    DefenderPool const& pool,
    std::array<bool, DDS_SUITS> const& fixed_seat_voids,
    std::array<bool, DDS_SUITS> const& other_seat_voids) -> ConstrainedDecomposition
{
    ConstrainedDecomposition result;

    // One pass in pool order, so free_cards is filtered, never re-sorted.
    for (Card const& card : pool.cards)
    {
        bool const fixed_void = fixed_seat_voids[static_cast<std::size_t>(card.suit)];
        bool const other_void = other_seat_voids[static_cast<std::size_t>(card.suit)];

        if (fixed_void && other_void)
        {
            // Both defenders are void in this suit, and the pool still holds the
            // card. It has no side, so nothing partial is reported.
            return ConstrainedDecomposition{.status = ConstrainedSpaceStatus::ContradictoryVoid};
        }
        if (fixed_void)
        {
            // The fixed seat is void: forced to the other defender.
            result.forced_to_other_seat.push_back(card);
        }
        else if (other_void)
        {
            // The other defender is void: forced to the fixed seat.
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
        // decomposition.status says which kind of empty.
        return 0;
    }
    return binomial_coefficient(
        static_cast<int>(decomposition.free_cards.size()), decomposition.fixed_seat_needed);
}

}  // namespace dds::belief_evaluation
