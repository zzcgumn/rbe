#include <gtest/gtest.h>

#include <cmath>

#include <belief_evaluation/kahan.hpp>

TEST(KahanAccumulator, RecoversPrecisionNaiveSummationLoses)
{
    constexpr int count = 100000;
    constexpr double term = 0.1;
    constexpr double exact = count * term;  // 10000.0, mathematically

    double naive = 0.0;
    KahanAccumulator kahan;
    for (int i = 0; i < count; ++i)
    {
        naive += term;
        kahan.add(term);
    }

    double const naive_error = std::abs(naive - exact);
    double const kahan_error = std::abs(kahan.value() - exact);

    EXPECT_GT(naive_error, 0.0)
        << "test sequence should demonstrate naive precision loss; pick a "
           "different sequence if this fails";
    EXPECT_LT(kahan_error, naive_error);
}

TEST(KahanAccumulator, EmptyAccumulatorIsZero)
{
    KahanAccumulator const kahan;
    EXPECT_EQ(kahan.value(), 0.0);
}
