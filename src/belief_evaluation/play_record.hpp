#pragma once

#include <optional>
#include <utility>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/history_verification.hpp>

namespace dds::belief_evaluation
{

/// Every card played before an evaluation's root, and the seat that opened
/// it -- common knowledge, immutable, and independent of any particular
/// root: unlike `ExhaustiveLayoutSource`'s own `history`/`opening_leader`
/// pair, a `PlayRecord` is never checked against a `Deal` at construction,
/// since it does not have one. What it *can* guarantee on its own: the
/// cards are shape-valid and no card appears twice, and `opening_leader` is
/// a seat in range. What it cannot: that `opening_leader` is the seat that
/// actually led `cards`' first trick -- that check needs a root to replay
/// against, and is made only once the record reaches
/// `ExhaustiveLayoutSource` or `evaluate()`.
///
/// Deliberately does not carry a contract or a score -- `evaluate()`
/// already takes `declarer` and `tricks_needed` separately, and a record
/// that also carried the contract would let a caller pass one inconsistent
/// with the arguments beside it.
class PlayRecord
{
public:
    /// Rejects with the same `HistoryVerdict` vocabulary
    /// `verify_history`/`ExhaustiveLayoutSource` already use, restricted to
    /// the two causes that need no `Deal` to mean anything: `InvalidInput`
    /// (`cards.number` outside `[0, 52]`, `opening_leader` outside
    /// `[0, DDS_HANDS)`, or a card's own `suit`/`rank` out of range) and
    /// `DuplicatedCard` (the same card twice within `cards` itself). An
    /// empty record and a trailing incomplete trick are both legal --
    /// there is nothing to reject in either shape alone.
    ///
    /// No way to reach a live `PlayRecord` whose cards and leader failed
    /// this check: the only constructor is private, reached only from
    /// here.
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
