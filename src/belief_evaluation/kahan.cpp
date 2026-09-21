#include <belief_evaluation/kahan.hpp>

namespace dds::belief_evaluation
{

auto KahanAccumulator::add(double value) -> void
{
    double const y = value - compensation_;
    double const t = sum_ + y;
    compensation_ = (t - sum_) - y;
    sum_ = t;
}

auto KahanAccumulator::value() const -> double
{
    return sum_;
}

}  // namespace dds::belief_evaluation
