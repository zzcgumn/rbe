#include <belief_evaluation/node.hpp>

#include <bit>
#include <cassert>

#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/layout_key.hpp>
#include <belief_evaluation/rank_map.hpp>
#include <utility/constants.h>

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
            if (defender_pool(candidate, declarer, dummy, suit)
                != defender_pool(root, declarer, dummy, suit))
            {
                return false;
            }
        }
        return true;
    }

    /// `root`'s declarer and dummy holdings verbatim; each defender's entry
    /// replaced by the union pool the two defenders hold between them, per
    /// ObservationState::known_holdings' own doxygen.
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

auto root_observation_state(Deal const& root_layout, int declarer, int tricks_needed) -> ObservationState
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
    return state;
}

auto make_root(
    Deal const& root_layout,
    int declarer,
    int tricks_needed,
    LayoutSource const& source,
    RootOptions const& options) -> RootConstructionResult
{
    std::optional<std::uint64_t> const size = source.size();
    if (! size.has_value())
    {
        return RootConstructionResult{std::nullopt, RootFailure::SourceNotEnumerable};
    }

    // Checked before the scan touches source.at() at all: a sample_size of
    // exactly 0 would otherwise satisfy the loop's own break condition
    // (node.layouts.size() >= *sample_size, trivially true at 0) on its
    // very first check, producing an empty node whose outcome computes to
    // SampleFilled and whose failure would then read NoLayoutSurvived --
    // misreporting a degenerate request as "the source was checked and had
    // nothing consistent in it," which it was never given the chance to be.
    if (options.sample_size.has_value() && *options.sample_size == 0)
    {
        return RootConstructionResult{std::nullopt, RootFailure::SampleSizeZero};
    }

    int const dummy = (declarer + 2) % DDS_HANDS;

    BeliefNode node{};
    node.state = root_observation_state(root_layout, declarer, tricks_needed);

    // Not reserved to *size: source.size() is user-supplied and may be far
    // larger than the number of layouts that actually survive filtering
    // (or simply enormous), so reserving it up front risks an oversized
    // allocation attempt before any filtering happens. Ordinary amortised
    // growth is safe here: nothing holds a reference into node.layouts
    // until after this function returns a fully-built node, so growth
    // during construction cannot invalidate anything a caller has seen.
    //
    // Scanned from index 0 regardless of whether options.sample_size or
    // options.scan_budget is set: any randomness in which layouts get
    // drawn is the source's own ordering, never this loop's -- see
    // RootOptions and this function's own doxygen.
    //
    // sample_size is checked before scan_budget at every step, so a
    // layout that fills the sample is never charged against the budget --
    // the priority this function's own doxygen documents for when both
    // would bind at once.
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
            // Root-space key, computed from the candidate before any card is
            // played and with a fixed defender seat -- see
            // BeliefNode::root_keys' own doxygen for why both of those must
            // hold everywhere this is computed.
            node.root_keys.push_back(layout_key(candidate, (declarer + 1) % DDS_HANDS));
        }
    }

    // Why the scan stopped: the source ran out (i reached *size, whatever
    // either cap was -- reaching a cap on the very last item still means
    // there was nothing left to find, not a premature stop), else whichever
    // cap actually bound, sample_size taking priority to match the loop's
    // own check order above.
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

    // True exactly when the scan stopped because a cap bound, not merely
    // because one was supplied -- see this function's own doxygen for why
    // options.sample_size.has_value() alone would be wrong here.
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
    assert(! node.layouts.empty());  // guaranteed by every construction site in this module
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

    // How many cards are already in the trick currently in progress (0..3),
    // read directly off currentTrickRank -- rank 0 is the empty-slot
    // sentinel, matching trick.cpp's own played_count() and
    // validation.cpp's led_suit(). deal.first is that trick's leader (see
    // trick.hpp/play()), not the root's leader once play has moved on, so
    // this is the rotation the offset below has to be measured against.
    int played_to_current_trick = 0;
    while (played_to_current_trick < 3 && deal.currentTrickRank[played_to_current_trick] != 0)
    {
        ++played_to_current_trick;
    }

    // declarer's rotational position within this trick, 0 = leader. If
    // that position is already behind how many cards have been played,
    // declarer's own card count is one short of tricks remaining --
    // declarer has already contributed its card to the trick in progress.
    int const declarer_position = (state.declarer - deal.first + DDS_HANDS) % DDS_HANDS;
    bool const declarer_already_played = declarer_position < played_to_current_trick;
    return declarer_already_played ? card_count + 1 : card_count;
}

}  // namespace dds::belief_evaluation
