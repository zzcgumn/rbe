#include <gtest/gtest.h>

#include <api/dds_data_types.hpp>
#include <utility/constants.h>

#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/validation.hpp>

#include "test_support.hpp"

namespace be = dds::belief_evaluation;

namespace
{
    constexpr int Two = 2;
    constexpr int Three = 3;
    constexpr int Jack = 11;
    constexpr int Queen = 12;
    constexpr int Ace = 14;

    constexpr int Spades = 0;
    constexpr int Hearts = 1;
    constexpr int Diamonds = 2;
    constexpr int Clubs = 3;

    constexpr int North = 0;  // declarer
    constexpr int East = 1;   // a defender
    constexpr int South = 2;  // dummy
    constexpr int West = 3;   // a defender

    /// A single full trick declarer is certain to win: North leads the ace
    /// of spades, East/South/West each hold one low spade and must follow
    /// suit, so North's ace always wins regardless of what anyone plays.
    auto make_one_trick_certain_win() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = North;
        deal.remainCards[North][Spades] = be::holding({Ace});
        deal.remainCards[East][Spades] = be::holding({Two});
        deal.remainCards[South][Spades] = be::holding({Three});
        deal.remainCards[West][Spades] = be::holding({Jack});
        return deal;
    }

    auto strategy(be::StrategyId id) -> be::DeclarerStrategy
    {
        return be::DeclarerStrategy{.id = id, .play = be::single_card_declarer_play, .state_key = nullptr};
    }
}

class EvaluateTest : public ::testing::Test
{
};

// --- certainty and impossibility: proves the recursion terminates and
// composes; the real oracle (hand-derived end-to-end cases) is in
// oracle_test.cpp.

TEST_F(EvaluateTest, CertaintyGivesPMakeOfOne)
{
    Deal const root_layout = make_one_trick_certain_win();
    be::VectorLayoutSource source({root_layout});

    be::EvaluationResult const result =
        be::evaluate(root_layout, North, /*tricks_needed=*/1, source, strategy(1), be::single_card_defender);

    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.by_strategy.count(1u), 1u);
    EXPECT_DOUBLE_EQ(result.by_strategy.at(1u).p_make, 1.0);
}

TEST_F(EvaluateTest, ImpossibilityGivesPMakeOfZero)
{
    Deal const root_layout = make_one_trick_certain_win();
    be::VectorLayoutSource source({root_layout});

    // Only one trick exists in the whole ending; needing two is impossible.
    be::EvaluationResult const result =
        be::evaluate(root_layout, North, /*tricks_needed=*/2, source, strategy(1), be::single_card_defender);

    ASSERT_FALSE(result.error.has_value());
    EXPECT_DOUBLE_EQ(result.by_strategy.at(1u).p_make, 0.0);
}

// --- error propagation: a bad callback return surfaces as an error, with
// the offending seat and layout, not an exception or an assertion.

TEST_F(EvaluateTest, AnUnenumerableSourceSurfacesAsARootConstructionError)
{
    Deal const root_layout = make_one_trick_certain_win();
    be::UnboundedLayoutSource source;  // size() == nullopt

    be::EvaluationResult const result = be::evaluate(
        root_layout, North, /*tricks_needed=*/1, source, strategy(1), be::single_card_defender);

    ASSERT_TRUE(result.error.has_value());
    EXPECT_TRUE(result.by_strategy.empty());
    EXPECT_EQ(result.error->callback, be::EvaluationCallback::RootConstruction);
    EXPECT_EQ(result.error->seat, North);
    EXPECT_EQ(result.error->root_failure, be::RootFailure::SourceNotEnumerable);
}

TEST_F(EvaluateTest, ANoLayoutSurvivingSourceSurfacesAsARootConstructionErrorWithADistinctCause)
{
    // The distinguishing half of the proof root_failure exists for: an unenumerable
    // source and a source that enumerates fine but has nothing consistent
    // with root_layout both surface as RootConstruction, but with different
    // root_failure values -- the test right above this one pins the first,
    // this one pins the second.
    Deal const root_layout = make_one_trick_certain_win();
    Deal inconsistent_layout = root_layout;
    // Declarer's own holding must match root_layout's exactly to survive
    // make_root's filter -- this doesn't.
    inconsistent_layout.remainCards[North][Spades] = be::holding({Two});
    be::VectorLayoutSource source({inconsistent_layout});

    be::EvaluationResult const result = be::evaluate(
        root_layout, North, /*tricks_needed=*/1, source, strategy(1), be::single_card_defender);

    ASSERT_TRUE(result.error.has_value());
    EXPECT_TRUE(result.by_strategy.empty());
    EXPECT_EQ(result.error->callback, be::EvaluationCallback::RootConstruction);
    EXPECT_EQ(result.error->seat, North);
    EXPECT_EQ(result.error->root_failure, be::RootFailure::NoLayoutSurvived);
}

TEST_F(EvaluateTest, AnIllegalCardFromPiSurfacesAsADeclarerPlayError)
{
    Deal const root_layout = make_one_trick_certain_win();
    be::VectorLayoutSource source({root_layout});
    be::RecordingDeclarerStrategy bad_pi(be::Card{Hearts, Two});  // North doesn't hold a heart at all

    be::EvaluationResult const result = be::evaluate(
        root_layout, North, /*tricks_needed=*/1, source, bad_pi.as_strategy(), be::single_card_defender);

    ASSERT_TRUE(result.error.has_value());
    EXPECT_TRUE(result.by_strategy.empty());
    EXPECT_EQ(result.error->callback, be::EvaluationCallback::DeclarerPlay);
    EXPECT_EQ(result.error->validation, be::ValidationError::CardNotHeld);
    EXPECT_EQ(result.error->seat, North);
    EXPECT_EQ(result.error->layout.remainCards[North][Spades], be::holding({Ace}));
}

TEST_F(EvaluateTest, AnIllegalDistributionFromDeltaSurfacesAsADefenderStrategyError)
{
    Deal const root_layout = make_one_trick_certain_win();
    be::VectorLayoutSource source({root_layout});
    auto const bad_delta = [](be::DefenderQuery const&) -> std::vector<be::WeightedCard>
    {
        return {be::WeightedCard{be::Card{Hearts, Two}, 1.0}};  // East doesn't hold a heart at all
    };

    be::EvaluationResult const result =
        be::evaluate(root_layout, North, /*tricks_needed=*/1, source, strategy(1), bad_delta);

    ASSERT_TRUE(result.error.has_value());
    EXPECT_TRUE(result.by_strategy.empty());
    EXPECT_EQ(result.error->callback, be::EvaluationCallback::DefenderStrategy);
    EXPECT_EQ(result.error->validation, be::ValidationError::CardNotHeld);
    EXPECT_EQ(result.error->seat, East);
    EXPECT_EQ(result.error->layout.remainCards[East][Spades], be::holding({Two}));
}

// --- root-child values ---------------------------------------------------

TEST_F(EvaluateTest, DeclarerRootChildrenAreAlternativesNotAPartition)
{
    // North holds two certain winners in different suits; East/South/West
    // each hold one low card per suit, so either lead wins its trick and
    // the other suit's trick follows automatically. Both candidate first
    // cards are therefore winners on their own -- root_children must NOT
    // sum to p_make (1.0 + 1.0 != 1.0), and p_make must equal whichever
    // one pi actually chose (single_card_declarer_play scans suits in
    // order, so pi leads the spade ace first).
    Deal root_layout{};
    root_layout.trump = DDS_NOTRUMP;
    root_layout.first = North;
    root_layout.remainCards[North][Spades] = be::holding({Ace});
    root_layout.remainCards[North][Hearts] = be::holding({Ace});
    root_layout.remainCards[East][Spades] = be::holding({Two});
    root_layout.remainCards[East][Hearts] = be::holding({Two});
    root_layout.remainCards[South][Spades] = be::holding({Three});
    root_layout.remainCards[South][Hearts] = be::holding({Three});
    root_layout.remainCards[West][Spades] = be::holding({Jack});
    root_layout.remainCards[West][Hearts] = be::holding({Jack});
    be::VectorLayoutSource source({root_layout});

    be::EvaluationResult const result = be::evaluate(
        root_layout, North, /*tricks_needed=*/2, source, strategy(1), be::single_card_defender);

    ASSERT_FALSE(result.error.has_value());
    be::EvaluationValue const& value = result.by_strategy.at(1u);
    ASSERT_EQ(value.root_children.size(), 2u);
    EXPECT_DOUBLE_EQ(value.root_children[0].value, 1.0);
    EXPECT_DOUBLE_EQ(value.root_children[1].value, 1.0);
    EXPECT_DOUBLE_EQ(value.p_make, 1.0);
    // Presence of two alternatives that don't sum to p_make is the point:
    EXPECT_DOUBLE_EQ(value.root_children[0].value + value.root_children[1].value, 2.0);

    // Both candidates happen to tie at 1.0 here (no-trump, no-ruff, single
    // layout: which suit's trick is won cannot depend on lead order), so
    // the value assertions above cannot by themselves catch p_make being
    // paired with the wrong root_children entry. single_card_declarer_play
    // scans suits ascending, so pi's actual choice is deterministically
    // the spade ace; pin that identity directly.
    EXPECT_EQ(value.root_children[0].card.suit, Spades);
    EXPECT_EQ(value.root_children[0].card.rank, Ace);
}

TEST_F(EvaluateTest, DefenderRootChildrenSumToPMake)
{
    // East leads first (a defender node at the root) and holds a card in
    // each of two suits; delta splits 50/50 over which suit East leads.
    // Everyone else holds one card in each of those two suits too, so
    // whichever suit East leads, the trick is followed correctly and North
    // wins with an ace either way -- and North then wins the other suit's
    // trick too, since it is the only card left. Both children are
    // therefore certain (0.5 each) and sum to p_make = 1.0 -- unlike the
    // declarer case above, this is a genuine partition (defender-node mass
    // conservation).
    Deal root_layout{};
    root_layout.trump = DDS_NOTRUMP;
    root_layout.first = East;
    root_layout.remainCards[North][Diamonds] = be::holding({Ace});
    root_layout.remainCards[North][Clubs] = be::holding({Ace});
    root_layout.remainCards[East][Diamonds] = be::holding({Queen});
    root_layout.remainCards[East][Clubs] = be::holding({Queen});
    root_layout.remainCards[South][Diamonds] = be::holding({Two});
    root_layout.remainCards[South][Clubs] = be::holding({Two});
    root_layout.remainCards[West][Diamonds] = be::holding({Three});
    root_layout.remainCards[West][Clubs] = be::holding({Three});
    be::VectorLayoutSource source({root_layout});

    auto const delta = [](be::DefenderQuery const& query) -> std::vector<be::WeightedCard>
    {
        // East's leading decision: nothing played yet, and East still
        // holds both suits. Every later query (East following the other
        // suit's trick, or West following either trick) has only one
        // legal card and falls through to single_card_defender.
        bool const is_easts_lead = query.seat == East && query.layout.currentTrickRank[0] == 0;
        if (is_easts_lead)
        {
            return {
                be::WeightedCard{be::Card{Diamonds, Queen}, 0.5},
                be::WeightedCard{be::Card{Clubs, Queen}, 0.5},
            };
        }
        return be::single_card_defender(query);
    };

    be::EvaluationResult const result =
        be::evaluate(root_layout, North, /*tricks_needed=*/2, source, strategy(1), delta);

    ASSERT_FALSE(result.error.has_value())
        << "callback=" << static_cast<int>(result.error->callback)
        << " validation=" << static_cast<int>(result.error->validation)
        << " seat=" << result.error->seat;
    be::EvaluationValue const& value = result.by_strategy.at(1u);
    ASSERT_EQ(value.root_children.size(), 2u);
    EXPECT_DOUBLE_EQ(value.root_children[0].value, 0.5);
    EXPECT_DOUBLE_EQ(value.root_children[1].value, 0.5);
    EXPECT_DOUBLE_EQ(value.p_make, 1.0);
    EXPECT_DOUBLE_EQ(
        value.root_children[0].value + value.root_children[1].value, value.p_make);
}

// --- retention is opt-in --------------------------------------------------

TEST_F(EvaluateTest, RetainedRootIsAbsentByDefaultAndPresentWhenRequested)
{
    Deal const root_layout = make_one_trick_certain_win();
    be::VectorLayoutSource source({root_layout});

    be::EvaluationResult const without_retention = be::evaluate(
        root_layout, North, /*tricks_needed=*/1, source, strategy(1), be::single_card_defender);
    ASSERT_FALSE(without_retention.error.has_value());
    EXPECT_FALSE(without_retention.by_strategy.at(1u).retained_root.has_value());

    be::EvaluationResult const with_retention = be::evaluate(
        root_layout,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        be::single_card_defender,
        be::EvaluateOptions{.retain_root = true});
    ASSERT_FALSE(with_retention.error.has_value());
    ASSERT_TRUE(with_retention.by_strategy.at(1u).retained_root.has_value());
    EXPECT_EQ(with_retention.by_strategy.at(1u).retained_root->layouts.size(), 1u);
}

// --- StrategyId mapping ---------------------------------------------------

TEST_F(EvaluateTest, TheResultIsIndexedByTheCallersOwnStrategyId)
{
    Deal const root_layout = make_one_trick_certain_win();
    be::VectorLayoutSource source({root_layout});

    be::EvaluationResult const result = be::evaluate(
        root_layout,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(9999),
        be::single_card_defender);

    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.by_strategy.size(), 1u);
    ASSERT_EQ(result.by_strategy.count(be::StrategyId{9999}), 1u);
    EXPECT_DOUBLE_EQ(result.by_strategy.at(be::StrategyId{9999}).p_make, 1.0);
}
