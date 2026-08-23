#include <belief_evaluation/node.hpp>

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
    node.state.history = PlayTraceBin{};
    node.state.declarer = declarer;
    node.state.tricks_needed = tricks_needed;
    node.state.tricks_won_by_declarer = 0;
    node.state.known_holdings = known_holdings_for(root_layout, declarer, dummy);
    node.state.ranks = make_rank_map(root_layout);

    node.layouts.reserve(*size);
    node.p.reserve(*size);
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
