#include <gtest/gtest.h>

#include <cmath>

#include <belief_evaluation/kahan.hpp>

// A namespace alias plus a targeted `using` declaration, not `using
// namespace dds::belief_evaluation;` -- see the other test files in this
// directory for why: kept consistent even though this particular file
// doesn't yet include anything that makes api/dds_data_types.hpp's global
// ::Card visible.
namespace be = dds::belief_evaluation;
using be::KahanAccumulator;

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
