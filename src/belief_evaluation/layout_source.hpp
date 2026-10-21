#pragma once

#include <cstdint>
#include <optional>

#include <api/dds_data_types.hpp>

namespace dds::belief_evaluation
{

/// An ordered, restartable index of candidate layouts. It knows nothing
/// about observations, weights or search nodes. The evaluator scans from
/// index 0 and does all filtering itself.
class LayoutSource
{
public:
    virtual ~LayoutSource() = default;

    /// The number of layouts, or nullopt if not enumerable. Without a size,
    /// a node can never know that it holds the whole belief space.
    virtual auto size() const -> std::optional<std::uint64_t> = 0;

    /// The layout at `index` in the fixed order. The same index must always
    /// return the same layout, so that sampling is reproducible.
    ///
    /// **Caller obligation, not checked:** sampling takes a *prefix* of
    /// this order. The order must already be random. **A sorted or grouped
    /// source gives a biased sample, with no warning.** This binds sampling
    /// callers only. `ExhaustiveLayoutSource` meets it, using a seed.
    virtual auto at(std::uint64_t index) const -> Deal = 0;
};

}  // namespace dds::belief_evaluation
