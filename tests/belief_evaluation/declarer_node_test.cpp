#include <gtest/gtest.h>

#include <utility/constants.h>

#include <belief_evaluation/dds_types.hpp>
#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/trick.hpp>
#include <belief_evaluation/validation.hpp>

#include "test_support.hpp"

namespace
{
    constexpr int King = 13;
    constexpr int Ace = 14;

    constexpr int North = 0;  // declarer
    constexpr int South = 2;  // dummy
    constexpr int West = 3;   // a defender

    /// A node with North (declarer) on lead, holding the ace and king of
    /// spades; South (dummy) holds nothing relevant. One layout, p = 1,
    /// kappa = 1.
    auto make_declarer_on_play_node() -> BeliefNode
    {
        BeliefNode node{};
        Deal layout{};
        layout.trump = DDS_NOTRUMP;
        layout.first = North;
        layout.remainCards[North][0] = holding({Ace, King});

        node.state.trump = DDS_NOTRUMP;
        node.state.first = North;
        node.state.declarer = North;
        node.state.tricks_needed = 1;
        node.state.tricks_won_by_declarer = 0;
        node.state.known_holdings = layout;
        node.layouts = {layout};
        node.p = {1.0};
        node.kappa = 1.0;
        return node;
    }

    /// Same shape, but the trick-in-progress state puts dummy (South) on
    /// lead instead of declarer.
    auto make_dummy_on_play_node() -> BeliefNode
    {
        BeliefNode node = make_declarer_on_play_node();
        node.state.known_holdings.first = South;
        node.state.known_holdings.remainCards[North][0] = 0;
        node.state.known_holdings.remainCards[South][0] = holding({Ace, King});
        node.layouts[0] = node.state.known_holdings;
        return node;
    }
}

class DeclarerNodeTest : public ::testing::Test
{
};

TEST_F(DeclarerNodeTest, PiIsCalledWhenDeclarerIsOnPlay)
{
    BeliefNode const node = make_declarer_on_play_node();
    RecordingDeclarerStrategy recorder(Card{0, Ace});

    ExpandResult const result = expand_declarer_node(node, recorder.as_strategy());

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
    // "the seat on play is declarer's side" is easy to write as "the seat
    // on play is declarer" by mistake, so this is its own test rather than
    // folded into the previous one.
    BeliefNode const node = make_dummy_on_play_node();
    RecordingDeclarerStrategy recorder(Card{0, Ace});

    ExpandResult const result = expand_declarer_node(node, recorder.as_strategy());

    ASSERT_TRUE(result.child.has_value());
    ASSERT_EQ(recorder.calls().size(), 1u);
}

TEST_F(DeclarerNodeTest, TheChildsBeliefSetAndWeightsCarryOverFromTheParent)
{
    BeliefNode const node = make_declarer_on_play_node();
    RecordingDeclarerStrategy recorder(Card{0, Ace});

    ExpandResult const result = expand_declarer_node(node, recorder.as_strategy());

    ASSERT_TRUE(result.child.has_value());
    BeliefNode const& child = *result.child;
    EXPECT_EQ(child.p, node.p);            // untouched, not just summing to the same total
    EXPECT_DOUBLE_EQ(child.kappa, node.kappa);
    ASSERT_EQ(child.layouts.size(), node.layouts.size());
    // Each layout is the parent's advanced by the played card — the belief
    // set's size and correspondence carry over even though the card played
    // is now gone from remainCards. Deal has no operator==, so compare the
    // field that changed.
    Deal const expected = play(node.layouts[0], Card{0, Ace});
    EXPECT_EQ(child.layouts[0].remainCards[North][0], expected.remainCards[North][0]);
    EXPECT_EQ(child.layouts[0].currentTrickSuit[0], expected.currentTrickSuit[0]);
    EXPECT_EQ(child.layouts[0].currentTrickRank[0], expected.currentTrickRank[0]);
}

TEST_F(DeclarerNodeTest, ACardNotHeldIsRejectedThroughValidateDeclarerCard)
{
    BeliefNode const node = make_declarer_on_play_node();
    // North does not hold the queen of spades.
    RecordingDeclarerStrategy recorder(Card{0, 12});

    ExpandResult const result = expand_declarer_node(node, recorder.as_strategy());

    EXPECT_FALSE(result.child.has_value());
    EXPECT_EQ(result.error, ValidationError::CardNotHeld);
}

TEST_F(DeclarerNodeTest, ACardIllegalForTheTrickIsRejectedThroughValidateDeclarerCard)
{
    BeliefNode node = make_declarer_on_play_node();
    // A heart has been led; North holds no hearts but does hold spades, so
    // playing a spade is illegal — North must follow suit or, since void,
    // may discard, but here North is not void: give North a heart too.
    node.state.known_holdings.currentTrickSuit[0] = 1;  // hearts led
    node.state.known_holdings.currentTrickRank[0] = 2;
    node.state.known_holdings.first = West;  // West led the heart; North is next to play
    node.state.known_holdings.remainCards[North][1] = holding({King});  // North holds a heart
    node.layouts[0] = node.state.known_holdings;

    RecordingDeclarerStrategy recorder(Card{0, Ace});  // North plays a spade instead

    ExpandResult const result = expand_declarer_node(node, recorder.as_strategy());

    EXPECT_FALSE(result.child.has_value());
    EXPECT_EQ(result.error, ValidationError::CardIllegalForTrick);
}

// --- mass pass-through with more than one child -------------------------
//
// The evaluator itself never currently produces two declarer children,
// so this is the only test that can falsify parent_mass / n_children —
// which equals parent_mass whenever n_children == 1, i.e. every other test
// in this file.

TEST_F(DeclarerNodeTest, EveryDeclarerChildInheritsTheParentsFullMass)
{
    BeliefNode parent = make_declarer_on_play_node();
    parent.p = {0.4, 0.2};
    parent.kappa = 0.5;
    parent.layouts = {parent.layouts[0], parent.layouts[0]};
    // 0.5 * (0.4 + 0.2) = 0.3
    std::vector<BeliefNode> const children =
        make_declarer_children(parent, {Card{0, Ace}, Card{0, King}});

    ASSERT_EQ(children.size(), 2u);
    for (BeliefNode const& child : children)
    {
        EXPECT_DOUBLE_EQ(node_mass(child), 0.3);
    }
}
