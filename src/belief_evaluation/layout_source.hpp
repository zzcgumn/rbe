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
    /// **A caller obligation this type cannot check or enforce**: sampling
    /// (`make_root`, `EvaluateOptions::sample_size`) takes a *prefix* of
    /// this order rather than drawing from it at random, on the premise
    /// that the order itself is already effectively random with respect to
    /// which layouts are consistent with any given root — "a randomised
    /// array of all possible layouts", in the evaluator's own terms. A
    /// source that is not shuffled (sorted, or grouped by some property
    /// correlated with consistency) yields a systematically biased sample
    /// with no diagnostic anywhere in this evaluator: nothing here can
    /// distinguish a well-shuffled source from a badly-ordered one, since
    /// both simply return layouts in whatever order `at()` presents them.
    /// The existing fixed-order test sources in this suite are exactly
    /// what their own tests want and remain correct — this obligation
    /// binds a *sampling* caller, not every caller.
    virtual auto at(std::uint64_t index) const -> Deal = 0;
};

}  // namespace dds::belief_evaluation
