#include <belief_evaluation/keyed_permutation.hpp>

#include <bit>
#include <cassert>

namespace dds::belief_evaluation
{

namespace
{
    /// Mixes `value`, `seed` and `round` into one 64-bit word: a
    /// splitmix64-style finaliser. It need not be a bijection: the Feistel
    /// structure provides that.
    auto mix(std::uint64_t value, std::uint64_t seed, int round) -> std::uint64_t
    {
        std::uint64_t h = value ^ (seed + 0x9E3779B97F4A7C15ULL * static_cast<std::uint64_t>(round));
        h ^= h >> 30;
        h *= 0xBF58476D1CE4E5B9ULL;
        h ^= h >> 27;
        h *= 0x94D049BB133111EBULL;
        h ^= h >> 31;
        return h;
    }

    /// See keyed_permutation.hpp for the measurement.
    constexpr int Rounds = 3;

    /// One Feistel pass over `[0, 2^b)`. A bijection for any round function
    /// and any split (see keyed_permutation.hpp).
    auto feistel_permute(std::uint64_t x, int b, std::uint64_t seed) -> std::uint64_t
    {
        int const low_bits = b / 2;
        int const high_bits = b - low_bits;
        std::uint64_t const low_mask = (low_bits == 0) ? 0u : ((1ULL << low_bits) - 1);
        std::uint64_t const high_mask = (high_bits == 0) ? 0u : ((1ULL << high_bits) - 1);

        std::uint64_t high = (x >> low_bits) & high_mask;
        std::uint64_t low = x & low_mask;
        for (int round = 1; round <= Rounds; ++round)
        {
            if (round % 2 == 1)
            {
                high = (high ^ mix(low, seed, round)) & high_mask;
            }
            else
            {
                low = (low ^ mix(high, seed, round)) & low_mask;
            }
        }
        return (high << low_bits) | low;
    }
}  // namespace

auto keyed_permutation(std::uint64_t index, std::uint64_t n, std::uint64_t seed) -> std::uint64_t
{
    if (n == 0)
    {
        // n == 0 is a contract violation, but must still return: n - 1 below
        // would underflow, and the cycle-walking loop would never end in a
        // release build. 0 is an arbitrary, repeatable answer.
        return 0;
    }
    assert(index < n);
    // The smallest b with 2^b >= n is std::bit_width(n - 1). n >= 1 here.
    int const b = std::bit_width(n - 1);

    std::uint64_t x = index;
    do
    {
        x = feistel_permute(x, b, seed);
    } while (x >= n);
    return x;
}

}  // namespace dds::belief_evaluation
