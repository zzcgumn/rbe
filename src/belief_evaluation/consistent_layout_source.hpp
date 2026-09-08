#pragma once

#include <cstdint>
#include <optional>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/defender_split.hpp>
#include <belief_evaluation/layout_source.hpp>

namespace dds::belief_evaluation
{

/// A `LayoutSource` that enumerates every layout consistent with a root
/// position, exactly: the defender-pool splits, and nothing else. Everything
/// `is_consistent` compares against a root -- trump, the seat on lead, the
/// current trick, declarer's exact holding, dummy's exact holding -- is held
/// fixed at the root's own value; the only freedom is which of the pooled
/// cards each defender holds (see `DefenderPool`'s own doxygen). This is the
/// first `LayoutSource` this module ships -- every other one in existence is
/// a hand-built `std::vector<Deal>` test double.
///
/// **`size()` is exact and never `nullopt`**: `C(n, k)` where `n` is the
/// pooled card count and `k` is the fixed seat's own count at the root, both
/// read once at construction (`defender_pool_decomposition`). `at(index)`
/// unranks rather than materialising the space -- a full thirteen-card
/// ending is `C(26, 13) = 10,400,600` layouts, about a gigabyte of `Deal` --
/// so construction and every `at()` call are O(pool size), allocating
/// nothing proportional to the space itself.
///
/// **The order is randomised by construction**, discharging the caller
/// obligation `LayoutSource::at`'s own doxygen states for a sampling caller
/// (that the order already be effectively random with respect to
/// consistency) -- this type is built so a caller never has to think about
/// it. That obligation still binds a caller supplying their own
/// `LayoutSource`; this is a correct implementation to point at, not a
/// closing of the obligation itself.
///
/// **The same `seed` and `root` layout give the same sample every time --
/// that is what makes a run reproducible.** Two different seeds enumerate
/// the identical *space* (the same set of layouts; only the visiting order
/// differs) but, taken as a bounded-`sample_size` prefix, two different
/// *samples* -- this is the intended way to assess sampling error. Raising
/// `sample_size` instead answers a different question (how big a sample of
/// *this* order looks like), not how much the answer varies across
/// independent samples.
///
/// **The evaluator itself still has no seed, and this is not a reversal of
/// that decision** -- randomness lives entirely in a source's own ordering
/// (here, in the constructor argument below); `EvaluateOptions` takes a
/// *prefix* of whatever order its source presents and cannot tell a
/// well-shuffled source from a badly-ordered one. A reader meeting both
/// facts later should read the second as this decision continued, not
/// overturned.
class ConsistentLayoutSource final : public LayoutSource
{
public:
    /// `root` and `declarer` fix the space (via `defender_pool_decomposition`,
    /// computed once here and cached for the life of this source); `seed`
    /// fixes the enumeration order within it. `root` is copied, not
    /// referenced -- this source outlives no caller-owned `Deal`.
    ConsistentLayoutSource(Deal const& root, int declarer, std::uint64_t seed);

    /// `C(n, k)`: the pooled card count choose the fixed seat's own count at
    /// `root`. Exact, and never `nullopt` -- this space is always fully
    /// enumerable.
    auto size() const -> std::optional<std::uint64_t> override;

    /// The layout at `index` in this source's own randomised order. `index`
    /// must be less than `size()`'s own value -- out of range is a caller
    /// arithmetic error, reachable from nothing but that, and is asserted
    /// rather than given any other defined behaviour (the evaluator itself
    /// never calls `at()` out of range: both `make_root` and
    /// `scan_for_replenishment` bound their own loops by `size()`).
    auto at(std::uint64_t index) const -> Deal override;

private:
    Deal root_;
    int declarer_;
    std::uint64_t seed_;
    DefenderPool pool_;
};

}  // namespace dds::belief_evaluation
