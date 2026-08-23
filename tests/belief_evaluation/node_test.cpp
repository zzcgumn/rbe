#include <gtest/gtest.h>

#include <api/dll.h>
#include <utility/constants.h>

#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/node.hpp>

#include "test_support.hpp"

namespace
{
    constexpr int Jack = 11;
    constexpr int Queen = 12;
    constexpr int King = 13;
    constexpr int Ace = 14;

    constexpr int North = 0;  // declarer throughout this file
    constexpr int East = 1;   // a defender
    constexpr int South = 2;  // dummy: (declarer + 2) % DDS_HANDS
    constexpr int West = 3;   // a defender

    /// North (declarer) holds spades A/K, South (dummy) holds hearts A/K,
    /// and East/West hold diamonds Q/J between them, split East=Q, West=J.
    /// Trump is notrump, North leads, nothing played yet.
    auto make_root_layout() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = North;
        deal.remainCards[North][0] = holding({Ace, King});    // spades
        deal.remainCards[South][1] = holding({Ace, King});    // hearts
        deal.remainCards[East][2] = holding({Queen});         // diamonds
        deal.remainCards[West][2] = holding({Jack});          // diamonds
        return deal;
    }

    /// Same shape as make_root_layout(), but with the diamond queen and
    /// jack swapped between the defenders — the same outstanding pool,
    /// split the other way.
    auto make_swapped_split_layout() -> Deal
    {
        Deal deal = make_root_layout();
        deal.remainCards[East][2] = holding({Jack});
        deal.remainCards[West][2] = holding({Queen});
        return deal;
    }

    /// Differs from make_root_layout() in dummy's holding (hearts A only,
    /// missing the king) — must not survive filtering against the root.
    auto make_wrong_dummy_layout() -> Deal
    {
        Deal deal = make_root_layout();
        deal.remainCards[South][1] = holding({Ace});
        return deal;
    }
}

class NodeTest : public ::testing::Test
{
};

TEST_F(NodeTest, RootInvariantsHoldOverEveryLayoutThatSurvivesFiltering)
{
    Deal const root_layout = make_root_layout();
    VectorLayoutSource source({make_root_layout(), make_swapped_split_layout()});

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source);
    ASSERT_TRUE(node.has_value());

    ASSERT_EQ(node->layouts.size(), 2u);
    ASSERT_EQ(node->p.size(), 2u);
    for (Probability const p_i : node->p)
    {
        EXPECT_DOUBLE_EQ(p_i, 1.0);
    }
    EXPECT_DOUBLE_EQ(node->kappa, 0.5);

    KahanAccumulator total_weight;
    for (Probability const p_i : node->p)
    {
        total_weight.add(node->kappa * p_i);
    }
    EXPECT_DOUBLE_EQ(total_weight.value(), 1.0);
}

TEST_F(NodeTest, KappaIsOneOverTheSurvivingCountNotTheRawSourceSize)
{
    Deal const root_layout = make_root_layout();
    // Three raw layouts, only two of which are consistent with the root —
    // kappa must be 1/2, not 1/3. A kappa computed from source.size()
    // directly would pass every other test in this file, since none of
    // them puts an inconsistent layout in the source alongside consistent
    // ones.
    VectorLayoutSource source(
        {make_root_layout(), make_swapped_split_layout(), make_wrong_dummy_layout()});

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source);
    ASSERT_TRUE(node.has_value());
    ASSERT_EQ(node->layouts.size(), 2u);
    EXPECT_DOUBLE_EQ(node->kappa, 0.5);
}

TEST_F(NodeTest, ASourceThatCannotReportItsSizeIsReportedAsAnError)
{
    Deal const root_layout = make_root_layout();
    UnboundedLayoutSource source;

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source);
    EXPECT_FALSE(node.has_value());
}

TEST_F(NodeTest, ASourceWhereNoLayoutSurvivesIsReportedAsAnError)
{
    Deal const root_layout = make_root_layout();
    VectorLayoutSource source({make_wrong_dummy_layout()});

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source);
    EXPECT_FALSE(node.has_value());
}

TEST_F(NodeTest, ADifferentDefenderSplitOfTheSamePoolSurvivesFiltering)
{
    Deal const root_layout = make_root_layout();
    VectorLayoutSource source({make_swapped_split_layout()});

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source);
    ASSERT_TRUE(node.has_value());
    ASSERT_EQ(node->layouts.size(), 1u);
    EXPECT_EQ(node->layouts[0].remainCards[East][2], holding({Jack}));
    EXPECT_EQ(node->layouts[0].remainCards[West][2], holding({Queen}));
}

TEST_F(NodeTest, ALayoutDifferingInDummysHoldingDoesNotSurviveFiltering)
{
    Deal const root_layout = make_root_layout();
    VectorLayoutSource source({make_root_layout(), make_wrong_dummy_layout()});

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source);
    ASSERT_TRUE(node.has_value());
    EXPECT_EQ(node->layouts.size(), 1u);  // only make_root_layout() survives
}

TEST_F(NodeTest, CommonKnowledgeFieldsAreSetFromTheRootLayoutAndCaller)
{
    Deal const root_layout = make_root_layout();
    VectorLayoutSource source({make_root_layout()});

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/7, source);
    ASSERT_TRUE(node.has_value());
    EXPECT_EQ(node->state.trump, DDS_NOTRUMP);
    EXPECT_EQ(node->state.first, North);
    EXPECT_EQ(node->state.declarer, North);
    EXPECT_EQ(node->state.tricks_needed, 7);
    EXPECT_EQ(node->state.tricks_won_by_declarer, 0);
    EXPECT_EQ(node->state.history.number, 0);
    // known_holdings: declarer/dummy exact, defenders as the union pool.
    EXPECT_EQ(node->state.known_holdings.remainCards[North][0], holding({Ace, King}));
    EXPECT_EQ(node->state.known_holdings.remainCards[South][1], holding({Ace, King}));
    EXPECT_EQ(node->state.known_holdings.remainCards[East][2], holding({Queen, Jack}));
    EXPECT_EQ(node->state.known_holdings.remainCards[West][2], holding({Queen, Jack}));
}
