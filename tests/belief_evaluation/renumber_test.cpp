#include <gtest/gtest.h>

#include <belief_evaluation/renumber.hpp>

// Tests that renumber removes the gaps in a pool and maps a holding onto
// the result.

// A namespace alias and a targeted `using` declaration, not `using
// namespace dds::belief_evaluation;`, to match the other test files. This
// file does not make the global ::Card visible.
namespace be = dds::belief_evaluation;
using be::renumber;

TEST(Renumber, OrderPreservingOverAPoolWithGaps)
{
    // Pool has bits 0, 2, 4 set (three outstanding cards with gaps at 1, 3).
    // Holding has the middle and top of the pool: bits 2 and 4.
    unsigned const pool = 0b10101;
    unsigned const holding = 0b10100;
    // After gap removal the pool's three members become consecutive
    // positions 0, 1, 2. Holding's bit 2 -> position 1, bit 4 -> position 2.
    EXPECT_EQ(renumber(holding, pool), 0b110u);
}

TEST(Renumber, IdempotentOnAnAlreadyDensePool)
{
    unsigned const pool = 0b111;
    unsigned const holding = 0b101;
    EXPECT_EQ(renumber(holding, pool), holding);
}

TEST(Renumber, EmptyHoldingStaysEmpty)
{
    EXPECT_EQ(renumber(0u, 0b10101u), 0u);
}

TEST(Renumber, FullPoolIsIdentity)
{
    unsigned const pool = 0x1FFFu;  // all 13 ranks outstanding
    unsigned const holding = 0x0A5Au;
    EXPECT_EQ(renumber(holding & pool, pool), holding & pool);
}

TEST(Renumber, DoesNotOverflowWhenPoolsHighestSetBitIsBit31)
{
    // Callers pass 13-bit aggregates, but the type allows any unsigned. Bit
    // 31 must not shift `in_bit` past the width of `unsigned` in the loop
    // over the pool's set bits.
    unsigned const pool = (1u << 31) | 0b1u;
    unsigned const holding = pool;
    EXPECT_EQ(renumber(holding, pool), 0b11u);
}
