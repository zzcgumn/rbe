#pragma once

#include <cstdint>
#include <optional>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/constrained_decomposition.hpp>
#include <belief_evaluation/defender_split.hpp>
#include <belief_evaluation/history_verification.hpp>
#include <belief_evaluation/layout_source.hpp>
#include <belief_evaluation/play_record.hpp>

namespace dds::belief_evaluation
{

/// A `LayoutSource` with every layout consistent with a root position.
/// With a play history, only the splits that respect every show-out.
///
/// **Facts only, not inferences.** This applies only what the play proves.
/// A caller with *inferred* information, such as from the bidding, writes
/// their own tighter `LayoutSource`. See the caller's guide, "Supply the
/// play history".
///
/// Without `record`, no voids apply. A supplied record is checked against
/// `root` first. See `history_verdict()` for a record that does not fit,
/// and `constrained_space_status()` for one that fits but leaves no legal
/// split.
///
/// `size()` is exact and never `nullopt`. `at(index)` computes the layout
/// instead of storing the space: a thirteen-card ending is `C(26, 13)`
/// layouts. Construction and each `at()` call are O(pool size).
///
/// **The order is randomised by `seed`.** The same `seed`, `root` and
/// `record` repeat a run exactly. Two seeds give the same space in a
/// different order, so a `sample_size` prefix of each is a different
/// sample. Use this to assess sampling error. A record changes the space,
/// so the order differs from the no-record order at the same seed.
class ExhaustiveLayoutSource final : public LayoutSource
{
public:
    /// `root` and `declarer` fix the space. `seed` fixes the order. `root`
    /// is copied. Without `record`, nothing is checked. With it, this
    /// checks that the named leader led the first trick of the record: a
    /// `PlayRecord` does not check that itself.
    ExhaustiveLayoutSource(
        Deal const& root, int declarer, std::uint64_t seed, std::optional<PlayRecord> const& record = std::nullopt);

    /// `C(n, k)`: the pool size choose the fixed seat's count. With voids,
    /// the free-card count choose what the fixed seat still needs. `0` for
    /// a rejected history and for a self-contradictory one. Use
    /// `history_verdict()` and `constrained_space_status()` to tell them
    /// apart.
    auto size() const -> std::optional<std::uint64_t> override;

    /// The layout at `index` in the randomised order. `index` must be below
    /// `size()`. Out of range is asserted. The evaluator never calls it out
    /// of range.
    auto at(std::uint64_t index) const -> Deal override;

    /// Whether the history belongs to `root`. `Consistent` when it does, or
    /// when there is no history. Otherwise the cause from `verify_history`.
    ///
    /// **Check this before `constrained_space_status()`,** which reports a
    /// meaningless `Ok` when this is not `Consistent`.
    ///
    /// `size() == 0` is not enough: `make_root` reports it as
    /// `RootFailure::NoLayoutSurvived`, which says fix the *source*. The fix
    /// is to the *history*.
    auto history_verdict() const -> HistoryVerdict;

    /// Why the constrained space is empty, when `history_verdict()` is
    /// `Consistent`. `Ok` when the space is not empty, or when there is no
    /// history. See `ConstrainedSpaceStatus`.
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
