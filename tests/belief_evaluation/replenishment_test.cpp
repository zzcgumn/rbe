#include <gtest/gtest.h>

#include <cmath>
#include <optional>
#include <vector>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/belief_view.hpp>
#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/play_record.hpp>
#include <belief_evaluation/replenishment.hpp>
#include <belief_evaluation/trick.hpp>

#include "test_support.hpp"

// Node-local replenishment: the end-to-end p_make, the rescale and mass
// conservation, exhaustion, and the scan that tops up a node.

namespace be = dds::belief_evaluation;
using be::BeliefNode;
using be::CountingLayoutSource;
using be::DeclarerStrategy;
using be::DefenderQuery;
using be::EvaluateOptions;
using be::EvaluationResult;
using be::HistoryVerdict;
using be::KahanAccumulator;
using be::ObservationState;
using be::PlayRecord;
using be::Probability;
using be::ScanResult;
using be::VectorLayoutSource;
using be::WeightedCard;
using be::evaluate;
using be::holding;
using be::layout_key;
using be::node_mass;
using be::root_observation_state;
using be::scan_for_replenishment;
using be::single_card_declarer_play;
using be::single_card_defender;
using be::tier2_dead;

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
    constexpr int Ace = 14;

    constexpr int Spades = 0;
    constexpr int Hearts = 1;

    constexpr int North = 0;  // declarer
    constexpr int East = 1;   // a defender
    constexpr int South = 2;  // dummy
    constexpr int West = 3;   // a defender

    /// A two-trick ending. North's ace always wins trick 1 (spades), whichever
    /// of two spade fillers ({three, five}, split between East and West) the
    /// layout carries. That harmless split lowers a node's layout count
    /// without deciding anything. Trick 2 (hearts) decides: North's nine beats
    /// every east_heart except the ten, as in reproduction_test.cpp's finesse
    /// fixture. Here it comes one ply later, at a node p_make() visits rather
    /// than in evaluate()'s root handling.
    auto make_layout(int east_spade, int east_heart) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = North;
        deal.remainCards[North][Spades] = holding({Ace});
        deal.remainCards[South][Spades] = holding({Two});
        deal.remainCards[East][Spades] = holding({east_spade});
        deal.remainCards[West][Spades] = holding({east_spade == Three ? Five : Three});
        deal.remainCards[North][Hearts] = holding({Nine});
        deal.remainCards[South][Hearts] = holding({Eight});
        deal.remainCards[East][Hearts] = holding({east_heart});
        unsigned west_heart_mask = 0;
        for (int rank : {Two, Three, Four, Five, Six, Seven, Ten})
        {
            if (rank != east_heart)
            {
                west_heart_mask |= holding({rank});
            }
        }
        deal.remainCards[West][Hearts] = west_heart_mask;
        return deal;
    }
}

class ReplenishmentTest : public ::testing::Test
{
};

// ===========================================================================
// End to end: p_make moves from a false certainty of 1.0 (no
// replenishment) to the true 3/4 (with it).
// ===========================================================================

TEST_F(ReplenishmentTest, PMakeMovesFromFalseCertaintyToTheHandDerivedTrueValue)
{
    // Eight layouts: three winning hearts under each spade filler (six), then
    // the losing heart (ten) under each filler, last in source order. So
    // sample_size = 6 draws the six winners and misses both Ten layouts.
    std::vector<Deal> layouts{
        make_layout(Three, Two),
        make_layout(Three, Three),
        make_layout(Three, Four),
        make_layout(Five, Five),
        make_layout(Five, Six),
        make_layout(Five, Seven),
        make_layout(Three, Ten),
        make_layout(Five, Ten),
    };
    be::assert_pool_matches(layouts);
    be::assert_forms_one_belief_node(layouts, North);
    VectorLayoutSource const source(layouts);
    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};

    // The hand-derived answer over all 8 layouts: 6 win (every heart but the
    // two Tens) and 2 lose, so 6/8 = 3/4.
    EvaluationResult const exhaustive =
        evaluate(layouts.front(), North, /*tricks_needed=*/2, source, pi, single_card_defender);
    ASSERT_FALSE(exhaustive.error.has_value());
    EXPECT_NEAR(exhaustive.by_strategy.at(1u).p_make, 0.75, 1e-9);

    // Without replenishment: sample_size = 6 draws only the winners, so both
    // spade branches see only wins. A false certainty, as in the M = 6 result
    // of reproduction_test.cpp.
    EvaluationResult const no_replenish = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/2,
        source,
        pi,
        single_card_defender,
        EvaluateOptions{.sampling = {.sample_size = 6u}});
    ASSERT_FALSE(no_replenish.error.has_value());
    EXPECT_DOUBLE_EQ(no_replenish.by_strategy.at(1u).p_make, 1.0);

    // With replenishment: East's spade ply splits the 6 sampled layouts into
    // two branches of 3, each below replenish_below = 6, so each is topped up. Each
    // finds its Ten layout (the only new candidate matching its spade ply) and
    // adds it, giving exactly 3/4.
    EvaluationResult const replenished = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/2,
        source,
        pi,
        single_card_defender,
        EvaluateOptions{.sampling = {.sample_size = 6u, .replenish_below = 6u}});
    ASSERT_FALSE(replenished.error.has_value());
    EXPECT_NEAR(replenished.by_strategy.at(1u).p_make, 0.75, 1e-9);
}

// ===========================================================================
// A supplied EvaluateOptions::play_record must reach delta's DefenderQuery
// unchanged during a replenishment scan too. replay_candidate()
// (replenishment.cpp) rebuilds its ObservationState with
// root_observation_state() instead of reusing the node's, so that rebuild
// must carry the record, as every other root_observation_state() call
// does.
// ===========================================================================

TEST_F(ReplenishmentTest, PlayRecordReachesADefenderQueryMadeDuringAReplenishmentScan)
{
    // The eight-layout fixture and options of
    // PMakeMovesFromFalseCertaintyToTheHandDerivedTrueValue, which shows that
    // they trigger scan_for_replenishment (sample_size = 6,
    // replenish_below = 6). The property here is that the record reaches this
    // defender query.
    std::vector<Deal> const layouts{
        make_layout(Three, Two),
        make_layout(Three, Three),
        make_layout(Three, Four),
        make_layout(Five, Five),
        make_layout(Five, Six),
        make_layout(Five, Seven),
        make_layout(Three, Ten),
        make_layout(Five, Ten),
    };
    be::assert_pool_matches(layouts);
    be::assert_forms_one_belief_node(layouts, North);
    VectorLayoutSource const source(layouts);
    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};

    PlayTraceBin cards{};
    cards.number = 1;
    cards.suit[0] = Spades;
    cards.rank[0] = Two;
    auto [record, verdict] = PlayRecord::create(cards, /*opening_leader=*/West);
    ASSERT_EQ(verdict, HistoryVerdict::Consistent);

    std::vector<ObservationState> defender_states;
    be::DefenderStrategy const recording_defender =
        [&defender_states](DefenderQuery const& query) -> std::vector<WeightedCard>
    {
        defender_states.push_back(query.state);
        return single_card_defender(query);
    };

    EvaluateOptions options{.sampling = {.sample_size = 6u, .replenish_below = 6u}};
    options.play_record = record;

    EvaluationResult const result =
        evaluate(layouts.front(), North, /*tricks_needed=*/2, source, pi, recording_defender, options);

    ASSERT_FALSE(result.error.has_value());
    ASSERT_FALSE(defender_states.empty());
    for (ObservationState const& state : defender_states)
    {
        ASSERT_NE(state.play_record, nullptr);
        EXPECT_EQ(state.play_record->cards().number, record->cards().number);
        EXPECT_EQ(state.play_record->opening_leader(), record->opening_leader());
    }
}

// ===========================================================================
// The rescale checked directly, independent of p_make: a hand-built node
// shaped like one depth-2 spade branch of the fixture, replenished with the
// public scan_for_replenishment and the rescale documented on
// EvaluateOptions::replenish_below (kappa' = kappa * E / E').
// ===========================================================================

TEST_F(ReplenishmentTest, ReplenishingByHandGrowsTheNodeAndConservesMassToADerivedTolerance)
{
    // The "spade = three" branch right after East's spade ply: three layouts
    // (hearts two, three and four, all winning), kappa = 1/6 from a six-layout
    // root, and p_i = 1 each (no defender ply has multiplied anything yet).
    Deal const root_layout = make_layout(Three, Two);
    be::Card const norths_ace{Spades, Ace};
    be::Card const easts_spade{Spades, Three};

    ObservationState state = root_observation_state(root_layout, North, /*tricks_needed=*/2);
    state = be::advance_state(state, norths_ace);
    state = be::advance_state(state, easts_spade);

    BeliefNode node{};
    node.state = state;
    for (int heart : {Two, Three, Four})
    {
        Deal layout = play(make_layout(Three, heart), norths_ace);
        layout = play(layout, easts_spade);
        node.layouts.push_back(layout);
        node.p.push_back(1.0);
        node.root_keys.push_back(layout_key(make_layout(Three, heart), East));
    }
    node.kappa = 1.0 / 6.0;
    double const mass_before = node_mass(node);
    EXPECT_DOUBLE_EQ(mass_before, 0.5);  // (1/6) * 3

    // The full 8-layout source, so the scan can find this branch's missing Ten
    // layout.
    std::vector<Deal> const source_layouts{
        make_layout(Three, Two),
        make_layout(Three, Three),
        make_layout(Three, Four),
        make_layout(Five, Five),
        make_layout(Five, Six),
        make_layout(Five, Seven),
        make_layout(Three, Ten),
        make_layout(Five, Ten),
    };
    VectorLayoutSource const source(source_layouts);

    ScanResult const scan =
        scan_for_replenishment(node, root_layout, source, single_card_defender, /*wanted=*/3, std::nullopt);
    ASSERT_EQ(scan.error, be::ValidationError::None);
    ASSERT_EQ(scan.candidates.size(), 1u);  // only "three, ten" matches this branch's spade ply
    // Hand-derived: the one defender ply so far (East's spade) is forced and
    // the same for every layout in this branch, including the Ten. delta's
    // reply is certain, so it multiplies p_j by exactly 1.
    EXPECT_DOUBLE_EQ(scan.candidates[0].p_j, 1.0);

    // The rescale as documented on EvaluateOptions::replenish_below:
    // kappa' = kappa * E / E'.
    KahanAccumulator mass_before_sum;
    for (Probability const p_i : node.p)
    {
        mass_before_sum.add(p_i);
    }
    BeliefNode replenished = node;
    for (auto const& candidate : scan.candidates)
    {
        replenished.layouts.push_back(candidate.layout);
        replenished.p.push_back(candidate.p_j);
        replenished.root_keys.push_back(candidate.root_key);
    }
    KahanAccumulator mass_after_sum;
    for (Probability const p_i : replenished.p)
    {
        mass_after_sum.add(p_i);
    }
    replenished.kappa = node.kappa * mass_before_sum.value() / mass_after_sum.value();

    EXPECT_EQ(replenished.layouts.size(), 4u);   // grew, from the pre-replenishment 3
    EXPECT_EQ(replenished.root_keys.size(), 4u);
    EXPECT_DOUBLE_EQ(replenished.kappa, (1.0 / 6.0) * (3.0 / 4.0));  // 1/8, hand-derived

    // Mass conservation, to a tolerance derived by hand, not tuned. The
    // computation is a few Kahan-summed additions (a couple of ULPs each) and
    // one divide-then-multiply, whose relative error is of order machine
    // epsilon (~2e-16) on a magnitude at most 1. That is far below the 1e-12
    // asserted here.
    double const mass_after = node_mass(replenished);
    EXPECT_NEAR(mass_after, mass_before, 1e-12);
}

// ===========================================================================
// With replenish_below absent, this fixture's evaluation does not change.
// Checked directly here.
// ===========================================================================

TEST_F(ReplenishmentTest, AbsentReplenishBelowLeavesTheFixtureBitIdenticalToASampleSizeOnlyRun)
{
    std::vector<Deal> const layouts{
        make_layout(Three, Two),
        make_layout(Three, Three),
        make_layout(Three, Four),
        make_layout(Five, Five),
        make_layout(Five, Six),
        make_layout(Five, Seven),
        make_layout(Three, Ten),
        make_layout(Five, Ten),
    };
    VectorLayoutSource const source(layouts);
    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};

    EvaluationResult const first = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/2,
        source,
        pi,
        single_card_defender,
        EvaluateOptions{.sampling = {.sample_size = 6u}});
    EvaluationResult const second = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/2,
        source,
        pi,
        single_card_defender,
        EvaluateOptions{.sampling = {.sample_size = 6u}});  // replenish_below absent, as in the first run

    ASSERT_FALSE(first.error.has_value());
    ASSERT_FALSE(second.error.has_value());
    EXPECT_EQ(first.by_strategy.at(1u).p_make, second.by_strategy.at(1u).p_make);
}

// ===========================================================================
// Exhaustion at depth: one node whose scan exhausts the source, and a
// sibling whose scan does not. Both descend from a sampled root and are
// built by hand, like the node in the rescale test above, so tier2_dead()
// can be checked directly.
// ===========================================================================

TEST_F(ReplenishmentTest, ExhaustedNodeIsNoLongerASampleAndItsSiblingStays)
{
    // Five layouts: two with spade = three (hearts two, three), two with
    // spade = five (hearts five, six), and one more spade = three (heart four)
    // last in source order. sample_size = 3 draws the first three (spade =
    // three twice, spade = five once). East's spade ply splits them into a
    // 2-layout "three" branch and a 1-layout "five" branch.
    std::vector<Deal> const source_layouts{
        make_layout(Three, Two),
        make_layout(Three, Three),
        make_layout(Five, Five),
        make_layout(Three, Four),
        make_layout(Five, Six),
    };
    be::assert_pool_matches(source_layouts);
    VectorLayoutSource const source(source_layouts);
    Deal const root_layout = source_layouts.front();

    // The root, sampled: is_sample true, three layouts drawn.
    be::RootConstructionResult const root_result =
        be::make_root(root_layout, North, /*tricks_needed=*/2, source, be::RootOptions{.sample_size = 3u});
    ASSERT_TRUE(root_result.node.has_value());
    ASSERT_TRUE(root_result.node->is_sample);
    ASSERT_EQ(root_result.node->layouts.size(), 3u);

    be::Card const norths_ace{Spades, Ace};

    auto const build_branch = [&](int east_spade, std::vector<int> const& drawn_hearts) -> BeliefNode
    {
        BeliefNode branch{};
        branch.state = root_result.node->state;
        branch.state = be::advance_state(branch.state, norths_ace);
        be::Card const easts_spade{Spades, east_spade};
        branch.state = be::advance_state(branch.state, easts_spade);
        branch.is_sample = true;  // inherited from the sampled root through both plies
        for (int heart : drawn_hearts)
        {
            Deal layout = play(make_layout(east_spade, heart), norths_ace);
            layout = play(layout, easts_spade);
            branch.layouts.push_back(layout);
            branch.p.push_back(1.0);
            branch.root_keys.push_back(layout_key(make_layout(east_spade, heart), East));
        }
        branch.kappa = 1.0 / 3.0;
        return branch;
    };

    BeliefNode three_branch = build_branch(Three, {Two, Three});
    BeliefNode five_branch = build_branch(Five, {Five});

    // Replenish both by hand, as replenish_node() does: scan, rescale if
    // anything was found, and clear is_sample on SourceExhausted.
    auto const replenish = [&](BeliefNode& branch, std::uint64_t wanted) -> ScanResult
    {
        ScanResult const scan =
            scan_for_replenishment(branch, root_layout, source, single_card_defender, wanted, std::nullopt);
        EXPECT_EQ(scan.error, be::ValidationError::None);
        bool const exhausted = scan.outcome == be::ScanOutcome::SourceExhausted;
        if (! scan.candidates.empty())
        {
            KahanAccumulator before;
            for (Probability const p_i : branch.p)
            {
                before.add(p_i);
            }
            for (auto const& candidate : scan.candidates)
            {
                branch.layouts.push_back(candidate.layout);
                branch.p.push_back(candidate.p_j);
                branch.root_keys.push_back(candidate.root_key);
            }
            KahanAccumulator after;
            for (Probability const p_i : branch.p)
            {
                after.add(p_i);
            }
            branch.kappa *= before.value() / after.value();
        }
        if (exhausted)
        {
            branch.is_sample = false;
        }
        return scan;
    };

    // three_branch wants 1 (3 - 2) and finds it (spade three, heart four)
    // before the source ends: SampleFilled, not exhausted.
    ScanResult const three_scan = replenish(three_branch, /*wanted=*/1);
    EXPECT_EQ(three_scan.outcome, be::ScanOutcome::SampleFilled);
    EXPECT_EQ(three_branch.layouts.size(), 3u);
    EXPECT_TRUE(three_branch.is_sample);

    // five_branch wants 2 (3 - 1), but the source has only one more spade =
    // five layout. It finds that one and then runs out: SourceExhausted,
    // having found something.
    ScanResult const five_scan = replenish(five_branch, /*wanted=*/2);
    EXPECT_EQ(five_scan.outcome, be::ScanOutcome::SourceExhausted);
    EXPECT_EQ(five_branch.layouts.size(), 2u);
    EXPECT_FALSE(five_branch.is_sample);

    // Neither child's scan touches the root.
    EXPECT_TRUE(root_result.node->is_sample);

    // The same dead bound and declaration: one node fires and the other does
    // not, and they differ only in is_sample.
    auto const always_dead = [](Deal const&) -> int { return 0; };
    be::EvaluateOptions const options{.bound = always_dead, .delta_is_double_dummy_optimal = true};

    EXPECT_TRUE(tier2_dead(five_branch, options));    // exhausted: the gate fires
    EXPECT_FALSE(tier2_dead(three_branch, options));  // still a sample: the gate stays off

    // space_size: make_belief_view needs no change. The exhausted node reports
    // its true size, and the sampled sibling reports 0.
    std::vector<be::BeliefEntry> scratch_five;
    be::BeliefView const five_view = be::make_belief_view(five_branch, scratch_five);
    EXPECT_EQ(five_view.space_size, five_branch.layouts.size());

    std::vector<be::BeliefEntry> scratch_three;
    be::BeliefView const three_view = be::make_belief_view(three_branch, scratch_three);
    EXPECT_EQ(three_view.space_size, 0u);
}

// ===========================================================================
// The no-more-available flag: once a node's scan exhausts the source, its
// descendants skip their scans instead of repeating a full scan that can
// only find nothing.
// ===========================================================================

TEST_F(ReplenishmentTest, ADescendantOfANodeThatCannotBeToppedUpMakesNoFurtherAtCalls)
{
    // One forced trick. East's spade ply (depth 1) splits the root's two drawn
    // layouts (one per spade filler) into two one-layout branches. Only a split
    // gives replenishment work, since a declarer ply never changes a node's
    // layout count. A third source entry (spade three again, another heart
    // filler) gives the "three" branch something to find; the "five" branch
    // has nothing waiting. North holds only the spade ace, unlike make_layout's
    // two-trick shape, so evaluate()'s root has no other first card to explore
    // and the fixture stays on the one line being counted.
    auto const make_fixture = [](int east_spade, int east_heart_filler) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = North;
        deal.remainCards[North][Spades] = holding({Ace});
        deal.remainCards[South][Spades] = holding({Two});
        deal.remainCards[East][Spades] = holding({east_spade});
        deal.remainCards[West][Spades] = holding({east_spade == Three ? Five : Three});
        // Never played in this one-trick fixture: it only tells root layouts
        // apart, as the spade split does. It uses the same fixed two-card pool
        // split as the spades.
        deal.remainCards[East][Hearts] = holding({east_heart_filler});
        deal.remainCards[West][Hearts] = holding({east_heart_filler == Two ? Three : Two});
        return deal;
    };
    std::vector<Deal> const source_layouts{
        make_fixture(Three, Two), make_fixture(Five, Two), make_fixture(Three, Three)};
    be::assert_pool_matches(source_layouts);
    VectorLayoutSource const underlying(source_layouts);
    CountingLayoutSource source(underlying);
    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};

    // sample_size = 2 draws one layout of each spade filler at the root
    // (source order: three, five). replenish_below = 2: after the split,
    // each one-layout branch is below it and triggers at depth 2.
    EvaluationResult const result = evaluate(
        source_layouts.front(),
        North,
        /*tricks_needed=*/1,
        source,
        pi,
        single_card_defender,
        EvaluateOptions{.sampling = {.sample_size = 2u, .replenish_below = 2u}});
    ASSERT_FALSE(result.error.has_value());

    // The answer is fixed however many redundant scans ran: North's ace is
    // forced and unbeatable, so the contract always makes.
    EXPECT_DOUBLE_EQ(result.by_strategy.at(1u).p_make, 1.0);

    // Hand-derived: the root scan stops after 2 of the 3 entries (2 at()
    // calls). At depth 2 the "three" branch scans all 3 entries (3 at() calls)
    // to find its new layout, exhausting the source. The "five" branch also
    // scans all 3 (3 at() calls), finds nothing and exhausts. 2 + 3 + 3 = 8.
    // Every node below on either branch is still below replenish_below (the
    // "five" branch stays at 1). Without the flag, each would scan again for
    // 3 at() calls: the three such nodes on the "five" branch alone (South's
    // play, West's play, the already_made() terminal) would add 9, for 17.
    EXPECT_EQ(source.at_calls(), 8u);
}
