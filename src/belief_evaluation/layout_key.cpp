#include <belief_evaluation/layout_key.hpp>

auto layout_key(Deal const& deal, int defender_seat) -> std::uint64_t
{
    auto const& holding = deal.remainCards[defender_seat];
    return (std::uint64_t(holding[0]) >> 2)
         | ((std::uint64_t(holding[1]) >> 2) << 13)
         | ((std::uint64_t(holding[2]) >> 2) << 26)
         | ((std::uint64_t(holding[3]) >> 2) << 39);
}
