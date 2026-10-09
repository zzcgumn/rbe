#include <belief_evaluation/touching_group.hpp>

namespace dds::belief_evaluation
{

auto true_lowest_rank(std::vector<Card> const& best_cards, FutureTricks const& fut, std::size_t index) -> int
{
    int const representative_rank = best_cards[index].rank;
    if (index >= static_cast<std::size_t>(fut.cards))
    {
        return representative_rank;
    }

    unsigned const equals = static_cast<unsigned>(fut.equals[index]);
    for (int rank = 2; rank < representative_rank; ++rank)
    {
        if ((equals & (1u << rank)) != 0)
        {
            return rank;
        }
    }
    return representative_rank;
}

}  // namespace dds::belief_evaluation
