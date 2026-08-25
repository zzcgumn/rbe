#include <gtest/gtest.h>

#include <api/dll.h>
#include <utility/constants.h>

#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/layout_key.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/validation.hpp>

#include "test_support.hpp"

namespace
{
    constexpr int Two = 2;
    constexpr int Queen = 12;
    constexpr int King = 13;
    constexpr int Ace = 14;

    constexpr int Spades = 0;
    constexpr int Diamonds = 2;
    constexpr int Clubs = 3;

    constexpr int North = 0;  // declarer
    constexpr int East = 1;   // the defender on play throughout this file
    constexpr int West = 3;   // the other defender

    /// East (the defender on play) holds `east_diamonds`; West's clubs
    /// holding distinguishes otherwise-identical layouts for the ad-hoc
    /// stochastic delta below. North and South hold nothing relevant.
    auto make_layout(std::initializer_list<int> east_diamonds, int west_club_rank) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[East][Diamonds] = holding(east_diamonds);
        deal.remainCards[West][Clubs] = holding({west_club_rank});
        return deal;
    }

    auto make_node(std::vector<Deal> layouts, std::vector<Probability> p, SampleWeight kappa)
        -> BeliefNode
    {
        BeliefNode node{};
        node.state.trump = DDS_NOTRUMP;
        node.state.first = North;
        node.state.declarer = North;
        node.state.tricks_needed = 1;
        node.state.tricks_won_by_declarer = 0;
        node.state.known_holdings = layouts.front();
        node.layouts = std::move(layouts);
        node.p = std::move(p);
        node.kappa = kappa;
        return node;
    }
}

class DefenderNodeTest : public ::testing::Test
{
};

// --- criterion 1: delta is called once per layout ------------------------

TEST_F(DefenderNodeTest, DeltaIsCalledOncePerLayoutWithThatLayoutAndSeat)
{
    Deal const layout0 = make_layout({King}, /*west_club_rank=*/Two);
    Deal const layout1 = make_layout({Two}, /*west_club_rank=*/Two);
    BeliefNode const node = make_node({layout0, layout1}, {0.5, 0.5}, 1.0);

    ScriptedDefender::Key const key0{layout_key(layout0, East), ""};
    ScriptedDefender::Key const key1{layout_key(layout1, East), ""};
    ScriptedDefender defender(
        {{key0, Card{Diamonds, King}}, {key1, Card{Diamonds, Two}}});

    ExpandDefenderResult const result = expand_defender_node(node, defender.as_strategy());

    ASSERT_TRUE(result.children.has_value());
    ASSERT_EQ(defender.queries().size(), 2u);
    EXPECT_EQ(defender.queries()[0].seat, East);
    EXPECT_EQ(defender.queries()[0].layout, layout_key(layout0, East));
    EXPECT_EQ(defender.queries()[1].seat, East);
    EXPECT_EQ(defender.queries()[1].layout, layout_key(layout1, East));
}

// --- criteria 2 and 3: grouping by card, absent rather than p = 0 --------

TEST_F(DefenderNodeTest, TwoLayoutsScriptedToDifferentCardsProduceTwoSingleLayoutChildren)
{
    Deal const layout0 = make_layout({King}, /*west_club_rank=*/Two);
    Deal const layout1 = make_layout({Two}, /*west_club_rank=*/Two);
    BeliefNode const node = make_node({layout0, layout1}, {0.5, 0.5}, 1.0);

    ScriptedDefender::Key const key0{layout_key(layout0, East), ""};
    ScriptedDefender::Key const key1{layout_key(layout1, East), ""};
    ScriptedDefender defender(
        {{key0, Card{Diamonds, King}}, {key1, Card{Diamonds, Two}}});

    ExpandDefenderResult const result = expand_defender_node(node, defender.as_strategy());

    ASSERT_TRUE(result.children.has_value());
    ASSERT_EQ(result.children->size(), 2u);
    for (BeliefNode const& child : *result.children)
    {
        EXPECT_EQ(child.layouts.size(), 1u);
        EXPECT_EQ(child.p.size(), 1u);
    }
}

// --- criterion 7: hand-computed per-child values for a stochastic defence

TEST_F(DefenderNodeTest, HandComputedPerChildValuesForAStochasticDefence)
{
    // Two layouts, East (the defender) on play, holding king and two of
    // diamonds in both -- ScriptedDefender's table is keyed on the queried
    // seat's own holding (layout_key), so the two layouts also need to
    // differ in *East's* holding, not just West's, to produce distinct
    // keys; a club filler card that swaps sides between East and West does
    // that while leaving the club pool itself (and hence the outstanding
    // pool assert_pool_matches would check) the same in both. Hand-computed:
    //   layout 0: p = 0.6, delta plays the king with certainty
    //   layout 1: p = 0.4, delta plays the king 0.25, the two 0.75
    // so the king's child holds both layouts with p = {0.6, 0.1}
    //   (0.6 * 1.0 = 0.6, 0.4 * 0.25 = 0.1)
    // and the two's child holds only layout 1 with p = {0.3}
    //   (0.4 * 0.75 = 0.3).
    // Total 0.6 + 0.1 + 0.3 = 1.0, which conservation alone would also see
    // under a wrong split — hence the per-child assertions below.
    auto const make_split_club_layout = [](int east_club_rank, int west_club_rank) -> Deal
    {
        Deal deal = make_layout({King, Two}, /*west_club_rank=*/west_club_rank);
        deal.remainCards[East][Clubs] = holding({east_club_rank});
        return deal;
    };
    Deal const layout0 = make_split_club_layout(/*east=*/Two, /*west=*/Ace);
    Deal const layout1 = make_split_club_layout(/*east=*/Ace, /*west=*/Two);
    assert_pool_matches({layout0, layout1});
    BeliefNode const node = make_node({layout0, layout1}, {0.6, 0.4}, 1.0);

    ScriptedDefender::Key const key0{layout_key(layout0, East), ""};
    ScriptedDefender::Key const key1{layout_key(layout1, East), ""};
    ScriptedDefender defender(
        {{key0, {WeightedCard{Card{Diamonds, King}, 1.0}}},
         {key1,
          {WeightedCard{Card{Diamonds, King}, 0.25}, WeightedCard{Card{Diamonds, Two}, 0.75}}}});

    ExpandDefenderResult const result = expand_defender_node(node, defender.as_strategy());

    ASSERT_TRUE(result.children.has_value());
    ASSERT_EQ(result.children->size(), 2u);
    // Grouping order is not part of the contract, so identify each child by
    // shape (the king's child holds both layouts, the two's child holds
    // only layout 1) rather than by index.
    bool const first_is_king_child = (*result.children)[0].layouts.size() == 2u;
    BeliefNode const& king_child = first_is_king_child ? (*result.children)[0] : (*result.children)[1];
    BeliefNode const& two_child = first_is_king_child ? (*result.children)[1] : (*result.children)[0];

    ASSERT_EQ(king_child.p.size(), 2u);
    EXPECT_DOUBLE_EQ(king_child.p[0], 0.6);
    EXPECT_DOUBLE_EQ(king_child.p[1], 0.1);

    ASSERT_EQ(two_child.p.size(), 1u);
    EXPECT_DOUBLE_EQ(two_child.p[0], 0.3);

    // criterion 4: kappa is untouched in every child.
    EXPECT_DOUBLE_EQ(king_child.kappa, node.kappa);
    EXPECT_DOUBLE_EQ(two_child.kappa, node.kappa);

    // criterion 5: mass conservation, to the same tolerance
    // validate_defender_distribution itself uses (1e-6) for a probability
    // distribution summing to 1 — the two checks guard the same kind of
    // floating-point drift, so reusing it keeps this test no stricter than
    // the contract delta itself is held to.
    KahanAccumulator total_child_mass;
    for (BeliefNode const& child : *result.children)
    {
        total_child_mass.add(node_mass(child));
    }
    EXPECT_NEAR(total_child_mass.value(), node_mass(node), 1e-6);
}

// --- criterion 6: a bad distribution is rejected, not asserted -----------

TEST_F(DefenderNodeTest, RejectsACardNotHeldBySeat)
{
    Deal const layout = make_layout({King}, /*west_club_rank=*/Two);
    BeliefNode const node = make_node({layout}, {1.0}, 1.0);
    auto const delta = [](DefenderQuery const&) -> std::vector<WeightedCard>
    {
        return {WeightedCard{Card{Diamonds, Ace}, 1.0}};  // East doesn't hold it
    };

    ExpandDefenderResult const result = expand_defender_node(node, delta);
    EXPECT_FALSE(result.children.has_value());
    EXPECT_EQ(result.error, ValidationError::CardNotHeld);
}

TEST_F(DefenderNodeTest, RejectsACardIllegalForTheTrick)
{
    Deal layout = make_layout({King}, /*west_club_rank=*/Two);
    layout.first = North;               // North led; East is next to play
    layout.currentTrickSuit[0] = Spades;
    layout.currentTrickRank[0] = Two;
    layout.remainCards[East][Spades] = holding({Queen});  // East holds the led suit too
    BeliefNode node = make_node({layout}, {1.0}, 1.0);
    node.state.known_holdings = layout;

    auto const delta = [](DefenderQuery const&) -> std::vector<WeightedCard>
    {
        return {WeightedCard{Card{Diamonds, King}, 1.0}};  // must follow spades instead
    };

    ExpandDefenderResult const result = expand_defender_node(node, delta);
    EXPECT_FALSE(result.children.has_value());
    EXPECT_EQ(result.error, ValidationError::CardIllegalForTrick);
}

TEST_F(DefenderNodeTest, RejectsANonPositiveProbability)
{
    Deal const layout = make_layout({King, Two}, /*west_club_rank=*/Two);
    BeliefNode const node = make_node({layout}, {1.0}, 1.0);
    auto const delta = [](DefenderQuery const&) -> std::vector<WeightedCard>
    {
        return {
            WeightedCard{Card{Diamonds, King}, 0.0},
            WeightedCard{Card{Diamonds, Two}, 1.0},
        };
    };

    ExpandDefenderResult const result = expand_defender_node(node, delta);
    EXPECT_FALSE(result.children.has_value());
    EXPECT_EQ(result.error, ValidationError::ProbabilityNonPositive);
}

TEST_F(DefenderNodeTest, RejectsProbabilitiesNotSummingToOne)
{
    Deal const layout = make_layout({King, Two}, /*west_club_rank=*/Two);
    BeliefNode const node = make_node({layout}, {1.0}, 1.0);
    auto const delta = [](DefenderQuery const&) -> std::vector<WeightedCard>
    {
        return {
            WeightedCard{Card{Diamonds, King}, 0.5},
            WeightedCard{Card{Diamonds, Two}, 0.25},
        };
    };

    ExpandDefenderResult const result = expand_defender_node(node, delta);
    EXPECT_FALSE(result.children.has_value());
    EXPECT_EQ(result.error, ValidationError::ProbabilitiesDoNotSumToOne);
}
