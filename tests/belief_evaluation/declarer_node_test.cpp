#include <gtest/gtest.h>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/trick.hpp>
#include <belief_evaluation/validation.hpp>

#include "test_support.hpp"

// Tests for expand_declarer_node and make_declarer_children: when pi is
// called, what the child carries over, and illegal cards.

namespace be = dds::belief_evaluation;

namespace
{
    constexpr int King = 13;
    constexpr int Ace = 14;

    constexpr int North = 0;  // declarer
    constexpr int East = 1;   // a defender: (declarer + 1) % DDS_HANDS
    constexpr int South = 2;  // dummy
    constexpr int West = 3;   // a defender

    /// A node with North (declarer) on lead, holding the ace and king of
    /// spades. South (dummy) holds nothing relevant. One layout, p = 1,
    /// kappa = 1.
    auto make_declarer_on_play_node() -> be::BeliefNode
    {
        be::BeliefNode node{};
        Deal layout{};
        layout.trump = DDS_NOTRUMP;
        layout.first = North;
        layout.remainCards[North][0] = be::holding({Ace, King});

        node.state.trump = DDS_NOTRUMP;
        node.state.first = North;
        node.state.declarer = North;
        node.state.tricks_needed = 1;
        node.state.tricks_won_by_declarer = 0;
        node.state.known_holdings = layout;
        node.layouts = {layout};
        node.p = {1.0};
        // East is (declarer + 1) % DDS_HANDS, make_root's fixed seat (see
        // node.hpp).
        node.root_keys = {be::layout_key(layout, East)};
        node.kappa = 1.0;
        return node;
    }

    /// Same shape, but the trick-in-progress state puts dummy (South) on
    /// lead instead of declarer.
    auto make_dummy_on_play_node() -> be::BeliefNode
    {
        be::BeliefNode node = make_declarer_on_play_node();
        node.state.known_holdings.first = South;
        node.state.known_holdings.remainCards[North][0] = 0;
        node.state.known_holdings.remainCards[South][0] = be::holding({Ace, King});
        node.layouts[0] = node.state.known_holdings;
        return node;
    }
}

class DeclarerNodeTest : public ::testing::Test
{
};

TEST_F(DeclarerNodeTest, PiIsCalledWhenDeclarerIsOnPlay)
{
    be::BeliefNode const node = make_declarer_on_play_node();
    be::RecordingDeclarerStrategy recorder(be::Card{0, Ace});

    be::ExpandResult const result = be::expand_declarer_node(node, recorder.as_strategy());

    ASSERT_TRUE(result.child.has_value());
    ASSERT_EQ(recorder.calls().size(), 1u);
    // The view pi receives is populated with real BeliefView entries, not
    // an empty placeholder.
    EXPECT_EQ(recorder.calls()[0].view_entry_count, node.layouts.size());
    EXPECT_EQ(recorder.calls()[0].state.declarer, North);
    EXPECT_EQ(recorder.calls()[0].state.tricks_needed, node.state.tricks_needed);
    EXPECT_EQ(recorder.calls()[0].state.tricks_won_by_declarer, node.state.tricks_won_by_declarer);
    EXPECT_EQ(recorder.calls()[0].state.history.number, node.state.history.number);
}

TEST_F(DeclarerNodeTest, PiIsCalledWhenDummyIsOnPlay)
{
    // "The seat on play is declarer's side" is easy to get wrong as "the seat
    // on play is declarer", so this case gets a separate test.
    be::BeliefNode const node = make_dummy_on_play_node();
    be::RecordingDeclarerStrategy recorder(be::Card{0, Ace});

    be::ExpandResult const result = be::expand_declarer_node(node, recorder.as_strategy());

    ASSERT_TRUE(result.child.has_value());
    ASSERT_EQ(recorder.calls().size(), 1u);
}

TEST_F(DeclarerNodeTest, TheChildsBeliefSetAndWeightsCarryOverFromTheParent)
{
    be::BeliefNode const node = make_declarer_on_play_node();
    be::RecordingDeclarerStrategy recorder(be::Card{0, Ace});

    be::ExpandResult const result = be::expand_declarer_node(node, recorder.as_strategy());

    ASSERT_TRUE(result.child.has_value());
    be::BeliefNode const& child = *result.child;
    EXPECT_EQ(child.p, node.p);            // untouched, not just summing to the same total
    EXPECT_DOUBLE_EQ(child.kappa, node.kappa);
    ASSERT_EQ(child.layouts.size(), node.layouts.size());
    // Each layout is the parent's, advanced by the played card. The belief
    // set's size and order carry over, though the played card has left
    // remainCards. Deal has no operator==, so compare the field that changed.
    Deal const expected = play(node.layouts[0], be::Card{0, Ace});
    EXPECT_EQ(child.layouts[0].remainCards[North][0], expected.remainCards[North][0]);
    EXPECT_EQ(child.layouts[0].currentTrickSuit[0], expected.currentTrickSuit[0]);
    EXPECT_EQ(child.layouts[0].currentTrickRank[0], expected.currentTrickRank[0]);
}

TEST_F(DeclarerNodeTest, RootKeysCarryOverUnchangedFromTheParent)
{
    // make_declarer_children copies root_keys whole. Declarer's play neither
    // filters nor renames any layout's root-space identity, unlike p or
    // layouts.
    be::BeliefNode const node = make_declarer_on_play_node();
    be::RecordingDeclarerStrategy recorder(be::Card{0, Ace});

    be::ExpandResult const result = be::expand_declarer_node(node, recorder.as_strategy());

    ASSERT_TRUE(result.child.has_value());
    be::BeliefNode const& child = *result.child;
    ASSERT_EQ(child.root_keys.size(), child.layouts.size());
    EXPECT_EQ(child.root_keys, node.root_keys);
}

TEST_F(DeclarerNodeTest, ACardNotHeldIsRejectedThroughValidateDeclarerCard)
{
    be::BeliefNode const node = make_declarer_on_play_node();
    // North does not hold the queen of spades.
    be::RecordingDeclarerStrategy recorder(be::Card{0, 12});

    be::ExpandResult const result = be::expand_declarer_node(node, recorder.as_strategy());

    EXPECT_FALSE(result.child.has_value());
    EXPECT_EQ(result.error, be::ValidationError::CardNotHeld);
}

TEST_F(DeclarerNodeTest, ACardIllegalForTheTrickIsRejectedThroughValidateDeclarerCard)
{
    be::BeliefNode node = make_declarer_on_play_node();
    // West has led a heart and North holds the king of hearts, so North
    // must follow suit. A spade is illegal.
    node.state.known_holdings.currentTrickSuit[0] = 1;  // hearts led
    node.state.known_holdings.currentTrickRank[0] = 2;
    node.state.known_holdings.first = West;  // West led the heart; North is next to play
    node.state.known_holdings.remainCards[North][1] = be::holding({King});  // North holds a heart
    node.layouts[0] = node.state.known_holdings;

    be::RecordingDeclarerStrategy recorder(be::Card{0, Ace});  // North plays a spade instead

    be::ExpandResult const result = be::expand_declarer_node(node, recorder.as_strategy());

    EXPECT_FALSE(result.child.has_value());
    EXPECT_EQ(result.error, be::ValidationError::CardIllegalForTrick);
}

// --- mass pass-through with more than one child -------------------------
//
// Every declarer child keeps the parent's full mass, not parent_mass /
// n_children. The two agree when n_children == 1, so only a test with two
// or more children can tell them apart.

TEST_F(DeclarerNodeTest, EveryDeclarerChildInheritsTheParentsFullMass)
{
    be::BeliefNode parent = make_declarer_on_play_node();
    parent.p = {0.4, 0.2};
    parent.kappa = 0.5;
    parent.layouts = {parent.layouts[0], parent.layouts[0]};
    // 0.5 * (0.4 + 0.2) = 0.3
    std::vector<be::BeliefNode> const children =
        be::make_declarer_children(parent, {be::Card{0, Ace}, be::Card{0, King}});

    ASSERT_EQ(children.size(), 2u);
    for (be::BeliefNode const& child : children)
    {
        EXPECT_DOUBLE_EQ(be::node_mass(child), 0.3);
    }
}
