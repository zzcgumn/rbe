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
/// cards each defender holds (see `DefenderPool`'s own doxygen). It applies
/// no narrowing beyond that: a caller with additional information -- what
/// the bidding ruled out, say -- wanting a source reflecting it supplies
/// their own, tighter `LayoutSource` instead.
///
/// `size()` is exact and never `nullopt`: `C(n, k)` where `n` is the pooled
/// card count and `k` is the fixed seat's own count at the root, both read
/// once at construction (`defender_pool_decomposition`). `at(index)` unranks
/// rather than materialising the space -- a full thirteen-card ending is
/// `C(26, 13) = 10,400,600` layouts, about a gigabyte of `Deal` -- so
/// construction and every `at()` call are O(pool size), allocating nothing
/// proportional to the space itself.
///
/// The enumeration order is randomised by construction, keyed on `seed`: the
/// same `seed` and `root` give the same sample every time, which is what
/// makes a run reproducible; two different seeds enumerate the identical
/// *space* (the same set of layouts, in a different order) and, taken as a
/// bounded-`sample_size` prefix, two different *samples* -- the way to
/// assess sampling error, as distinct from raising `sample_size`, which
/// instead asks how big a sample of *this* order looks like.
///
/// The evaluator itself takes no seed; randomness lives entirely in a
/// source's own ordering, fixed here at construction.
class UnconstrainedLayoutSource final : public LayoutSource
{
public:
    /// `root` and `declarer` fix the space (via `defender_pool_decomposition`,
    /// computed once here and cached for the life of this source); `seed`
    /// fixes the enumeration order within it. `root` is copied, not
    /// referenced -- this source outlives no caller-owned `Deal`.
    UnconstrainedLayoutSource(Deal const& root, int declarer, std::uint64_t seed);

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
