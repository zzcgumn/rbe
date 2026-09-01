#include <gtest/gtest.h>

#include <belief_evaluation/renumber.hpp>

using namespace dds::belief_evaluation;

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
    // No documented caller passes a pool wider than 13 bits (renumber.hpp's
    // doxygen scopes both arguments to dds's aggregate convention), but the
    // function is unsigned-typed generally rather than scoped to 13 bits by
    // its type, so this pins down that the top bit doesn't shift `in_bit`
    // past the width of `unsigned` in the loop that walks pool's set bits.
    unsigned const pool = (1u << 31) | 0b1u;
    unsigned const holding = pool;
    EXPECT_EQ(renumber(holding, pool), 0b11u);
}
