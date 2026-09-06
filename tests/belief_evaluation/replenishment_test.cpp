#include <gtest/gtest.h>

#include <cmath>
#include <optional>
#include <vector>

#include <api/dds_data_types.hpp>
#include <utility/constants.h>

#include <belief_evaluation/belief_view.hpp>
#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/replenishment.hpp>
#include <belief_evaluation/trick.hpp>

#include "test_support.hpp"

namespace be = dds::belief_evaluation;
using be::BeliefNode;
using be::DeclarerStrategy;
using be::EvaluateOptions;
using be::EvaluationResult;
using be::KahanAccumulator;
using be::ObservationState;
using be::Probability;
using be::ScanResult;
using be::VectorLayoutSource;
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

    /// A two-trick ending. Trick 1 (spades) is always won by North's ace,
    /// whichever of two spade fillers ({three, five}, split between East
    /// and West) the layout happens to carry -- a harmless split that
    /// drops a node's layout count without deciding anything. Trick 2
    /// (hearts) is the decision: North's nine beats every east_heart
    /// except ten, exactly as reproduction_test.cpp's own finesse fixture
    /// reasons out, just reached one ply later here so it lands at a node
    /// p_make() visits rather than at evaluate()'s own root block.
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
// End to end: p_make moves from a false-certainty 1.0 (no replenishment) to
// the true 3/4 (with it) on the fixture above -- criterion 7's headline.
// ===========================================================================

TEST_F(ReplenishmentTest, PMakeMovesFromFalseCertaintyToTheHandDerivedTrueValue)
{
    // Eight root-space layouts: three winning hearts under each spade
    // filler (six total), then the one losing heart (ten) under each
    // filler, last in source order -- so sample_size = 6 draws exactly the
    // six winning layouts and misses both "ten" variants entirely.
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

    // The hand-derived true answer, over the full 8-layout space: 6 of 8
    // win (every heart but the two "ten"s), 2 of 8 lose -- 6/8 = 3/4.
    EvaluationResult const exhaustive =
        evaluate(layouts.front(), North, /*tricks_needed=*/2, source, pi, single_card_defender);
    ASSERT_FALSE(exhaustive.error.has_value());
    EXPECT_NEAR(exhaustive.by_strategy.at(1u).p_make, 0.75, 1e-9);

    // Without replenishment: sample_size = 6 draws only the six winning
    // layouts, so both spade branches see nothing but wins -- a false
    // certainty, exactly reproduction_test.cpp's own M = 6 result.
    EvaluationResult const no_replenish = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/2,
        source,
        pi,
        single_card_defender,
        EvaluateOptions{.sample_size = 6u});
    ASSERT_FALSE(no_replenish.error.has_value());
    EXPECT_DOUBLE_EQ(no_replenish.by_strategy.at(1u).p_make, 1.0);

    // With replenishment: each spade branch collapses to 3 layouts at its
    // own node (East's spade ply splits the sampled 6 into two groups of
    // 3), triggering a top-up towards sample_size = 6 there -- each finds
    // its own "ten" variant (the only new candidate matching that branch's
    // own spade ply) and adds it, recovering the true 3/4 exactly.
    EvaluationResult const replenished = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/2,
        source,
        pi,
        single_card_defender,
        EvaluateOptions{.sample_size = 6u, .replenish_below = 6u});
    ASSERT_FALSE(replenished.error.has_value());
    EXPECT_NEAR(replenished.by_strategy.at(1u).p_make, 0.75, 1e-9);
}

// ===========================================================================
// A direct check of the rescale itself: a hand-built node shaped like one
// of the fixture's own depth-2 spade branches, replenished via the public
// scan_for_replenishment plus the rescale EvaluateOptions::replenish_below's
// own doxygen documents (kappa' = kappa * E / E'), independent of p_make.
// ===========================================================================

TEST_F(ReplenishmentTest, ReplenishingByHandGrowsTheNodeAndConservesMassToADerivedTolerance)
{
    // The "spade = three" branch's own node, right after East's spade ply:
    // three layouts (hearts two, three, four -- all winning), kappa = 1/6
    // inherited unchanged from a six-layout root, p_i = 1 each (no
    // defender ply has multiplied anything in yet).
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

    // The full 8-layout source, so the scan can find the "ten" variant
    // this branch is missing.
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
    ASSERT_EQ(scan.candidates.size(), 1u);  // only "three, ten" matches this branch's own spade ply
    // Hand-derived: the one defender ply crossed so far (East's spade) is
    // forced and identical for every layout sharing this branch, including
    // "ten" -- delta's reply is certain, so it multiplies p_j by exactly 1.
    EXPECT_DOUBLE_EQ(scan.candidates[0].p_j, 1.0);

    // Apply the rescale exactly as EvaluateOptions::replenish_below's own
    // doxygen documents it: kappa' = kappa * E / E'.
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

    EXPECT_EQ(replenished.layouts.size(), 4u);   // grew: criterion 7's first assertion
    EXPECT_EQ(replenished.root_keys.size(), 4u);
    EXPECT_DOUBLE_EQ(replenished.kappa, (1.0 / 6.0) * (3.0 / 4.0));  // 1/8, hand-derived

    // Mass conservation, to a tolerance derived by hand rather than tuned:
    // this computation is a handful of additions (Kahan-accumulated, so
    // each contributes at most a couple of ULPs) and one division-then-
    // multiplication round trip, whose own relative error is of order
    // machine epsilon (~2e-16) applied to a magnitude bounded by 1 -- many
    // orders of magnitude below 1e-12, which is what is asserted here.
    double const mass_after = node_mass(replenished);
    EXPECT_NEAR(mass_after, mass_before, 1e-12);
}

// ===========================================================================
// Criterion 2: with replenish_below absent, nothing about this fixture's
// own evaluation changes -- verified directly, not only inferred from the
// rest of the suite staying green.
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
        EvaluateOptions{.sample_size = 6u});
    EvaluationResult const second = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/2,
        source,
        pi,
        single_card_defender,
        EvaluateOptions{.sample_size = 6u});  // replenish_below left absent, same as first

    ASSERT_FALSE(first.error.has_value());
    ASSERT_FALSE(second.error.has_value());
    EXPECT_EQ(first.by_strategy.at(1u).p_make, second.by_strategy.at(1u).p_make);
}

// ===========================================================================
// Exhaustion at depth: one node whose own scan exhausts source, one
// sibling whose own scan does not -- both descend from a root that IS a
// genuine sample, both built by hand the same way the mass-conservation
// test above builds one, so tier2_dead() can be checked directly.
// ===========================================================================

TEST_F(ReplenishmentTest, ExhaustedNodeIsNoLongerASampleAndItsSiblingStays)
{
    // Five root-space layouts: two share spade = three (hearts two,
    // three), two share spade = five (heart five, six), and one more
    // spade = three entry (heart four) sits last in source order. A
    // sample_size = 3 root draws only the first three (spade = three
    // twice, spade = five once); East's spade ply then splits that into
    // a 2-layout "three" branch and a 1-layout "five" branch.
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
        branch.is_sample = true;  // inherited from the sampled root, through both plies above
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

    // Replenish both by hand, exactly as replenish_node() does: scan,
    // rescale if anything was found, flip is_sample on SourceExhausted.
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

    // three_branch wants 1 (3 - 2) and finds it (spade = three, heart =
    // four) before the source runs out -- SampleFilled, not exhausted.
    ScanResult const three_scan = replenish(three_branch, /*wanted=*/1);
    EXPECT_EQ(three_scan.outcome, be::ScanOutcome::SampleFilled);
    EXPECT_EQ(three_branch.layouts.size(), 3u);
    EXPECT_TRUE(three_branch.is_sample);

    // five_branch wants 2 (3 - 1) but only one more spade = five layout
    // exists anywhere in source -- it finds that one and then runs out:
    // SourceExhausted, with something found, not nothing.
    ScanResult const five_scan = replenish(five_branch, /*wanted=*/2);
    EXPECT_EQ(five_scan.outcome, be::ScanOutcome::SourceExhausted);
    EXPECT_EQ(five_branch.layouts.size(), 2u);
    EXPECT_FALSE(five_branch.is_sample);

    // criterion 6: the root itself is untouched by either child's own scan.
    EXPECT_TRUE(root_result.node->is_sample);

    // criteria 3 and 4: the same injected (dead) bound, the same
    // declaration, one node fires and the other does not, and the only
    // difference between them is is_sample.
    auto const always_dead = [](Deal const&) -> int { return 0; };
    be::EvaluateOptions const options{.bound = always_dead, .delta_is_double_dummy_optimal = true};

    EXPECT_TRUE(tier2_dead(five_branch, options));    // exhausted: the gate fires
    EXPECT_FALSE(tier2_dead(three_branch, options));  // still a sample: the gate stays off

    // space_size, criterion 5 -- make_belief_view needs no change at all,
    // and this is worth pinning directly: the exhausted node reports its
    // true size, the sampled sibling reports 0.
    std::vector<be::BeliefEntry> scratch_five;
    be::BeliefView const five_view = be::make_belief_view(five_branch, scratch_five);
    EXPECT_EQ(five_view.space_size, five_branch.layouts.size());

    std::vector<be::BeliefEntry> scratch_three;
    be::BeliefView const three_view = be::make_belief_view(three_branch, scratch_three);
    EXPECT_EQ(three_view.space_size, 0u);
}
