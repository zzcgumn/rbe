#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include <utility/constants.h>

#include <belief_evaluation/dds_types.hpp>
#include <belief_evaluation/types.hpp>

#include "test_support.hpp"

// Guard rails for fixture construction: each helper converts a class of
// fixture mistake that would otherwise surface as a confusing failure deep
// inside the recursion (an unheld card several frames into a callback, or a
// BeliefView with fewer entries than the test author expected) into an
// assertion that names the fixture problem directly.

namespace
{
    constexpr int North = 0;
    constexpr int East = 1;
    constexpr int South = 2;
    constexpr int West = 3;

    constexpr int Spades = 0;
    constexpr int Hearts = 1;

    /// Every hand holds exactly two cards, one spade and one heart, at a
    /// trick boundary (no cards currently in progress) -- the baseline
    /// "good" fixture every test below perturbs.
    auto make_equal_hands_deal() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = North;
        deal.remainCards[North][Spades] = holding({2});
        deal.remainCards[North][Hearts] = holding({3});
        deal.remainCards[East][Spades] = holding({4});
        deal.remainCards[East][Hearts] = holding({5});
        deal.remainCards[South][Spades] = holding({6});
        deal.remainCards[South][Hearts] = holding({7});
        deal.remainCards[West][Spades] = holding({8});
        deal.remainCards[West][Hearts] = holding({9});
        return deal;
    }
}

class FixtureValidationTest : public ::testing::Test
{
};

// --- assert_equal_hand_sizes ----------------------------------------------

TEST_F(FixtureValidationTest, EqualHandSizesPass)
{
    Deal const deal = make_equal_hands_deal();
    assert_equal_hand_sizes(deal);  // no ADD_FAILURE => this test itself stays green
}

TEST_F(FixtureValidationTest, UnequalHandSizesAreRejected)
{
    Deal deal = make_equal_hands_deal();
    deal.remainCards[East][Spades] |= holding({10});  // East gets a third card

    EXPECT_NONFATAL_FAILURE(assert_equal_hand_sizes(deal), "hand 1");
}

// --- assert_pool_matches ---------------------------------------------------

TEST_F(FixtureValidationTest, MatchingPoolsPass)
{
    Deal const layout0 = make_equal_hands_deal();
    Deal const layout1 = make_equal_hands_deal();  // identical pool, same split

    assert_pool_matches({layout0, layout1});
}

TEST_F(FixtureValidationTest, MismatchedPoolIsRejected)
{
    Deal const layout0 = make_equal_hands_deal();
    Deal layout1 = make_equal_hands_deal();
    layout1.remainCards[East][Spades] = holding({10});  // spade pool now differs from layout0's

    EXPECT_NONFATAL_FAILURE(assert_pool_matches({layout0, layout1}), "suit 0");
}

// --- assert_forms_one_belief_node ------------------------------------------

TEST_F(FixtureValidationTest, MatchingLayoutsFormOneBeliefNode)
{
    Deal const layout0 = make_equal_hands_deal();
    Deal layout1 = make_equal_hands_deal();
    // Same declarer/dummy holdings and same defender pool, but the pool
    // split between the two defenders (East/West) differs -- exactly what
    // one belief node is supposed to tolerate.
    layout1.remainCards[East][Spades] = holding({8});
    layout1.remainCards[West][Spades] = holding({4});

    assert_forms_one_belief_node({layout0, layout1}, North);
}

TEST_F(FixtureValidationTest, DifferingTrumpIsRejected)
{
    Deal const layout0 = make_equal_hands_deal();
    Deal layout1 = make_equal_hands_deal();
    layout1.trump = Spades;

    EXPECT_NONFATAL_FAILURE(assert_forms_one_belief_node({layout0, layout1}, North), "trump");
}

TEST_F(FixtureValidationTest, DifferingDeclarerHoldingIsRejected)
{
    Deal const layout0 = make_equal_hands_deal();
    Deal layout1 = make_equal_hands_deal();
    layout1.remainCards[North][Spades] = holding({10});  // declarer's own holding changed

    EXPECT_NONFATAL_FAILURE(
        assert_forms_one_belief_node({layout0, layout1}, North), "declarer holding");
}

TEST_F(FixtureValidationTest, DifferingPoolIsRejected)
{
    Deal const layout0 = make_equal_hands_deal();
    Deal layout1 = make_equal_hands_deal();
    layout1.remainCards[East][Spades] = holding({10});  // defender pool itself changed, not just its split

    EXPECT_NONFATAL_FAILURE(assert_forms_one_belief_node({layout0, layout1}, North), "defender pool");
}
