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
    // diamonds in both. West's club rank distinguishes the two layouts so
    // the ad-hoc delta below can tell them apart. Hand-computed:
    //   layout 0: p = 0.6, delta plays the king with certainty
    //   layout 1: p = 0.4, delta plays the king 0.25, the two 0.75
    // so the king's child holds both layouts with p = {0.6, 0.1}
    //   (0.6 * 1.0 = 0.6, 0.4 * 0.25 = 0.1)
    // and the two's child holds only layout 1 with p = {0.3}
    //   (0.4 * 0.75 = 0.3).
    // Total 0.6 + 0.1 + 0.3 = 1.0, which conservation alone would also see
    // under a wrong split — hence the per-child assertions below.
    Deal const layout0 = make_layout({King, Two}, /*west_club_rank=*/Two);
    Deal const layout1 = make_layout({King, Two}, /*west_club_rank=*/Ace);
    BeliefNode const node = make_node({layout0, layout1}, {0.6, 0.4}, 1.0);

    auto const delta = [](DefenderQuery const& query) -> std::vector<WeightedCard>
    {
        bool const is_layout0 = query.layout.remainCards[West][Clubs] == holding({Two});
        if (is_layout0)
        {
            return {WeightedCard{Card{Diamonds, King}, 1.0}};
        }
        return {
            WeightedCard{Card{Diamonds, King}, 0.25},
            WeightedCard{Card{Diamonds, Two}, 0.75},
        };
    };

    ExpandDefenderResult const result = expand_defender_node(node, delta);

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

// --- a layout appearing in several children -------------------------------
//
// The path expand_defender_node has supported since it was written but
// which no earlier test walked: delta assigning one layout's probability
// across more than one card. algorithm.md notes this happens whenever the
// defenders have had more than one real choice.

TEST_F(DefenderNodeTest, ALayoutSplitIntoTwoChildrenIsAdvancedCorrectlyInBothChildrensCommonState)
{
    // One node, two layouts, East (the defender) on play, holding king and
    // queen of diamonds in both. Built via make_root, which -- unlike the
    // file's synthetic make_node helper used elsewhere -- always assigns
    // p_i = 1 to every surviving layout, so the split is over that fixed
    // prior rather than a hand-picked one:
    //   layout 0: p = 1. delta plays the king 0.5, the queen 0.5.
    //   layout 1: p = 1. delta plays the king with certainty.
    // The king's child holds both layouts, p = {0.5, 1.0}.
    // The queen's child holds layout 0 only,  p = {0.5}.
    // Total 0.5 + 1.0 + 0.5 = 2.0 = the parent's (kappa = 1/2, so this is
    // mass 1.0 either side) -- conservation alone would also accept a wrong
    // split summing to the same total, hence the per-child assertions below.
    //
    // Built via make_root (not the file's synthetic make_node helper) so
    // node.state.known_holdings is a genuine pooled common-knowledge Deal --
    // both East and West show the same king+queen pool -- which is what
    // makes this the first test where a partially-clearing advance_state()
    // would actually show up: the king's child and the queen's child are
    // two independent advance_state() calls from the *same* parent state,
    // removing two different cards of the *same* suit, so any state that
    // only cleared the queried seat's own slot (leaving the other
    // defender's identical pool entry stale) would show up as the two
    // children disagreeing about a pool that must actually be identical in
    // shape (each missing exactly the one card it was advanced by).
    auto const make_split_diamond_layout = [](int east_club_rank, int west_club_rank) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[East][Diamonds] = holding({King, Queen});
        deal.remainCards[East][Clubs] = holding({east_club_rank});
        deal.remainCards[West][Clubs] = holding({west_club_rank});
        return deal;
    };
    Deal const layout0 = make_split_diamond_layout(/*east=*/Two, /*west=*/Ace);
    Deal const layout1 = make_split_diamond_layout(/*east=*/Ace, /*west=*/Two);
    // Not assert_equal_hand_sizes: this file's fixtures are deliberately
    // single-suit-plus-filler remnants (North and South hold nothing) built
    // to exercise expand_defender_node() directly, not full 13-card deals
    // played out via evaluate() -- the check does not apply to this shape.
    assert_pool_matches({layout0, layout1});
    assert_forms_one_belief_node({layout0, layout1}, North);

    VectorLayoutSource source({layout0, layout1});
    BeliefNode const node = *make_root(layout0, North, /*tricks_needed=*/1, source);
    ASSERT_EQ(node.layouts.size(), 2u);  // both layouts survived make_root's filter

    ScriptedDefender::Key const key0{layout_key(layout0, East), ""};
    ScriptedDefender::Key const key1{layout_key(layout1, East), ""};
    ScriptedDefender defender = ScriptedDefender::stochastic(
        {{key0,
          {WeightedCard{Card{Diamonds, King}, 0.5}, WeightedCard{Card{Diamonds, Queen}, 0.5}}},
         {key1, {WeightedCard{Card{Diamonds, King}, 1.0}}}});

    ExpandDefenderResult const result = expand_defender_node(node, defender.as_strategy());

    ASSERT_TRUE(result.children.has_value());
    ASSERT_EQ(result.children->size(), 2u);
    bool const first_is_king_child = (*result.children)[0].layouts.size() == 2u;
    BeliefNode const& king_child = first_is_king_child ? (*result.children)[0] : (*result.children)[1];
    BeliefNode const& queen_child = first_is_king_child ? (*result.children)[1] : (*result.children)[0];

    // The split: layout 0 appears in both children, p correctly multiplied
    // per child.
    ASSERT_EQ(king_child.layouts.size(), 2u);
    ASSERT_EQ(king_child.p.size(), 2u);
    EXPECT_DOUBLE_EQ(king_child.p[0], 0.5);
    EXPECT_DOUBLE_EQ(king_child.p[1], 1.0);

    ASSERT_EQ(queen_child.layouts.size(), 1u);
    ASSERT_EQ(queen_child.p.size(), 1u);
    EXPECT_DOUBLE_EQ(queen_child.p[0], 0.5);

    // Mass conservation.
    KahanAccumulator total_child_mass;
    for (BeliefNode const& child : *result.children)
    {
        total_child_mass.add(node_mass(child));
    }
    EXPECT_NEAR(total_child_mass.value(), node_mass(node), 1e-6);

    // kappa is untouched -- defender children partition p, not kappa.
    EXPECT_DOUBLE_EQ(king_child.kappa, node.kappa);
    EXPECT_DOUBLE_EQ(queen_child.kappa, node.kappa);

    // Per-child common-knowledge pool, hand-derived. The
    // king's child is missing exactly the king from both defenders' entries
    // (still showing the queen); the queen's child the reverse -- in
    // *both* entries, not just East's (the one queried), which is exactly
    // what a partial clear would get wrong.
    unsigned const queen_only = holding({Queen});
    unsigned const king_only = holding({King});
    EXPECT_EQ(king_child.state.known_holdings.remainCards[East][Diamonds], queen_only);
    EXPECT_EQ(king_child.state.known_holdings.remainCards[West][Diamonds], queen_only);
    EXPECT_EQ(queen_child.state.known_holdings.remainCards[East][Diamonds], king_only);
    EXPECT_EQ(queen_child.state.known_holdings.remainCards[West][Diamonds], king_only);
}

TEST_F(DefenderNodeTest, AMixedNodeHandlesASplittingAndANonSplittingLayoutTogether)
{
    // Three layouts sharing the same king+queen diamond pool, split three
    // different ways between the two defenders so each has a distinct key:
    //   layout 0: p = 0.5. delta plays the king 0.5, the queen 0.5 (splits).
    //   layout 1: p = 0.3. delta plays the king with certainty (does not split).
    //   layout 2: p = 0.2. delta plays the queen with certainty (does not split).
    // The king's child holds layouts 0 and 1, p = {0.25, 0.30}.
    // The queen's child holds layouts 0 and 2, p = {0.25, 0.20}.
    // Total 0.25 + 0.30 + 0.25 + 0.20 = 1.00.
    auto const make_layout_with_split =
        [](unsigned east_diamonds, unsigned west_diamonds, int east_club, int west_club) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[East][Diamonds] = east_diamonds;
        deal.remainCards[West][Diamonds] = west_diamonds;
        deal.remainCards[East][Clubs] = holding({east_club});
        deal.remainCards[West][Clubs] = holding({west_club});
        return deal;
    };
    unsigned const both_honours = holding({King, Queen});
    Deal const layout0 = make_layout_with_split(both_honours, 0u, /*east=*/Two, /*west=*/Ace);
    Deal const layout1 = make_layout_with_split(both_honours, 0u, /*east=*/Ace, /*west=*/Two);
    Deal const layout2 =
        make_layout_with_split(holding({Queen}), holding({King}), /*east=*/Two, /*west=*/Ace);
    assert_pool_matches({layout0, layout1, layout2});
    BeliefNode const node = make_node({layout0, layout1, layout2}, {0.5, 0.3, 0.2}, 1.0);

    ScriptedDefender::Key const key0{layout_key(layout0, East), ""};
    ScriptedDefender::Key const key1{layout_key(layout1, East), ""};
    ScriptedDefender::Key const key2{layout_key(layout2, East), ""};
    ScriptedDefender defender = ScriptedDefender::stochastic(
        {{key0,
          {WeightedCard{Card{Diamonds, King}, 0.5}, WeightedCard{Card{Diamonds, Queen}, 0.5}}},
         {key1, {WeightedCard{Card{Diamonds, King}, 1.0}}},
         {key2, {WeightedCard{Card{Diamonds, Queen}, 1.0}}}});

    ExpandDefenderResult const result = expand_defender_node(node, defender.as_strategy());

    ASSERT_TRUE(result.children.has_value());
    ASSERT_EQ(result.children->size(), 2u);
    bool const first_is_king_child = (*result.children)[0].layouts.size() == 2u
        && (*result.children)[0].layouts[1].remainCards[West][Diamonds] == 0u;
    // Both children hold two layouts here, so identify by which layout each
    // one's *second* entry is: the king's child holds layout 1 (West holds
    // no diamonds), the queen's child holds layout 2 (West holds the king).
    BeliefNode const& king_child = first_is_king_child ? (*result.children)[0] : (*result.children)[1];
    BeliefNode const& queen_child = first_is_king_child ? (*result.children)[1] : (*result.children)[0];

    ASSERT_EQ(king_child.layouts.size(), 2u);
    ASSERT_EQ(king_child.p.size(), 2u);
    EXPECT_DOUBLE_EQ(king_child.p[0], 0.25);
    EXPECT_DOUBLE_EQ(king_child.p[1], 0.30);

    ASSERT_EQ(queen_child.layouts.size(), 2u);
    ASSERT_EQ(queen_child.p.size(), 2u);
    EXPECT_DOUBLE_EQ(queen_child.p[0], 0.25);
    EXPECT_DOUBLE_EQ(queen_child.p[1], 0.20);

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
