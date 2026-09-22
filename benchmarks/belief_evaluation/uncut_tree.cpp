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
    // Local, matching tests/belief_evaluation/test_support.hpp's
    // own card_count exactly -- that header is private to the core C++
    // test suite (its own comment says so), so duplicated here rather
    // than reused, the same choice fixtures.cpp's own binomial-coefficient
    // copy already made for the same reason.
    auto card_count(Deal const& deal, int hand) -> int
    {
        int count = 0;
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            count += std::popcount(deal.remainCards[hand][suit]);
        }
        return count;
    }

    // Whether `seat` could conceivably be asked for a card at `node`:
    // false the moment *any* layout the node holds shows `seat` void
    // there. `node.state.known_holdings` is exact for declarer and dummy
    // but is the *aggregate pool* for a defender seat -- both defenders'
    // entries hold the same union value, not each one's own split (see
    // ObservationState::known_holdings's own doxygen) -- so it can read
    // nonzero for a defender who is actually void in every layout this
    // node holds, once belief has narrowed enough that a real
    // per-layout split is already certain. Checked directly against
    // `node.layouts` instead, which is correct for both kinds of seat
    // (declarer/dummy hold the same exact hand in every layout by
    // construction, so checking layouts there costs nothing extra and
    // agrees with checking known_holdings). "Any layout void" rather
    // than "every layout void": expand_defender_node calls delta once
    // per layout and delta cannot legally answer for a layout where the
    // seat holds nothing there (WeightedCard's own contract requires the
    // card be held), so a mixed node (void in some layouts, not others)
    // would fail expansion on exactly those layouts regardless -- this
    // check stops before that happens rather than reaching it and
    // reporting a spurious "uncut walker failed".
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
    // trick.hpp's own public legal_cards() -- identical to evaluate.cpp's
    // own private enumerate_legal_cards (same two-line flattening of the
    // same public bitmask function), duplicated here rather than reused
    // since that one is anonymous-namespace-private to evaluate.cpp.
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

    // An explicit worklist, not native recursion: a defender node can
    // have several children and this walker's whole point is trees a real
    // evaluate() call would have cut short, which can run considerably
    // deeper than any cut version of the same fixture -- no reason to
    // trust the call stack with that.
    std::vector<BeliefNode> pending;
    pending.push_back(*root_result.node);

    // Set once, on the very first iteration below (the worklist starts
    // with exactly one entry -- the root -- so the first pop is
    // guaranteed to be it, and nothing pushed during that same iteration
    // can be popped before the loop moves on). Needed because a
    // declarer-led *root* specifically needs different handling from
    // every other declarer node -- see the branch below.
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
            // Not is_terminal() (some other hand still holds cards) but
            // this seat has nothing to contribute -- a structural boundary,
            // not a cut. See this file's own header doxygen and
            // seat_can_play's own comment.
            continue;
        }

        if (seat == declarer || seat == dummy)
        {
            // Not `const`: result is dead after this branch either way,
            // and moving *result.child (rather than copying it) avoids an
            // otherwise-avoidable O(layout-count) deep copy of every
            // BeliefNode this walker enqueues -- on the larger ladders
            // (pool8's own 12,870-layout rungs) this walker's own worklist
            // is exactly where that cost would show up.
            ExpandResult result = expand_declarer_node(node, pi);
            if (! result.child.has_value())
            {
                return std::nullopt;
            }
            pending.push_back(std::move(*result.child));

            if (was_root)
            {
                // evaluate()'s own root-handling block (evaluate.cpp)
                // additionally expands every *other* legal root card too,
                // to populate root_children -- p_make()'s own general
                // recursion, which handles every declarer node but the
                // root (including every other one this walker's own
                // expand_declarer_node call above reaches), only ever
                // follows pi's single chosen card. Missing this would
                // silently undercount a declarer-led root's true node
                // count -- found directly against evaluate.cpp's own
                // root-handling block, not assumed. Fixed by mirroring
                // its exact chosen/other-cards structure with the same
                // two public pieces it is itself built from:
                // expand_declarer_node for the chosen card (above),
                // make_declarer_children for the rest.
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
