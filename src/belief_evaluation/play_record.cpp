#include <belief_evaluation/play_record.hpp>

namespace dds::belief_evaluation
{

namespace
{
    /// A record carries no `Deal`, so its own validation reuses
    /// `verify_history` against a throwaway empty root purely to reach its
    /// two root-independent checks (shape, internal duplicates) -- both run,
    /// and can return, before `verify_history` ever reads `root`: neither
    /// touches `root` at all. Any verdict *other than*
    /// `InvalidInput`/`DuplicatedCard` therefore means those two checks
    /// already passed, whatever the placeholder root made `verify_history`'s
    /// later, root-dependent checks say -- their answer is meaningless here
    /// and is exactly why it's discarded rather than surfaced.
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
