#include <belief_evaluation/play_record.hpp>

namespace dds::belief_evaluation
{

namespace
{
    /// Runs `verify_history` against an empty placeholder root, to reach its
    /// two checks that need no root: shape and duplicates. Both run before
    /// `root` is read. Any other verdict comes from the placeholder and is
    /// discarded.
    auto verify_shape_and_duplicates(PlayTraceBin const& cards, int opening_leader) -> HistoryVerdict
    {
        Deal const placeholder_root{};
        HistoryVerdict const verdict = verify_history(placeholder_root, /*declarer=*/0, cards, opening_leader);
        if (verdict == HistoryVerdict::InvalidInput || verdict == HistoryVerdict::DuplicatedCard)
        {
            return verdict;
        }
        return HistoryVerdict::Consistent;
    }
}  // namespace

PlayRecord::PlayRecord(PlayTraceBin cards, int opening_leader)
    : cards_(cards)
    , opening_leader_(opening_leader)
{
}

auto PlayRecord::create(PlayTraceBin const& cards, int opening_leader)
    -> std::pair<std::optional<PlayRecord>, HistoryVerdict>
{
    HistoryVerdict const verdict = verify_shape_and_duplicates(cards, opening_leader);
    if (verdict != HistoryVerdict::Consistent)
    {
        return {std::nullopt, verdict};
    }
    return {PlayRecord(cards, opening_leader), HistoryVerdict::Consistent};
}

auto PlayRecord::cards() const -> PlayTraceBin const&
{
    return cards_;
}

auto PlayRecord::opening_leader() const -> int
{
    return opening_leader_;
}

}  // namespace dds::belief_evaluation
