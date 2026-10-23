#include <gtest/gtest.h>

#include <vector>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/play_record.hpp>
#include <belief_evaluation/validation.hpp>

#include "test_support.hpp"

// Tests for evaluate(): termination, error reporting, root-child values,
// result options and the play_record option.

namespace be = dds::belief_evaluation;

// Most tests in this file qualify belief_evaluation symbols with `be::`.
// The SamplingOptions and play_record tests below use this list instead.
using be::EvaluateOptions;
using be::EvaluationResult;
using be::HistoryVerdict;
using be::PlayRecord;
using be::VectorLayoutSource;
using be::evaluate;
using be::single_card_defender;

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

    /// One full trick declarer is certain to win. North leads the ace of
    /// spades, and East, South and West each hold one lower spade and must
    /// follow, so the ace wins whatever anyone plays.
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

// --- certainty and impossibility: the recursion terminates and composes.
// The hand-derived end-to-end cases are in oracle_test.cpp.

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

    // The ending has only one trick, so needing two is impossible.
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
    // root_failure tells two RootConstruction errors apart: an unenumerable
    // source (the test above) and a source with nothing consistent with
    // root_layout (this test).
    Deal const root_layout = make_one_trick_certain_win();
    Deal inconsistent_layout = root_layout;
    // Declarer's holding must match root_layout's exactly to survive
    // make_root's filter. This one does not.
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
    // North holds two certain winners in different suits. East, South and
    // West each hold one lower card per suit, so either lead wins its trick
    // and the other suit's trick follows. Both first cards are winners on
    // their own, so root_children must NOT sum to p_make (1.0 + 1.0 != 1.0).
    // p_make must equal the one pi chose: single_card_declarer_play scans
    // suits in order, so pi leads the spade ace.
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
    // Two alternatives that do not sum to p_make are the point:
    EXPECT_DOUBLE_EQ(value.root_children[0].value + value.root_children[1].value, 2.0);

    // Both candidates tie at 1.0 here (notrump, no ruffs, one layout), so the
    // value assertions above cannot catch p_make paired with the wrong
    // root_children entry. single_card_declarer_play scans suits ascending,
    // so pi always picks the spade ace. Pin that directly.
    EXPECT_EQ(value.root_children[0].card.suit, Spades);
    EXPECT_EQ(value.root_children[0].card.rank, Ace);

    // The kind of root is reported, not left to the consumer to derive. The
    // flag is useful only if it predicts the property above, so it is asserted
    // beside it.
    EXPECT_TRUE(value.root_is_declaring_side);
}

TEST_F(EvaluateTest, RootIsDeclaringSideIsTrueWithDummyOnLeadNotOnlyDeclarer)
{
    // The reason this is a flag and not `root.first == declarer`. South is
    // dummy to North's declarer, and a root led by dummy is a declarer root:
    // pi chooses the card, the children are alternatives, and they do not sum.
    // A consumer testing `first == declarer` gets this position backwards.
    Deal root_layout{};
    root_layout.trump = DDS_NOTRUMP;
    root_layout.first = South;  // dummy, with North as declarer
    root_layout.remainCards[South][Spades] = be::holding({Ace});
    root_layout.remainCards[South][Hearts] = be::holding({Ace});
    root_layout.remainCards[West][Spades] = be::holding({Two});
    root_layout.remainCards[West][Hearts] = be::holding({Two});
    root_layout.remainCards[North][Spades] = be::holding({Three});
    root_layout.remainCards[North][Hearts] = be::holding({Three});
    root_layout.remainCards[East][Spades] = be::holding({Jack});
    root_layout.remainCards[East][Hearts] = be::holding({Jack});
    be::VectorLayoutSource source({root_layout});

    be::EvaluationResult const result = be::evaluate(
        root_layout, North, /*tricks_needed=*/2, source, strategy(1), be::single_card_defender);

    ASSERT_FALSE(result.error.has_value());
    be::EvaluationValue const& value = result.by_strategy.at(1u);
    EXPECT_TRUE(value.root_is_declaring_side);
    // The property the flag predicts: alternatives, so the sum exceeds
    // p_make.
    ASSERT_EQ(value.root_children.size(), 2u);
    EXPECT_DOUBLE_EQ(value.root_children[0].value + value.root_children[1].value,
                     2.0 * value.p_make);
}

TEST_F(EvaluateTest, DefenderRootChildrenSumToPMake)
{
    // East leads (a defender node at the root), holding a card in each of two
    // suits, and delta splits 50/50 over which suit East leads. Everyone else
    // holds one card in each of those suits too. Whichever suit East leads,
    // North wins with the ace and then wins the other suit with its last card.
    // Both children are certain (0.5 each) and sum to p_make = 1.0. Unlike the
    // declarer case above, this is a true partition (defender-node mass
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
        // East's lead: nothing played yet, and East holds both suits. Every
        // later query (East or West following to a trick) has one legal card and
        // falls through to single_card_defender.
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

    // The flag's other value, beside the property it predicts here.
    EXPECT_FALSE(value.root_is_declaring_side);
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

// --- the sampling trio is a nested type -----------------------------------

TEST_F(EvaluateTest, SampleSizeScanBudgetAndReplenishBelowLiveOnANestedSamplingType)
{
    Deal const root_layout = make_one_trick_certain_win();
    VectorLayoutSource source({root_layout});

    EvaluateOptions const options{
        .sampling = {.sample_size = 1u, .scan_budget = 5u, .replenish_below = 1u}};
    EXPECT_EQ(options.sampling.sample_size, 1u);
    EXPECT_EQ(options.sampling.scan_budget, 5u);
    EXPECT_EQ(options.sampling.replenish_below, 1u);

    EvaluationResult const result = evaluate(
        root_layout, North, /*tricks_needed=*/1, source, strategy(1), single_card_defender, options);
    ASSERT_FALSE(result.error.has_value());
    EXPECT_DOUBLE_EQ(result.by_strategy.at(1u).p_make, 1.0);
}

TEST_F(EvaluateTest, ADefaultConstructedSamplingOptionsMeansExactlyWhatAbsentFieldsMeantBefore)
{
    EvaluateOptions const options{};
    EXPECT_FALSE(options.sampling.sample_size.has_value());
    EXPECT_FALSE(options.sampling.scan_budget.has_value());
    EXPECT_FALSE(options.sampling.replenish_below.has_value());
}

// --- EvaluateOptions::play_record: reaches every node unchanged, disturbs
// nothing else -----------------------------------------------------------

TEST_F(EvaluateTest, SuppliedPlayRecordDoesNotDisturbHistory)
{
    Deal const root_layout = make_one_trick_certain_win();  // no trick in progress
    VectorLayoutSource source({root_layout});

    PlayTraceBin cards{};
    cards.number = 1;
    cards.suit[0] = Diamonds;
    cards.rank[0] = 2;
    auto [record, verdict] = PlayRecord::create(cards, /*opening_leader=*/West);
    ASSERT_EQ(verdict, HistoryVerdict::Consistent);

    // Not RecordingDeclarerStrategy: its one scripted card is North's only
    // legal card, but pi is also asked for South (dummy). For South the card
    // is illegal (already played, and not South's). single_card_declarer_play
    // picks the lowest legal card for whichever seat is asked.
    std::vector<be::ObservationState> pi_states;
    be::DeclarerStrategy const pi{
        .id = 0,
        .play =
            [&pi_states](be::ObservationState const& state, be::BeliefView const&) -> be::Card
        {
            pi_states.push_back(state);
            return be::lowest_legal_card(state.known_holdings, be::seat_on_play(state.known_holdings));
        },
        .state_key = nullptr,
    };
    EvaluateOptions options{};
    options.play_record = record;

    EvaluationResult const result =
        evaluate(root_layout, North, /*tricks_needed=*/1, source, pi, single_card_defender, options);

    ASSERT_FALSE(result.error.has_value());
    ASSERT_FALSE(pi_states.empty());
    EXPECT_EQ(pi_states.front().history.number, 0);
}

TEST_F(EvaluateTest, SuppliedPlayRecordReachesTheRootStateUnchanged)
{
    Deal const root_layout = make_one_trick_certain_win();
    VectorLayoutSource source({root_layout});

    PlayTraceBin cards{};
    cards.number = 1;
    cards.suit[0] = Diamonds;
    cards.rank[0] = 2;
    auto [record, verdict] = PlayRecord::create(cards, /*opening_leader=*/West);
    ASSERT_EQ(verdict, HistoryVerdict::Consistent);

    std::vector<be::ObservationState> pi_states;
    be::DeclarerStrategy const pi{
        .id = 0,
        .play =
            [&pi_states](be::ObservationState const& state, be::BeliefView const&) -> be::Card
        {
            pi_states.push_back(state);
            return be::lowest_legal_card(state.known_holdings, be::seat_on_play(state.known_holdings));
        },
        .state_key = nullptr,
    };
    EvaluateOptions options{};
    options.play_record = record;

    EvaluationResult const result =
        evaluate(root_layout, North, /*tricks_needed=*/1, source, pi, single_card_defender, options);

    ASSERT_FALSE(result.error.has_value());
    ASSERT_FALSE(pi_states.empty());
    for (auto const& state : pi_states)
    {
        ASSERT_NE(state.play_record, nullptr);
        EXPECT_EQ(state.play_record->cards().number, record->cards().number);
    }
}

TEST_F(EvaluateTest, ASampledRootWithAPlayRecordSupplied)
{
    // Sampling and play_record are independent inputs. The source is one
    // layout, so sample_size=1 narrows nothing. This is the same light shape
    // as SampleSizeScanBudgetAndReplenishBelowLiveOnANestedSamplingType above:
    // it shows no crash and no interaction, not a claim about sampling.
    Deal const root_layout = make_one_trick_certain_win();
    VectorLayoutSource source({root_layout});

    PlayTraceBin cards{};
    cards.number = 1;
    cards.suit[0] = Diamonds;
    cards.rank[0] = 2;
    auto [record, verdict] = PlayRecord::create(cards, /*opening_leader=*/West);
    ASSERT_EQ(verdict, HistoryVerdict::Consistent);

    EvaluateOptions options{};
    options.play_record = record;
    options.sampling.sample_size = 1u;

    EvaluationResult const result =
        evaluate(root_layout, North, /*tricks_needed=*/1, source, strategy(1), single_card_defender, options);

    ASSERT_FALSE(result.error.has_value());
    EXPECT_DOUBLE_EQ(result.by_strategy.at(1u).p_make, 1.0);
}

TEST_F(EvaluateTest, RetainedRootsPlayRecordIsClearedEvenWhenOneWasSupplied)
{
    // retained_root lives on inside the returned EvaluationResult, but
    // options (and the PlayRecord inside it) may not. An ObservationState used
    // only during the call has no such problem. So evaluate() clears the copy
    // embedded here instead of returning a pointer that dangles once the
    // caller's options goes out of scope (for example, a local EvaluateOptions
    // built in a helper function).
    Deal const root_layout = make_one_trick_certain_win();
    VectorLayoutSource source({root_layout});

    PlayTraceBin cards{};
    cards.number = 1;
    cards.suit[0] = Diamonds;
    cards.rank[0] = 2;
    auto [record, verdict] = PlayRecord::create(cards, /*opening_leader=*/West);
    ASSERT_EQ(verdict, HistoryVerdict::Consistent);

    EvaluateOptions options{.retain_root = true};
    options.play_record = record;

    EvaluationResult const result =
        evaluate(root_layout, North, /*tricks_needed=*/1, source, strategy(1), single_card_defender, options);

    ASSERT_FALSE(result.error.has_value());
    ASSERT_TRUE(result.by_strategy.at(1u).retained_root.has_value());
    EXPECT_EQ(result.by_strategy.at(1u).retained_root->state.play_record, nullptr);
}
