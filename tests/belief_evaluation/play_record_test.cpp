#include <gtest/gtest.h>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/play_record.hpp>

namespace be = dds::belief_evaluation;

using be::HistoryVerdict;
using be::PlayRecord;

namespace
{
    constexpr int Spades = 0;
    constexpr int Hearts = 1;

    constexpr int North = 0;
    constexpr int West = 3;

    auto trace_of(std::vector<std::pair<int, int>> const& cards) -> PlayTraceBin
    {
        PlayTraceBin trace{};
        trace.number = static_cast<int>(cards.size());
        for (std::size_t i = 0; i < cards.size(); ++i)
        {
            trace.suit[i] = cards[i].first;
            trace.rank[i] = cards[i].second;
        }
        return trace;
    }
}  // namespace

TEST(PlayRecordTest, AnEmptyRecordIsAccepted)
{
    auto [record, verdict] = PlayRecord::create(PlayTraceBin{}, West);

    EXPECT_EQ(verdict, HistoryVerdict::Consistent);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->cards().number, 0);
    EXPECT_EQ(record->opening_leader(), West);
}

TEST(PlayRecordTest, ATrailingIncompleteTrickIsAccepted)
{
    PlayTraceBin const cards = trace_of(
        {{Spades, 6}, {Spades, 9}, {Spades, 4}, {Spades, 14}, {Spades, 5}});  // 5 cards: one trick plus one

    auto [record, verdict] = PlayRecord::create(cards, West);

    EXPECT_EQ(verdict, HistoryVerdict::Consistent);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->cards().number, 5);
}

TEST(PlayRecordTest, AnOpeningLeaderOutOfRangeIsRejected)
{
    auto [record, verdict] = PlayRecord::create(PlayTraceBin{}, /*opening_leader=*/4);

    EXPECT_EQ(verdict, HistoryVerdict::InvalidInput);
    EXPECT_FALSE(record.has_value());
}

TEST(PlayRecordTest, ANegativeOpeningLeaderIsRejected)
{
    auto [record, verdict] = PlayRecord::create(PlayTraceBin{}, /*opening_leader=*/-1);

    EXPECT_EQ(verdict, HistoryVerdict::InvalidInput);
    EXPECT_FALSE(record.has_value());
}

TEST(PlayRecordTest, ACardWithSuitOutOfRangeIsRejected)
{
    PlayTraceBin const cards = trace_of({{DDS_SUITS, 6}});

    auto [record, verdict] = PlayRecord::create(cards, North);

    EXPECT_EQ(verdict, HistoryVerdict::InvalidInput);
    EXPECT_FALSE(record.has_value());
}

TEST(PlayRecordTest, ACardWithRankOutOfRangeIsRejected)
{
    PlayTraceBin const cards = trace_of({{Spades, 1}});

    auto [record, verdict] = PlayRecord::create(cards, North);

    EXPECT_EQ(verdict, HistoryVerdict::InvalidInput);
    EXPECT_FALSE(record.has_value());
}

TEST(PlayRecordTest, ADuplicatedCardIsRejected)
{
    PlayTraceBin const cards = trace_of({{Spades, 6}, {Hearts, 2}, {Spades, 6}});

    auto [record, verdict] = PlayRecord::create(cards, North);

    EXPECT_EQ(verdict, HistoryVerdict::DuplicatedCard);
    EXPECT_FALSE(record.has_value());
}

TEST(PlayRecordTest, CardsAndOpeningLeaderRoundTripExactly)
{
    PlayTraceBin const cards = trace_of({{Spades, 6}, {Spades, 9}, {Spades, 4}, {Spades, 14}});

    auto [record, verdict] = PlayRecord::create(cards, West);

    ASSERT_EQ(verdict, HistoryVerdict::Consistent);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->opening_leader(), West);
    ASSERT_EQ(record->cards().number, 4);
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_EQ(record->cards().suit[i], cards.suit[i]);
        EXPECT_EQ(record->cards().rank[i], cards.rank[i]);
    }
}
