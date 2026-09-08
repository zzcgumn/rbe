#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <set>
#include <vector>

#include <belief_evaluation/keyed_permutation.hpp>

namespace be = dds::belief_evaluation;

using be::keyed_permutation;

namespace
{
    /// Every output keyed_permutation produces over the whole of [0, n) --
    /// the shape criterion 2's exhaustive bijection check needs.
    auto all_outputs(std::uint64_t n, std::uint64_t seed) -> std::vector<std::uint64_t>
    {
        std::vector<std::uint64_t> outputs;
        outputs.reserve(n);
        for (std::uint64_t index = 0; index < n; ++index)
        {
            outputs.push_back(keyed_permutation(index, n, seed));
        }
        return outputs;
    }

    auto is_bijection(std::uint64_t n, std::uint64_t seed) -> bool
    {
        std::vector<std::uint64_t> const outputs = all_outputs(n, seed);
        std::set<std::uint64_t> const distinct(outputs.begin(), outputs.end());
        if (distinct.size() != n)
        {
            return false;
        }
        for (std::uint64_t const value : distinct)
        {
            if (value >= n)
            {
                return false;
            }
        }
        return true;
    }
}  // namespace

// --- criterion 2: exhaustive bijection, several N shapes -------------------

TEST(KeyedPermutationTest, IsABijectionOnAnOddN)
{
    EXPECT_TRUE(is_bijection(11u, 7u));
    EXPECT_TRUE(is_bijection(11u, 99u));
}

TEST(KeyedPermutationTest, IsABijectionOnAPrimeN)
{
    EXPECT_TRUE(is_bijection(31u, 1u));
    EXPECT_TRUE(is_bijection(97u, 2u));
}

TEST(KeyedPermutationTest, IsABijectionJustAbovePowerOfTwo)
{
    EXPECT_TRUE(is_bijection(17u, 3u));   // just above 16
    EXPECT_TRUE(is_bijection(129u, 4u));  // just above 128
}

TEST(KeyedPermutationTest, IsABijectionJustBelowPowerOfTwo)
{
    EXPECT_TRUE(is_bijection(15u, 5u));   // just below 16
    EXPECT_TRUE(is_bijection(127u, 6u));  // just below 128
}

TEST(KeyedPermutationTest, IsABijectionExactlyAtAPowerOfTwo)
{
    EXPECT_TRUE(is_bijection(16u, 8u));
    EXPECT_TRUE(is_bijection(128u, 9u));
}

TEST(KeyedPermutationTest, IsABijectionOnALargerSpaceCloseToTheBridgeSizedBound)
{
    // Not the full C(26, 13) -- too slow to enumerate exhaustively in a
    // unit test -- but large enough to exercise a wide bit width.
    EXPECT_TRUE(is_bijection(100000u, 123456789u));
}

// --- criterion 3: deterministic ---------------------------------------------

TEST(KeyedPermutationTest, SameIndexNSeedGivesTheSameOutputAcrossSeparateCalls)
{
    for (std::uint64_t index = 0; index < 50; ++index)
    {
        EXPECT_EQ(keyed_permutation(index, 50u, 999u), keyed_permutation(index, 50u, 999u));
    }
}

// --- criterion 4: different seeds differ; no seed gives the identity -------

TEST(KeyedPermutationTest, TwoDifferentSeedsGiveDifferentPermutations)
{
    std::vector<std::uint64_t> const a = all_outputs(50u, 1u);
    std::vector<std::uint64_t> const b = all_outputs(50u, 2u);
    EXPECT_NE(a, b);
}

TEST(KeyedPermutationTest, NoSeedTestedGivesTheIdentityPermutation)
{
    for (std::uint64_t seed = 0; seed < 20; ++seed)
    {
        std::vector<std::uint64_t> const outputs = all_outputs(64u, seed);
        std::vector<std::uint64_t> identity(64u);
        for (std::uint64_t i = 0; i < 64u; ++i)
        {
            identity[i] = i;
        }
        EXPECT_NE(outputs, identity) << "seed=" << seed;
    }
}

// --- criterion 5: not order-preserving, no obviously structured map --------

TEST(KeyedPermutationTest, OutputIsNotMonotoneIncreasing)
{
    std::vector<std::uint64_t> const outputs = all_outputs(64u, 42u);
    bool increasing = true;
    for (std::size_t i = 0; i + 1 < outputs.size(); ++i)
    {
        if (outputs[i] >= outputs[i + 1])
        {
            increasing = false;
            break;
        }
    }
    EXPECT_FALSE(increasing);
}

TEST(KeyedPermutationTest, NoWholeHalfOfTheBitsPassesThroughUnchanged)
{
    // A direct symptom this pins: an under-diffused round leaves one whole
    // Feistel half literally untouched, so those bits of the output equal
    // those same bits of the input for every index -- caught here as the
    // low half-width bits of output vs input agreeing far more often than
    // chance would predict, rather than by the avalanche measurement that
    // originally picked the round count (documented on keyed_permutation's
    // own doxygen; not re-run as a test since it is a measurement, not a
    // pass/fail property of one call).
    std::uint64_t const n = 1024u;  // b = 10, split 5/5
    std::uint64_t const low_mask = (1u << 5) - 1;
    int unchanged = 0;
    for (std::uint64_t index = 0; index < n; ++index)
    {
        std::uint64_t const output = keyed_permutation(index, n, 7u);
        if ((output & low_mask) == (index & low_mask))
        {
            ++unchanged;
        }
    }
    // Chance alone predicts about n / 32 (~32) matches on 5 low bits;
    // an under-diffused single-round map would instead match on every one
    // of the n indices covered by that pass. A generous margin --
    // comfortably above chance, comfortably below "every index" -- is
    // enough to catch the structural failure without being a flaky
    // statistical test.
    EXPECT_LT(unchanged, static_cast<int>(n) / 2);
}

// --- criterion 6: terminates; cycle-walking cost stays low ------------------

TEST(KeyedPermutationTest, TerminatesWithFewCycleWalkSteps)
{
    // Indirect: no step counter is exposed, so this instead checks the
    // documented consequence of "expected steps under 2" -- every call in
    // a modestly sized domain returns promptly (no test timeout) and the
    // bijection property (checked exhaustively above) already proves the
    // walk always lands in range rather than looping forever undetected.
    for (std::uint64_t index = 0; index < 500; ++index)
    {
        EXPECT_LT(keyed_permutation(index, 500u, 55u), 500u);
    }
}

TEST(KeyedPermutationTest, TerminatesForAnNThatDoesNotDivideAPowerOfTwoAtAll)
{
    // n = 1 is the domain that most stresses cycle-walking's own
    // termination argument: the tightest possible power-of-two domain
    // (2^0 = 1) with nothing else in it to walk to.
    EXPECT_EQ(keyed_permutation(0u, 1u, 123u), 0u);
}

// --- n == 0: an empty domain has no valid index, but must not hang -------

TEST(KeyedPermutationTest, AnEmptyDomainReturnsDeterministicallyRatherThanUnderflowingOrLooping)
{
    // n == 0 is always a contract violation (there is no valid index into
    // an empty domain), but naively computing n - 1 would underflow to
    // UINT64_MAX and the cycle-walking loop's own exit condition
    // (x < n) can then never be satisfied, hanging forever rather than
    // merely computing a wrong answer -- a release build compiles away
    // an assert, so this must be a hard, deterministic case, not only an
    // asserted one. No particular return value is meaningful for an
    // empty domain; only that this returns promptly and repeatably.
    EXPECT_EQ(keyed_permutation(0u, 0u, 1u), keyed_permutation(0u, 0u, 1u));
}
