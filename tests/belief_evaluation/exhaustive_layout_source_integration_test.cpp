#include <gtest/gtest.h>

#include <vector>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/exhaustive_layout_source.hpp>
#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/node.hpp>

#include "test_support.hpp"

// Integration tests: defender_pool_decomposition, the keyed permutation
// and ExhaustiveLayoutSource, run through make_root and evaluate()
// together. Each piece is tested separately in
// defender_split_test.cpp, keyed_permutation_test.cpp and
// exhaustive_layout_source_test.cpp.
namespace be = dds::belief_evaluation;
using be::ExhaustiveLayoutSource;
using be::DeclarerStrategy;
using be::EvaluateOptions;
using be::EvaluationResult;
using be::RootConstructionResult;
using be::ScanOutcome;
using be::evaluate;
using be::holding;
using be::make_root;
using be::single_card_declarer_play;
using be::single_card_defender;

namespace
{
    constexpr int Two = 2;
    constexpr int Three = 3;
    constexpr int Four = 4;
    constexpr int Five = 5;
    constexpr int Six = 6;
    constexpr int Seven = 7;
    constexpr int Eight = 8;
    constexpr int Nine = 9;
    constexpr int Ten = 10;

    constexpr int Spades = 0;

    constexpr int North = 0;  // declarer
    constexpr int East = 1;   // a defender
    constexpr int South = 2;  // dummy
    constexpr int West = 3;   // a defender

    /// North holds one middling card (the nine) and South (dummy) one lower
    /// card (the eight). East and West share a seven-card pool {Two..Seven,
    /// Ten}, and the only freedom is which pool card East holds: the shape
    /// ExhaustiveLayoutSource enumerates, on one suit. East leads its only
    /// card, and West plays its lowest, which is never the ten. Whoever holds
    /// the ten decides the trick, as in reproduction_test.cpp's
    /// make_finesse_style_layout. Here the production source generates every
    /// split instead of a hand-written list. East holds one of the seven cards
    /// (fixed_seat_count = 1), so size() == C(7, 1) == 7. The true answer is
    /// 6/7: every East card but the ten loses to North's nine.
    auto make_one_card_finesse_root() -> Deal
    {
        Deal root{};
        root.trump = DDS_NOTRUMP;
        root.first = East;
        root.remainCards[North][Spades] = holding({Nine});
        root.remainCards[South][Spades] = holding({Eight});
        // East holds one pool card at the root. Which one does not matter:
        // ExhaustiveLayoutSource's space depends only on the pool and the count,
        // so any single card gives the same C(7, 1) = 7 space. West gets the
        // rest.
        root.remainCards[East][Spades] = holding({Two});
        root.remainCards[West][Spades] = holding({Three, Four, Five, Six, Seven, Ten});
        return root;
    }

    /// The same pool, but East holds two of the seven cards (fixed_seat_count
    /// = 2, C(7, 2) = 21 splits). After East's lead (its lower card), several
    /// two-card holdings share the same lead. So depth-1 nodes can hold more
    /// than one sampled layout, and when a sample misses some, a node-local
    /// replenishment scan can find more.
    auto make_two_card_finesse_root() -> Deal
    {
        Deal root{};
        root.trump = DDS_NOTRUMP;
        root.first = East;
        root.remainCards[North][Spades] = holding({Nine});
        root.remainCards[South][Spades] = holding({Eight});
        root.remainCards[East][Spades] = holding({Two, Three});
        root.remainCards[West][Spades] = holding({Four, Five, Six, Seven, Ten});
        return root;
    }
}  // namespace

// --- make_root with no sample_size holds the whole space -------------------

TEST(ExhaustiveLayoutSourceIntegrationTest, MakeRootWithNoSampleSizeHoldsTheWholeSpace)
{
    Deal const root = make_one_card_finesse_root();
    ExhaustiveLayoutSource const source(root, North, /*seed=*/1u);
    ASSERT_EQ(source.size(), 7u);

    RootConstructionResult const result = make_root(root, North, /*tricks_needed=*/1, source);
    ASSERT_TRUE(result.node.has_value());
    EXPECT_EQ(result.node->layouts.size(), 7u);
    EXPECT_FALSE(result.node->is_sample);
    EXPECT_EQ(result.outcome, ScanOutcome::SourceExhausted);
}

// --- make_root's consistency filter drops nothing ---------------------------

TEST(ExhaustiveLayoutSourceIntegrationTest, MakeRootsConsistencyFilterDropsNothing)
{
    // Every candidate ExhaustiveLayoutSource offers is consistent by
    // construction, so make_root must keep exactly source.size(). A drop would
    // mean apply_defender_split produced layouts inconsistent with the root.
    // defender_split_test.cpp already rules that out for this shape; this is
    // the end-to-end version of the same claim.
    Deal const root = make_two_card_finesse_root();
    ExhaustiveLayoutSource const source(root, North, /*seed=*/3u);
    ASSERT_EQ(source.size(), 21u);  // C(7, 2)

    RootConstructionResult const result = make_root(root, North, /*tricks_needed=*/1, source);
    ASSERT_TRUE(result.node.has_value());
    EXPECT_EQ(result.node->layouts.size(), 21u);
}

// --- a hand-derivable p_make -------------------------------------------------

TEST(ExhaustiveLayoutSourceIntegrationTest, ExhaustiveEvaluationMatchesTheHandDerivedAnswer)
{
    Deal const root = make_one_card_finesse_root();
    ExhaustiveLayoutSource const source(root, North, /*seed=*/5u);
    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};

    EvaluationResult const result =
        evaluate(root, North, /*tricks_needed=*/1, source, pi, single_card_defender);
    ASSERT_FALSE(result.error.has_value());
    // Hand-derived: East holds one of {Two, Three, Four, Five, Six, Seven,
    // Ten}. North's nine wins the trick unless East's card beats it. Only the
    // ten does: West's forced lowest card is never the ten. 6 of 7 -> 6/7.
    EXPECT_NEAR(result.by_strategy.at(1u).p_make, 6.0 / 7.0, 1e-9);
}

// --- sample_size >= size() reproduces the exhaustive answer, bitwise -------

TEST(ExhaustiveLayoutSourceIntegrationTest, SampleSizeAtLeastSizeReproducesExhaustiveBitwise)
{
    Deal const root = make_one_card_finesse_root();
    ExhaustiveLayoutSource const source(root, North, /*seed=*/9u);
    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};

    EvaluationResult const exhaustive =
        evaluate(root, North, /*tricks_needed=*/1, source, pi, single_card_defender);
    EvaluationResult const capped = evaluate(
        root, North, /*tricks_needed=*/1, source, pi, single_card_defender,
        EvaluateOptions{.sampling = {.sample_size = 1000u}});  // 1000 >= size() == 7

    ASSERT_FALSE(exhaustive.error.has_value());
    ASSERT_FALSE(capped.error.has_value());
    // Bitwise, not EXPECT_DOUBLE_EQ: with sample_size >= size(), the answer
    // matches the exhaustive one exactly. Other tests check this on small,
    // hand-built sources; this checks it on a larger, real source.
    EXPECT_EQ(exhaustive.by_strategy.at(1u).p_make, capped.by_strategy.at(1u).p_make);
}

// --- seeding changes the sample, not the space ------------------------------

TEST(ExhaustiveLayoutSourceIntegrationTest, TwoSeedsGiveTheSameExhaustiveAnswer)
{
    Deal const root = make_one_card_finesse_root();
    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};

    ExhaustiveLayoutSource const source_a(root, North, /*seed=*/11u);
    ExhaustiveLayoutSource const source_b(root, North, /*seed=*/12u);
    EvaluationResult const a =
        evaluate(root, North, /*tricks_needed=*/1, source_a, pi, single_card_defender);
    EvaluationResult const b =
        evaluate(root, North, /*tricks_needed=*/1, source_b, pi, single_card_defender);

    ASSERT_FALSE(a.error.has_value());
    ASSERT_FALSE(b.error.has_value());
    EXPECT_EQ(a.by_strategy.at(1u).p_make, b.by_strategy.at(1u).p_make);
}

TEST(ExhaustiveLayoutSourceIntegrationTest, TwoSeedsGiveDifferentSampledAnswersAtSmallM)
{
    // Seeds 0 and 1 were picked against this construction. A sample of 6 of
    // the 7 splits excludes the one losing case (the ten to East) under seed 0
    // and includes it under seed 1. So the two seeds land on opposite sides of
    // that boundary: seeding changes which six are sampled, not just the
    // order.
    Deal const root = make_one_card_finesse_root();
    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};

    ExhaustiveLayoutSource const source_a(root, North, /*seed=*/0u);
    ExhaustiveLayoutSource const source_b(root, North, /*seed=*/1u);
    EvaluationResult const a = evaluate(
        root, North, /*tricks_needed=*/1, source_a, pi, single_card_defender,
        EvaluateOptions{.sampling = {.sample_size = 6u}});
    EvaluationResult const b = evaluate(
        root, North, /*tricks_needed=*/1, source_b, pi, single_card_defender,
        EvaluateOptions{.sampling = {.sample_size = 6u}});

    ASSERT_FALSE(a.error.has_value());
    ASSERT_FALSE(b.error.has_value());
    EXPECT_NE(a.by_strategy.at(1u).p_make, b.by_strategy.at(1u).p_make);
    EXPECT_DOUBLE_EQ(a.by_strategy.at(1u).p_make, 1.0);       // seed 0's M=6 sample excludes Ten
    EXPECT_NEAR(b.by_strategy.at(1u).p_make, 5.0 / 6.0, 1e-9);  // seed 1's includes it
}

// --- a sampled, replenishing run works end to end ---------------------------

TEST(ExhaustiveLayoutSourceIntegrationTest, ASampledReplenishingRunAddsAtLeastOneLayout)
{
    // Not a measurement: this asserts that the mechanism runs, and reports no
    // timing. East holds two of the seven pool cards (C(7, 2) = 21 splits). A
    // sample of 10 leaves depth-1 nodes (grouped by East's lead, its lower
    // card) thin enough that at least one falls below replenish_below and
    // finds more.
    Deal const root = make_two_card_finesse_root();
    ExhaustiveLayoutSource const source(root, North, /*seed=*/4u);
    ASSERT_EQ(source.size(), 21u);
    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};

    EvaluationResult const result = evaluate(
        root,
        North,
        /*tricks_needed=*/1,
        source,
        pi,
        single_card_defender,
        EvaluateOptions{.collect_counters = true, .sampling = {.sample_size = 10u, .replenish_below = 5u}});

    ASSERT_FALSE(result.error.has_value());
    ASSERT_TRUE(result.by_strategy.at(1u).counters.has_value());
    std::uint64_t total_attempted = 0;
    std::uint64_t total_succeeded = 0;
    std::uint64_t total_layouts_added = 0;
    for (be::DepthReplenishmentStats const& stats : result.by_strategy.at(1u).counters->replenishment_by_depth)
    {
        total_attempted += stats.attempted;
        total_succeeded += stats.succeeded;
        total_layouts_added += stats.layouts_added;
    }
    EXPECT_GT(total_attempted, 0u);
    EXPECT_GT(total_succeeded, 0u);
    EXPECT_GT(total_layouts_added, 0u);
}
