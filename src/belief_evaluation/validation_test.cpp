#include <gtest/gtest.h>

#include <vector>

#include <api/dll.h>
#include <utility/constants.h>

#include <belief_evaluation/validation.hpp>

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
        validate_declarer_card(deal, 2, Card{0, 14}),  // ace of spades, not held
        ValidationError::CardNotHeld);
}

TEST(ValidateDeclarerCard, RejectsACardIllegalForTheTrickInProgress)
{
    Deal deal = deal_with_south_holding_two_and_three_of_spades();
    deal.currentTrickSuit[0] = 0;  // spades led
    deal.currentTrickRank[0] = 5;  // by some other seat
    EXPECT_EQ(
        validate_declarer_card(deal, 2, Card{1, 4}),  // 4H, but South holds spades
        ValidationError::CardIllegalForTrick);
}

TEST(ValidateDeclarerCard, AcceptsAHeldCardThatFollowsSuit)
{
    Deal deal = deal_with_south_holding_two_and_three_of_spades();
    deal.currentTrickSuit[0] = 0;
    deal.currentTrickRank[0] = 5;
    EXPECT_EQ(validate_declarer_card(deal, 2, Card{0, 2}), ValidationError::None);
}

TEST(ValidateDeclarerCard, AcceptsAnyHeldCardWhenLeading)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    EXPECT_EQ(validate_declarer_card(deal, 2, Card{1, 4}), ValidationError::None);
}

TEST(ValidateDeclarerCard, AcceptsAnyHeldCardWhenVoidInTheLedSuit)
{
    Deal deal = deal_with_south_holding_two_and_three_of_spades();
    deal.currentTrickSuit[0] = 2;  // diamonds led; South holds none
    deal.currentTrickRank[0] = 5;
    EXPECT_EQ(validate_declarer_card(deal, 2, Card{1, 4}), ValidationError::None);
}

TEST(ValidateDeclarerCard, RejectsAnOutOfRangeSuitWithoutUndefinedBehaviour)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    EXPECT_EQ(validate_declarer_card(deal, 2, Card{4, 5}), ValidationError::CardNotHeld);
    EXPECT_EQ(validate_declarer_card(deal, 2, Card{-1, 5}), ValidationError::CardNotHeld);
}

TEST(ValidateDeclarerCard, RejectsAnOutOfRangeRankWithoutUndefinedBehaviour)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    EXPECT_EQ(validate_declarer_card(deal, 2, Card{0, 15}), ValidationError::CardNotHeld);
    EXPECT_EQ(validate_declarer_card(deal, 2, Card{0, 1}), ValidationError::CardNotHeld);
    // Close to the width of the shift in is_held(); must not be undefined
    // behaviour even though it is nowhere near a legal rank.
    EXPECT_EQ(validate_declarer_card(deal, 2, Card{0, 31}), ValidationError::CardNotHeld);
}

TEST(ValidateDeclarerCard, RejectsAnOutOfRangeSeatWithoutUndefinedBehaviour)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    EXPECT_EQ(validate_declarer_card(deal, 4, Card{0, 2}), ValidationError::CardNotHeld);
    EXPECT_EQ(validate_declarer_card(deal, -1, Card{0, 2}), ValidationError::CardNotHeld);
}

TEST(ValidateDefenderDistribution, RejectsACardNotHeldBySeat)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    std::vector<WeightedCard> const distribution{{Card{0, 14}, 1.0}};
    EXPECT_EQ(
        validate_defender_distribution(deal, 2, distribution),
        ValidationError::CardNotHeld);
}

TEST(ValidateDefenderDistribution, RejectsANonPositiveProbability)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    std::vector<WeightedCard> const distribution{{Card{0, 2}, 0.0}, {Card{0, 3}, 1.0}};
    EXPECT_EQ(
        validate_defender_distribution(deal, 2, distribution),
        ValidationError::ProbabilityNonPositive);
}

TEST(ValidateDefenderDistribution, RejectsANegativeProbability)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    std::vector<WeightedCard> const distribution{{Card{0, 2}, -0.5}, {Card{0, 3}, 1.5}};
    EXPECT_EQ(
        validate_defender_distribution(deal, 2, distribution),
        ValidationError::ProbabilityNonPositive);
}

TEST(ValidateDefenderDistribution, RejectsProbabilitiesNotSummingToOne)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    std::vector<WeightedCard> const distribution{{Card{0, 2}, 0.4}, {Card{0, 3}, 0.4}};
    EXPECT_EQ(
        validate_defender_distribution(deal, 2, distribution),
        ValidationError::ProbabilitiesDoNotSumToOne);
}

TEST(ValidateDefenderDistribution, AcceptsAValidDistribution)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    std::vector<WeightedCard> const distribution{{Card{0, 2}, 0.5}, {Card{0, 3}, 0.5}};
    EXPECT_EQ(validate_defender_distribution(deal, 2, distribution), ValidationError::None);
}

TEST(ValidateDefenderDistribution, AcceptsASingleCertainCard)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    std::vector<WeightedCard> const distribution{{Card{1, 4}, 1.0}};
    EXPECT_EQ(validate_defender_distribution(deal, 2, distribution), ValidationError::None);
}

TEST(ValidateDefenderDistribution, RejectsACardIllegalForTheTrickInProgress)
{
    Deal deal = deal_with_south_holding_two_and_three_of_spades();
    deal.currentTrickSuit[0] = 0;  // spades led
    deal.currentTrickRank[0] = 5;
    // South's 4H, but South holds spades and must follow suit.
    std::vector<WeightedCard> const distribution{{Card{1, 4}, 1.0}};
    EXPECT_EQ(
        validate_defender_distribution(deal, 2, distribution),
        ValidationError::CardIllegalForTrick);
}

TEST(ValidateDefenderDistribution, AcceptsCardsThatFollowSuit)
{
    Deal deal = deal_with_south_holding_two_and_three_of_spades();
    deal.currentTrickSuit[0] = 0;
    deal.currentTrickRank[0] = 5;
    std::vector<WeightedCard> const distribution{{Card{0, 2}, 0.5}, {Card{0, 3}, 0.5}};
    EXPECT_EQ(validate_defender_distribution(deal, 2, distribution), ValidationError::None);
}

TEST(ValidateDefenderDistribution, AcceptsAnyHeldCardWhenVoidInTheLedSuit)
{
    Deal deal = deal_with_south_holding_two_and_three_of_spades();
    deal.currentTrickSuit[0] = 2;  // diamonds led; South holds none
    deal.currentTrickRank[0] = 5;
    std::vector<WeightedCard> const distribution{{Card{1, 4}, 1.0}};
    EXPECT_EQ(validate_defender_distribution(deal, 2, distribution), ValidationError::None);
}

TEST(ValidateDefenderDistribution, RejectsAnOutOfRangeCardWithoutUndefinedBehaviour)
{
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    std::vector<WeightedCard> const distribution{{Card{9, 9}, 1.0}};
    EXPECT_EQ(
        validate_defender_distribution(deal, 2, distribution),
        ValidationError::CardNotHeld);
}
