#pragma once

#include <cstdint>
#include <optional>

#include <api/dll.h>

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
    virtual auto at(std::uint64_t index) const -> Deal = 0;
};
