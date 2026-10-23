#pragma once

#include <optional>
#include <utility>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/history_verification.hpp>

namespace dds::belief_evaluation
{

/// Every card played before an evaluation's root, and the seat that led
/// first. Common knowledge, immutable, and not tied to any root.
///
/// It checks on its own that the cards are well-formed and distinct, and
/// that `opening_leader` is a valid seat. It cannot check that
/// `opening_leader` led the first trick: that needs a root.
/// `ExhaustiveLayoutSource` makes that check, through `verify_history`.
///
/// **`evaluate()` never checks `EvaluateOptions::play_record` against a
/// root.** Keeping it consistent with the `LayoutSource` is the caller's
/// job.
///
/// It holds no contract or score. `evaluate()` takes `declarer` and
/// `tricks_needed` as arguments, and a second copy could disagree.
class PlayRecord
{
public:
    /// Rejects with the `HistoryVerdict` values that need no `Deal`:
    /// - `InvalidInput`: `cards.number` outside `[0, 52]`, `opening_leader`
    ///   outside `[0, DDS_HANDS)`, or a card's `suit` or `rank` out of range;
    /// - `DuplicatedCard`: the same card twice in `cards`.
    ///
    /// An empty record and an incomplete last trick are both legal.
    ///
    /// The only constructor is private, so every `PlayRecord` has passed this
    /// check.
    static auto create(PlayTraceBin const& cards, int opening_leader)
        -> std::pair<std::optional<PlayRecord>, HistoryVerdict>;

    [[nodiscard]] auto cards() const -> PlayTraceBin const&;
    [[nodiscard]] auto opening_leader() const -> int;

private:
    PlayRecord(PlayTraceBin cards, int opening_leader);

    PlayTraceBin cards_;
    int opening_leader_;
};

}  // namespace dds::belief_evaluation
