#include <gtest/gtest.h>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/layout_key.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/validation.hpp>

#include "test_support.hpp"

// Tests for expand_defender_node: one delta call per layout, grouping by
// card, per-child values, and rejected distributions.

namespace be = dds::belief_evaluation;

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

    /// East (the defender on play) holds `east_diamonds`. West's club holding
    /// tells otherwise identical layouts apart for the stochastic delta below.
    /// North and South hold nothing relevant.
    auto make_layout(std::initializer_list<int> east_diamonds, int west_club_rank) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[East][Diamonds] = be::holding(east_diamonds);
        deal.remainCards[West][Clubs] = be::holding({west_club_rank});
        return deal;
    }

    auto make_node(std::vector<Deal> layouts, std::vector<be::Probability> p, be::SampleWeight kappa)
        -> be::BeliefNode
    {
        be::BeliefNode node{};
        node.state.trump = DDS_NOTRUMP;
        node.state.first = North;
        node.state.declarer = North;
        node.state.tricks_needed = 1;
        node.state.tricks_won_by_declarer = 0;
        node.state.known_holdings = layouts.front();
        node.root_keys.reserve(layouts.size());
        for (Deal const& layout : layouts)
        {
            node.root_keys.push_back(be::layout_key(layout, (North + 1) % DDS_HANDS));
        }
        node.layouts = std::move(layouts);
        node.p = std::move(p);
        node.kappa = kappa;
        return node;
    }
}

class DefenderNodeTest : public ::testing::Test
{
};

// --- delta is called once per layout -----------------------------------------

TEST_F(DefenderNodeTest, DeltaIsCalledOncePerLayoutWithThatLayoutAndSeat)
{
    Deal const layout0 = make_layout({King}, /*west_club_rank=*/Two);
    Deal const layout1 = make_layout({Two}, /*west_club_rank=*/Two);
    be::BeliefNode const node = make_node({layout0, layout1}, {0.5, 0.5}, 1.0);

    be::ScriptedDefender::Key const key0{be::layout_key(layout0, East), ""};
    be::ScriptedDefender::Key const key1{be::layout_key(layout1, East), ""};
    be::ScriptedDefender defender(
        {{key0, be::Card{Diamonds, King}}, {key1, be::Card{Diamonds, Two}}});

    be::ExpandDefenderResult const result = be::expand_defender_node(node, defender.as_strategy());

    ASSERT_TRUE(result.children.has_value());
    ASSERT_EQ(defender.queries().size(), 2u);
    EXPECT_EQ(defender.queries()[0].seat, East);
    EXPECT_EQ(defender.queries()[0].layout, be::layout_key(layout0, East));
    EXPECT_EQ(defender.queries()[1].seat, East);
    EXPECT_EQ(defender.queries()[1].layout, be::layout_key(layout1, East));
}

// --- grouping by card, absent rather than p = 0 ------------------------------

TEST_F(DefenderNodeTest, TwoLayoutsScriptedToDifferentCardsProduceTwoSingleLayoutChildren)
{
    Deal const layout0 = make_layout({King}, /*west_club_rank=*/Two);
    Deal const layout1 = make_layout({Two}, /*west_club_rank=*/Two);
    be::BeliefNode const node = make_node({layout0, layout1}, {0.5, 0.5}, 1.0);

    be::ScriptedDefender::Key const key0{be::layout_key(layout0, East), ""};
    be::ScriptedDefender::Key const key1{be::layout_key(layout1, East), ""};
    be::ScriptedDefender defender(
        {{key0, be::Card{Diamonds, King}}, {key1, be::Card{Diamonds, Two}}});

    be::ExpandDefenderResult const result = be::expand_defender_node(node, defender.as_strategy());

    ASSERT_TRUE(result.children.has_value());
    ASSERT_EQ(result.children->size(), 2u);
    for (be::BeliefNode const& child : *result.children)
    {
        EXPECT_EQ(child.layouts.size(), 1u);
        EXPECT_EQ(child.p.size(), 1u);
    }
}

// --- hand-computed per-child values for a stochastic defence -----------------

TEST_F(DefenderNodeTest, HandComputedPerChildValuesForAStochasticDefence)
{
    // Two layouts, East (the defender) on play, holding the king and two of
    // diamonds in both. West's club rank lets the delta below tell the layouts
    // apart. Hand-computed:
    //   layout 0: p = 0.6, delta plays the king with certainty
    //   layout 1: p = 0.4, delta plays the king 0.25, the two 0.75
    // The king's child holds both layouts with p = {0.6, 0.1}
    //   (0.6 * 1.0 = 0.6, 0.4 * 0.25 = 0.1).
    // The two's child holds only layout 1 with p = {0.3}
    //   (0.4 * 0.75 = 0.3).
    // The total is 0.6 + 0.1 + 0.3 = 1.0. A wrong split could give the same
    // total, so the assertions below check each child.
    Deal const layout0 = make_layout({King, Two}, /*west_club_rank=*/Two);
    Deal const layout1 = make_layout({King, Two}, /*west_club_rank=*/Ace);
    be::BeliefNode const node = make_node({layout0, layout1}, {0.6, 0.4}, 1.0);

    auto const delta = [](be::DefenderQuery const& query) -> std::vector<be::WeightedCard>
    {
        bool const is_layout0 = query.layout.remainCards[West][Clubs] == be::holding({Two});
        if (is_layout0)
        {
            return {be::WeightedCard{be::Card{Diamonds, King}, 1.0}};
        }
        return {
            be::WeightedCard{be::Card{Diamonds, King}, 0.25},
            be::WeightedCard{be::Card{Diamonds, Two}, 0.75},
        };
    };

    be::ExpandDefenderResult const result = be::expand_defender_node(node, delta);

    ASSERT_TRUE(result.children.has_value());
    ASSERT_EQ(result.children->size(), 2u);
    // Grouping order is not part of the contract, so identify each child by
    // shape: the king's child holds both layouts, the two's child only
    // layout 1.
    bool const first_is_king_child = (*result.children)[0].layouts.size() == 2u;
    be::BeliefNode const& king_child = first_is_king_child ? (*result.children)[0] : (*result.children)[1];
    be::BeliefNode const& two_child = first_is_king_child ? (*result.children)[1] : (*result.children)[0];

    ASSERT_EQ(king_child.p.size(), 2u);
    EXPECT_DOUBLE_EQ(king_child.p[0], 0.6);
    EXPECT_DOUBLE_EQ(king_child.p[1], 0.1);

    ASSERT_EQ(two_child.p.size(), 1u);
    EXPECT_DOUBLE_EQ(two_child.p[0], 0.3);

    // kappa is untouched in every child.
    EXPECT_DOUBLE_EQ(king_child.kappa, node.kappa);
    EXPECT_DOUBLE_EQ(two_child.kappa, node.kappa);

    // Mass conservation, to the tolerance validate_defender_distribution uses
    // for a distribution summing to 1 (1e-6). Both checks guard against the
    // same floating-point drift, so this test is no stricter than the delta
    // contract.
    be::KahanAccumulator total_child_mass;
    for (be::BeliefNode const& child : *result.children)
    {
        total_child_mass.add(be::node_mass(child));
    }
    EXPECT_NEAR(total_child_mass.value(), be::node_mass(node), 1e-6);
}

// --- a layout appearing in several children -------------------------------
//
// delta can split one layout's probability across more than one card.
// algorithm.md notes this happens whenever the defenders have had more
// than one real choice.

TEST_F(DefenderNodeTest, ALayoutSplitIntoTwoChildrenIsAdvancedCorrectlyInBothChildrensCommonState)
{
    // One node, two layouts, East (the defender) on play, holding the king and
    // queen of diamonds in both. make_root gives every surviving layout
    // p_i = 1, so the split is over that fixed prior:
    //   layout 0: p = 1. delta plays the king 0.5, the queen 0.5.
    //   layout 1: p = 1. delta plays the king with certainty.
    // The king's child holds both layouts, p = {0.5, 1.0}.
    // The queen's child holds layout 0 only, p = {0.5}.
    // The total is 0.5 + 1.0 + 0.5 = 2.0, the parent's (with kappa = 1/2, mass
    // 1.0 on both sides). A wrong split could give the same total, so the
    // assertions below check each child.
    //
    // make_root, unlike this file's make_node helper, builds a real pooled
    // common-knowledge Deal in node.state.known_holdings: East and West both
    // show the king and queen. The two children come from two advance_state()
    // calls on the same parent state, each removing a different card of the
    // same suit. A state that cleared only the queried seat's slot would leave
    // the other defender's pool entry stale. The two children would then
    // disagree about a pool that must have the same shape in both (each
    // missing only the card it was advanced by).
    auto const make_split_diamond_layout = [](int east_club_rank, int west_club_rank) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[East][Diamonds] = be::holding({King, Queen});
        deal.remainCards[East][Clubs] = be::holding({east_club_rank});
        deal.remainCards[West][Clubs] = be::holding({west_club_rank});
        return deal;
    };
    Deal const layout0 = make_split_diamond_layout(/*east=*/Two, /*west=*/Ace);
    Deal const layout1 = make_split_diamond_layout(/*east=*/Ace, /*west=*/Two);
    // Not assert_equal_hand_sizes: these fixtures are single-suit remnants
    // with a filler (North and South hold nothing), built to test
    // expand_defender_node() directly. They are not full deals played out by
    // evaluate(), so the check does not apply.
    be::assert_pool_matches({layout0, layout1});
    be::assert_forms_one_belief_node({layout0, layout1}, North);

    be::VectorLayoutSource source({layout0, layout1});
    be::BeliefNode const node = *be::make_root(layout0, North, /*tricks_needed=*/1, source).node;
    ASSERT_EQ(node.layouts.size(), 2u);  // both layouts survived make_root's filter

    be::ScriptedDefender::Key const key0{be::layout_key(layout0, East), ""};
    be::ScriptedDefender::Key const key1{be::layout_key(layout1, East), ""};
    be::ScriptedDefender defender = be::ScriptedDefender::stochastic(
        {{key0,
          {be::WeightedCard{be::Card{Diamonds, King}, 0.5}, be::WeightedCard{be::Card{Diamonds, Queen}, 0.5}}},
         {key1, {be::WeightedCard{be::Card{Diamonds, King}, 1.0}}}});

    be::ExpandDefenderResult const result = be::expand_defender_node(node, defender.as_strategy());

    ASSERT_TRUE(result.children.has_value());
    ASSERT_EQ(result.children->size(), 2u);
    bool const first_is_king_child = (*result.children)[0].layouts.size() == 2u;
    be::BeliefNode const& king_child = first_is_king_child ? (*result.children)[0] : (*result.children)[1];
    be::BeliefNode const& queen_child = first_is_king_child ? (*result.children)[1] : (*result.children)[0];

    // The split: layout 0 is in both children, with p multiplied per child.
    ASSERT_EQ(king_child.layouts.size(), 2u);
    ASSERT_EQ(king_child.p.size(), 2u);
    EXPECT_DOUBLE_EQ(king_child.p[0], 0.5);
    EXPECT_DOUBLE_EQ(king_child.p[1], 1.0);

    ASSERT_EQ(queen_child.layouts.size(), 1u);
    ASSERT_EQ(queen_child.p.size(), 1u);
    EXPECT_DOUBLE_EQ(queen_child.p[0], 0.5);

    // Mass conservation.
    be::KahanAccumulator total_child_mass;
    for (be::BeliefNode const& child : *result.children)
    {
        total_child_mass.add(be::node_mass(child));
    }
    EXPECT_NEAR(total_child_mass.value(), be::node_mass(node), 1e-6);

    // kappa is untouched: defender children partition p, not kappa.
    EXPECT_DOUBLE_EQ(king_child.kappa, node.kappa);
    EXPECT_DOUBLE_EQ(queen_child.kappa, node.kappa);

    // The per-child common-knowledge pool, hand-derived. The king's child is
    // missing the king from both defenders' entries and still shows the queen.
    // The queen's child is the reverse. Both entries must change, not just
    // East's (the queried seat): a partial clear gets this wrong.
    unsigned const queen_only = be::holding({Queen});
    unsigned const king_only = be::holding({King});
    EXPECT_EQ(king_child.state.known_holdings.remainCards[East][Diamonds], queen_only);
    EXPECT_EQ(king_child.state.known_holdings.remainCards[West][Diamonds], queen_only);
    EXPECT_EQ(queen_child.state.known_holdings.remainCards[East][Diamonds], king_only);
    EXPECT_EQ(queen_child.state.known_holdings.remainCards[West][Diamonds], king_only);
}

TEST_F(DefenderNodeTest, AMixedNodeHandlesASplittingAndANonSplittingLayoutTogether)
{
    // Three layouts share the king and queen of diamonds, split differently
    // between the defenders so each has a distinct key:
    //   layout 0: p = 0.5. delta plays the king 0.5, the queen 0.5 (splits).
    //   layout 1: p = 0.3. delta plays the king with certainty.
    //   layout 2: p = 0.2. delta plays the queen with certainty.
    // The king's child holds layouts 0 and 1, p = {0.25, 0.30}.
    // The queen's child holds layouts 0 and 2, p = {0.25, 0.20}.
    // The total is 0.25 + 0.30 + 0.25 + 0.20 = 1.00.
    auto const make_layout_with_split =
        [](unsigned east_diamonds, unsigned west_diamonds, int east_club, int west_club) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[East][Diamonds] = east_diamonds;
        deal.remainCards[West][Diamonds] = west_diamonds;
        deal.remainCards[East][Clubs] = be::holding({east_club});
        deal.remainCards[West][Clubs] = be::holding({west_club});
        return deal;
    };
    unsigned const both_honours = be::holding({King, Queen});
    Deal const layout0 = make_layout_with_split(both_honours, 0u, /*east=*/Two, /*west=*/Ace);
    Deal const layout1 = make_layout_with_split(both_honours, 0u, /*east=*/Ace, /*west=*/Two);
    Deal const layout2 =
        make_layout_with_split(be::holding({Queen}), be::holding({King}), /*east=*/Two, /*west=*/Ace);
    be::assert_pool_matches({layout0, layout1, layout2});
    be::BeliefNode const node = make_node({layout0, layout1, layout2}, {0.5, 0.3, 0.2}, 1.0);

    be::ScriptedDefender::Key const key0{be::layout_key(layout0, East), ""};
    be::ScriptedDefender::Key const key1{be::layout_key(layout1, East), ""};
    be::ScriptedDefender::Key const key2{be::layout_key(layout2, East), ""};
    be::ScriptedDefender defender = be::ScriptedDefender::stochastic(
        {{key0,
          {be::WeightedCard{be::Card{Diamonds, King}, 0.5}, be::WeightedCard{be::Card{Diamonds, Queen}, 0.5}}},
         {key1, {be::WeightedCard{be::Card{Diamonds, King}, 1.0}}},
         {key2, {be::WeightedCard{be::Card{Diamonds, Queen}, 1.0}}}});

    be::ExpandDefenderResult const result = be::expand_defender_node(node, defender.as_strategy());

    ASSERT_TRUE(result.children.has_value());
    ASSERT_EQ(result.children->size(), 2u);
    bool const first_is_king_child = (*result.children)[0].layouts.size() == 2u
        && (*result.children)[0].layouts[1].remainCards[West][Diamonds] == 0u;
    // Both children hold two layouts, so identify each by its second entry.
    // The king's child holds layout 1 (West holds no diamonds), and the
    // queen's child holds layout 2 (West holds the king).
    be::BeliefNode const& king_child = first_is_king_child ? (*result.children)[0] : (*result.children)[1];
    be::BeliefNode const& queen_child = first_is_king_child ? (*result.children)[1] : (*result.children)[0];

    ASSERT_EQ(king_child.layouts.size(), 2u);
    ASSERT_EQ(king_child.p.size(), 2u);
    EXPECT_DOUBLE_EQ(king_child.p[0], 0.25);
    EXPECT_DOUBLE_EQ(king_child.p[1], 0.30);

    ASSERT_EQ(queen_child.layouts.size(), 2u);
    ASSERT_EQ(queen_child.p.size(), 2u);
    EXPECT_DOUBLE_EQ(queen_child.p[0], 0.25);
    EXPECT_DOUBLE_EQ(queen_child.p[1], 0.20);

    be::KahanAccumulator total_child_mass;
    for (be::BeliefNode const& child : *result.children)
    {
        total_child_mass.add(be::node_mass(child));
    }
    EXPECT_NEAR(total_child_mass.value(), be::node_mass(node), 1e-6);
}

// --- root_keys partition exactly as p does --------------------------------

TEST_F(DefenderNodeTest, RootKeysArePartitionedExactlyLikePInTheSameLoop)
{
    Deal const layout0 = make_layout({King}, /*west_club_rank=*/Two);
    Deal const layout1 = make_layout({Two}, /*west_club_rank=*/Two);
    be::BeliefNode const node = make_node({layout0, layout1}, {0.5, 0.5}, 1.0);
    ASSERT_EQ(node.root_keys.size(), 2u);

    be::ScriptedDefender::Key const key0{be::layout_key(layout0, East), ""};
    be::ScriptedDefender::Key const key1{be::layout_key(layout1, East), ""};
    be::ScriptedDefender defender(
        {{key0, be::Card{Diamonds, King}}, {key1, be::Card{Diamonds, Two}}});

    be::ExpandDefenderResult const result = be::expand_defender_node(node, defender.as_strategy());

    ASSERT_TRUE(result.children.has_value());
    ASSERT_EQ(result.children->size(), 2u);
    for (be::BeliefNode const& child : *result.children)
    {
        ASSERT_EQ(child.root_keys.size(), child.p.size());
        ASSERT_EQ(child.root_keys.size(), child.layouts.size());
    }
    // Each one-layout child's root_keys entry is the parent's entry for the
    // layout it holds, not the other one: the same one-to-one link p has.
    be::BeliefNode const& king_child =
        (*result.children)[0].layouts.front().remainCards[East][Diamonds] == be::holding({King})
        ? (*result.children)[0]
        : (*result.children)[1];
    be::BeliefNode const& two_child =
        (&king_child == &(*result.children)[0]) ? (*result.children)[1] : (*result.children)[0];
    EXPECT_EQ(king_child.root_keys[0], node.root_keys[0]);
    EXPECT_EQ(two_child.root_keys[0], node.root_keys[1]);
}

// --- a bad distribution is rejected, not asserted ----------------------------

TEST_F(DefenderNodeTest, RejectsACardNotHeldBySeat)
{
    Deal const layout = make_layout({King}, /*west_club_rank=*/Two);
    be::BeliefNode const node = make_node({layout}, {1.0}, 1.0);
    auto const delta = [](be::DefenderQuery const&) -> std::vector<be::WeightedCard>
    {
        return {be::WeightedCard{be::Card{Diamonds, Ace}, 1.0}};  // East doesn't hold it
    };

    be::ExpandDefenderResult const result = be::expand_defender_node(node, delta);
    EXPECT_FALSE(result.children.has_value());
    EXPECT_EQ(result.error, be::ValidationError::CardNotHeld);
}

TEST_F(DefenderNodeTest, RejectsACardIllegalForTheTrick)
{
    Deal layout = make_layout({King}, /*west_club_rank=*/Two);
    layout.first = North;               // North led; East is next to play
    layout.currentTrickSuit[0] = Spades;
    layout.currentTrickRank[0] = Two;
    layout.remainCards[East][Spades] = be::holding({Queen});  // East holds the led suit too
    be::BeliefNode node = make_node({layout}, {1.0}, 1.0);
    node.state.known_holdings = layout;

    auto const delta = [](be::DefenderQuery const&) -> std::vector<be::WeightedCard>
    {
        return {be::WeightedCard{be::Card{Diamonds, King}, 1.0}};  // must follow spades instead
    };

    be::ExpandDefenderResult const result = be::expand_defender_node(node, delta);
    EXPECT_FALSE(result.children.has_value());
    EXPECT_EQ(result.error, be::ValidationError::CardIllegalForTrick);
}

TEST_F(DefenderNodeTest, RejectsANonPositiveProbability)
{
    Deal const layout = make_layout({King, Two}, /*west_club_rank=*/Two);
    be::BeliefNode const node = make_node({layout}, {1.0}, 1.0);
    auto const delta = [](be::DefenderQuery const&) -> std::vector<be::WeightedCard>
    {
        return {
            be::WeightedCard{be::Card{Diamonds, King}, 0.0},
            be::WeightedCard{be::Card{Diamonds, Two}, 1.0},
        };
    };

    be::ExpandDefenderResult const result = be::expand_defender_node(node, delta);
    EXPECT_FALSE(result.children.has_value());
    EXPECT_EQ(result.error, be::ValidationError::ProbabilityNonPositive);
}

TEST_F(DefenderNodeTest, RejectsProbabilitiesNotSummingToOne)
{
    Deal const layout = make_layout({King, Two}, /*west_club_rank=*/Two);
    be::BeliefNode const node = make_node({layout}, {1.0}, 1.0);
    auto const delta = [](be::DefenderQuery const&) -> std::vector<be::WeightedCard>
    {
        return {
            be::WeightedCard{be::Card{Diamonds, King}, 0.5},
            be::WeightedCard{be::Card{Diamonds, Two}, 0.25},
        };
    };

    be::ExpandDefenderResult const result = be::expand_defender_node(node, delta);
    EXPECT_FALSE(result.children.has_value());
    EXPECT_EQ(result.error, be::ValidationError::ProbabilitiesDoNotSumToOne);
}

TEST_F(DefenderNodeTest, RejectsAnEmptyDistributionUnderItsOwnCause)
{
    // An empty distribution also fails validate_defender_distribution's
    // sum-to-one check, since zero is not one. expand_defender_node checks for
    // it first and reports DistributionEmpty, not ProbabilitiesDoNotSumToOne:
    // "you returned nothing" is the clearer answer here (see
    // ValidationError::DistributionEmpty). DoubleDummyDefenderTest drives the
    // same distinction end to end through a real solver failure.
    Deal const layout = make_layout({King, Two}, /*west_club_rank=*/Two);
    be::BeliefNode const node = make_node({layout}, {1.0}, 1.0);
    auto const delta = [](be::DefenderQuery const&) -> std::vector<be::WeightedCard> { return {}; };

    be::ExpandDefenderResult const result = be::expand_defender_node(node, delta);
    EXPECT_FALSE(result.children.has_value());
    EXPECT_EQ(result.error, be::ValidationError::DistributionEmpty);
    EXPECT_EQ(result.offending_layout.remainCards[East][Diamonds], layout.remainCards[East][Diamonds]);
}
