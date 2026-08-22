#pragma once

/// Compensated (Kahan) summation. Tracks a running compensation term for the
/// low-order bits lost to each addition's rounding, so that a long running
/// sum accumulates far less error than naive `sum += value`.
class KahanAccumulator
{
public:
    auto add(double value) -> void;
    auto value() const -> double;

private:
    double sum_ = 0.0;
    double compensation_ = 0.0;
};
