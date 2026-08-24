#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include <api/dll.h>
#include <utility/constants.h>

#include <belief_evaluation/layout_key.hpp>
#include <belief_evaluation/types.hpp>

#include "test_support.hpp"

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
        deal.remainCards[East][2] = holding({King, Queen});  // diamonds
        return deal;
    }
}

class ScriptedDefenderTest : public ::testing::Test
{
};

TEST_F(ScriptedDefenderTest, ReturnsTheScriptedCardWithCertaintyOnAMatchingQuery)
{
    Deal const layout = make_layout();
    ObservationState state{};

    ScriptedDefender::Key const key{layout_key(layout, East), ""};
    ScriptedDefender defender({{key, Card{2, King}}});

    DefenderQuery const query{layout, East, state};
    std::vector<WeightedCard> const distribution = defender.as_strategy()(query);

    ASSERT_EQ(distribution.size(), 1u);
    EXPECT_EQ(distribution[0].card.suit, 2);
    EXPECT_EQ(distribution[0].card.rank, King);
    EXPECT_DOUBLE_EQ(distribution[0].probability, 1.0);
}

TEST_F(ScriptedDefenderTest, RecordsTheSeatAndLayoutOfEveryQuery)
{
    Deal const layout = make_layout();
    ObservationState state{};

    ScriptedDefender::Key const key{layout_key(layout, East), ""};
    ScriptedDefender defender({{key, Card{2, King}}});

    DefenderQuery const query{layout, East, state};
    defender.as_strategy()(query);

    ASSERT_EQ(defender.queries().size(), 1u);
    EXPECT_EQ(defender.queries()[0].seat, East);
    EXPECT_EQ(defender.queries()[0].layout, layout_key(layout, East));
}

TEST_F(ScriptedDefenderTest, DistinguishesTwoQueriesWithTheSamePoolButDifferentHistory)
{
    Deal const layout = make_layout();
    ObservationState state_at_root{};
    ObservationState state_after_one_card{};
    state_after_one_card.history.number = 1;
    state_after_one_card.history.suit[0] = 0;
    state_after_one_card.history.rank[0] = 14;

    ScriptedDefender::Key const key_at_root{layout_key(layout, East), ""};
    ScriptedDefender::Key const key_after_one_card{layout_key(layout, East), "0:14,"};
    ScriptedDefender defender({{key_at_root, Card{2, King}}, {key_after_one_card, Card{2, Queen}}});

    auto const strategy = defender.as_strategy();
    std::vector<WeightedCard> const at_root = strategy(DefenderQuery{layout, East, state_at_root});
    std::vector<WeightedCard> const after_one_card =
        strategy(DefenderQuery{layout, East, state_after_one_card});

    ASSERT_EQ(at_root.size(), 1u);
    ASSERT_EQ(after_one_card.size(), 1u);
    EXPECT_EQ(at_root[0].card.rank, King);
    EXPECT_EQ(after_one_card[0].card.rank, Queen);
}

TEST_F(ScriptedDefenderTest, AMissingTableEntryIsALoudNonFatalFailure)
{
    Deal const layout = make_layout();
    ObservationState state{};

    ScriptedDefender defender({});  // empty table: any query is a miss

    std::vector<WeightedCard> distribution;
    EXPECT_NONFATAL_FAILURE(
        distribution = defender.as_strategy()(DefenderQuery{layout, East, state}),
        "no scripted entry");

    // The failure is the intended behaviour, per this double's own doxygen;
    // it must still return something obviously invalid rather than crash.
    EXPECT_TRUE(distribution.empty());
}

TEST_F(ScriptedDefenderTest, AScriptedIllegalCardIsALoudNonFatalFailure)
{
    Deal const layout = make_layout();
    ObservationState state{};

    // East does not hold the ace of diamonds — an illegal script.
    ScriptedDefender::Key const key{layout_key(layout, East), ""};
    ScriptedDefender defender({{key, Card{2, 14}}});

    std::vector<WeightedCard> distribution;
    EXPECT_NONFATAL_FAILURE(
        distribution = defender.as_strategy()(DefenderQuery{layout, East, state}),
        "illegal defence");
}
