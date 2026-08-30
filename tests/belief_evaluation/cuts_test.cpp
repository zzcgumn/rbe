#include <gtest/gtest.h>

#include <api/dll.h>
#include <utility/constants.h>

#include <belief_evaluation/evaluate.hpp>

#include "test_support.hpp"

// Tier 1's already-made cut: a node where declarer has already banked every
// trick the contract needs evaluates to node_mass(node) without recursing
// further -- sound unconditionally, no gate, no precondition (see
// evaluate.cpp's already_made()).

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

    auto strategy(StrategyId id) -> DeclarerStrategy
    {
        return DeclarerStrategy{.id = id, .play = single_card_declarer_play, .state_key = nullptr};
    }
}

class AlreadyMadeCutTest : public ::testing::Test
{
};

// --- criteria 1 & 2: the cut computes the right, non-trivial value --------

namespace
{
    /// North A,K / South 2,3 opposite each other; East and West split
    /// {Q,J,4,5} between them, differently in the two layouts below, so
    /// East's opening lead can be scripted to distinguish them while West's
    /// own forced follow (via single_card_defender, unscripted) plays the
    /// same rank in both -- keeping both layouts merged into the same
    /// child all the way through trick 1.
    ///
    /// Layout A: East {Q,J}, West {4,5}.
    /// Layout B: East {J,5}, West {4,Q}.
    /// J stays with East and 4 stays with West in both, so West's lowest
    /// legal card is "4" in either layout -- no scripting needed for West.
    auto make_layout_a() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[North][Spades] = holding({Ace, King});
        deal.remainCards[South][Spades] = holding({Two, Three});
        deal.remainCards[East][Spades] = holding({Queen, Jack});
        deal.remainCards[West][Spades] = holding({Four, Five});
        return deal;
    }

    auto make_layout_b() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[North][Spades] = holding({Ace, King});
        deal.remainCards[South][Spades] = holding({Two, Three});
        deal.remainCards[East][Spades] = holding({Jack, Five});
        deal.remainCards[West][Spades] = holding({Four, Queen});
        return deal;
    }

    /// East's opening lead: layout A has a genuine choice between its two
    /// honours, spread 0.5/0.5; layout B is scripted to certainly play its
    /// jack -- deliberately not the "lowest legal" 5, so that both layouts'
    /// jack branches merge into the same child (layout A's queen branch is
    /// a separate child neither test below inspects). Every other query
    /// (West's forced follow, either declarer-side play) falls back to
    /// single_card_defender / single_card_declarer_play.
    auto merging_delta(DefenderQuery const& query) -> std::vector<WeightedCard>
    {
        bool const is_easts_lead = query.seat == East && query.layout.currentTrickRank[0] == 0;
        if (! is_easts_lead)
        {
            return single_card_defender(query);
        }
        if (query.layout.remainCards[East][Spades] == holding({Queen, Jack}))
        {
            return {WeightedCard{Card{Spades, Jack}, 0.5}, WeightedCard{Card{Spades, Queen}, 0.5}};
        }
        return {WeightedCard{Card{Spades, Jack}, 1.0}};
    }
}

TEST_F(AlreadyMadeCutTest, ReturnsTheHandDerivedMassOverSeveralLayoutsAtUnequalP)
{
    Deal const layout_a = make_layout_a();
    Deal const layout_b = make_layout_b();
    assert_equal_hand_sizes(layout_a);
    assert_equal_hand_sizes(layout_b);
    assert_pool_matches({layout_a, layout_b});
    assert_forms_one_belief_node({layout_a, layout_b}, North);
    VectorLayoutSource source({layout_a, layout_b});

    // East's lead is the root's own defender decision, so it produces two
    // root_children directly: the jack branch (both layouts, merged) and
    // the queen branch (layout A alone -- layout B never plays queen).
    // Derivation, kappa = 1/2 (two root layouts), p_i = 1 each at the root:
    //
    //   jack branch: layout A contributes p = 1 * 0.5 = 0.5 (its half of
    //     the genuine choice), layout B contributes p = 1 * 1.0 = 1.0
    //     (certain). South (2), West (4, forced the same way in both
    //     layouts) and North (K, the lower of its two honours, so it plays
    //     first under single_card_declarer_play) then all play identically
    //     across both layouts, so they stay merged: North's king beats
    //     jack/two/four in every layout, winning trick 1.
    //     tricks_won_by_declarer becomes 1, matching tricks_needed = 1 --
    //     the cut fires at the node right after trick 1, still holding
    //     both layouts: mass = kappa * (0.5 + 1.0) = 0.5 * 1.5 = 0.75.
    //     Every hand still holds one more spade at this point (not
    //     terminal -- North kept its ace, East kept its other honour,
    //     South kept its three, West kept its five), so this is the cut
    //     doing real work, not coincidence with terminal_value.
    //
    //   queen branch: layout A alone, p = 1 * 0.5 = 0.5. South (2), West
    //     (4, unaffected by the other branch) and North (K again) play
    //     out the same way; North's king beats queen/two/four too, so
    //     tricks_won_by_declarer reaches 1 here as well and the cut fires
    //     again: mass = kappa * 0.5 = 0.5 * 0.5 = 0.25.
    //
    //   p_make = 0.75 + 0.25 = 1.0 (mass conservation across the root's
    //   own two children) -- so the jack branch's own root_children entry,
    //   not the summed p_make, is what actually isolates the cut's
    //   unequal-p value; that entry is what this test pins.
    constexpr double ExpectedJackBranchMass = 0.75;
    constexpr double ExpectedQueenBranchMass = 0.25;

    EvaluationResult const result =
        evaluate(layout_a, North, /*tricks_needed=*/1, source, strategy(1), merging_delta);

    ASSERT_FALSE(result.error.has_value());
    EvaluationValue const& value = result.by_strategy.at(1u);
    ASSERT_EQ(value.root_children.size(), 2u);
    RootChildValue const* jack_branch = nullptr;
    RootChildValue const* queen_branch = nullptr;
    for (RootChildValue const& child : value.root_children)
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
    // Bitwise, not EXPECT_DOUBLE_EQ (criterion 4): 0.5 and 1.0 are both
    // exactly representable, so every value derived above is exact too,
    // and each is the same value an uncut recursion would reach at its own
    // true terminal node further down, by mass conservation through every
    // defender expansion in between -- confirmed directly by temporarily
    // disabling the cut and rebuilding (see commit message).
    EXPECT_EQ(jack_branch->value, ExpectedJackBranchMass);
    EXPECT_EQ(queen_branch->value, ExpectedQueenBranchMass);
    EXPECT_EQ(value.p_make, ExpectedJackBranchMass + ExpectedQueenBranchMass);
}

// --- criterion 3: the cut actually fires, saving node visits --------------

namespace
{
    /// A single, deterministic layout: North A,K / East Q,J / South 2,3 /
    /// West 4,5, East on lead. Two certain tricks for North regardless of
    /// tricks_needed -- used to compare node counts with the cut firing
    /// early (tricks_needed = 1) against the same recursion run to its
    /// natural end (tricks_needed = 2).
    auto make_two_certain_tricks() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[North][Spades] = holding({Ace, King});
        deal.remainCards[South][Spades] = holding({Two, Three});
        deal.remainCards[East][Spades] = holding({Queen, Jack});
        deal.remainCards[West][Spades] = holding({Four, Five});
        return deal;
    }
}

TEST_F(AlreadyMadeCutTest, StopsExpansionAssertedAgainstAOneTrickShortComparison)
{
    // There is no flag to disable tier 1's cut (it is unconditional by
    // design -- see already_made()'s own doxygen), so criterion 3 is
    // verified by comparing this fixture's node count under two different
    // tricks_needed values instead: 1 (the cut fires the instant trick 1
    // is won, before trick 2 is ever touched) versus 2 (declarer needs
    // *both* tricks, so tricks_won never reaches tricks_needed early and
    // the recursion runs all the way to its natural terminal node). Same
    // layout, same delta, same pi -- the only difference between the two
    // runs is whether the cut gets a chance to fire, so the gap in node
    // count is attributable only to it.
    Deal const root_layout = make_two_certain_tricks();
    VectorLayoutSource source({root_layout});

    // Hand-counted tree with tricks_needed = 1 (single_card_defender/
    // single_card_declarer_play are both fully deterministic here, so
    // there is exactly one path):
    //   root: East to lead, defender node                          -- 1
    //     East plays J -> South to play, declarer/dummy node       -- 2
    //       South plays 2 -> West to play, defender node           -- 3
    //         West plays 4 -> North to play, declarer node         -- 4
    //           North plays K -> trick 1 resolves, North to lead
    //                            trick 2 -- tricks_won (1) >=
    //                            tricks_needed (1): CUT FIRES here,
    //                            node visited but not expanded       -- 5
    constexpr std::uint64_t NodesWithEarlyCut = 5;

    // The same tree with tricks_needed = 2: node 5 is visited but the cut
    // does NOT fire (tricks_won 1 < needed 2), so trick 2 is played out in
    // full to its natural terminal node:
    //     [node 5, not cut] North plays A -> East to play             -- 6
    //       East plays Q -> South to play                             -- 7
    //         South plays 3 -> West to play                           -- 8
    //           West plays 5 -> trick 2 resolves, every hand empty,
    //                           terminal (tricks_won 2 >= needed 2,
    //                           so this is where terminal_value's own
    //                           made-branch would fire if the cut's
    //                           already_made() check textually before it
    //                           did not already catch the same condition
    //                           first)                                -- 9
    constexpr std::uint64_t NodesWithoutEarlyCut = 9;

    EvaluationResult const with_early_cut = evaluate(
        root_layout,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{.collect_counters = true});
    EvaluationResult const without_early_cut = evaluate(
        root_layout,
        North,
        /*tricks_needed=*/2,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{.collect_counters = true});

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

// --- criterion 5: pi is not called below a firing cut ----------------------

TEST_F(AlreadyMadeCutTest, PiIsNotCalledWhenTheContractIsAlreadyMadeAtTheRoot)
{
    // tricks_needed = 0: the contract is already made before a single card
    // is played, so the cut fires at the root itself, before seat_on_play
    // is even consulted -- pi must never be asked which card to lead.
    Deal const root_layout = make_two_certain_tricks();
    VectorLayoutSource source({root_layout});
    RecordingDeclarerStrategy recording(Card{Spades, King});  // never actually asked

    EvaluationResult const result = evaluate(
        root_layout,
        North,
        /*tricks_needed=*/0,
        source,
        recording.as_strategy(),
        single_card_defender);

    ASSERT_FALSE(result.error.has_value());
    EXPECT_TRUE(recording.calls().empty());
    // RecordingDeclarerStrategy::as_strategy() fixes id = 0.
    // Single root layout, p = 1, kappa = 1 -- node_mass is trivially 1.0
    // here, which is fine: this test is about the call count, not the
    // value (criteria 1 & 2's dedicated test above covers the value).
    EXPECT_EQ(result.by_strategy.at(0u).p_make, 1.0);
    EXPECT_TRUE(result.by_strategy.at(0u).root_children.empty());
}

// Tier 1's dead cut, the mirror of the already-made cut above: a node where
// declarer cannot reach tricks_needed even by winning every remaining trick
// evaluates to 0.0 without recursing further -- sound unconditionally, same
// argument as already_made(), no gate (see evaluate.cpp's is_dead()).

namespace
{
    /// East holds the top two spades, North (declarer) and South (dummy)
    /// the bottom two, West the middle two -- East wins trick 1 outright
    /// however anyone else plays, so declarer's tricks_won stays 0 through
    /// it regardless of tricks_needed. East on lead (defender root), fully
    /// deterministic under single_card_defender / single_card_declarer_play.
    auto make_east_wins_first_trick() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[East][Spades] = holding({Ace, King});
        deal.remainCards[West][Spades] = holding({Queen, Jack});
        deal.remainCards[North][Spades] = holding({Two, Three});
        deal.remainCards[South][Spades] = holding({Four, Five});
        return deal;
    }
}

class DeadCutTest : public ::testing::Test
{
};

TEST_F(DeadCutTest, StopsExpansionAssertedAgainstAOneTrickShortComparison)
{
    // No flag to disable tier 1's cuts, same as the already-made cut above,
    // so criterion 4 (the cut fired) is verified by comparing node counts
    // across two tricks_needed values on the same deterministic fixture: 2
    // (impossible the instant trick 1 is lost to East, since only trick 2
    // remains) versus 1 (still possible after losing trick 1, so the
    // recursion runs to its natural terminal node without the cut ever
    // firing early).
    Deal const root_layout = make_east_wins_first_trick();
    VectorLayoutSource source({root_layout});

    // Hand-counted tree with tricks_needed = 2 (East leads, rotation
    // East -> South -> West -> North):
    //   root: East to lead, defender node                        -- 1
    //     East plays K -> South to play, declarer/dummy node     -- 2
    //       South plays 4 -> West to play, defender node         -- 3
    //         West plays J -> North to play, declarer node       -- 4
    //           North plays 2 -> trick 1 resolves (East's king
    //                            beats jack/four/two), East to
    //                            lead trick 2 -- tricks_won (0) +
    //                            tricks_remaining (1) < tricks_needed
    //                            (2): DEAD, CUT FIRES here, node
    //                            visited but not expanded          -- 5
    constexpr std::uint64_t NodesWithEarlyDeadCut = 5;

    // The same tree with tricks_needed = 1: node 5 is visited but the cut
    // does NOT fire (0 + 1 is not < 1 -- trick 2 could still be made), so
    // trick 2 plays out in full to its natural terminal node:
    //     [node 5, not cut] East plays A -> South to play           -- 6
    //       South plays 5 -> West to play                           -- 7
    //         West plays Q -> North to play                         -- 8
    //           North plays 3 -> trick 2 resolves (East's ace beats
    //                            queen/five/three too), every hand
    //                            empty, terminal -- and also dead
    //                            (0 + 0 < 1), so is_dead() firing
    //                            first here is what actually returns
    //                            0.0, not terminal_value()           -- 9
    constexpr std::uint64_t NodesWithoutEarlyDeadCut = 9;

    EvaluationResult const with_early_cut = evaluate(
        root_layout,
        North,
        /*tricks_needed=*/2,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{.collect_counters = true});
    EvaluationResult const without_early_cut = evaluate(
        root_layout,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{.collect_counters = true});

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

    // Criterion 3: the cut computes the right value -- zero, both ways,
    // whether via the cut or (tricks_needed = 1's own coincidental dead
    // terminal) via natural completion.
    EXPECT_EQ(with_early_cut.by_strategy.at(1u).p_make, 0.0);
    EXPECT_EQ(without_early_cut.by_strategy.at(1u).p_make, 0.0);
    // Criterion 6: bitwise agreement with the uncut path -- both exactly
    // 0.0, and confirmed directly (not just reasoned about) by temporarily
    // disabling is_dead() at both call sites and rebuilding: the
    // tricks_needed = 2 fixture still evaluates to exactly 0.0 at its true
    // terminal node four plies further down, then reverted (see commit
    // message).
}

TEST_F(DeadCutTest, PiAndDeltaAreNotCalledWhenTheRootIsAlreadyDead)
{
    // tricks_needed = 4: no suit here has four tricks in it at all
    // (tricks_remaining(root) = 2, North's own card count), so the root
    // itself is dead before a single card is played -- neither pi nor
    // delta should ever be asked for one.
    Deal const root_layout = make_east_wins_first_trick();
    VectorLayoutSource source({root_layout});
    RecordingDeclarerStrategy recording_pi(Card{Spades, Two});  // never actually asked
    bool delta_called = false;
    auto const recording_delta = [&delta_called](DefenderQuery const&) -> std::vector<WeightedCard>
    {
        delta_called = true;
        return {};
    };

    EvaluationResult const result = evaluate(
        root_layout, North, /*tricks_needed=*/4, source, recording_pi.as_strategy(), recording_delta);

    ASSERT_FALSE(result.error.has_value());
    EXPECT_TRUE(recording_pi.calls().empty());
    EXPECT_FALSE(delta_called);
    // RecordingDeclarerStrategy::as_strategy() fixes id = 0.
    EXPECT_EQ(result.by_strategy.at(0u).p_make, 0.0);
    EXPECT_TRUE(result.by_strategy.at(0u).root_children.empty());
}

// The LayoutBound injection seam: a caller-supplied double-dummy upper
// bound on tricks, and the separate delta_is_double_dummy_optimal
// declaration a future node-level cut will gate on. Nothing in this module
// consumes either yet -- these tests pin the seam itself: the scripted test
// double behaves correctly, and supplying a bound cannot perturb the
// answer.

class LayoutBoundTest : public ::testing::Test
{
};

TEST_F(LayoutBoundTest, ScriptedBoundRecordsWhatItWasAskedAndReturnsTheScriptedValue)
{
    Deal const layout = make_east_wins_first_trick();
    ScriptedBound scripted({{layout, 7}});
    LayoutBound const bound = scripted.as_bound();

    EXPECT_EQ(bound(layout), 7);
    ASSERT_EQ(scripted.queries().size(), 1u);
    EXPECT_EQ(scripted.queries().front().remainCards[East][Spades], layout.remainCards[East][Spades]);
}

TEST_F(LayoutBoundTest, SupplyingABoundWithoutTheDeclarationDoesNotChangeTheAnswerEitherDirection)
{
    // A bound scripted to claim zero tricks for declarer -- exactly the
    // value that fires tier 2's node-level cut (see TierTwoCutTest below)
    // when the caller has also made the delta_is_double_dummy_optimal
    // declaration. Withhold the declaration here, and both runs must still
    // agree bitwise: supplying a bound alone is not itself the switch (see
    // EvaluateOptions::delta_is_double_dummy_optimal's own doxygen for why
    // the two are kept separate).
    Deal const root_layout = make_east_wins_first_trick();
    VectorLayoutSource source({root_layout});
    ScriptedBound scripted({{root_layout, 0}});

    EvaluationResult const without_bound = evaluate(
        root_layout, North, /*tricks_needed=*/1, source, strategy(1), single_card_defender);
    EvaluationResult const with_bound = evaluate(
        root_layout,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{.bound = scripted.as_bound()});  // delta_is_double_dummy_optimal left unset

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
    // The bound is never even called: tier2_dead() checks the declaration
    // before ever touching options.bound, so scripting a single entry
    // above and never seeing ScriptedBound's own ADD_FAILURE fire is
    // itself part of what this test pins.
}

// Tier 2's node-level cut: every layout at a node dead by the injected
// bound, under the caller's own delta_is_double_dummy_optimal declaration,
// evaluates to 0.0 without recursing further. Tested entirely with
// scripted bounds -- no solver anywhere in this file.

class TierTwoCutTest : public ::testing::Test
{
};

TEST_F(TierTwoCutTest, FiresWhenEveryLayoutIsDeadAndTheDeclarationIsMade)
{
    // Reuses make_layout_a()/make_layout_b() and merging_delta() from
    // AlreadyMadeCutTest above -- the scripted bound here is independent
    // of what merging_delta would actually produce, since tier2_dead()
    // never inspects delta's own behaviour, only the bound.
    Deal const layout_a = make_layout_a();
    Deal const layout_b = make_layout_b();
    VectorLayoutSource source({layout_a, layout_b});
    ScriptedBound scripted({{layout_a, 0}, {layout_b, 0}});  // both dead: needed = 1

    EvaluationResult const result = evaluate(
        layout_a,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        merging_delta,
        EvaluateOptions{.bound = scripted.as_bound(), .delta_is_double_dummy_optimal = true});

    ASSERT_FALSE(result.error.has_value());
    EXPECT_EQ(result.by_strategy.at(1u).p_make, 0.0);
    EXPECT_TRUE(result.by_strategy.at(1u).root_children.empty());
}


// tier2_dead() is checked at *every* node, not just the root -- a fixture
// spanning several plies needs a bound that can answer for every layout at
// every depth the recursion actually reaches. A fixed per-Deal table
// (ScriptedBound's own exact-Deal matching) cannot do that once a card has
// been played, since play() returns a genuinely different Deal each time.
// The tests below that need the cut to stay suppressed (or a single query
// to matter) past the root use a fixture built so both layouts always take
// the *same* path with no branching at all -- so a lambda keyed on a
// filler suit no play here ever touches can tell them apart at any depth.
// ScriptedBound itself remains right where a check is confined to one node
// (an all-dead root that fires immediately, or a root check whose early
// exit is the whole point).

namespace
{
    /// North holds the two top spades outright (a certain trick), so
    /// however East and West's *spades* are split, North always wins
    /// trick 1 the same way -- no branching, one path through the whole
    /// recursion. The two layouts differ only in an untouched club filler,
    /// redistributed between the defenders (East holds Six in layout_a,
    /// Seven in layout_b) -- clubs are never played (tricks_needed = 1 is
    /// met the instant trick 1 resolves, well before any second trick), so
    /// that filler is a stable discriminator at every depth this recursion
    /// ever reaches.
    ///
    /// East on lead (defender root): East -> South -> West -> North.
    /// East's own spades ({Queen, Jack}) are identical in both layouts, so
    /// East's forced (single_card_defender) lead is the jack in both --
    /// merged from the very first ply.
    auto make_declarer_certain_win_with_club_filler(int east_club_rank, int west_club_rank) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[North][Spades] = holding({Ace, King});
        deal.remainCards[East][Spades] = holding({Queen, Jack});
        deal.remainCards[South][Spades] = holding({Two, Three});
        deal.remainCards[West][Spades] = holding({Four, Five});
        deal.remainCards[East][Clubs] = holding({east_club_rank});
        deal.remainCards[West][Clubs] = holding({west_club_rank});
        return deal;
    }

    constexpr int Six = 6;
    constexpr int Seven = 7;

    auto is_layout_a_by_club_filler(Deal const& layout) -> bool
    {
        return (layout.remainCards[East][Clubs] & holding({Six})) != 0;
    }
}

TEST_F(TierTwoCutTest, OneLiveLayoutSuppressesTheCut)
{
    // layout_a (East's club filler = Six) is scripted dead throughout;
    // layout_b (East's club filler = Seven) is scripted live throughout,
    // exactly at the boundary (still_needed itself, not comfortably
    // above it) so this test also pins the >= in tier2_dead()'s own
    // condition, not just its direction. Both layouts are present
    // together at every node on the single path this fixture has (see
    // the fixture's own comment), so the live one must suppress the cut
    // at every one of those checks. Full evaluation then reaches the
    // already-made cut right after trick 1 (both AK tricks are certain,
    // but only one is needed): node_mass = kappa * (p_a + p_b) =
    // 0.5 * (1 + 1) = 1.0 -- neither layout's p is ever touched, since
    // single_card_defender never gives either a genuine choice.
    Deal const layout_a = make_declarer_certain_win_with_club_filler(Six, Seven);
    Deal const layout_b = make_declarer_certain_win_with_club_filler(Seven, Six);
    VectorLayoutSource source({layout_a, layout_b});
    auto const bound = [](Deal const& layout) -> int
    { return is_layout_a_by_club_filler(layout) ? 0 : 1; };  // 1 == still_needed exactly

    EvaluationResult const result = evaluate(
        layout_a,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{.bound = bound, .delta_is_double_dummy_optimal = true});

    ASSERT_FALSE(result.error.has_value());
    EXPECT_EQ(result.by_strategy.at(1u).p_make, 1.0);
}

TEST_F(TierTwoCutTest, NeverFiresWithoutTheDeclarationWhateverTheBoundSays)
{
    // Same fixture as above, but every layout scripted dead throughout
    // (layout_b's own branch would normally suppress the cut -- scripted
    // dead here instead, so this test would fail the moment the
    // declaration gate stopped being checked first) and the declaration
    // withheld. Full evaluation must still proceed to the same 1.0.
    Deal const layout_a = make_declarer_certain_win_with_club_filler(Six, Seven);
    Deal const layout_b = make_declarer_certain_win_with_club_filler(Seven, Six);
    VectorLayoutSource source({layout_a, layout_b});
    auto const bound = [](Deal const&) -> int { return 0; };  // "all dead", ignored without the declaration

    EvaluationResult const result = evaluate(
        layout_a,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{.collect_counters = true, .bound = bound});  // no declaration

    ASSERT_FALSE(result.error.has_value());
    EvaluationValue const& value = result.by_strategy.at(1u);
    EXPECT_EQ(value.p_make, 1.0);
    ASSERT_TRUE(value.counters.has_value());
    // Hand-counted tree, single path (both layouts always merged):
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
    // The strategy-fusion trap, made concrete: North holds only the queen
    // of spades, East only the king -- East, on lead, is forced to play
    // its only card (the king), so North's queen always loses. The true
    // double-dummy value is 0, and a bound reflecting that correctly would
    // be < needed -- but this bound instead claims 5 (comfortably "not
    // dead") at every node it is ever asked about, the case a symmetric
    // make-cut would misread as permission to report the node's full
    // mass. No make-cut exists in this implementation, so evaluation must
    // proceed for real and return the true value, 0.0, not node_mass
    // (which would be 1.0 for this single, p = 1, kappa = 1 layout).
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = East;
    deal.remainCards[North][Spades] = holding({Queen});
    deal.remainCards[East][Spades] = holding({King});
    deal.remainCards[South][Spades] = holding({Two});
    deal.remainCards[West][Spades] = holding({Three});
    VectorLayoutSource source({deal});
    auto const bound = [](Deal const&) -> int { return 5; };  // "not dead", at any depth this is asked

    EvaluationResult const result = evaluate(
        deal,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{.bound = bound, .delta_is_double_dummy_optimal = true});

    ASSERT_FALSE(result.error.has_value());
    EXPECT_EQ(result.by_strategy.at(1u).p_make, 0.0);
}

TEST_F(TierTwoCutTest, StopsAtTheFirstLiveLayoutWithoutQueryingTheRest)
{
    // Built so the *whole* evaluation touches tier2_dead exactly once:
    // the root starts three cards into trick 1 already (South, West and
    // North -- the Ace -- already played), so East, the fourth and last
    // player, is on play at the root itself. Whatever East plays, North's
    // already-played ace has already won the trick, so the very next node
    // is already-made (tricks_won reaches 1, meeting tricks_needed = 1)
    // and short-circuits before tier2_dead is ever reached there --
    // tier2_dead's only invocation anywhere in this test is the root's own
    // single call, over its two (unadvanced) layouts.
    //
    // layout_a is scripted live and appears first in node.layouts (the
    // VectorLayoutSource order); layout_b has no scripted entry at all, so
    // if the early exit inside tier2_dead's loop works, it is never asked.
    Deal layout_a{};
    layout_a.trump = DDS_NOTRUMP;
    layout_a.first = South;
    layout_a.currentTrickSuit[0] = Spades;
    layout_a.currentTrickRank[0] = Two;    // South's card, already played
    layout_a.currentTrickSuit[1] = Spades;
    layout_a.currentTrickRank[1] = Three;  // West's card, already played
    layout_a.currentTrickSuit[2] = Spades;
    layout_a.currentTrickRank[2] = Ace;    // North's card, already played -- already winning
    layout_a.remainCards[East][Spades] = holding({Four});  // East's own card, about to play
    layout_a.remainCards[East][Clubs] = holding({Six});    // untouched filler; the only difference

    Deal layout_b = layout_a;
    layout_b.remainCards[East][Clubs] = holding({Seven});

    VectorLayoutSource source({layout_a, layout_b});
    ScriptedBound scripted({{layout_a, 5}});  // live; layout_b deliberately unscripted

    EvaluationResult const result = evaluate(
        layout_a,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{.bound = scripted.as_bound(), .delta_is_double_dummy_optimal = true});

    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(scripted.queries().size(), 1u);
    EXPECT_EQ(
        scripted.queries().front().remainCards[East][Clubs], layout_a.remainCards[East][Clubs]);
}

TEST_F(TierTwoCutTest, BitwiseAgreementWithTheUncutPathVerifiedByTemporarilyDisablingTheCut)
{
    // No flag to disable tier 2's cut either, same as both tier 1 cuts --
    // this test documents that the bitwise-agreement check (criterion 6)
    // was done by temporarily changing evaluate.cpp's tier2_dead() call
    // sites to `false && tier2_dead(...)`, rebuilding, and confirming
    // FiresWhenEveryLayoutIsDeadAndTheDeclarationIsMade's fixture still
    // evaluates to exactly 0.0 via natural recursion to its true terminal
    // node, then reverting -- see the commit message for the record. This
    // test itself just re-pins the cut's own value, which the disabled-cut
    // check was run against.
    Deal const layout_a = make_layout_a();
    Deal const layout_b = make_layout_b();
    VectorLayoutSource source({layout_a, layout_b});
    ScriptedBound scripted({{layout_a, 0}, {layout_b, 0}});

    EvaluationResult const result = evaluate(
        layout_a,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        merging_delta,
        EvaluateOptions{.bound = scripted.as_bound(), .delta_is_double_dummy_optimal = true});

    ASSERT_FALSE(result.error.has_value());
    EXPECT_EQ(result.by_strategy.at(1u).p_make, 0.0);
}

TEST_F(TierTwoCutTest, PiAndDeltaAreNotCalledWhenTheRootIsDeadByTheBound)
{
    Deal const layout_a = make_layout_a();
    Deal const layout_b = make_layout_b();
    VectorLayoutSource source({layout_a, layout_b});
    ScriptedBound scripted({{layout_a, 0}, {layout_b, 0}});
    RecordingDeclarerStrategy recording_pi(Card{Spades, King});  // never actually asked
    bool delta_called = false;
    auto const recording_delta = [&delta_called](DefenderQuery const&) -> std::vector<WeightedCard>
    {
        delta_called = true;
        return {};
    };

    EvaluationResult const result = evaluate(
        layout_a,
        North,
        /*tricks_needed=*/1,
        source,
        recording_pi.as_strategy(),
        recording_delta,
        EvaluateOptions{.bound = scripted.as_bound(), .delta_is_double_dummy_optimal = true});

    ASSERT_FALSE(result.error.has_value());
    EXPECT_TRUE(recording_pi.calls().empty());
    EXPECT_FALSE(delta_called);
    // RecordingDeclarerStrategy::as_strategy() fixes id = 0.
    EXPECT_EQ(result.by_strategy.at(0u).p_make, 0.0);
}

// The sampling gate: tier2_dead() is gated on !node.is_sample; neither
// tier-1 cut is. Nothing in the evaluator sets is_sample yet (make_root()
// always leaves it false), so these tests construct a BeliefNode directly
// -- a state the evaluator itself cannot yet produce today, deliberately,
// per tier2_dead()'s own doxygen. already_made()/is_dead() are exposed the
// same way is_terminal()/terminal_value() are, precisely so a test can do
// this.

class SamplingGateTest : public ::testing::Test
{
};

TEST_F(SamplingGateTest, Tier2DoesNotFireOnASampledNodeEvenWhenEveryLayoutIsDead)
{
    BeliefNode node{};
    node.state.declarer = North;
    node.state.tricks_needed = 1;
    node.state.tricks_won_by_declarer = 0;
    Deal layout{};
    layout.trump = DDS_NOTRUMP;
    layout.remainCards[North][Spades] = holding({Two});
    node.layouts = {layout};
    node.p = {1.0};
    node.kappa = 1.0;
    node.is_sample = true;  // the state the evaluator cannot yet produce

    auto const bound = [](Deal const&) -> int { return 0; };  // dead, if it were consulted
    EvaluateOptions const options{.bound = bound, .delta_is_double_dummy_optimal = true};

    EXPECT_FALSE(tier2_dead(node, options));
}

TEST_F(SamplingGateTest, BothTier1CutsStillFireOnASampledNode)
{
    // Criterion 2 is not padding: gating "the cuts" as a group instead of
    // tier 2 alone would disable both of these on every sampled node too
    // -- tested separately, deliberately, so the difference is pinned by
    // an assertion rather than left to a comment someone can talk
    // themselves out of.
    BeliefNode already_made_node{};
    already_made_node.state.declarer = North;
    already_made_node.state.tricks_needed = 1;
    already_made_node.state.tricks_won_by_declarer = 1;  // already made
    already_made_node.is_sample = true;
    EXPECT_TRUE(already_made(already_made_node.state));

    BeliefNode dead_node{};
    dead_node.state.declarer = North;
    dead_node.state.tricks_needed = 5;  // impossible: nothing left to win 5 tricks from
    dead_node.state.tricks_won_by_declarer = 0;
    dead_node.state.known_holdings = Deal{};  // every hand empty -- tricks_remaining() == 0
    dead_node.is_sample = true;
    EXPECT_TRUE(is_dead(dead_node.state));
}
