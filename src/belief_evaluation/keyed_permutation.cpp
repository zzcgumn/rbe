#include <belief_evaluation/keyed_permutation.hpp>

#include <bit>
#include <cassert>

namespace dds::belief_evaluation
{

namespace
{
    /// Mixes `value`, `seed` and `round` into one 64-bit word with no
    /// discernible structure -- a splitmix64-shaped finaliser, chosen for
    /// being a small, well-known bit mixer rather than anything this module
    /// invents. Only ever consumed masked down to a round's own half-width,
    /// so nothing about this function's own bijectivity (it is not a
    /// bijection, and does not need to be) matters to keyed_permutation's.
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

    /// See keyed_permutation.hpp's own doxygen for the measurement behind
    /// this number.
    constexpr int Rounds = 3;

    /// One Feistel pass over `[0, 2^b)`: a bijection on that domain for any
    /// round function and any width split, by the alternating-XOR
    /// construction documented on keyed_permutation's own doxygen.
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
        // n == 0 is always a contract violation -- an empty domain has no
        // valid index -- but it must be handled as a hard, deterministic
        // case rather than only an asserted one: n - 1 below would
        // underflow to UINT64_MAX, and the cycle-walking loop's own exit
        // condition (x < n) can then never be satisfied, hanging forever
        // rather than merely computing a wrong answer. A release build
        // compiles the assert below away, so that failure mode would
        // survive exactly where it matters least to hit it. No return
        // value is meaningful for an empty domain; 0 is returned only
        // because *some* value must be, promptly and repeatably.
        return 0;
    }
    assert(index < n);
    // Smallest b with 2^b >= n: std::bit_width(n - 1) is the number of bits
    // needed to represent n - 1, so 2^that value is the smallest power of
    // two strictly greater than n - 1, i.e. >= n. n >= 1 from here on (the
    // n == 0 case returned above), so n - 1 never underflows.
    int const b = std::bit_width(n - 1);

    std::uint64_t x = index;
    do
    {
        x = feistel_permute(x, b, seed);
    } while (x >= n);
    return x;
}

}  // namespace dds::belief_evaluation
