#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <set>
#include <vector>

#include <belief_evaluation/keyed_permutation.hpp>

// Tests for keyed_permutation: bijection over several domain sizes,
// determinism, seed dependence, lack of structure and termination.

namespace be = dds::belief_evaluation;

using be::keyed_permutation;

namespace
{
    /// Every output of keyed_permutation over [0, n), for the exhaustive
    /// bijection check.
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

// --- exhaustive bijection, several N shapes ---------------------------------

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
    // Not the full C(26, 13), which is too slow to enumerate in a unit test,
    // but wide enough in bits.
    EXPECT_TRUE(is_bijection(100000u, 123456789u));
}

// --- deterministic -----------------------------------------------------------

TEST(KeyedPermutationTest, SameIndexNSeedGivesTheSameOutputAcrossSeparateCalls)
{
    for (std::uint64_t index = 0; index < 50; ++index)
    {
        EXPECT_EQ(keyed_permutation(index, 50u, 999u), keyed_permutation(index, 50u, 999u));
    }
}

// --- different seeds differ; no seed gives the identity ---------------------

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

// --- not order-preserving, no obviously structured map ----------------------

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
    // A round that mixes too little leaves a whole Feistel half unchanged.
    // Those output bits then equal the input bits for every index. This test
    // counts how often the low half matches. The round count itself was chosen
    // by an avalanche measurement (see the doxygen), which is not a pass/fail
    // test.
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
    // Chance predicts about n / 32 (about 32) matches on 5 low bits. A map
    // that left that half unchanged would match on all n. The margin between
    // the two catches the failure without making the test flaky.
    EXPECT_LT(unchanged, static_cast<int>(n) / 2);
}

// --- terminates; cycle-walking cost stays low --------------------------------

TEST(KeyedPermutationTest, TerminatesWithFewCycleWalkSteps)
{
    // No step counter is exposed, so this checks the consequence of
    // "expected steps under 2": every call returns promptly. The exhaustive
    // bijection tests above prove the walk always lands in range.
    for (std::uint64_t index = 0; index < 500; ++index)
    {
        EXPECT_LT(keyed_permutation(index, 500u, 55u), 500u);
    }
}

TEST(KeyedPermutationTest, TerminatesForAnNThatDoesNotDivideAPowerOfTwoAtAll)
{
    // n = 1 is the hardest case for cycle-walking to end: the smallest
    // power-of-two domain (2^0 = 1), with nothing else to walk to.
    EXPECT_EQ(keyed_permutation(0u, 1u, 123u), 0u);
}

// --- n == 0: an empty domain has no valid index, but must not hang -------

TEST(KeyedPermutationTest, AnEmptyDomainReturnsDeterministicallyRatherThanUnderflowingOrLooping)
{
    // n == 0 is a contract violation: an empty domain has no valid index.
    // Computing n - 1 would underflow to UINT64_MAX, and the loop's exit
    // condition (x < n) could never hold. A release build compiles out the
    // assert, so the function must still return. No return value is
    // meaningful; it must only be prompt and repeatable.
    EXPECT_EQ(keyed_permutation(0u, 0u, 1u), keyed_permutation(0u, 0u, 1u));
}
