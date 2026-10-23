#include <belief_evaluation/layout_key.hpp>

#include <api/dds_constants.hpp>

namespace dds::belief_evaluation
{

namespace
{
    constexpr std::uint64_t ThirteenBitMask = (std::uint64_t{1} << 13) - 1;
}

auto layout_key(Deal const& deal, int defender_seat) -> std::uint64_t
{
    if (defender_seat < 0 || defender_seat >= DDS_HANDS)
    {
        return 0;
    }

    auto const& holding = deal.remainCards[defender_seat];
    // Each suit is masked to 13 bits after the shift, so a stray bit above
    // rank 14 cannot reach the next suit's field.
    return ((std::uint64_t(holding[0]) >> 2) & ThirteenBitMask)
         | (((std::uint64_t(holding[1]) >> 2) & ThirteenBitMask) << 13)
         | (((std::uint64_t(holding[2]) >> 2) & ThirteenBitMask) << 26)
         | (((std::uint64_t(holding[3]) >> 2) & ThirteenBitMask) << 39);
}

}  // namespace dds::belief_evaluation
