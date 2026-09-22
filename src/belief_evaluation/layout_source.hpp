#pragma once

#include <cstdint>
#include <optional>

#include <api/dds_data_types.hpp>

namespace dds::belief_evaluation
{

/// A dumb, ordered, restartable index space over candidate layouts. Knows
/// nothing about observations, weights, or the current search node — the
/// evaluator filters candidates against path history and a node's exclusion
/// set itself, scanning from index 0 until enough matches are found.
class LayoutSource
{
public:
    virtual ~LayoutSource() = default;

    /// Total layouts in the space, or nullopt if not enumerable. A source
    /// that cannot report a size can never let a search node establish that
    /// it holds the whole remaining belief space rather than a sample of it.
    virtual auto size() const -> std::optional<std::uint64_t> = 0;

    /// The layout at `index` in this source's fixed order. Repeated calls
    /// with the same index must return the same layout — determinism here is
    /// what makes sampling reproducible.
    ///
    /// **A caller obligation this type cannot check**: sampling takes a
    /// *prefix* of this order rather than drawing at random, on the
    /// premise that the order is already effectively random with respect
    /// to which layouts are consistent with a given root. A sorted or
    /// grouped source yields a systematically biased sample with no
    /// diagnostic anywhere — nothing here can tell a well-shuffled source
    /// from a badly-ordered one. Binds a *sampling* caller only, so the
    /// fixed-order test sources in this suite remain correct.
    /// `ExhaustiveLayoutSource` discharges it, keyed on a seed.
    virtual auto at(std::uint64_t index) const -> Deal = 0;
};

}  // namespace dds::belief_evaluation
