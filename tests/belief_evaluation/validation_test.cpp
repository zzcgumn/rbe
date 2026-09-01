#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include <api/dds_data_types.hpp>
#include <utility/constants.h>

#include <belief_evaluation/validation.hpp>

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
    // Close to the width of the shift in is_held(); must not be undefined
    // behaviour even though it is nowhere near a legal rank.
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
    // NaN fails every comparison, including `<= 0.0` and the final
    // sum-tolerance check, so an unguarded NaN slips past both.
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
    // With a non-empty distribution, an invalid seat is already caught by
    // is_held()'s per-entry guard. An *empty* distribution never enters that
    // loop, so without an explicit upfront check it fell through to
    // ProbabilitiesDoNotSumToOne (0.0 is never close to 1.0) — a misleading
    // error that names the wrong problem, and inconsistent with
    // validate_declarer_card, which always reports an invalid seat as
    // CardNotHeld regardless of the card.
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
    // A defender must return at least one card; distinct from the seat
    // check above, this is still ProbabilitiesDoNotSumToOne when the seat
    // itself is fine.
    Deal const deal = deal_with_south_holding_two_and_three_of_spades();
    std::vector<be::WeightedCard> const empty_distribution;
    EXPECT_EQ(
        be::validate_defender_distribution(deal, 2, empty_distribution),
        be::ValidationError::ProbabilitiesDoNotSumToOne);
}
