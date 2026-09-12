#pragma once

#include <cstdint>
#include <optional>

#include <api/dds_data_types.hpp>
#include <utility/constants.h>

#include <belief_evaluation/constrained_decomposition.hpp>
#include <belief_evaluation/defender_split.hpp>
#include <belief_evaluation/history_verification.hpp>
#include <belief_evaluation/layout_source.hpp>

namespace dds::belief_evaluation
{

/// A `LayoutSource` that enumerates every layout consistent with a root
/// position and, when a play history is supplied, with the voids that
/// history establishes: the defender-pool splits that respect every
/// completed and in-progress show-out, and nothing else. Everything
/// `is_consistent` compares against a root -- trump, the seat on lead, the
/// current trick, declarer's exact holding, dummy's exact holding -- is held
/// fixed at the root's own value; of what is left, a supplied history
/// narrows the space to the splits it makes possible, and nothing beyond
/// that: a caller with *inferred* information -- what the bidding ruled out,
/// say -- wanting a source reflecting it still supplies their own, tighter
/// `LayoutSource`. The distinction is between fact and inference, not
/// between "this source narrows" and "this source does not": this one now
/// applies exactly the narrowing the play establishes as fact, and a caller
/// needs to know which side of that line their own information falls on.
///
/// `history` and `opening_leader` are defaulted, so every call site written
/// before this capability existed compiles and behaves identically: an
/// empty history applies no void constraint at all and reproduces the
/// original enumeration exactly (see `size()`'s own doxygen for the
/// resulting expression). A non-empty history is checked against `root`
/// (`verify_history`) before anything else; see `history_verdict()` for what
/// happens when it does not fit, and `constrained_space_status()` for what
/// happens when it fits but leaves no legal split.
///
/// `size()` is exact and never `nullopt`, in every case: with no history,
/// `C(n, k)` where `n` is the pooled card count and `k` is the fixed seat's
/// own count at the root (both read once at construction, via
/// `defender_pool_decomposition`); with a history, the same computation
/// restricted to the free cards a void does not force one way or the other
/// (see `decompose_constrained`). `at(index)` unranks rather than
/// materialising the space -- a full thirteen-card ending is
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
/// A supplied `history` changes the *space itself* (it is smaller), so the
/// same `seed` and `root` give a different order once a history is supplied
/// than without one. That is correct, not a sign the seed stopped working:
/// same `seed`, `root` and `history` still reproduce a run exactly, which is
/// the property that actually matters.
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
    ///
    /// `history` (played from `opening_leader`) is optional: the default,
    /// an empty `PlayTraceBin`, applies no void constraint and is not
    /// checked against `root` at all -- a caller with no play record is not
    /// making a claim `verify_history` could reject, and this is the mode
    /// every call site written before this parameter existed still runs in.
    /// A non-empty history *is* checked; see `history_verdict()`.
    UnconstrainedLayoutSource(
        Deal const& root,
        int declarer,
        std::uint64_t seed,
        PlayTraceBin const& history = PlayTraceBin{},
        int opening_leader = 0);

    /// `C(n, k)`: the pooled card count choose the fixed seat's own count at
    /// `root`, when no history was supplied or restricts nothing further
    /// than `root` already does; the free-card count choose what the fixed
    /// seat still needs once a supplied history's voids are applied,
    /// otherwise (see `decompose_constrained`). Exact, and never `nullopt`
    /// -- this space is always fully enumerable, though a rejected history
    /// or a self-contradictory one both make it *empty*: `0` in either
    /// case, indistinguishable from `size()` alone. That is deliberate --
    /// see `history_verdict()` and `constrained_space_status()`, which
    /// between them are what a caller needing to tell those cases apart (or
    /// apart from an ordinary empty root) actually consults.
    auto size() const -> std::optional<std::uint64_t> override;

    /// The layout at `index` in this source's own randomised order. `index`
    /// must be less than `size()`'s own value -- out of range is a caller
    /// arithmetic error, reachable from nothing but that, and is asserted
    /// rather than given any other defined behaviour (the evaluator itself
    /// never calls `at()` out of range: both `make_root` and
    /// `scan_for_replenishment` bound their own loops by `size()`).
    ///
    /// Calling this when `history_verdict()` is not
    /// `HistoryVerdict::Consistent` is the same class of caller error --
    /// `size()` is `0` then, so no in-range `index` exists to call it with.
    auto at(std::uint64_t index) const -> Deal override;

    /// Whether the `history` this source was constructed with actually
    /// belongs to `root`: `HistoryVerdict::Consistent` when it does, or when
    /// no history was supplied at all (nothing to reject); otherwise the
    /// specific cause `verify_history` reported. Check this **before**
    /// `constrained_space_status()` -- when this is not `Consistent`, the
    /// constrained decomposition was never attempted, and
    /// `constrained_space_status()` reports the meaningless default
    /// (`Ok`) rather than anything about why the space is empty.
    ///
    /// This is the accessor `size() == 0` alone cannot substitute for: a
    /// `LayoutSource` returning `0` reaches `make_root` as
    /// `RootFailure::NoLayoutSurvived`, which tells a caller to fix their
    /// *source*. For a rejected history the right advice is to fix the
    /// *history* (or the root it was built against), and this is what lets
    /// a caller give that advice instead of the wrong one.
    auto history_verdict() const -> HistoryVerdict;

    /// Why the constrained space is empty, when it is, and `history_verdict()`
    /// is `HistoryVerdict::Consistent` -- `ConstrainedSpaceStatus::Ok` when
    /// it is not empty, or when no history was supplied (nothing was ever
    /// constrained). See `history_verdict()`'s own doxygen for the check to
    /// make first, and `ConstrainedSpaceStatus` for what each non-`Ok` value
    /// means: a self-contradictory history, or one that fits `root` but
    /// leaves the fixed seat's own hand size unreachable in one direction or
    /// the other. Every one of these is a different sentence to a caller
    /// than "the root simply has nothing consistent in it", which is what a
    /// bare `size() == 0` would otherwise read as.
    auto constrained_space_status() const -> ConstrainedSpaceStatus;

private:
    Deal root_;
    std::uint64_t seed_;
    int fixed_seat_;
    int other_seat_;
    DefenderPool pool_;
    HistoryVerdict verdict_;
    ConstrainedDecomposition decomposition_;
};

}  // namespace dds::belief_evaluation
