#include <belief_evaluation/node.hpp>

#include <bit>
#include <cassert>

#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/rank_map.hpp>
#include <utility/constants.h>

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

    /// The cards already played to root's trick in progress (0..3 of
    /// them), in order — the only prior history recoverable from a Deal:
    /// once a trick resolves, currentTrick* is cleared and no record of
    /// what was played to it survives, so a root that starts after one or
    /// more completed tricks can never have those tricks reconstructed
    /// from root_layout alone. Rank 0 is the empty-slot sentinel, matching
    /// validation.cpp's led_suit() and trick.cpp's played_count().
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

auto make_root(
    Deal const& root_layout,
    int declarer,
    int tricks_needed,
    LayoutSource const& source) -> std::optional<BeliefNode>
{
    std::optional<std::uint64_t> const size = source.size();
    if (! size.has_value())
    {
        return std::nullopt;
    }

    int const dummy = (declarer + 2) % DDS_HANDS;

    BeliefNode node{};
    node.state.trump = root_layout.trump;
    node.state.first = root_layout.first;
    node.state.history = history_for(root_layout);
    node.state.declarer = declarer;
    node.state.tricks_needed = tricks_needed;
    node.state.tricks_won_by_declarer = 0;
    node.state.known_holdings = known_holdings_for(root_layout, declarer, dummy);
    node.state.ranks = make_rank_map(root_layout);

    // Not reserved to *size: source.size() is user-supplied and may be far
    // larger than the number of layouts that actually survive filtering
    // (or simply enormous), so reserving it up front risks an oversized
    // allocation attempt before any filtering happens. Ordinary amortised
    // growth is safe here: nothing holds a reference into node.layouts
    // until after this function returns a fully-built node, so growth
    // during construction cannot invalidate anything a caller has seen.
    for (std::uint64_t i = 0; i < *size; ++i)
    {
        Deal const candidate = source.at(i);
        if (is_consistent(candidate, root_layout, declarer, dummy))
        {
            node.layouts.push_back(candidate);
            node.p.push_back(1.0);
        }
    }

    if (node.layouts.empty())
    {
        return std::nullopt;
    }

    node.kappa = 1.0 / static_cast<double>(node.layouts.size());
    return node;
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
