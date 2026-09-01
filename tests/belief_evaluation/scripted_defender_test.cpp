#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include <api/dds_data_types.hpp>
#include <utility/constants.h>

#include <belief_evaluation/layout_key.hpp>
#include <belief_evaluation/types.hpp>

#include "test_support.hpp"

namespace be = dds::belief_evaluation;

namespace
{
    constexpr int King = 13;
    constexpr int Queen = 12;
    constexpr int East = 1;

    auto make_layout() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[East][2] = be::holding({King, Queen});  // diamonds
        return deal;
    }
}

class ScriptedDefenderTest : public ::testing::Test
{
};

TEST_F(ScriptedDefenderTest, ReturnsTheScriptedCardWithCertaintyOnAMatchingQuery)
{
    Deal const layout = make_layout();
    be::ObservationState state{};

    be::ScriptedDefender::Key const key{be::layout_key(layout, East), ""};
    be::ScriptedDefender defender({{key, be::Card{2, King}}});

    be::DefenderQuery const query{layout, East, state};
    std::vector<be::WeightedCard> const distribution = defender.as_strategy()(query);

    ASSERT_EQ(distribution.size(), 1u);
    EXPECT_EQ(distribution[0].card.suit, 2);
    EXPECT_EQ(distribution[0].card.rank, King);
    EXPECT_DOUBLE_EQ(distribution[0].probability, 1.0);
}

TEST_F(ScriptedDefenderTest, RecordsTheSeatAndLayoutOfEveryQuery)
{
    Deal const layout = make_layout();
    be::ObservationState state{};

    be::ScriptedDefender::Key const key{be::layout_key(layout, East), ""};
    be::ScriptedDefender defender({{key, be::Card{2, King}}});

    be::DefenderQuery const query{layout, East, state};
    defender.as_strategy()(query);

    ASSERT_EQ(defender.queries().size(), 1u);
    EXPECT_EQ(defender.queries()[0].seat, East);
    EXPECT_EQ(defender.queries()[0].layout, be::layout_key(layout, East));
}

TEST_F(ScriptedDefenderTest, DistinguishesTwoQueriesWithTheSamePoolButDifferentHistory)
{
    Deal const layout = make_layout();
    be::ObservationState state_at_root{};
    be::ObservationState state_after_one_card{};
    state_after_one_card.history.number = 1;
    state_after_one_card.history.suit[0] = 0;
    state_after_one_card.history.rank[0] = 14;

    be::ScriptedDefender::Key const key_at_root{be::layout_key(layout, East), ""};
    be::ScriptedDefender::Key const key_after_one_card{be::layout_key(layout, East), "0:14,"};
    be::ScriptedDefender defender({{key_at_root, be::Card{2, King}}, {key_after_one_card, be::Card{2, Queen}}});

    auto const strategy = defender.as_strategy();
    std::vector<be::WeightedCard> const at_root = strategy(be::DefenderQuery{layout, East, state_at_root});
    std::vector<be::WeightedCard> const after_one_card =
        strategy(be::DefenderQuery{layout, East, state_after_one_card});

    ASSERT_EQ(at_root.size(), 1u);
    ASSERT_EQ(after_one_card.size(), 1u);
    EXPECT_EQ(at_root[0].card.rank, King);
    EXPECT_EQ(after_one_card[0].card.rank, Queen);
}

TEST_F(ScriptedDefenderTest, AMissingTableEntryIsALoudNonFatalFailure)
{
    Deal const layout = make_layout();
    be::ObservationState state{};

    be::ScriptedDefender defender({});  // empty table: any query is a miss

    std::vector<be::WeightedCard> distribution;
    EXPECT_NONFATAL_FAILURE(
        distribution = defender.as_strategy()(be::DefenderQuery{layout, East, state}),
        "no scripted entry");

    // The failure is the intended behaviour, per this double's own doxygen;
    // it must still return something obviously invalid rather than crash.
    EXPECT_TRUE(distribution.empty());
}

TEST_F(ScriptedDefenderTest, AScriptedIllegalCardIsALoudNonFatalFailure)
{
    Deal const layout = make_layout();
    be::ObservationState state{};

    // East does not hold the ace of diamonds — an illegal script.
    be::ScriptedDefender::Key const key{be::layout_key(layout, East), ""};
    be::ScriptedDefender defender({{key, be::Card{2, 14}}});

    std::vector<be::WeightedCard> distribution;
    EXPECT_NONFATAL_FAILURE(
        distribution = defender.as_strategy()(be::DefenderQuery{layout, East, state}),
        "illegal defence");
}

// --- the stochastic (multi-entry) factory ----------------------------------

TEST_F(ScriptedDefenderTest, ReturnsAScriptedMultiEntryDistribution)
{
    Deal const layout = make_layout();
    be::ObservationState state{};

    be::ScriptedDefender::Key const key{be::layout_key(layout, East), ""};
    be::ScriptedDefender defender = be::ScriptedDefender::stochastic(
        {{key, {be::WeightedCard{be::Card{2, King}, 0.5}, be::WeightedCard{be::Card{2, Queen}, 0.5}}}});

    std::vector<be::WeightedCard> const distribution =
        defender.as_strategy()(be::DefenderQuery{layout, East, state});

    ASSERT_EQ(distribution.size(), 2u);
    EXPECT_EQ(distribution[0].card.rank, King);
    EXPECT_DOUBLE_EQ(distribution[0].probability, 0.5);
    EXPECT_EQ(distribution[1].card.rank, Queen);
    EXPECT_DOUBLE_EQ(distribution[1].probability, 0.5);
}

TEST_F(ScriptedDefenderTest, TheDeterministicConstructorStillReturnsASingleCertainCard)
{
    // Same assertion as ReturnsTheScriptedCardWithCertaintyOnAMatchingQuery
    // above, kept separate here to sit next to the multi-entry case as a
    // reminder that the constructor and the stochastic factory must keep
    // working identically.
    Deal const layout = make_layout();
    be::ObservationState state{};

    be::ScriptedDefender::Key const key{be::layout_key(layout, East), ""};
    be::ScriptedDefender defender({{key, be::Card{2, King}}});

    std::vector<be::WeightedCard> const distribution =
        defender.as_strategy()(be::DefenderQuery{layout, East, state});

    ASSERT_EQ(distribution.size(), 1u);
    EXPECT_EQ(distribution[0].card.rank, King);
    EXPECT_DOUBLE_EQ(distribution[0].probability, 1.0);
}

TEST_F(ScriptedDefenderTest, RecordingWorksTheSameForTheMultiEntryConstructor)
{
    Deal const layout = make_layout();
    be::ObservationState state{};

    be::ScriptedDefender::Key const key{be::layout_key(layout, East), ""};
    be::ScriptedDefender defender = be::ScriptedDefender::stochastic(
        {{key, {be::WeightedCard{be::Card{2, King}, 0.5}, be::WeightedCard{be::Card{2, Queen}, 0.5}}}});

    defender.as_strategy()(be::DefenderQuery{layout, East, state});

    ASSERT_EQ(defender.queries().size(), 1u);
    EXPECT_EQ(defender.queries()[0].seat, East);
    EXPECT_EQ(defender.queries()[0].layout, be::layout_key(layout, East));
}

TEST_F(ScriptedDefenderTest, AScriptedDistributionNotSummingToOneIsALoudNonFatalFailure)
{
    Deal const layout = make_layout();
    be::ObservationState state{};

    be::ScriptedDefender::Key const key{be::layout_key(layout, East), ""};
    // 0.5 + 0.4 = 0.9, not 1 -- an illegal script.
    be::ScriptedDefender defender = be::ScriptedDefender::stochastic(
        {{key, {be::WeightedCard{be::Card{2, King}, 0.5}, be::WeightedCard{be::Card{2, Queen}, 0.4}}}});

    std::vector<be::WeightedCard> distribution;
    EXPECT_NONFATAL_FAILURE(
        distribution = defender.as_strategy()(be::DefenderQuery{layout, East, state}),
        "illegal defence");
}
