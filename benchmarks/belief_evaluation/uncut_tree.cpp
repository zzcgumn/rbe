#include "uncut_tree.hpp"

#include <bit>
#include <cstdint>
#include <optional>
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
    // Local, matching library/tests/belief_evaluation/test_support.hpp's
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

    while (! pending.empty())
    {
        BeliefNode const node = std::move(pending.back());
        pending.pop_back();
        ++count;

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
            ExpandResult const result = expand_declarer_node(node, pi);
            if (! result.child.has_value())
            {
                return std::nullopt;
            }
            pending.push_back(*result.child);
        }
        else
        {
            ExpandDefenderResult const result = expand_defender_node(node, delta);
            if (! result.children.has_value())
            {
                return std::nullopt;
            }
            for (BeliefNode const& child : *result.children)
            {
                pending.push_back(child);
            }
        }
    }

    return count;
}

}  // namespace dds::belief_evaluation::benchmarks
