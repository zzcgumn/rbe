#include <belief_evaluation/node.hpp>

#include <bit>
#include <cassert>

#include <api/dds_constants.hpp>
#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/layout_key.hpp>
#include <belief_evaluation/rank_map.hpp>

namespace dds::belief_evaluation
{

namespace
{
    /// The outstanding pool one suit's two defenders hold between them,
    /// regardless of how it is split.
    auto defender_pool(Deal const& deal, int declarer, int dummy, int suit) -> unsigned
    {
        unsigned pool = 0;
        for (int hand = 0; hand < DDS_HANDS; ++hand)
        {
            if (hand != declarer && hand != dummy)
            {
                pool |= deal.remainCards[hand][suit];
            }
        }
        return pool;
    }


    /// The declarer and dummy holdings of `root`, with each defender entry
    /// replaced by the defenders' shared pool. See
    /// ObservationState::known_holdings.
    auto known_holdings_for(Deal const& root, int declarer, int dummy) -> Deal
    {
        Deal result = root;
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            unsigned const pool = defender_pool(root, declarer, dummy, suit);
            for (int hand = 0; hand < DDS_HANDS; ++hand)
            {
                if (hand != declarer && hand != dummy)
                {
                    result.remainCards[hand][suit] = pool;
                }
            }
        }
        return result;
    }
}

auto is_consistent(Deal const& candidate, Deal const& root, int declarer, int dummy) -> bool
{
    if (candidate.trump != root.trump || candidate.first != root.first)
    {
        return false;
    }
    for (int i = 0; i < 3; ++i)
    {
        if (candidate.currentTrickSuit[i] != root.currentTrickSuit[i]
            || candidate.currentTrickRank[i] != root.currentTrickRank[i])
        {
            return false;
        }
    }

    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        if (candidate.remainCards[declarer][suit] != root.remainCards[declarer][suit]
            || candidate.remainCards[dummy][suit] != root.remainCards[dummy][suit])
        {
            return false;
        }
        if (defender_pool(candidate, declarer, dummy, suit) != defender_pool(root, declarer, dummy, suit))
        {
            return false;
        }
    }
    return true;
}

auto history_for(Deal const& root_layout) -> PlayTraceBin
{
    PlayTraceBin history{};
    while (history.number < 3 && root_layout.currentTrickRank[history.number] != 0)
    {
        history.suit[history.number] = root_layout.currentTrickSuit[history.number];
        history.rank[history.number] = root_layout.currentTrickRank[history.number];
        ++history.number;
    }
    return history;
}

auto root_observation_state(Deal const& root_layout, int declarer, int tricks_needed, PlayRecord const* record)
    -> ObservationState
{
    int const dummy = (declarer + 2) % DDS_HANDS;

    ObservationState state{};
    state.trump = root_layout.trump;
    state.first = root_layout.first;
    state.history = history_for(root_layout);
    state.declarer = declarer;
    state.tricks_needed = tricks_needed;
    state.tricks_won_by_declarer = 0;
    state.known_holdings = known_holdings_for(root_layout, declarer, dummy);
    state.ranks = make_rank_map(root_layout);
    state.play_record = record;
    return state;
}

auto make_root(
    Deal const& root_layout,
    int declarer,
    int tricks_needed,
    LayoutSource const& source,
    RootOptions const& options,
    PlayRecord const* record) -> RootConstructionResult
{
    std::optional<std::uint64_t> const size = source.size();
    if (! size.has_value())
    {
        return RootConstructionResult{std::nullopt, RootFailure::SourceNotEnumerable};
    }

    // Checked before any source.at() call. Otherwise sample_size 0 would end
    // the loop at once and be reported as NoLayoutSurvived.
    if (options.sample_size.has_value() && *options.sample_size == 0)
    {
        return RootConstructionResult{std::nullopt, RootFailure::SampleSizeZero};
    }

    int const dummy = (declarer + 2) % DDS_HANDS;

    BeliefNode node{};
    node.state = root_observation_state(root_layout, declarer, tricks_needed, record);

    // Not reserved to *size: source.size() can be far larger than the number
    // of layouts that survive. Nothing refers into node.layouts until this
    // returns, so growth is safe.
    //
    // Scanned from index 0 always: randomness comes from the order of the
    // source.
    //
    // sample_size is checked before scan_budget at every step, so a layout
    // that fills the sample is never charged to the budget.
    std::uint64_t scanned = 0;
    std::uint64_t i = 0;
    for (; i < *size; ++i)
    {
        if (options.sample_size.has_value() && node.layouts.size() >= *options.sample_size)
        {
            break;
        }
        if (options.scan_budget.has_value() && scanned >= *options.scan_budget)
        {
            break;
        }
        Deal const candidate = source.at(i);
        ++scanned;
        if (is_consistent(candidate, root_layout, declarer, dummy))
        {
            node.layouts.push_back(candidate);
            node.p.push_back(1.0);
            // Root-space key: from the candidate before any card is played, with the
            // fixed defender seat. See BeliefNode::root_keys.
            node.root_keys.push_back(layout_key(candidate, (declarer + 1) % DDS_HANDS));
        }
    }

    // Why the scan stopped. Reaching the end of source is SourceExhausted,
    // even if a cap was reached on the last item. Otherwise the cap that
    // stopped it, sample_size first, as in the loop.
    ScanOutcome const outcome = (i >= *size)
        ? ScanOutcome::SourceExhausted
        : ((options.sample_size.has_value() && node.layouts.size() >= *options.sample_size)
               ? ScanOutcome::SampleFilled
               : ScanOutcome::BudgetExhausted);

    if (node.layouts.empty())
    {
        RootFailure const failure = (outcome == ScanOutcome::BudgetExhausted)
            ? RootFailure::ScanBudgetExhausted
            : RootFailure::NoLayoutSurvived;
        return RootConstructionResult{std::nullopt, failure};
    }

    // True when a cap stopped the scan, not when a cap was given. See the
    // header.
    node.is_sample = outcome != ScanOutcome::SourceExhausted;

    node.kappa = 1.0 / static_cast<double>(node.layouts.size());
    return RootConstructionResult{std::move(node), RootFailure::None, outcome};
}

auto node_mass(BeliefNode const& node) -> double
{
    KahanAccumulator total;
    for (Probability const p_i : node.p)
    {
        total.add(node.kappa * p_i);
    }
    return total.value();
}

auto terminal_value(BeliefNode const& node) -> double
{
    return (node.state.tricks_won_by_declarer >= node.state.tricks_needed) ? node_mass(node) : 0.0;
}

auto is_terminal(BeliefNode const& node) -> bool
{
    assert(! node.layouts.empty());  // every construction site guarantees this
    Deal const& layout = node.layouts.front();
    for (int hand = 0; hand < DDS_HANDS; ++hand)
    {
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            if (layout.remainCards[hand][suit] != 0)
            {
                return false;
            }
        }
    }
    return true;
}

auto tricks_remaining(ObservationState const& state) -> int
{
    Deal const& deal = state.known_holdings;

    int card_count = 0;
    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        card_count += std::popcount(deal.remainCards[state.declarer][suit]);
    }

    // The number of cards in the current trick (0..3); rank 0 marks an empty
    // slot. deal.first leads this trick. It is not the root's leader once
    // play has moved on.
    int played_to_current_trick = 0;
    while (played_to_current_trick < 3 && deal.currentTrickRank[played_to_current_trick] != 0)
    {
        ++played_to_current_trick;
    }

    // Declarer's position in this trick, 0 = leader. If declarer has already
    // played, its card count is one short of the tricks remaining.
    int const declarer_position = (state.declarer - deal.first + DDS_HANDS) % DDS_HANDS;
    bool const declarer_already_played = declarer_position < played_to_current_trick;
    return declarer_already_played ? card_count + 1 : card_count;
}

}  // namespace dds::belief_evaluation
