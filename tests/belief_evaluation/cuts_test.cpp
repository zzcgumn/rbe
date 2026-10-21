#include <gtest/gtest.h>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/evaluate.hpp>

#include "test_support.hpp"

namespace be = dds::belief_evaluation;

// Tier 1's already-made cut: a node where declarer has every trick the
// contract needs evaluates to node_mass(node) without recursing. It is
// sound unconditionally: no gate, no precondition (see already_made() in
// evaluate.cpp).

namespace
{
    constexpr int Two = 2;
    constexpr int Three = 3;
    constexpr int Four = 4;
    constexpr int Five = 5;
    constexpr int Jack = 11;
    constexpr int Queen = 12;
    constexpr int King = 13;
    constexpr int Ace = 14;

    constexpr int Spades = 0;
    constexpr int Clubs = 3;

    constexpr int North = 0;  // declarer
    constexpr int East = 1;   // a defender
    constexpr int South = 2;  // dummy
    constexpr int West = 3;   // a defender

    auto strategy(be::StrategyId id) -> be::DeclarerStrategy
    {
        return be::DeclarerStrategy{.id = id, .play = be::single_card_declarer_play, .state_key = nullptr};
    }
}

class AlreadyMadeCutTest : public ::testing::Test
{
};

// --- the cut computes the right, non-trivial value --------------------------

namespace
{
    /// North A,K and South 2,3. East and West split {Q,J,4,5} differently in
    /// the two layouts, so East's lead can be scripted to tell them apart.
    /// West's forced follow (single_card_defender, unscripted) is the same rank
    /// in both, so both layouts stay in one child through trick 1.
    ///
    /// Layout A: East {Q,J}, West {4,5}.
    /// Layout B: East {J,5}, West {4,Q}.
    /// East keeps the J and West the 4 in both, so West's lowest legal card is
    /// the 4 either way.
    auto make_layout_a() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[North][Spades] = be::holding({Ace, King});
        deal.remainCards[South][Spades] = be::holding({Two, Three});
        deal.remainCards[East][Spades] = be::holding({Queen, Jack});
        deal.remainCards[West][Spades] = be::holding({Four, Five});
        return deal;
    }

    auto make_layout_b() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[North][Spades] = be::holding({Ace, King});
        deal.remainCards[South][Spades] = be::holding({Two, Three});
        deal.remainCards[East][Spades] = be::holding({Jack, Five});
        deal.remainCards[West][Spades] = be::holding({Four, Queen});
        return deal;
    }

    /// East's opening lead. Layout A chooses between its honours, 0.5/0.5.
    /// Layout B is scripted to play the jack for certain, not its lowest card
    /// (the 5), so both layouts' jack branches merge into one child. Layout A's
    /// queen branch is a separate child that neither test inspects. Every other
    /// query falls back to single_card_defender or
    /// single_card_declarer_play.
    auto merging_delta(be::DefenderQuery const& query) -> std::vector<be::WeightedCard>
    {
        bool const is_easts_lead = query.seat == East && query.layout.currentTrickRank[0] == 0;
        if (! is_easts_lead)
        {
            return be::single_card_defender(query);
        }
        if (query.layout.remainCards[East][Spades] == be::holding({Queen, Jack}))
        {
            return {be::WeightedCard{be::Card{Spades, Jack}, 0.5}, be::WeightedCard{be::Card{Spades, Queen}, 0.5}};
        }
        return {be::WeightedCard{be::Card{Spades, Jack}, 1.0}};
    }
}

TEST_F(AlreadyMadeCutTest, ReturnsTheHandDerivedMassOverSeveralLayoutsAtUnequalP)
{
    Deal const layout_a = make_layout_a();
    Deal const layout_b = make_layout_b();
    be::assert_equal_hand_sizes(layout_a);
    be::assert_equal_hand_sizes(layout_b);
    be::assert_pool_matches({layout_a, layout_b});
    be::assert_forms_one_belief_node({layout_a, layout_b}, North);
    be::VectorLayoutSource source({layout_a, layout_b});

    // East's lead is the root's defender decision, so it gives two
    // root_children: the jack branch (both layouts, merged) and the queen
    // branch (layout A alone). kappa = 1/2 (two root layouts), and p_i = 1 at
    // the root.
    //
    //   jack branch: layout A contributes p = 1 * 0.5 = 0.5, layout B
    //     p = 1 * 1.0 = 1.0. South (2), West (4) and North (K, the lower
    //     honour) play the same in both layouts, so they stay merged. North's
    //     king wins trick 1, tricks_won_by_declarer becomes 1 = tricks_needed,
    //     and the cut fires with both layouts present:
    //     mass = kappa * (0.5 + 1.0) = 0.5 * 1.5 = 0.75. Every hand still holds
    //     a spade, so the node is not terminal: this is the cut at work, not
    //     terminal_value.
    //
    //   queen branch: layout A alone, p = 1 * 0.5 = 0.5. The play is the same;
    //     North's king wins, and the cut fires again:
    //     mass = kappa * 0.5 = 0.5 * 0.5 = 0.25.
    //
    //   p_make = 0.75 + 0.25 = 1.0 by mass conservation. So the jack branch's
    //   root_children entry, not p_make, isolates the cut's unequal-p value,
    //   and that entry is what this test pins.
    constexpr double ExpectedJackBranchMass = 0.75;
    constexpr double ExpectedQueenBranchMass = 0.25;

    be::EvaluationResult const result =
        be::evaluate(layout_a, North, /*tricks_needed=*/1, source, strategy(1), merging_delta);

    ASSERT_FALSE(result.error.has_value());
    be::EvaluationValue const& value = result.by_strategy.at(1u);
    ASSERT_EQ(value.root_children.size(), 2u);
    be::RootChildValue const* jack_branch = nullptr;
    be::RootChildValue const* queen_branch = nullptr;
    for (be::RootChildValue const& child : value.root_children)
    {
        if (child.card.rank == Jack)
        {
            jack_branch = &child;
        }
        else if (child.card.rank == Queen)
        {
            queen_branch = &child;
        }
    }
    ASSERT_NE(jack_branch, nullptr);
    ASSERT_NE(queen_branch, nullptr);
    // Bitwise, not EXPECT_DOUBLE_EQ: 0.5 and 1.0 are exact, so every value
    // above is exact. By mass conservation an uncut recursion reaches the same
    // value at its terminal node; disabling the cut confirmed this.
    EXPECT_EQ(jack_branch->value, ExpectedJackBranchMass);
    EXPECT_EQ(queen_branch->value, ExpectedQueenBranchMass);
    EXPECT_EQ(value.p_make, ExpectedJackBranchMass + ExpectedQueenBranchMass);
}

// --- the cut actually fires, saving node visits ----------------------------

namespace
{
    /// One deterministic layout: North A,K / East Q,J / South 2,3 / West 4,5,
    /// East on lead. North wins two certain tricks. Node counts compare the cut
    /// firing early (tricks_needed = 1) with the recursion run to its end
    /// (tricks_needed = 2).
    auto make_two_certain_tricks() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[North][Spades] = be::holding({Ace, King});
        deal.remainCards[South][Spades] = be::holding({Two, Three});
        deal.remainCards[East][Spades] = be::holding({Queen, Jack});
        deal.remainCards[West][Spades] = be::holding({Four, Five});
        return deal;
    }
}

TEST_F(AlreadyMadeCutTest, StopsExpansionAssertedAgainstAOneTrickShortComparison)
{
    // Tier 1 has no off switch (see already_made()), so the test compares node
    // counts for two tricks_needed values instead. With 1, the cut fires as
    // soon as trick 1 is won. With 2, tricks_won never reaches tricks_needed
    // early, and the recursion runs to its terminal node. Layout, delta and pi
    // are the same, so the difference in node count is the cut's alone.
    Deal const root_layout = make_two_certain_tricks();
    be::VectorLayoutSource source({root_layout});

    // Hand-counted tree with tricks_needed = 1. Both scripted strategies are
    // deterministic here, so there is one path:
    //   root: East to lead, defender node                          -- 1
    //     East plays J -> South to play, declarer/dummy node       -- 2
    //       South plays 2 -> West to play, defender node           -- 3
    //         West plays 4 -> North to play, declarer node         -- 4
    //           North plays K -> trick 1 resolves, North to lead
    //                            trick 2. tricks_won (1) >=
    //                            tricks_needed (1): CUT FIRES,
    //                            node visited but not expanded       -- 5
    constexpr std::uint64_t NodesWithEarlyCut = 5;

    // The same tree with tricks_needed = 2. At node 5 the cut does NOT fire
    // (tricks_won 1 < needed 2), so trick 2 is played to its terminal node:
    //     [node 5, not cut] North plays A -> East to play             -- 6
    //       East plays Q -> South to play                             -- 7
    //         South plays 3 -> West to play                           -- 8
    //           West plays 5 -> trick 2 resolves, every hand empty,
    //                           terminal (tricks_won 2 >= needed 2).
    //                           already_made() runs before
    //                           terminal_value's made branch and
    //                           catches it first                      -- 9
    constexpr std::uint64_t NodesWithoutEarlyCut = 9;

    be::EvaluationResult const with_early_cut = be::evaluate(
        root_layout,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        be::single_card_defender,
        be::EvaluateOptions{.collect_counters = true});
    be::EvaluationResult const without_early_cut = be::evaluate(
        root_layout,
        North,
        /*tricks_needed=*/2,
        source,
        strategy(1),
        be::single_card_defender,
        be::EvaluateOptions{.collect_counters = true});

    ASSERT_FALSE(with_early_cut.error.has_value());
    ASSERT_FALSE(without_early_cut.error.has_value());
    ASSERT_TRUE(with_early_cut.by_strategy.at(1u).counters.has_value());
    ASSERT_TRUE(without_early_cut.by_strategy.at(1u).counters.has_value());
    EXPECT_EQ(with_early_cut.by_strategy.at(1u).counters->nodes_visited, NodesWithEarlyCut);
    EXPECT_EQ(
        without_early_cut.by_strategy.at(1u).counters->nodes_visited, NodesWithoutEarlyCut);
    EXPECT_LT(
        with_early_cut.by_strategy.at(1u).counters->nodes_visited,
        without_early_cut.by_strategy.at(1u).counters->nodes_visited);
}

// --- pi is not called below a firing cut ------------------------------------

TEST_F(AlreadyMadeCutTest, PiIsNotCalledWhenTheContractIsAlreadyMadeAtTheRoot)
{
    // tricks_needed = 0: the contract is made before any card is played, so
    // the cut fires at the root, before seat_on_play is consulted. pi must
    // never be asked for a lead.
    Deal const root_layout = make_two_certain_tricks();
    be::VectorLayoutSource source({root_layout});
    be::RecordingDeclarerStrategy recording(be::Card{Spades, King});  // never actually asked

    be::EvaluationResult const result = be::evaluate(
        root_layout,
        North,
        /*tricks_needed=*/0,
        source,
        recording.as_strategy(),
        be::single_card_defender);

    ASSERT_FALSE(result.error.has_value());
    EXPECT_TRUE(recording.calls().empty());
    // RecordingDeclarerStrategy::as_strategy() fixes id = 0.
    // One root layout, p = 1, kappa = 1, so node_mass is 1.0. This test is
    // about the call count; the test above covers the value.
    EXPECT_EQ(result.by_strategy.at(0u).p_make, 1.0);
    EXPECT_TRUE(result.by_strategy.at(0u).root_children.empty());
}

TEST_F(AlreadyMadeCutTest, TierCutCountersFireAtTheRootBlockSite)
{
    // The same fixture and tricks_needed as
    // PiIsNotCalledWhenTheContractIsAlreadyMadeAtTheRoot, which pins the root
    // cut by call count. This one pins it by its counter. The cut fires in
    // evaluate()'s root handling, never in p_make(), so a counter incremented
    // only in p_make() would miss it. counters_test.cpp's
    // TierCutCountersDistinguishTheAlreadyMadeCutFromTheOthers covers the
    // p_make() site.
    Deal const root_layout = make_two_certain_tricks();
    be::VectorLayoutSource source({root_layout});

    be::EvaluationResult const result = be::evaluate(
        root_layout,
        North,
        /*tricks_needed=*/0,
        source,
        strategy(1),
        be::single_card_defender,
        be::EvaluateOptions{.collect_counters = true});

    ASSERT_FALSE(result.error.has_value());
    be::EvaluationValue const& value = result.by_strategy.at(1u);
    ASSERT_TRUE(value.counters.has_value());
    EXPECT_EQ(value.counters->tier1_made_cuts, 1u);
    EXPECT_EQ(value.counters->tier1_dead_cuts, 0u);
    EXPECT_EQ(value.counters->tier2_cuts, 0u);
}

// Tier 1's dead cut, the mirror of the already-made cut: a node where
// declarer cannot reach tricks_needed even by winning every remaining
// trick evaluates to 0.0 without recursing. Sound unconditionally, for the
// same reason as already_made(); no gate (see is_dead() in
// evaluate.cpp).

namespace
{
    /// East holds the top two spades, North (declarer) and South (dummy) the
    /// bottom two, and West the middle two. East wins trick 1 whatever anyone
    /// plays, so declarer's tricks_won stays 0. East on lead (defender root),
    /// deterministic under the scripted strategies.
    auto make_east_wins_first_trick() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[East][Spades] = be::holding({Ace, King});
        deal.remainCards[West][Spades] = be::holding({Queen, Jack});
        deal.remainCards[North][Spades] = be::holding({Two, Three});
        deal.remainCards[South][Spades] = be::holding({Four, Five});
        return deal;
    }
}

class DeadCutTest : public ::testing::Test
{
};

TEST_F(DeadCutTest, StopsExpansionAssertedAgainstAOneTrickShortComparison)
{
    // No off switch here either, so the test compares node counts on the same
    // fixture: tricks_needed = 2 (impossible as soon as East wins trick 1,
    // with one trick left) against 1 (still possible, so the recursion runs to
    // its terminal node).
    Deal const root_layout = make_east_wins_first_trick();
    be::VectorLayoutSource source({root_layout});

    // Hand-counted tree with tricks_needed = 2 (East leads; rotation
    // East -> South -> West -> North):
    //   root: East to lead, defender node                        -- 1
    //     East plays K -> South to play, declarer/dummy node     -- 2
    //       South plays 4 -> West to play, defender node         -- 3
    //         West plays J -> North to play, declarer node       -- 4
    //           North plays 2 -> trick 1 resolves (East's king
    //                            wins), East to lead trick 2.
    //                            tricks_won (0) + tricks_remaining
    //                            (1) < tricks_needed (2): DEAD,
    //                            CUT FIRES, node visited but not
    //                            expanded                          -- 5
    constexpr std::uint64_t NodesWithEarlyDeadCut = 5;

    // The same tree with tricks_needed = 1. At node 5 the cut does NOT fire
    // (0 + 1 is not < 1), so trick 2 is played to its terminal node:
    //     [node 5, not cut] East plays A -> South to play           -- 6
    //       South plays 5 -> West to play                           -- 7
    //         West plays Q -> North to play                         -- 8
    //           North plays 3 -> trick 2 resolves (East's ace
    //                            wins), every hand empty. Terminal
    //                            and also dead (0 + 0 < 1), so
    //                            is_dead() returns the 0.0 before
    //                            terminal_value() does             -- 9
    constexpr std::uint64_t NodesWithoutEarlyDeadCut = 9;

    be::EvaluationResult const with_early_cut = be::evaluate(
        root_layout,
        North,
        /*tricks_needed=*/2,
        source,
        strategy(1),
        be::single_card_defender,
        be::EvaluateOptions{.collect_counters = true});
    be::EvaluationResult const without_early_cut = be::evaluate(
        root_layout,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        be::single_card_defender,
        be::EvaluateOptions{.collect_counters = true});

    ASSERT_FALSE(with_early_cut.error.has_value());
    ASSERT_FALSE(without_early_cut.error.has_value());
    ASSERT_TRUE(with_early_cut.by_strategy.at(1u).counters.has_value());
    ASSERT_TRUE(without_early_cut.by_strategy.at(1u).counters.has_value());
    EXPECT_EQ(with_early_cut.by_strategy.at(1u).counters->nodes_visited, NodesWithEarlyDeadCut);
    EXPECT_EQ(
        without_early_cut.by_strategy.at(1u).counters->nodes_visited, NodesWithoutEarlyDeadCut);
    EXPECT_LT(
        with_early_cut.by_strategy.at(1u).counters->nodes_visited,
        without_early_cut.by_strategy.at(1u).counters->nodes_visited);

    // Zero both ways: by the cut, and by natural completion (with
    // tricks_needed = 1 the terminal node happens to be dead too).
    EXPECT_EQ(with_early_cut.by_strategy.at(1u).p_make, 0.0);
    EXPECT_EQ(without_early_cut.by_strategy.at(1u).p_make, 0.0);
    // Bitwise agreement with the uncut path: both exactly 0.0. With is_dead()
    // disabled, the tricks_needed = 2 fixture still evaluates to 0.0 at its
    // terminal node four plies down.
}

TEST_F(DeadCutTest, TierCutCountersDistinguishTheDeadCutFromTheOthers)
{
    // The same fixture and tricks_needed as
    // StopsExpansionAssertedAgainstAOneTrickShortComparison (its
    // with_early_cut run): the dead cut fires at node 5, inside p_make(), not
    // at the root (node 1 is not yet dead). Neither other cut can fire here:
    // no node is made before it is dead, and no bound is supplied. This half
    // shows that the dead counter counts only its tier; counters_test.cpp's
    // TierCutCountersDistinguishTheAlreadyMadeCutFromTheOthers is the other
    // half.
    Deal const root_layout = make_east_wins_first_trick();
    be::VectorLayoutSource source({root_layout});

    be::EvaluationResult const result = be::evaluate(
        root_layout,
        North,
        /*tricks_needed=*/2,
        source,
        strategy(1),
        be::single_card_defender,
        be::EvaluateOptions{.collect_counters = true});

    ASSERT_FALSE(result.error.has_value());
    be::EvaluationValue const& value = result.by_strategy.at(1u);
    ASSERT_TRUE(value.counters.has_value());
    EXPECT_EQ(value.counters->tier1_dead_cuts, 1u);
    EXPECT_EQ(value.counters->tier1_made_cuts, 0u);
    EXPECT_EQ(value.counters->tier2_cuts, 0u);
}

TEST_F(DeadCutTest, PiAndDeltaAreNotCalledWhenTheRootIsAlreadyDead)
{
    // tricks_needed = 4, but tricks_remaining(root) is 2 (North's card
    // count), so the root is dead before any card is played. Neither pi nor
    // delta may be asked for a card.
    Deal const root_layout = make_east_wins_first_trick();
    be::VectorLayoutSource source({root_layout});
    be::RecordingDeclarerStrategy recording_pi(be::Card{Spades, Two});  // never actually asked
    bool delta_called = false;
    auto const recording_delta = [&delta_called](be::DefenderQuery const&) -> std::vector<be::WeightedCard>
    {
        delta_called = true;
        return {};
    };

    be::EvaluationResult const result = be::evaluate(
        root_layout, North, /*tricks_needed=*/4, source, recording_pi.as_strategy(), recording_delta);

    ASSERT_FALSE(result.error.has_value());
    EXPECT_TRUE(recording_pi.calls().empty());
    EXPECT_FALSE(delta_called);
    // RecordingDeclarerStrategy::as_strategy() fixes id = 0.
    EXPECT_EQ(result.by_strategy.at(0u).p_make, 0.0);
    EXPECT_TRUE(result.by_strategy.at(0u).root_children.empty());
}

TEST_F(DeadCutTest, TierCutCountersFireAtTheRootBlockSite)
{
    // The same fixture and tricks_needed as
    // PiAndDeltaAreNotCalledWhenTheRootIsAlreadyDead, which pins the root cut
    // by call count. This one pins it by its counter. The cut fires in
    // evaluate()'s root handling, which a counter in p_make() alone would
    // miss.
    Deal const root_layout = make_east_wins_first_trick();
    be::VectorLayoutSource source({root_layout});

    be::EvaluationResult const result = be::evaluate(
        root_layout,
        North,
        /*tricks_needed=*/4,
        source,
        strategy(1),
        be::single_card_defender,
        be::EvaluateOptions{.collect_counters = true});

    ASSERT_FALSE(result.error.has_value());
    be::EvaluationValue const& value = result.by_strategy.at(1u);
    ASSERT_TRUE(value.counters.has_value());
    EXPECT_EQ(value.counters->tier1_dead_cuts, 1u);
    EXPECT_EQ(value.counters->tier1_made_cuts, 0u);
    EXPECT_EQ(value.counters->tier2_cuts, 0u);
}

// The LayoutBound seam: a caller-supplied double-dummy upper bound on
// tricks, and the separate delta_is_double_dummy_optimal declaration that
// tier 2 is gated on. These tests pin the seam: the scripted test double
// behaves correctly, and a bound without the declaration cannot change the
// answer.

class LayoutBoundTest : public ::testing::Test
{
};

TEST_F(LayoutBoundTest, ScriptedBoundRecordsWhatItWasAskedAndReturnsTheScriptedValue)
{
    Deal const layout = make_east_wins_first_trick();
    be::ScriptedBound scripted({{layout, 7}});
    be::LayoutBound const bound = scripted.as_bound();

    EXPECT_EQ(bound(layout), 7);
    ASSERT_EQ(scripted.queries().size(), 1u);
    EXPECT_EQ(scripted.queries().front().remainCards[East][Spades], layout.remainCards[East][Spades]);
}

TEST_F(LayoutBoundTest, SupplyingABoundWithoutTheDeclarationDoesNotChangeTheAnswerEitherDirection)
{
    // A bound scripted to claim zero tricks for declarer: the value that
    // fires tier 2's cut (see TierTwoCutTest below) once the declaration is
    // made. Without the declaration both runs must agree bitwise: a bound
    // alone does not switch the cut on (see
    // EvaluateOptions::delta_is_double_dummy_optimal for why the two are
    // separate).
    Deal const root_layout = make_east_wins_first_trick();
    be::VectorLayoutSource source({root_layout});
    be::ScriptedBound scripted({{root_layout, 0}});

    be::EvaluationResult const without_bound = be::evaluate(
        root_layout, North, /*tricks_needed=*/1, source, strategy(1), be::single_card_defender);
    be::EvaluationResult const with_bound = be::evaluate(
        root_layout,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        be::single_card_defender,
        be::EvaluateOptions{.bound = scripted.as_bound()});  // no delta_is_double_dummy_optimal

    ASSERT_FALSE(without_bound.error.has_value());
    ASSERT_FALSE(with_bound.error.has_value());
    EXPECT_EQ(without_bound.by_strategy.at(1u).p_make, with_bound.by_strategy.at(1u).p_make);
    ASSERT_EQ(
        without_bound.by_strategy.at(1u).root_children.size(),
        with_bound.by_strategy.at(1u).root_children.size());
    for (std::size_t i = 0; i < without_bound.by_strategy.at(1u).root_children.size(); ++i)
    {
        EXPECT_EQ(
            without_bound.by_strategy.at(1u).root_children[i].value,
            with_bound.by_strategy.at(1u).root_children[i].value);
    }
    // The bound is never called: tier2_dead() checks the declaration before
    // it reads options.bound. ScriptedBound's ADD_FAILURE not firing is part
    // of what this test pins.
}

// Tier 2's node-level cut: a node whose every layout is dead by the
// injected bound, under the caller's delta_is_double_dummy_optimal
// declaration, evaluates to 0.0 without recursing. Every bound here is
// scripted; no solver is used in this file.

class TierTwoCutTest : public ::testing::Test
{
};

TEST_F(TierTwoCutTest, FiresWhenEveryLayoutIsDeadAndTheDeclarationIsMade)
{
    // Reuses make_layout_a(), make_layout_b() and merging_delta() from
    // AlreadyMadeCutTest. The scripted bound need not match what
    // merging_delta does: tier2_dead() reads only the bound, never delta.
    Deal const layout_a = make_layout_a();
    Deal const layout_b = make_layout_b();
    be::VectorLayoutSource source({layout_a, layout_b});
    be::ScriptedBound scripted({{layout_a, 0}, {layout_b, 0}});  // both dead: needed = 1

    be::EvaluationResult const result = be::evaluate(
        layout_a,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        merging_delta,
        be::EvaluateOptions{.bound = scripted.as_bound(), .delta_is_double_dummy_optimal = true});

    ASSERT_FALSE(result.error.has_value());
    EXPECT_EQ(result.by_strategy.at(1u).p_make, 0.0);
    EXPECT_TRUE(result.by_strategy.at(1u).root_children.empty());
}

TEST_F(TierTwoCutTest, TierCutCountersFireAtTheRootBlockSite)
{
    // The same fixture, bound and declaration as
    // FiresWhenEveryLayoutIsDeadAndTheDeclarationIsMade, which pins the
    // value. This one pins the same firing by its counter. Both layouts are
    // dead before any card is played, so the cut fires in evaluate()'s root
    // handling, which a counter in p_make() alone would miss.
    Deal const layout_a = make_layout_a();
    Deal const layout_b = make_layout_b();
    be::VectorLayoutSource source({layout_a, layout_b});
    be::ScriptedBound scripted({{layout_a, 0}, {layout_b, 0}});

    be::EvaluationResult const result = be::evaluate(
        layout_a,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        merging_delta,
        be::EvaluateOptions{
            .collect_counters = true,
            .bound = scripted.as_bound(),
            .delta_is_double_dummy_optimal = true});

    ASSERT_FALSE(result.error.has_value());
    be::EvaluationValue const& value = result.by_strategy.at(1u);
    ASSERT_TRUE(value.counters.has_value());
    EXPECT_EQ(value.counters->tier2_cuts, 1u);
    EXPECT_EQ(value.counters->tier1_made_cuts, 0u);
    EXPECT_EQ(value.counters->tier1_dead_cuts, 0u);
}


// tier2_dead() runs at every node, not only the root, so a fixture over
// several plies needs a bound that answers for every layout at every
// depth. ScriptedBound matches exact Deals, and every play() returns a new
// Deal, so it cannot answer past the root. The tests below that need a
// bound beyond the root use a fixture where both layouts take the same
// single path. A lambda keyed on a club filler that is never played then
// tells them apart at any depth. ScriptedBound is still right where the
// check happens at one node only (an all-dead root, or a root check whose
// early exit is the point).

namespace
{
    /// North holds the two top spades, so however East and West split their
    /// spades, North wins trick 1 the same way: one path through the whole
    /// recursion. The layouts differ only in a club filler swapped between the
    /// defenders (East holds the Six in layout_a, the Seven in layout_b). Clubs
    /// are never played (tricks_needed = 1 is met as trick 1 resolves), so the
    /// filler tells the layouts apart at every depth.
    ///
    /// East on lead (defender root): East -> South -> West -> North. East's
    /// spades ({Queen, Jack}) are the same in both layouts, so East's forced
    /// lead is the jack in both, merged from the first ply.
    auto make_declarer_certain_win_with_club_filler(int east_club_rank, int west_club_rank) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[North][Spades] = be::holding({Ace, King});
        deal.remainCards[East][Spades] = be::holding({Queen, Jack});
        deal.remainCards[South][Spades] = be::holding({Two, Three});
        deal.remainCards[West][Spades] = be::holding({Four, Five});
        deal.remainCards[East][Clubs] = be::holding({east_club_rank});
        deal.remainCards[West][Clubs] = be::holding({west_club_rank});
        return deal;
    }

    constexpr int Six = 6;
    constexpr int Seven = 7;

    auto is_layout_a_by_club_filler(Deal const& layout) -> bool
    {
        return (layout.remainCards[East][Clubs] & be::holding({Six})) != 0;
    }
}

TEST_F(TierTwoCutTest, OneLiveLayoutSuppressesTheCut)
{
    // layout_a (East's club Six) is scripted dead throughout. layout_b
    // (East's club Seven) is scripted live, at exactly still_needed, so this
    // test also pins the >= in tier2_dead(), not only its direction. Both
    // layouts share every node on the single path, so the live one must
    // suppress the cut at every check. Evaluation then reaches the
    // already-made cut after trick 1 (both AK tricks are certain; one is
    // needed): node_mass = kappa * (p_a + p_b) = 0.5 * (1 + 1) = 1.0. No p
    // changes, since single_card_defender never gives a choice.
    Deal const layout_a = make_declarer_certain_win_with_club_filler(Six, Seven);
    Deal const layout_b = make_declarer_certain_win_with_club_filler(Seven, Six);
    be::VectorLayoutSource source({layout_a, layout_b});
    auto const bound = [](Deal const& layout) -> int
    { return is_layout_a_by_club_filler(layout) ? 0 : 1; };  // 1 is exactly still_needed

    be::EvaluationResult const result = be::evaluate(
        layout_a,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        be::single_card_defender,
        be::EvaluateOptions{.bound = bound, .delta_is_double_dummy_optimal = true});

    ASSERT_FALSE(result.error.has_value());
    EXPECT_EQ(result.by_strategy.at(1u).p_make, 1.0);
}

TEST_F(TierTwoCutTest, NeverFiresWithoutTheDeclarationWhateverTheBoundSays)
{
    // The same fixture, but every layout scripted dead and the declaration
    // withheld. layout_b would normally suppress the cut; dead here, it makes
    // this test fail if the declaration stopped being checked first.
    // Evaluation must still reach the same 1.0.
    Deal const layout_a = make_declarer_certain_win_with_club_filler(Six, Seven);
    Deal const layout_b = make_declarer_certain_win_with_club_filler(Seven, Six);
    be::VectorLayoutSource source({layout_a, layout_b});
    auto const bound = [](Deal const&) -> int { return 0; };  // "all dead", ignored without the declaration

    be::EvaluationResult const result = be::evaluate(
        layout_a,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        be::single_card_defender,
        be::EvaluateOptions{.collect_counters = true, .bound = bound});  // no declaration

    ASSERT_FALSE(result.error.has_value());
    be::EvaluationValue const& value = result.by_strategy.at(1u);
    EXPECT_EQ(value.p_make, 1.0);
    ASSERT_TRUE(value.counters.has_value());
    // Hand-counted tree, one path (both layouts always merged):
    //   root: East to lead                                    -- 1
    //     East plays J -> South to play                        -- 2
    //       South plays 2 -> West to play                      -- 3
    //         West plays 4 -> North to play                    -- 4
    //           North plays K -> trick 1 resolves, tricks_won
    //                            (1) >= tricks_needed (1):
    //                            already-made cut, visited but
    //                            not expanded                   -- 5
    EXPECT_EQ(value.counters->nodes_visited, 5u);
}

TEST_F(TierTwoCutTest, TheBoundIsNeverConsultedForAMakeCut)
{
    // The strategy-fusion trap. North holds only the spade queen, East only
    // the king. East, on lead, must play the king, so North's queen always
    // loses: the double-dummy value is 0. This bound wrongly claims 5 ("not
    // dead") at every node. A symmetric make cut would read that as licence
    // to return the node's full mass (1.0 here: one layout, p = 1,
    // kappa = 1). There is no make cut, so evaluation must return the true
    // value, 0.0.
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = East;
    deal.remainCards[North][Spades] = be::holding({Queen});
    deal.remainCards[East][Spades] = be::holding({King});
    deal.remainCards[South][Spades] = be::holding({Two});
    deal.remainCards[West][Spades] = be::holding({Three});
    be::VectorLayoutSource source({deal});
    auto const bound = [](Deal const&) -> int { return 5; };  // "not dead", at any depth this is asked

    be::EvaluationResult const result = be::evaluate(
        deal,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        be::single_card_defender,
        be::EvaluateOptions{.bound = bound, .delta_is_double_dummy_optimal = true});

    ASSERT_FALSE(result.error.has_value());
    EXPECT_EQ(result.by_strategy.at(1u).p_make, 0.0);
}

TEST_F(TierTwoCutTest, StopsAtTheFirstLiveLayoutWithoutQueryingTheRest)
{
    // Built so tier2_dead runs exactly once in the whole evaluation. The root
    // starts three cards into trick 1 (South, West and North's Ace already
    // played), so East, the last player, is on play at the root. Whatever East
    // plays, North's ace wins, so the next node is already made (tricks_won
    // reaches tricks_needed = 1) and returns before tier2_dead. The only call
    // is the root's, over its two layouts.
    //
    // layout_a is scripted live and comes first in node.layouts (the
    // VectorLayoutSource order). layout_b has no scripted entry, so if the
    // early exit in tier2_dead's loop works, it is never asked.
    //
    // The two layouts must be splits of the same pool. The club filler is
    // swapped between East and West (as in
    // make_declarer_certain_win_with_club_filler()), so the pool {Six, Seven}
    // is the same and both layouts survive make_root() into one node. This is
    // asserted below. With different pools, make_root()'s consistency filter
    // drops layout_b, and the test passes for the wrong reason.
    Deal layout_a{};
    layout_a.trump = DDS_NOTRUMP;
    layout_a.first = South;
    layout_a.currentTrickSuit[0] = Spades;
    layout_a.currentTrickRank[0] = Two;    // South's card, already played
    layout_a.currentTrickSuit[1] = Spades;
    layout_a.currentTrickRank[1] = Three;  // West's card, already played
    layout_a.currentTrickSuit[2] = Spades;
    layout_a.currentTrickRank[2] = Ace;    // North's card, already played and winning
    layout_a.remainCards[East][Spades] = be::holding({Four});  // East's own card, about to play
    layout_a.remainCards[East][Clubs] = be::holding({Six});    // untouched filler pool, split one way
    layout_a.remainCards[West][Clubs] = be::holding({Seven});

    Deal layout_b = layout_a;
    layout_b.remainCards[East][Clubs] = be::holding({Seven});  // same pool, split the other way
    layout_b.remainCards[West][Clubs] = be::holding({Six});

    be::VectorLayoutSource source({layout_a, layout_b});

    // Check that both layouts survive make_root() into one node before
    // trusting the early-exit assertion (see the fixture comment).
    std::optional<be::BeliefNode> const root =
        be::make_root(layout_a, North, /*tricks_needed=*/1, source).node;
    ASSERT_TRUE(root.has_value());
    ASSERT_EQ(root->layouts.size(), 2u);

    be::ScriptedBound scripted({{layout_a, 5}});  // live; layout_b deliberately unscripted

    be::EvaluationResult const result = be::evaluate(
        layout_a,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        be::single_card_defender,
        be::EvaluateOptions{.bound = scripted.as_bound(), .delta_is_double_dummy_optimal = true});

    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(scripted.queries().size(), 1u);
    EXPECT_EQ(
        scripted.queries().front().remainCards[East][Clubs], layout_a.remainCards[East][Clubs]);
}

TEST_F(TierTwoCutTest, BitwiseAgreementWithTheUncutPathVerifiedByTemporarilyDisablingTheCut)
{
    // Tier 2 has no off switch either. With the tier2_dead() calls in
    // evaluate.cpp disabled, the fixture of
    // FiresWhenEveryLayoutIsDeadAndTheDeclarationIsMade still evaluates to
    // exactly 0.0 through natural recursion. This test pins the cut's value,
    // which that check was run against.
    Deal const layout_a = make_layout_a();
    Deal const layout_b = make_layout_b();
    be::VectorLayoutSource source({layout_a, layout_b});
    be::ScriptedBound scripted({{layout_a, 0}, {layout_b, 0}});

    be::EvaluationResult const result = be::evaluate(
        layout_a,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        merging_delta,
        be::EvaluateOptions{.bound = scripted.as_bound(), .delta_is_double_dummy_optimal = true});

    ASSERT_FALSE(result.error.has_value());
    EXPECT_EQ(result.by_strategy.at(1u).p_make, 0.0);
}

TEST_F(TierTwoCutTest, PiAndDeltaAreNotCalledWhenTheRootIsDeadByTheBound)
{
    Deal const layout_a = make_layout_a();
    Deal const layout_b = make_layout_b();
    be::VectorLayoutSource source({layout_a, layout_b});
    be::ScriptedBound scripted({{layout_a, 0}, {layout_b, 0}});
    be::RecordingDeclarerStrategy recording_pi(be::Card{Spades, King});  // never actually asked
    bool delta_called = false;
    auto const recording_delta = [&delta_called](be::DefenderQuery const&) -> std::vector<be::WeightedCard>
    {
        delta_called = true;
        return {};
    };

    be::EvaluationResult const result = be::evaluate(
        layout_a,
        North,
        /*tricks_needed=*/1,
        source,
        recording_pi.as_strategy(),
        recording_delta,
        be::EvaluateOptions{.bound = scripted.as_bound(), .delta_is_double_dummy_optimal = true});

    ASSERT_FALSE(result.error.has_value());
    EXPECT_TRUE(recording_pi.calls().empty());
    EXPECT_FALSE(delta_called);
    // RecordingDeclarerStrategy::as_strategy() fixes id = 0.
    EXPECT_EQ(result.by_strategy.at(0u).p_make, 0.0);
}

// PredicateBound: the depth-independent alternative to ScriptedBound (see
// test_support.hpp). ScriptedBound matches exact Deals, and each play
// produces a new Deal, so an entry scripted for the root answers nothing
// one ply down. tier2_dead() runs at every node, so driving a
// ScriptedBound through evaluate() hits its ADD_FAILURE one ply in.

class PredicateBoundTest : public ::testing::Test
{
};

namespace
{
    constexpr int Hearts = 1;

    /// North holds two certain winners in two tricks (the AK of spades and
    /// of hearts). North holds 4 cards at every node in trick 1, then fewer
    /// once it plays. That gives a predicate two regimes to tell apart without
    /// an entry per node. East on lead (defender root): East -> South -> West
    /// -> North. Deterministic under the scripted strategies: each seat holds
    /// one card per suit it can legally play at every step.
    auto make_two_trick_declarer_certain_win() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[North][Spades] = be::holding({Ace, King});
        deal.remainCards[North][Hearts] = be::holding({Ace, King});
        deal.remainCards[East][Spades] = be::holding({Queen, Jack});
        deal.remainCards[East][Hearts] = be::holding({Queen, Jack});
        deal.remainCards[South][Spades] = be::holding({Two, Three});
        deal.remainCards[South][Hearts] = be::holding({Two, Three});
        deal.remainCards[West][Spades] = be::holding({Four, Five});
        deal.remainCards[West][Hearts] = be::holding({Four, Five});
        return deal;
    }
}

TEST_F(PredicateBoundTest, AnswersEveryNodeInATwoTrickFixtureWithoutATableEntryPerNode)
{
    Deal const root_layout = make_two_trick_declarer_certain_win();
    be::assert_equal_hand_sizes(root_layout);
    be::VectorLayoutSource source({root_layout});
    // Two predicates, tried in order. "North still holds all four cards"
    // (every node in trick 1) gives a bound of 2, not less than still_needed
    // (2, nothing won yet). The catch-all gives 1, not less than still_needed
    // (1, once trick 1 is won) at every node in trick 2. Both are live, so the
    // cut never fires. This test is about the double answering every node;
    // TierTwoCutTest covers firing.
    be::PredicateBound predicate_bound(
        {{[](Deal const& layout) -> bool { return be::card_count(layout, North) >= 4; }, 2},
         {[](Deal const&) -> bool { return true; }, 1}});

    be::EvaluationResult const result = be::evaluate(
        root_layout,
        North,
        /*tricks_needed=*/2,
        source,
        strategy(1),
        be::single_card_defender,
        be::EvaluateOptions{
            .bound = predicate_bound.as_bound(), .delta_is_double_dummy_optimal = true});

    ASSERT_FALSE(result.error.has_value());
    // Both tricks are certain, so the recursion is one deterministic path
    // (one root layout, kappa = 1, p = 1, no choice) and reaches
    // node_mass = 1.0 when trick 2 is won. Nothing is dead by the bound, so
    // suppressing the cut gives the same value.
    EXPECT_EQ(result.by_strategy.at(1u).p_make, 1.0);
    // Hand-counted: tier2_dead() runs at every node up to the one before
    // North's winning play in trick 2. That is 8 nodes: East, South, West and
    // North in trick 1, then North, East, South and West in trick 2. At the
    // 9th node the already-made cut fires first. The first four queries see
    // North holding all four cards (the first predicate); the last four see
    // fewer (the catch-all). Two different plies, one two-entry table.
    ASSERT_EQ(predicate_bound.queries().size(), 8u);
    EXPECT_EQ(be::card_count(predicate_bound.queries().front(), North), 4);
    EXPECT_LT(be::card_count(predicate_bound.queries().back(), North), 4);
}

// The sampling gate: tier2_dead() is gated on !node.is_sample; neither
// tier-1 cut is. These tests build a BeliefNode by hand, rather than
// reaching is_sample through evaluate(), to test the predicate alone.
// already_made() and is_dead() are public, like is_terminal() and
// terminal_value(), so a test can do this.

class SamplingGateTest : public ::testing::Test
{
};

TEST_F(SamplingGateTest, Tier2DoesNotFireOnASampledNodeEvenWhenEveryLayoutIsDead)
{
    be::BeliefNode node{};
    node.state.declarer = North;
    node.state.tricks_needed = 1;
    node.state.tricks_won_by_declarer = 0;
    Deal layout{};
    layout.trump = DDS_NOTRUMP;
    layout.remainCards[North][Spades] = be::holding({Two});
    node.layouts = {layout};
    node.p = {1.0};
    node.kappa = 1.0;
    node.is_sample = true;  // set by hand to isolate the predicate

    auto const bound = [](Deal const&) -> int { return 0; };  // dead, if it were consulted
    be::EvaluateOptions const options{.bound = bound, .delta_is_double_dummy_optimal = true};

    EXPECT_FALSE(be::tier2_dead(node, options));
}

TEST_F(SamplingGateTest, BothTier1CutsStillFireOnASampledNode)
{
    // Tested separately on purpose: gating all the cuts instead of tier 2
    // alone would disable these two on every sampled node. One assertion per
    // tier pins it.
    be::BeliefNode already_made_node{};
    already_made_node.state.declarer = North;
    already_made_node.state.tricks_needed = 1;
    already_made_node.state.tricks_won_by_declarer = 1;  // already made
    already_made_node.is_sample = true;
    EXPECT_TRUE(be::already_made(already_made_node.state));

    be::BeliefNode dead_node{};
    dead_node.state.declarer = North;
    dead_node.state.tricks_needed = 5;  // impossible: nothing left to win 5 tricks from
    dead_node.state.tricks_won_by_declarer = 0;
    dead_node.state.known_holdings = Deal{};  // every hand empty, so tricks_remaining() == 0
    dead_node.is_sample = true;
    EXPECT_TRUE(be::is_dead(dead_node.state));
}
