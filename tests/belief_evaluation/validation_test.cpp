#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/validation.hpp>

// Tests for validate_declarer_card and validate_defender_distribution.

namespace be = dds::belief_evaluation;

namespace
{
    auto deal_with_south_holding_two_and_three_of_spades() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = 2;
        deal.remainCards[2][0] = (1u << 2) | (1u << 3);  // South: 2S, 3S
        deal.remainCards[2][1] = 1u << 4;                // South: 4H
        return deal;
    }
}

TEST(ValidateDeclarerCard, RejectsACardNotHeldBySeat)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    EXPECT_EQ(
        be::validate_declarer_card(deal, 2, be::Card{0, 14}),  // ace of spades, not held
        be::ValidationError::CardNotHeld);
}

TEST(ValidateDeclarerCard, RejectsACardIllegalForTheTrickInProgress)
{
    Deal deal = deal_with_south_holding_two_and_three_of_spades();
    deal.currentTrickSuit[0] = 0;  // spades led
    deal.currentTrickRank[0] = 5;  // by some other seat
    EXPECT_EQ(
        be::validate_declarer_card(deal, 2, be::Card{1, 4}),  // 4H, but South holds spades
        be::ValidationError::CardIllegalForTrick);
}

TEST(ValidateDeclarerCard, AcceptsAHeldCardThatFollowsSuit)
{
    Deal deal = deal_with_south_holding_two_and_three_of_spades();
    deal.currentTrickSuit[0] = 0;
    deal.currentTrickRank[0] = 5;
    EXPECT_EQ(be::validate_declarer_card(deal, 2, be::Card{0, 2}), be::ValidationError::None);
}

TEST(ValidateDeclarerCard, AcceptsAnyHeldCardWhenLeading)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    EXPECT_EQ(be::validate_declarer_card(deal, 2, be::Card{1, 4}), be::ValidationError::None);
}

TEST(ValidateDeclarerCard, AcceptsAnyHeldCardWhenVoidInTheLedSuit)
{
    Deal deal = deal_with_south_holding_two_and_three_of_spades();
    deal.currentTrickSuit[0] = 2;  // diamonds led; South holds none
    deal.currentTrickRank[0] = 5;
    EXPECT_EQ(be::validate_declarer_card(deal, 2, be::Card{1, 4}), be::ValidationError::None);
}

TEST(ValidateDeclarerCard, RejectsAnOutOfRangeSuitWithoutUndefinedBehaviour)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    EXPECT_EQ(be::validate_declarer_card(deal, 2, be::Card{4, 5}), be::ValidationError::CardNotHeld);
    EXPECT_EQ(be::validate_declarer_card(deal, 2, be::Card{-1, 5}), be::ValidationError::CardNotHeld);
}

TEST(ValidateDeclarerCard, RejectsAnOutOfRangeRankWithoutUndefinedBehaviour)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    EXPECT_EQ(be::validate_declarer_card(deal, 2, be::Card{0, 15}), be::ValidationError::CardNotHeld);
    EXPECT_EQ(be::validate_declarer_card(deal, 2, be::Card{0, 1}), be::ValidationError::CardNotHeld);
    // Near the width of the shift in is_held(). Must not be undefined
    // behaviour.
    EXPECT_EQ(be::validate_declarer_card(deal, 2, be::Card{0, 31}), be::ValidationError::CardNotHeld);
}

TEST(ValidateDeclarerCard, RejectsAnOutOfRangeSeatWithoutUndefinedBehaviour)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    EXPECT_EQ(be::validate_declarer_card(deal, 4, be::Card{0, 2}), be::ValidationError::CardNotHeld);
    EXPECT_EQ(be::validate_declarer_card(deal, -1, be::Card{0, 2}), be::ValidationError::CardNotHeld);
}

TEST(ValidateDefenderDistribution, RejectsACardNotHeldBySeat)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    std::vector<be::WeightedCard> const distribution{{be::Card{0, 14}, 1.0}};
    EXPECT_EQ(
        be::validate_defender_distribution(deal, 2, distribution),
        be::ValidationError::CardNotHeld);
}

TEST(ValidateDefenderDistribution, RejectsANonPositiveProbability)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    std::vector<be::WeightedCard> const distribution{{be::Card{0, 2}, 0.0}, {be::Card{0, 3}, 1.0}};
    EXPECT_EQ(
        be::validate_defender_distribution(deal, 2, distribution),
        be::ValidationError::ProbabilityNonPositive);
}

TEST(ValidateDefenderDistribution, RejectsANegativeProbability)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    std::vector<be::WeightedCard> const distribution{{be::Card{0, 2}, -0.5}, {be::Card{0, 3}, 1.5}};
    EXPECT_EQ(
        be::validate_defender_distribution(deal, 2, distribution),
        be::ValidationError::ProbabilityNonPositive);
}

TEST(ValidateDefenderDistribution, RejectsProbabilitiesNotSummingToOne)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    std::vector<be::WeightedCard> const distribution{{be::Card{0, 2}, 0.4}, {be::Card{0, 3}, 0.4}};
    EXPECT_EQ(
        be::validate_defender_distribution(deal, 2, distribution),
        be::ValidationError::ProbabilitiesDoNotSumToOne);
}

TEST(ValidateDefenderDistribution, AcceptsAValidDistribution)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    std::vector<be::WeightedCard> const distribution{{be::Card{0, 2}, 0.5}, {be::Card{0, 3}, 0.5}};
    EXPECT_EQ(be::validate_defender_distribution(deal, 2, distribution), be::ValidationError::None);
}

TEST(ValidateDefenderDistribution, AcceptsASingleCertainCard)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    std::vector<be::WeightedCard> const distribution{{be::Card{1, 4}, 1.0}};
    EXPECT_EQ(be::validate_defender_distribution(deal, 2, distribution), be::ValidationError::None);
}

TEST(ValidateDefenderDistribution, RejectsACardIllegalForTheTrickInProgress)
{
    Deal deal = deal_with_south_holding_two_and_three_of_spades();
    deal.currentTrickSuit[0] = 0;  // spades led
    deal.currentTrickRank[0] = 5;
    // South's 4H, but South holds spades and must follow suit.
    std::vector<be::WeightedCard> const distribution{{be::Card{1, 4}, 1.0}};
    EXPECT_EQ(
        be::validate_defender_distribution(deal, 2, distribution),
        be::ValidationError::CardIllegalForTrick);
}

TEST(ValidateDefenderDistribution, AcceptsCardsThatFollowSuit)
{
    Deal deal = deal_with_south_holding_two_and_three_of_spades();
    deal.currentTrickSuit[0] = 0;
    deal.currentTrickRank[0] = 5;
    std::vector<be::WeightedCard> const distribution{{be::Card{0, 2}, 0.5}, {be::Card{0, 3}, 0.5}};
    EXPECT_EQ(be::validate_defender_distribution(deal, 2, distribution), be::ValidationError::None);
}

TEST(ValidateDefenderDistribution, AcceptsAnyHeldCardWhenVoidInTheLedSuit)
{
    Deal deal = deal_with_south_holding_two_and_three_of_spades();
    deal.currentTrickSuit[0] = 2;  // diamonds led; South holds none
    deal.currentTrickRank[0] = 5;
    std::vector<be::WeightedCard> const distribution{{be::Card{1, 4}, 1.0}};
    EXPECT_EQ(be::validate_defender_distribution(deal, 2, distribution), be::ValidationError::None);
}

TEST(ValidateDefenderDistribution, RejectsANaNProbability)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    // NaN fails every comparison, including `<= 0.0` and the sum-tolerance
    // check, so without a guard it would pass both.
    std::vector<be::WeightedCard> const distribution{
        {be::Card{0, 2}, std::numeric_limits<double>::quiet_NaN()}};
    EXPECT_EQ(
        be::validate_defender_distribution(deal, 2, distribution),
        be::ValidationError::ProbabilityNonPositive);
}

TEST(ValidateDefenderDistribution, RejectsAPositiveInfiniteProbability)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    std::vector<be::WeightedCard> const distribution{
        {be::Card{0, 2}, std::numeric_limits<double>::infinity()}};
    EXPECT_EQ(
        be::validate_defender_distribution(deal, 2, distribution),
        be::ValidationError::ProbabilityNonPositive);
}

TEST(ValidateDefenderDistribution, RejectsAnOutOfRangeCardWithoutUndefinedBehaviour)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    std::vector<be::WeightedCard> const distribution{{be::Card{9, 9}, 1.0}};
    EXPECT_EQ(
        be::validate_defender_distribution(deal, 2, distribution),
        be::ValidationError::CardNotHeld);
}

TEST(ValidateDefenderDistribution, RejectsAnOutOfRangeSeatEvenForAnEmptyDistribution)
{
    // A non-empty distribution meets is_held()'s per-entry seat guard. An
    // empty one skips that loop, so the seat needs a separate upfront check.
    // Without it the result would be ProbabilitiesDoNotSumToOne, which names
    // the wrong problem. CardNotHeld matches validate_declarer_card.
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    std::vector<be::WeightedCard> const empty_distribution;
    EXPECT_EQ(
        be::validate_defender_distribution(deal, DDS_HANDS, empty_distribution),
        be::ValidationError::CardNotHeld);
    EXPECT_EQ(
        be::validate_defender_distribution(deal, -1, empty_distribution),
        be::ValidationError::CardNotHeld);
}

TEST(ValidateDefenderDistribution, RejectsAnEmptyDistributionForAValidSeat)
{
    // A defender must return at least one card. With a valid seat, an empty
    // distribution is ProbabilitiesDoNotSumToOne.
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    std::vector<be::WeightedCard> const empty_distribution;
    EXPECT_EQ(
        be::validate_defender_distribution(deal, 2, empty_distribution),
        be::ValidationError::ProbabilitiesDoNotSumToOne);
}
