#pragma once

#include <cstdint>
#include <optional>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/constrained_decomposition.hpp>
#include <belief_evaluation/defender_split.hpp>
#include <belief_evaluation/history_verification.hpp>
#include <belief_evaluation/layout_source.hpp>

namespace dds::belief_evaluation
{

/// A `LayoutSource` enumerating every layout consistent with a root
/// position and, when a play history is supplied, with the voids that
/// history establishes — the defender-pool splits that respect every
/// show-out, and nothing else.
///
/// **Fact, not inference.** This applies exactly the narrowing the play
/// establishes. A caller with *inferred* information — what the bidding
/// ruled out, say — supplies their own tighter `LayoutSource`, and needs to
/// know which side of that line their information falls on. See the
/// caller's guide, "The play history: needed, not merely optional".
///
/// `history` and `opening_leader` are defaulted; an empty history applies
/// no void constraint and reproduces the unconstrained enumeration. A
/// non-empty one is checked against `root` before anything else — see
/// `history_verdict()` for a history that does not fit, and
/// `constrained_space_status()` for one that fits but leaves no legal
/// split.
///
/// `size()` is exact and never `nullopt`. `at(index)` unranks rather than
/// materialising the space — a full thirteen-card ending is `C(26, 13)`
/// layouts, about a gigabyte of `Deal` — so construction and every `at()`
/// call are O(pool size) and allocate nothing proportional to the space.
///
/// **The enumeration order is randomised, keyed on `seed`.** Same `seed`,
/// `root` and `history` reproduce a run exactly. Two seeds enumerate the
/// identical *space* in a different order, so a bounded-`sample_size`
/// prefix of each gives two different samples — which is how to assess
/// sampling error, as distinct from raising `sample_size`, which asks a
/// different question. A supplied history changes the space itself, so the
/// order differs from the no-history order at the same seed; that is
/// correct, not a sign the seed stopped working.
class ExhaustiveLayoutSource final : public LayoutSource
{
public:
    /// `root` and `declarer` fix the space, `seed` fixes the order within
    /// it. `root` is copied, so this source outlives no caller-owned
    /// `Deal`. An empty `history` is not checked against `root` at all — a
    /// caller with no play record is making no claim to reject.
    ExhaustiveLayoutSource(
        Deal const& root,
        int declarer,
        std::uint64_t seed,
        PlayTraceBin const& history = PlayTraceBin{},
        int opening_leader = 0);

    /// `C(n, k)`: the pooled card count choose the fixed seat's own count,
    /// or — once a history's voids are applied — the free-card count choose
    /// what the fixed seat still needs. Exact and never `nullopt`, but `0`
    /// both for a rejected history and for a self-contradictory one, which
    /// `size()` alone cannot tell apart; that is what `history_verdict()`
    /// and `constrained_space_status()` are for.
    auto size() const -> std::optional<std::uint64_t> override;

    /// The layout at `index` in this source's randomised order. `index`
    /// must be below `size()`; out of range is a caller arithmetic error
    /// and is asserted. The evaluator never calls it out of range — both
    /// `make_root` and `scan_for_replenishment` bound their loops by
    /// `size()`.
    auto at(std::uint64_t index) const -> Deal override;

    /// Whether the supplied `history` belongs to `root`: `Consistent` when
    /// it does, or when no history was supplied; otherwise the cause
    /// `verify_history` reported. Check this **before**
    /// `constrained_space_status()`, which was never computed when this is
    /// not `Consistent` and reports a meaningless `Ok`.
    ///
    /// This is what `size() == 0` alone cannot substitute for: an empty
    /// source reaches `make_root` as `RootFailure::NoLayoutSurvived`, which
    /// tells a caller to fix their *source*, when the right advice is to
    /// fix the *history*.
    auto history_verdict() const -> HistoryVerdict;

    /// Why the constrained space is empty, when it is and
    /// `history_verdict()` is `Consistent` — `Ok` when it is not empty, or
    /// when no history was supplied. See `ConstrainedSpaceStatus` for what
    /// each non-`Ok` value means; each is a different sentence to a caller
    /// than "the root has nothing consistent in it".
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
