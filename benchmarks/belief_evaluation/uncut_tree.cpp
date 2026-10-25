#include "uncut_tree.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/trick.hpp>

namespace dds::belief_evaluation::benchmarks
{

namespace
{
    // A local copy of card_count from tests/belief_evaluation/
    // test_support.hpp, which is private to the core C++ tests. fixtures.cpp
    // copies its binomial coefficient for the same reason.
    auto card_count(Deal const& deal, int hand) -> int
    {
        int count = 0;
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            count += std::popcount(deal.remainCards[hand][suit]);
        }
        return count;
    }

    // Whether `seat` can be asked for a card at `node`: false as soon as any
    // layout at the node shows `seat` void.
    //
    // It reads `node.layouts`, not `node.state.known_holdings`. For a defender
    // seat known_holdings is the whole pool, the same value for both
    // defenders (see ObservationState::known_holdings), so it can read
    // non-zero for a defender who is void in every layout. For declarer and
    // dummy the layouts all agree with known_holdings, so the check costs
    // nothing extra there.
    //
    // "Any layout void", not "every layout void": expand_defender_node calls
    // delta once per layout, and delta cannot answer for a layout where the
    // seat holds nothing (WeightedCard requires a held card). A mixed node
    // would fail expansion on those layouts. Stopping first avoids a spurious
    // "uncut walker failed".
    auto seat_can_play(BeliefNode const& node, int seat) -> bool
    {
        for (Deal const& layout : node.layouts)
        {
            if (card_count(layout, seat) == 0)
            {
                return false;
            }
        }
        return true;
    }

    // Every card `seat` may legally play first at `deal`, flattened from
    // trick.hpp's legal_cards(). The same two lines as evaluate.cpp's
    // enumerate_legal_cards, which is private to that file.
    auto enumerate_legal_cards(Deal const& deal, int seat) -> std::vector<Card>
    {
        std::array<unsigned, DDS_SUITS> const legal = legal_cards(deal, seat);
        std::vector<Card> cards;
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            for (int rank = 2; rank <= 14; ++rank)
            {
                if ((legal[suit] & (1u << rank)) != 0)
                {
                    cards.push_back(Card{suit, rank});
                }
            }
        }
        return cards;
    }
}  // namespace

auto count_uncut_nodes(
    Deal const& root, int declarer, int tricks_needed, LayoutSource const& source,
    DeclarerStrategy const& pi, DefenderStrategy const& delta) -> std::optional<std::uint64_t>
{
    RootConstructionResult const root_result = make_root(root, declarer, tricks_needed, source, {});
    if (! root_result.node.has_value())
    {
        return std::nullopt;
    }

    int const dummy = (declarer + 2) % DDS_HANDS;
    std::uint64_t count = 0;

    // An explicit worklist, not recursion. The trees here are the ones
    // evaluate() would cut short, so they can run much deeper. The call stack
    // is not trusted with that.
    std::vector<BeliefNode> pending;
    pending.push_back(*root_result.node);

    // True only on the first iteration. The worklist starts with the root
    // alone, so the first pop is the root. A declarer-led root needs
    // different handling from every other declarer node (see below).
    bool is_root = true;

    while (! pending.empty())
    {
        BeliefNode const node = std::move(pending.back());
        pending.pop_back();
        ++count;
        bool const was_root = is_root;
        is_root = false;

        if (is_terminal(node))
        {
            continue;
        }

        int const seat = seat_on_play(node.state.known_holdings);
        if (! seat_can_play(node, seat))
        {
            // Not is_terminal(), since another hand still holds cards, but this
            // seat has nothing to play. A structural boundary, not a cut (see
            // seat_can_play).
            continue;
        }

        if (seat == declarer || seat == dummy)
        {
            // Not `const`, so *result.child can be moved, not copied. A copy would
            // deep-copy every enqueued BeliefNode, which is O(layout count). On
            // pool8 (12,870 layouts) that cost would dominate the worklist.
            ExpandResult result = expand_declarer_node(node, pi);
            if (! result.child.has_value())
            {
                return std::nullopt;
            }
            pending.push_back(std::move(*result.child));

            if (was_root)
            {
                // evaluate() expands every other legal root card too, to fill
                // root_children (see its root handling in evaluate.cpp). Its general
                // recursion, like expand_declarer_node above, follows only pi's card.
                // Without this branch a declarer-led root is undercounted. It mirrors
                // evaluate()'s structure with the same public pieces:
                // expand_declarer_node for the chosen card, make_declarer_children for
                // the rest.
                std::vector<Card> const legal = enumerate_legal_cards(node.state.known_holdings, seat);
                std::vector<Card> other_cards;
                other_cards.reserve(legal.size());
                for (Card const& card : legal)
                {
                    if (card.suit != result.card.suit || card.rank != result.card.rank)
                    {
                        other_cards.push_back(card);
                    }
                }
                std::vector<BeliefNode> other_children = make_declarer_children(node, other_cards);
                for (BeliefNode& child : other_children)
                {
                    pending.push_back(std::move(child));
                }
            }
        }
        else
        {
            ExpandDefenderResult result = expand_defender_node(node, delta);
            if (! result.children.has_value())
            {
                return std::nullopt;
            }
            for (BeliefNode& child : *result.children)
            {
                pending.push_back(std::move(child));
            }
        }
    }

    return count;
}

}  // namespace dds::belief_evaluation::benchmarks
