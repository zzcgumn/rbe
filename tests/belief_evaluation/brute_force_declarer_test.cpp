#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>
#include <api/solve_board.hpp>
#include <solver_context/solver_context.hpp>

#include <belief_evaluation/brute_force_declarer.hpp>
#include <belief_evaluation/double_dummy_bound.hpp>
#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/rank_map.hpp>
#include <belief_evaluation/trick.hpp>
#include <belief_evaluation/types.hpp>

#include "test_support.hpp"

namespace be = dds::belief_evaluation;

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
    constexpr int Jack = 11;
    constexpr int King = 13;
    constexpr int Ace = 14;

    constexpr int Spades = 0;
    constexpr int Hearts = 1;
    constexpr int Diamonds = 2;
    constexpr int Clubs = 3;

    constexpr int North = 0;  // declarer
    constexpr int East = 1;
    constexpr int South = 2;  // dummy
    constexpr int West = 3;

    // North (declarer) on lead with one legal card. Enough to show play()
    // returns a legal card and state_key is wired up, with no search.
    auto make_one_card_root() -> be::ObservationState
    {
        Deal layout{};
        layout.trump = DDS_NOTRUMP;
        layout.first = North;
        layout.remainCards[North][Spades] = be::holding({Nine});
        layout.remainCards[South][Spades] = be::holding({Eight});
        layout.remainCards[East][Spades] = be::holding({2});
        layout.remainCards[West][Spades] = be::holding({3});

        be::ObservationState state{};
        state.trump = DDS_NOTRUMP;
        state.first = North;
        state.declarer = North;
        state.tricks_needed = 1;
        state.tricks_won_by_declarer = 0;
        state.known_holdings = layout;
        state.ranks = be::make_rank_map(layout);
        return state;
    }

    auto make_entries(std::vector<Deal> const& layouts, std::vector<be::Probability> const& p)
        -> std::vector<be::BeliefEntry>
    {
        std::vector<be::BeliefEntry> entries;
        entries.reserve(layouts.size());
        for (std::size_t i = 0; i < layouts.size(); ++i)
        {
            entries.push_back(be::BeliefEntry{layouts[i], p[i]});
        }
        return entries;
    }
}

class BruteForceDeclarerTest : public ::testing::Test
{
};

TEST_F(BruteForceDeclarerTest, ConstructionWithNoOpponentModelDoesNotThrow)
{
    SolverContext ctx;
    EXPECT_NO_THROW(be::BruteForceDeclarer declarer(ctx));
}

TEST_F(BruteForceDeclarerTest, ConstructionAcceptsACallerSuppliedOpponentModel)
{
    be::DefenderStrategy const spy = [](be::DefenderQuery const&) -> std::vector<be::WeightedCard>
    {
        return {{be::Card{0, 2}, 1.0}};
    };
    SolverContext ctx;
    EXPECT_NO_THROW(
        be::BruteForceDeclarer declarer(ctx, be::DeclarerObjective::MaximizeExpectedTricks, spy));
}

TEST_F(BruteForceDeclarerTest, ConstructionRejectsZeroMaxLayouts)
{
    // max_layouts = 0 would keep no layouts: every pruned node would be
    // empty, and layouts.front() in make_brute_force_cache_key would read
    // past the end. It is rejected at construction, as make_root() rejects
    // sample_size == 0 (node.cpp).
    SolverContext ctx;
    be::BruteForceOptions options{};
    options.max_layouts = 0;
    EXPECT_THROW(
        be::BruteForceDeclarer declarer(ctx, be::DeclarerObjective::MaximizeExpectedTricks, nullptr, options),
        std::invalid_argument);
}

TEST_F(BruteForceDeclarerTest, ConstructionRejectsNegativeMaxDepth)
{
    // max_depth counts plies from 1. A negative value is not a smaller
    // budget: check_leaf's `depth >= *max_depth` fires on the first node,
    // silently cutting off at once instead of reporting the mistake.
    SolverContext ctx;
    be::BruteForceOptions options{};
    options.max_depth = -1;
    EXPECT_THROW(
        be::BruteForceDeclarer declarer(ctx, be::DeclarerObjective::MaximizeExpectedTricks, nullptr, options),
        std::invalid_argument);
}

TEST_F(BruteForceDeclarerTest, IsNeitherCopyableNorMovable)
{
    // The default opponent model's lambda (DoubleDummyDefender::as_strategy())
    // captures the address of this instance's `default_opponent_`. A copy or
    // move would leave `opponent_model_` bound to the original's address.
    // Checked at compile time, because there is no runtime symptom to test:
    // the callback silently calls the wrong, or freed, object.
    static_assert(! std::is_copy_constructible_v<be::BruteForceDeclarer>);
    static_assert(! std::is_copy_assignable_v<be::BruteForceDeclarer>);
    static_assert(! std::is_move_constructible_v<be::BruteForceDeclarer>);
    static_assert(! std::is_move_assignable_v<be::BruteForceDeclarer>);
}

TEST_F(BruteForceDeclarerTest, ReusingOneInstanceForASecondDeclarerSeatThrows)
{
    // The class requires one declarer seat for its lifetime. This is the
    // runtime check, not an assert(), which --config=opt compiles out (NDEBUG).
    // Without it, the first declarer's DoubleDummyBound would silently give
    // wrong cutoff values for the second.
    SolverContext ctx;
    be::BruteForceDeclarer declarer(ctx);
    be::DeclarerStrategy const pi = declarer.as_strategy();

    // Named so it outlives the pi.play() call. BeliefEntry::layout is a
    // Deal const&, so a braced temporary passed to make_entries() would
    // dangle once make_entries() returns.
    be::ObservationState const first_root = make_one_card_root();  // declarer = North
    std::vector<Deal> const first_layouts = {first_root.known_holdings};
    std::vector<be::BeliefEntry> const first_entries = make_entries(first_layouts, {1.0});
    pi.play(first_root, be::BeliefView{first_entries, false, first_entries.size()});

    // A second root for a different declarer seat: East and West are declarer
    // and dummy, North and South the defenders.
    Deal layout{};
    layout.trump = DDS_NOTRUMP;
    layout.first = East;
    layout.remainCards[East][Spades] = be::holding({Nine});
    layout.remainCards[West][Spades] = be::holding({Eight});
    layout.remainCards[North][Spades] = be::holding({Two});
    layout.remainCards[South][Spades] = be::holding({Three});

    be::ObservationState second_root{};
    second_root.trump = DDS_NOTRUMP;
    second_root.first = East;
    second_root.declarer = East;
    second_root.tricks_needed = 1;
    second_root.tricks_won_by_declarer = 0;
    second_root.known_holdings = layout;
    second_root.ranks = be::make_rank_map(layout);

    std::vector<Deal> const second_layouts = {layout};
    std::vector<be::BeliefEntry> const second_entries = make_entries(second_layouts, {1.0});
    EXPECT_THROW(
        pi.play(second_root, be::BeliefView{second_entries, false, second_entries.size()}),
        std::logic_error);
}

TEST_F(BruteForceDeclarerTest, PlayReturnsALegalCard)
{
    be::ObservationState const state = make_one_card_root();
    std::vector<Deal> const layouts = {state.known_holdings};
    std::vector<be::Probability> const p = {1.0};
    std::vector<be::BeliefEntry> const entries = make_entries(layouts, p);

    SolverContext ctx;
    be::BruteForceDeclarer declarer(ctx);
    be::DeclarerStrategy const pi = declarer.as_strategy();
    be::Card const card = pi.play(state, be::BeliefView{entries, false, entries.size()});

    auto const legal = be::enumerate_legal_cards(state.known_holdings, be::seat_on_play(state.known_holdings));
    EXPECT_NE(
        std::find_if(
            legal.begin(), legal.end(),
            [&](be::Card const& c) { return c.suit == card.suit && c.rank == card.rank; }),
        legal.end());
}

TEST_F(BruteForceDeclarerTest, StateKeyIsStableUnderPermutingTheBeliefSpace)
{
    be::ObservationState const state = make_one_card_root();
    Deal b = state.known_holdings;
    b.remainCards[East][Spades] = be::holding({3});
    b.remainCards[West][Spades] = be::holding({2});

    std::vector<Deal> const layouts = {state.known_holdings, b};
    std::vector<be::Probability> const p = {0.4, 0.6};
    std::vector<be::BeliefEntry> const entries = make_entries(layouts, p);

    std::vector<Deal> const permuted_layouts = {b, state.known_holdings};
    std::vector<be::Probability> const permuted_p = {0.6, 0.4};
    std::vector<be::BeliefEntry> const permuted_entries = make_entries(permuted_layouts, permuted_p);

    SolverContext ctx;
    be::BruteForceDeclarer declarer(ctx);
    be::DeclarerStrategy const pi = declarer.as_strategy();

    be::StateKey const key_1 = pi.state_key(state, be::BeliefView{entries, false, entries.size()});
    be::StateKey const key_2 =
        pi.state_key(state, be::BeliefView{permuted_entries, false, permuted_entries.size()});

    EXPECT_EQ(key_1, key_2);
}

TEST_F(BruteForceDeclarerTest, StateKeyDiffersWhenPosteriorsDiffer)
{
    be::ObservationState const state = make_one_card_root();
    std::vector<Deal> const layouts = {state.known_holdings};

    std::vector<be::BeliefEntry> const entries_1 = make_entries(layouts, {0.5});
    std::vector<be::BeliefEntry> const entries_2 = make_entries(layouts, {0.9});

    SolverContext ctx;
    be::BruteForceDeclarer declarer(ctx);
    be::DeclarerStrategy const pi = declarer.as_strategy();

    be::StateKey const key_1 = pi.state_key(state, be::BeliefView{entries_1, false, entries_1.size()});
    be::StateKey const key_2 = pi.state_key(state, be::BeliefView{entries_2, false, entries_2.size()});

    EXPECT_NE(key_1, key_2);
}

// --- terminal and depth-cutoff leaves -------------------------------------

TEST_F(BruteForceDeclarerTest, TerminalLeafUsesMassDirectlyForProbabilityToMake)
{
    be::BeliefNode node{};
    node.p = {0.3, 0.7};
    node.kappa = 1.0;
    node.state.tricks_needed = 2;
    node.state.tricks_won_by_declarer = 2;  // made in every layout; common knowledge at a terminal node

    EXPECT_DOUBLE_EQ(
        be::terminal_leaf_value(node, be::DeclarerObjective::MaximizeProbabilityToMake), 1.0);
}

TEST_F(BruteForceDeclarerTest, TerminalLeafGivesTrickCountForExpectedTricks)
{
    be::BeliefNode node{};
    node.p = {0.3, 0.7};
    node.kappa = 1.0;
    node.state.tricks_won_by_declarer = 5;

    EXPECT_DOUBLE_EQ(
        be::terminal_leaf_value(node, be::DeclarerObjective::MaximizeExpectedTricks), 5.0);
}

TEST_F(BruteForceDeclarerTest, CutoffLeafUsesPerLayoutBoundsNotANodeWideNumber)
{
    // North (declarer) holds the King, Eight and Five of spades.
    // - deal_a: East alone holds the dangerous Nine and must spend it on
    //   North's King lead, going void before the Eight is at risk (as in
    //   UndercountsWhenTheShorterOpponentHoldsTheOnlyDangerousCard in
    //   suit_top_tricks_test.cpp).
    // - deal_b: West, the longer hand, holds the Nine and Two. West ducks the
    //   first round with the Two and keeps the Nine to beat the Eight.
    // North and South hold the same spades in both; only the defender split
    // differs. The bounds are read from the solver below, not asserted by
    // hand. They need only differ for this test to exercise per-layout
    // branching.
    //
    // solve_board returns a real score only when every hand holds the same
    // number of cards (see the filler-suit note in
    // double_dummy_bound_test.cpp). North's three spades set that count, so
    // South, East and West get filler clubs no one has a choice over, the
    // same in both deals. Any filler effect is then the same in both.
    Deal deal_a{};
    deal_a.trump = DDS_NOTRUMP;
    deal_a.first = North;
    deal_a.remainCards[North][Spades] = be::holding({King, Eight, Five});
    deal_a.remainCards[East][Spades] = be::holding({Nine});
    deal_a.remainCards[West][Spades] = be::holding({Two, Three});
    deal_a.remainCards[South][Clubs] = be::holding({Two, Three, Four});
    deal_a.remainCards[East][Clubs] = be::holding({Five, Six});
    deal_a.remainCards[West][Clubs] = be::holding({Seven});

    Deal deal_b{};
    deal_b.trump = DDS_NOTRUMP;
    deal_b.first = North;
    deal_b.remainCards[North][Spades] = be::holding({King, Eight, Five});
    deal_b.remainCards[East][Spades] = be::holding({Three});
    deal_b.remainCards[West][Spades] = be::holding({Nine, Two});
    deal_b.remainCards[South][Clubs] = be::holding({Two, Three, Four});
    deal_b.remainCards[East][Clubs] = be::holding({Five, Six});
    deal_b.remainCards[West][Clubs] = be::holding({Seven});

    SolverContext ctx;
    be::DoubleDummyBound bound(ctx, /*declarer=*/North);

    // Read from a direct solve_board call, not assumed from the comment
    // above.
    int const bound_a = bound.as_bound()(deal_a);
    int const bound_b = bound.as_bound()(deal_b);
    ASSERT_NE(bound_a, 14);  // not the solver-failure sentinel
    ASSERT_NE(bound_b, 14);
    ASSERT_NE(bound_a, bound_b)
        << "fixture must give the two layouts genuinely different bounds for this test to "
           "exercise per-layout branching at all";

    be::BeliefNode node{};
    node.layouts = {deal_a, deal_b};
    node.p = {0.4, 0.6};
    node.kappa = 1.0;
    node.state.declarer = North;
    node.state.tricks_won_by_declarer = 0;
    // The higher bound, so the layout with that bound makes and the other
    // does not. The fixture discriminates between the layouts whatever their
    // bounds are.
    node.state.tricks_needed = std::max(bound_a, bound_b);

    double const expected_expected_tricks = 0.4 * (0 + bound_a) + 0.6 * (0 + bound_b);
    EXPECT_DOUBLE_EQ(
        be::cutoff_leaf_value(node, bound.as_bound(), be::DeclarerObjective::MaximizeExpectedTricks),
        expected_expected_tricks);

    double const expected_p_make =
        0.4 * ((0 + bound_a) >= node.state.tricks_needed ? 1.0 : 0.0)
        + 0.6 * ((0 + bound_b) >= node.state.tricks_needed ? 1.0 : 0.0);
    EXPECT_DOUBLE_EQ(
        be::cutoff_leaf_value(node, bound.as_bound(), be::DeclarerObjective::MaximizeProbabilityToMake),
        expected_p_make);
}

// --- the search itself: branching, both sides -----------------------------
//
// A trick needs all four seats, so no realistic fixture reaches a
// declarer decision without a defender's turn soon after. The two
// branches are tested together.

TEST_F(BruteForceDeclarerTest, PicksTheCardThatWinsTheTrickOverTheOneThatLosesIt)
{
    // Every hand holds two cards throughout, as in a real ending: each trick
    // takes one card from each hand, and an unbalanced fixture leaves some
    // seat without a legal card. North (declarer) holds Jack, Two; South
    // (dummy) Three, Five; East King, Six; West Four, Seven.
    // single_card_defender plays each defender's lowest card first, so East's
    // King appears only after its Six.
    //
    // Leading the Jack: trick 1 is {Jack (North), Six (East, forced low),
    // dummy's low card, Four (West)}. The Jack wins and North leads again.
    // Trick 2 is {Two (North, forced), King (East, its last card), dummy's
    // other card, Seven (West)}. The King wins. One trick for declarer.
    //
    // Leading the Two: trick 1 is {Two (North), Six (East, forced low),
    // dummy's low card, Four (West)}. East's Six beats the Two, so East wins
    // and leads. Trick 2 is {King (East, its last card), Jack (North,
    // forced), dummy's other card, Seven (West)}. The King wins again. Zero
    // tricks for declarer.
    //
    // tricks_needed = 1: the Jack makes, the Two does not. Only a search of
    // both cards through two real tricks finds that.
    Deal layout{};
    layout.trump = DDS_NOTRUMP;
    layout.first = North;
    layout.remainCards[North][Spades] = be::holding({Jack, Two});
    layout.remainCards[South][Spades] = be::holding({Three, Five});
    layout.remainCards[East][Spades] = be::holding({King, Six});
    layout.remainCards[West][Spades] = be::holding({Four, Seven});

    be::ObservationState state{};
    state.trump = DDS_NOTRUMP;
    state.first = North;
    state.declarer = North;
    state.tricks_needed = 1;
    state.tricks_won_by_declarer = 0;
    state.known_holdings = layout;
    state.ranks = be::make_rank_map(layout);

    // Named so it outlives the pi.play() call. BeliefEntry::layout is a
    // Deal const&, so a braced temporary passed to make_entries() would
    // dangle once make_entries() returns.
    std::vector<Deal> const layouts = {layout};
    std::vector<be::BeliefEntry> const entries = make_entries(layouts, {1.0});

    SolverContext ctx;
    be::BruteForceDeclarer declarer(
        ctx, be::DeclarerObjective::MaximizeProbabilityToMake, be::single_card_defender);
    be::DeclarerStrategy const pi = declarer.as_strategy();

    be::Card const chosen = pi.play(state, be::BeliefView{entries, false, entries.size()});

    EXPECT_EQ(chosen.suit, Spades);
    EXPECT_EQ(chosen.rank, Jack);
}

TEST_F(BruteForceDeclarerTest, ACacheHitSkipsRecomputingASubtreeReachedByTransposition)
{
    // Three independent one-round suits. North holds the Ace in each, South
    // (dummy), East and West low cards. North's Ace always wins, so North
    // stays on lead and chooses at every trick which remaining suit to lead.
    //
    // The tree shares prefixes, so count nodes, not paths. Trick 1's East and
    // West queries happen once per root choice (3 each, 6 in total). Trick 2's
    // happen once per root choice and remaining suit, 3 * 2 = 6 times each
    // (12). Trick 3 is forced but still reached 6 times each (12). With no
    // cache: 6 + 12 + 12 = 30 opponent-model queries.
    //
    // Trick 3 has a real transposition. For each suit left last, the other
    // two were played in one of two orders, and both orders leave the same
    // position (the same holdings, layout and tricks_won_by_declarer), so the
    // same BruteForceCacheKey. There are three such shapes, each reached by
    // two of the six paths. The first visit computes and caches it (2
    // queries); the second is a cache hit (0). That saves 3 * 2 = 6 queries,
    // so the expected total is exactly 30 - 6 = 24.
    auto const make_suit_layout = [](int suit) -> Deal
    {
        Deal layout{};
        layout.trump = DDS_NOTRUMP;
        layout.first = North;
        layout.remainCards[North][suit] = be::holding({Ace});
        layout.remainCards[South][suit] = be::holding({Two});
        layout.remainCards[East][suit] = be::holding({Three});
        layout.remainCards[West][suit] = be::holding({Four});
        return layout;
    };

    Deal layout{};
    layout.trump = DDS_NOTRUMP;
    layout.first = North;
    for (int suit : {Spades, Hearts, Diamonds})
    {
        Deal const one_suit = make_suit_layout(suit);
        for (int hand = 0; hand < DDS_HANDS; ++hand)
        {
            layout.remainCards[hand][suit] = one_suit.remainCards[hand][suit];
        }
    }

    be::ObservationState state{};
    state.trump = DDS_NOTRUMP;
    state.first = North;
    state.declarer = North;
    state.tricks_needed = 3;
    state.tricks_won_by_declarer = 0;
    state.known_holdings = layout;
    state.ranks = be::make_rank_map(layout);

    // Named so it outlives the pi.play() call (see
    // PicksTheCardThatWinsTheTrickOverTheOneThatLosesIt).
    std::vector<Deal> const layouts = {layout};
    std::vector<be::BeliefEntry> const entries = make_entries(layouts, {1.0});

    int call_count = 0;
    be::DefenderStrategy const spy = [&](be::DefenderQuery const& query) -> std::vector<be::WeightedCard>
    {
        ++call_count;
        return be::single_card_defender(query);
    };

    SolverContext ctx;
    be::BruteForceDeclarer declarer(ctx, be::DeclarerObjective::MaximizeExpectedTricks, spy);
    be::DeclarerStrategy const pi = declarer.as_strategy();

    pi.play(state, be::BeliefView{entries, false, entries.size()});

    EXPECT_EQ(call_count, 24);
}

TEST_F(BruteForceDeclarerTest, AMalformedOpponentModelThrowsRatherThanMiscomputing)
{
    // East holds a card, so a real defender decision is reached, but the
    // scripted opponent model returns an empty distribution:
    // ValidationError::DistributionEmpty, which expand_defender_node detects
    // at evaluate()'s level. DeclarerStrategy::play has no error channel, so
    // it must throw. The test checks that it throws a specific type carrying
    // the real cause.
    Deal layout{};
    layout.trump = DDS_NOTRUMP;
    layout.first = North;
    layout.remainCards[North][Spades] = be::holding({Ace});
    layout.remainCards[South][Spades] = be::holding({Two});
    layout.remainCards[East][Spades] = be::holding({Three});
    layout.remainCards[West][Spades] = be::holding({Four});

    be::ObservationState state{};
    state.trump = DDS_NOTRUMP;
    state.first = North;
    state.declarer = North;
    state.tricks_needed = 1;
    state.tricks_won_by_declarer = 0;
    state.known_holdings = layout;
    state.ranks = be::make_rank_map(layout);

    std::vector<Deal> const layouts = {layout};
    std::vector<be::BeliefEntry> const entries = make_entries(layouts, {1.0});

    be::DefenderStrategy const broken = [](be::DefenderQuery const&) -> std::vector<be::WeightedCard>
    {
        return {};
    };

    SolverContext ctx;
    be::BruteForceDeclarer declarer(ctx, be::DeclarerObjective::MaximizeExpectedTricks, broken);
    be::DeclarerStrategy const pi = declarer.as_strategy();

    try
    {
        pi.play(state, be::BeliefView{entries, false, entries.size()});
        FAIL() << "expected BruteForceOpponentModelError";
    }
    catch (be::BruteForceOpponentModelError const& error)
    {
        EXPECT_EQ(error.validation, be::ValidationError::DistributionEmpty);
        EXPECT_EQ(error.offending_layout.remainCards[East][Spades], be::holding({Three}));
    }
}

TEST_F(BruteForceDeclarerTest, AMalformedInternalOpponentModelIsReportedNotThrownThroughEvaluate)
{
    // The same fixture and broken internal opponent model as
    // AMalformedOpponentModelThrowsRatherThanMiscomputing, but driven through
    // evaluate(). The spec says no callback contract violation may escape
    // evaluate() as an exception. expand_declarer_node catches
    // DeclarerStrategyContractViolation; without that catch the process would
    // terminate. The reported error is DefenderStrategy, East and East's
    // layout: the internal model's seat and layout, carried from the
    // exception. Reporting the root's DeclarerPlay, North and known_holdings
    // would misplace the violation.
    Deal layout{};
    layout.trump = DDS_NOTRUMP;
    layout.first = North;
    layout.remainCards[North][Spades] = be::holding({Ace});
    layout.remainCards[South][Spades] = be::holding({Two});
    layout.remainCards[East][Spades] = be::holding({Three});
    layout.remainCards[West][Spades] = be::holding({Four});

    be::DefenderStrategy const broken_internal_opponent_model =
        [](be::DefenderQuery const&) -> std::vector<be::WeightedCard>
    {
        return {};
    };

    SolverContext ctx;
    be::BruteForceDeclarer declarer(
        ctx, be::DeclarerObjective::MaximizeExpectedTricks, broken_internal_opponent_model);

    // The outer delta is an ordinary working one (single_card_defender). The
    // only broken callable is BruteForceDeclarer's internal opponent model,
    // reached once pi.play() runs for the root's declarer node.
    be::VectorLayoutSource const source({layout});
    be::EvaluationResult result{};
    EXPECT_NO_THROW(
        result = be::evaluate(
            layout, /*declarer=*/North, /*tricks_needed=*/1, source, declarer.as_strategy(),
            be::single_card_defender));

    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error->validation, be::ValidationError::DistributionEmpty);
    EXPECT_EQ(result.error->callback, be::EvaluationCallback::DefenderStrategy);
    EXPECT_EQ(result.error->seat, East);
    EXPECT_EQ(result.error->layout.remainCards[East][Spades], be::holding({Three}));
}

// --- max_layouts: the lowest-posterior-first cap --------------------------

TEST_F(BruteForceDeclarerTest, DropLowestPosteriorLayoutsKeepsTheHighestWeightedOnes)
{
    Deal a{};
    a.remainCards[North][Spades] = be::holding({Two});
    Deal b{};
    b.remainCards[North][Spades] = be::holding({Three});
    Deal c{};
    c.remainCards[North][Spades] = be::holding({Four});

    be::BeliefNode node{};
    node.layouts = {a, b, c};
    node.p = {0.1, 0.6, 0.3};
    node.root_keys = {10, 11, 12};
    node.kappa = 1.0;

    be::BeliefNode const pruned = be::drop_lowest_posterior_layouts(node, /*keep=*/2);

    ASSERT_EQ(pruned.layouts.size(), 2u);
    ASSERT_EQ(pruned.p.size(), 2u);
    ASSERT_EQ(pruned.root_keys.size(), 2u);
    // deal_b (p=0.6, root_key=11) and deal_c (p=0.3, root_key=12) survive;
    // deal_a (p=0.1, root_key=10) is dropped. Only membership is asserted,
    // through the parallel root_keys array, not order.
    bool const has_11 = (pruned.root_keys[0] == 11 || pruned.root_keys[1] == 11);
    bool const has_12 = (pruned.root_keys[0] == 12 || pruned.root_keys[1] == 12);
    EXPECT_TRUE(has_11);
    EXPECT_TRUE(has_12);
}

TEST_F(BruteForceDeclarerTest, DropLowestPosteriorLayoutsDoesNotRedistributeMass)
{
    Deal a{};
    Deal b{};
    Deal c{};

    be::BeliefNode node{};
    node.layouts = {a, b, c};
    node.p = {0.1, 0.6, 0.3};
    node.root_keys = {10, 11, 12};
    node.kappa = 1.0;

    be::BeliefNode const pruned = be::drop_lowest_posterior_layouts(node, /*keep=*/2);

    EXPECT_DOUBLE_EQ(node_mass(pruned), 0.9);  // 0.6 + 0.3, not renormalised to 1.0
}

TEST_F(BruteForceDeclarerTest, DropLowestPosteriorLayoutsIsANoOpWhenNothingExceedsKeep)
{
    Deal a{};
    Deal b{};

    be::BeliefNode node{};
    node.layouts = {a, b};
    node.p = {0.5, 0.5};
    node.root_keys = {1, 2};
    node.kappa = 1.0;

    be::BeliefNode const pruned = be::drop_lowest_posterior_layouts(node, /*keep=*/5);

    EXPECT_EQ(pruned.layouts.size(), 2u);
    EXPECT_DOUBLE_EQ(node_mass(pruned), 1.0);
}

TEST_F(BruteForceDeclarerTest, DropLowestPosteriorLayoutsBreaksTiesByContentNotInputOrder)
{
    // Two layouts tie on posterior. The survivor must be chosen by layout
    // content (layout_key), not position in node.layouts. Two belief spaces
    // that are permutations of each other, which the sorted state_key and
    // make_brute_force_cache_key already treat as identical, then keep the
    // same survivor.
    Deal deal_a{};
    deal_a.remainCards[North][Spades] = be::holding({Nine});
    deal_a.remainCards[South][Spades] = be::holding({Eight});
    deal_a.remainCards[East][Spades] = be::holding({Two});
    deal_a.remainCards[West][Spades] = be::holding({Three});

    Deal deal_b = deal_a;  // the same outstanding pool, the other defender split
    deal_b.remainCards[East][Spades] = be::holding({Three});
    deal_b.remainCards[West][Spades] = be::holding({Two});

    be::BeliefNode order_1{};
    order_1.layouts = {deal_a, deal_b};
    order_1.p = {0.5, 0.5};
    order_1.root_keys = {0, 1};
    order_1.kappa = 1.0;
    order_1.state.declarer = North;

    be::BeliefNode order_2{};
    order_2.layouts = {deal_b, deal_a};  // the same two layouts, reversed
    order_2.p = {0.5, 0.5};
    order_2.root_keys = {0, 1};
    order_2.kappa = 1.0;
    order_2.state.declarer = North;

    be::BeliefNode const pruned_1 = be::drop_lowest_posterior_layouts(order_1, /*keep=*/1);
    be::BeliefNode const pruned_2 = be::drop_lowest_posterior_layouts(order_2, /*keep=*/1);

    ASSERT_EQ(pruned_1.layouts.size(), 1u);
    ASSERT_EQ(pruned_2.layouts.size(), 1u);
    EXPECT_EQ(pruned_1.layouts[0].remainCards[East][Spades], pruned_2.layouts[0].remainCards[East][Spades]);
    EXPECT_EQ(pruned_1.layouts[0].remainCards[West][Spades], pruned_2.layouts[0].remainCards[West][Spades]);
}

TEST_F(BruteForceDeclarerTest, MaxLayoutsAbsentChangesNothing)
{
    // The fixture of PicksTheCardThatWinsTheTrickOverTheOneThatLosesIt with
    // max_layouts absent (the default): the unbounded path gives the same
    // result.
    Deal layout{};
    layout.trump = DDS_NOTRUMP;
    layout.first = North;
    layout.remainCards[North][Spades] = be::holding({Jack, Two});
    layout.remainCards[South][Spades] = be::holding({Three, Five});
    layout.remainCards[East][Spades] = be::holding({King, Six});
    layout.remainCards[West][Spades] = be::holding({Four, Seven});

    be::ObservationState state{};
    state.trump = DDS_NOTRUMP;
    state.first = North;
    state.declarer = North;
    state.tricks_needed = 1;
    state.tricks_won_by_declarer = 0;
    state.known_holdings = layout;
    state.ranks = be::make_rank_map(layout);

    std::vector<Deal> const layouts = {layout};
    std::vector<be::BeliefEntry> const entries = make_entries(layouts, {1.0});

    SolverContext ctx;
    be::BruteForceDeclarer declarer(
        ctx, be::DeclarerObjective::MaximizeProbabilityToMake, be::single_card_defender);
    be::DeclarerStrategy const pi = declarer.as_strategy();

    be::Card const chosen = pi.play(state, be::BeliefView{entries, false, entries.size()});

    EXPECT_EQ(chosen.rank, Jack);
}

TEST_F(BruteForceDeclarerTest, MaxLayoutsCapsHowManyLayoutsTheOpponentModelIsQueriedOver)
{
    // North holds one card, so declarer has no choice and the test isolates
    // max_layouts. Two layouts differ only in which defender holds which
    // filler; only their count matters. The trick has two defender turns
    // (East, then West), and expand_defender_node queries the opponent model
    // once per layout at each. Without a cap: 2 layouts * 2 turns = 4 queries.
    // With max_layouts = 1, one layout survives pruning before either
    // defender node: 1 * 2 = 2 queries.
    Deal layout_a{};
    layout_a.trump = DDS_NOTRUMP;
    layout_a.first = North;
    layout_a.remainCards[North][Spades] = be::holding({Ace});
    layout_a.remainCards[South][Spades] = be::holding({Two});
    layout_a.remainCards[East][Spades] = be::holding({Three});
    layout_a.remainCards[West][Spades] = be::holding({Four});

    Deal layout_b = layout_a;
    layout_b.remainCards[East][Spades] = be::holding({Four});
    layout_b.remainCards[West][Spades] = be::holding({Three});

    be::ObservationState state{};
    state.trump = DDS_NOTRUMP;
    state.first = North;
    state.declarer = North;
    state.tricks_needed = 1;
    state.tricks_won_by_declarer = 0;
    state.known_holdings = layout_a;
    state.ranks = be::make_rank_map(layout_a);

    std::vector<Deal> const layouts = {layout_a, layout_b};
    std::vector<be::BeliefEntry> const entries = make_entries(layouts, {0.7, 0.3});

    int call_count = 0;
    be::DefenderStrategy const spy = [&](be::DefenderQuery const& query) -> std::vector<be::WeightedCard>
    {
        ++call_count;
        return be::single_card_defender(query);
    };

    be::BruteForceOptions options{};
    options.max_layouts = 1;
    SolverContext ctx;
    be::BruteForceDeclarer declarer(ctx, be::DeclarerObjective::MaximizeProbabilityToMake, spy, options);
    be::DeclarerStrategy const pi = declarer.as_strategy();

    pi.play(state, be::BeliefView{entries, false, entries.size()});

    EXPECT_EQ(call_count, 2);
}

// --- self-consistency: BruteForceDeclarer against DoubleDummyBound's
// ceiling, end to end through evaluate() ------------------------------------

TEST_F(BruteForceDeclarerTest, EndToEndMatchesDoubleDummyBoundsOwnCeiling)
{
    // The balanced two-card fixture of
    // PicksTheCardThatWinsTheTrickOverTheOneThatLosesIt, now with a real
    // DoubleDummyDefender as both BruteForceDeclarer's internal opponent model
    // and evaluate()'s outer delta: the intended configuration. One
    // SolverContext and one DoubleDummyDefender serve both roles. That is safe
    // here: the test is single-threaded, and DoubleDummyDefender is only
    // unsafe for concurrent use.
    //
    // Under double-dummy defence East beats either lead. Against the Jack it
    // rises with the King, rather than ducking with the Six as the scripted
    // model did. Against the Two it plays the Six, which already wins, and
    // keeps the King for the Jack. So declarer's double-dummy trick count is
    // 0, not 1. DoubleDummyBound confirms this below.
    Deal layout{};
    layout.trump = DDS_NOTRUMP;
    layout.first = North;
    layout.remainCards[North][Spades] = be::holding({Jack, Two});
    layout.remainCards[South][Spades] = be::holding({Three, Five});
    layout.remainCards[East][Spades] = be::holding({King, Six});
    layout.remainCards[West][Spades] = be::holding({Four, Seven});

    SolverContext ctx;
    be::DoubleDummyDefender dd(ctx);
    be::BruteForceDeclarer brute_force(
        ctx, be::DeclarerObjective::MaximizeProbabilityToMake, dd.as_strategy());

    be::DoubleDummyBound bound(ctx, North);
    int const true_ceiling = bound.as_bound()(layout);
    ASSERT_EQ(true_ceiling, 0)
        << "fixture's own derivation expects optimal defence to deny declarer every trick here";

    be::VectorLayoutSource const source({layout});
    be::EvaluationResult const result = be::evaluate(
        layout, /*declarer=*/North, /*tricks_needed=*/1, source, brute_force.as_strategy(),
        dd.as_strategy());

    ASSERT_FALSE(result.error.has_value());
    be::EvaluationValue const& value = result.by_strategy.begin()->second;

    double const expected_p_make = (0 + true_ceiling >= 1) ? 1.0 : 0.0;
    EXPECT_NEAR(value.p_make, expected_p_make, be::ProbabilitySumTolerance);

    // Stronger than the aggregate: both of North's leads, not only pi's
    // choice, report the same value. A real tie at the ceiling, not the luck
    // of which card was picked.
    ASSERT_TRUE(value.root_is_declaring_side);
    ASSERT_EQ(value.root_children.size(), 2u);
    for (be::RootChildValue const& child : value.root_children)
    {
        EXPECT_NEAR(child.value, expected_p_make, be::ProbabilitySumTolerance)
            << "card (" << child.card.suit << "," << child.card.rank << ")";
    }
}

// --- max_depth: cross-call cache scoping -----------------------------------

TEST_F(BruteForceDeclarerTest, MaxDepthDoesNotCarryTheCacheAcrossSeparatePlayCalls)
{
    // The three-suit fixture of
    // ACacheHitSkipsRecomputingASubtreeReachedByTransposition, now with
    // max_depth set. A finite max_depth cuts off at a depth relative to the
    // play() call, so the same node reached at another depth by another call
    // can have a different correct answer. Carrying the cache across calls is
    // sound only without max_depth (see BruteForceDeclarer).
    //
    // Running the identical call twice is enough to catch a regression. If
    // the cache were carried over, the second call would hit for every node
    // (same root, tree and depths), and its opponent-model count would drop
    // to near zero. With a fresh cache per call, the two counts match
    // exactly. This case shows only whether reuse happened, not whether it
    // was correct: here the reuse would in fact be safe.
    auto const make_suit_layout = [](int suit) -> Deal
    {
        Deal layout{};
        layout.trump = DDS_NOTRUMP;
        layout.first = North;
        layout.remainCards[North][suit] = be::holding({Ace});
        layout.remainCards[South][suit] = be::holding({Two});
        layout.remainCards[East][suit] = be::holding({Three});
        layout.remainCards[West][suit] = be::holding({Four});
        return layout;
    };

    Deal layout{};
    layout.trump = DDS_NOTRUMP;
    layout.first = North;
    for (int suit : {Spades, Hearts, Diamonds})
    {
        Deal const one_suit = make_suit_layout(suit);
        for (int hand = 0; hand < DDS_HANDS; ++hand)
        {
            layout.remainCards[hand][suit] = one_suit.remainCards[hand][suit];
        }
    }

    be::ObservationState state{};
    state.trump = DDS_NOTRUMP;
    state.first = North;
    state.declarer = North;
    state.tricks_needed = 3;
    state.tricks_won_by_declarer = 0;
    state.known_holdings = layout;
    state.ranks = be::make_rank_map(layout);

    std::vector<Deal> const layouts = {layout};
    std::vector<be::BeliefEntry> const entries = make_entries(layouts, {1.0});

    int call_count = 0;
    be::DefenderStrategy const spy = [&](be::DefenderQuery const& query) -> std::vector<be::WeightedCard>
    {
        ++call_count;
        return be::single_card_defender(query);
    };

    be::BruteForceOptions options{};
    options.max_depth = 5;  // cuts off at trick 2's East response; see the sibling
                             // test for the depth-by-depth layout
    SolverContext ctx;
    be::BruteForceDeclarer declarer(ctx, be::DeclarerObjective::MaximizeExpectedTricks, spy, options);
    be::DeclarerStrategy const pi = declarer.as_strategy();

    call_count = 0;
    be::Card const first_card =
        pi.play(state, be::BeliefView{entries, false, entries.size()});
    int const first_call_count = call_count;
    ASSERT_GT(first_call_count, 0);

    call_count = 0;
    be::Card const second_card =
        pi.play(state, be::BeliefView{entries, false, entries.size()});
    int const second_call_count = call_count;

    EXPECT_EQ(second_call_count, first_call_count);
    EXPECT_EQ(second_card.suit, first_card.suit);
    EXPECT_EQ(second_card.rank, first_card.rank);
}

TEST_F(BruteForceDeclarerTest, MaxLayoutsMustBeAppliedBeforeCutoffLeafValueNotAfter)
{
    // Tests a cost bound, not correctness. With max_depth small enough for a
    // cutoff before any node prunes (0 or 1 cuts off at the first node),
    // cutoff_leaf_value must still see at most max_layouts layouts. It calls
    // DoubleDummyBound, a real solve, once per layout.
    //
    // play()'s returned card cannot show this. Every legal card at a declarer
    // node shares the same, undivided belief space (see node.hpp), so an
    // extra layout shifts every candidate's value by the same amount and
    // never changes which one wins. The absolute value is not public:
    // search() is private, and evaluate()'s root_children for a declarer root
    // come from evaluate()'s own recursion. So this test calls check_leaf(),
    // the function search() calls, for the depth-cutoff case: prune, then
    // check the cutoff. is_terminal is checked before pruning, since a
    // terminal leaf costs no solve (see
    // MaxLayoutsMustNotBeAppliedBeforeATerminalLeaf below). A wrong order in
    // check_leaf() (cutoff before pruning, or pruning only when branching)
    // fails this test directly.
    //
    // Two layouts:
    // - well-formed, posterior 0.9: East holds the Clubs Ace and South and
    //   West low cards. East's ace wins the last trick, so declarer takes 0
    //   more tricks (checked below with DoubleDummyBound);
    // - garbage, posterior 0.1: a default, all-empty Deal, the malformed input
    //   of AMalformedLayoutSurfacesAsTheSentinelNotARealBound in
    //   double_dummy_bound_test.cpp. DoubleDummyBound answers it with its
    //   too-high sentinel, 14.
    // max_layouts = 1 keeps only the well-formed one.
    Deal well_formed{};
    well_formed.trump = DDS_NOTRUMP;
    well_formed.first = North;
    well_formed.currentTrickSuit[0] = Clubs;
    well_formed.currentTrickRank[0] = Two;  // North's forced lead, already played
    well_formed.remainCards[South][Clubs] = be::holding({King});
    well_formed.remainCards[East][Clubs] = be::holding({Ace});
    well_formed.remainCards[West][Clubs] = be::holding({Three});

    SolverContext ctx;
    be::DoubleDummyBound bound(ctx, /*declarer=*/North);
    int const bound_well_formed = bound.as_bound()(well_formed);
    ASSERT_EQ(bound_well_formed, 0)
        << "fixture's own derivation expects East's ace to win the only remaining trick outright";

    int const bound_garbage = bound.as_bound()(Deal{});
    ASSERT_EQ(bound_garbage, 14) << "the solver-failure sentinel, confirming this layout is "
                                    "genuinely unscoreable, not accidentally valid";

    be::ObservationState state{};
    state.tricks_needed = 1;
    state.tricks_won_by_declarer = 0;

    be::BeliefNode node{};
    node.layouts = {well_formed, Deal{}};
    node.p = {0.9, 0.1};
    node.root_keys = {0, 1};  // drop_lowest_posterior_layouts indexes this parallel array
    node.kappa = 1.0;
    node.state = state;

    // check_leaf() called as search() calls it, with depth at max_depth so
    // the depth cutoff fires.
    be::BruteForceOptions options{};
    options.max_depth = 1;
    options.max_layouts = 1;

    be::LeafCheckResult const result = be::check_leaf(
        node, /*depth=*/1, options, bound.as_bound(), be::DeclarerObjective::MaximizeExpectedTricks);

    ASSERT_TRUE(result.value.has_value()) << "depth 1 >= max_depth 1 must select the cutoff leaf";
    EXPECT_DOUBLE_EQ(*result.value, 0.0);  // only the well-formed layout contributes

    // What the wrong order would give: cutoff first, pruning only further
    // down, so cutoff_leaf_value runs on the unpruned node. The garbage
    // layout's sentinel value (0 + 14 = 14) adds 0.1 * 14 = 1.4, far from the
    // correct 0.0. This is illustration only, from calling cutoff_leaf_value
    // directly; the assertion above is the check.
    double const wrong_order_value = be::cutoff_leaf_value(
        node, bound.as_bound(), be::DeclarerObjective::MaximizeExpectedTricks);
    EXPECT_NEAR(wrong_order_value, 1.4, 1e-9);
    EXPECT_GT(wrong_order_value, 1.0);
}

TEST_F(BruteForceDeclarerTest, MaxLayoutsMustNotBeAppliedBeforeATerminalLeaf)
{
    // The opposite mistake: pruning before checking is_terminal. A terminal
    // leaf calls no solver. terminal_leaf_value is O(1): it reads
    // tricks_won_by_declarer (common knowledge once nothing is left) and sums
    // node.p. So max_layouts saves nothing there and would only lose mass.
    // search() checks is_terminal on the node as given, before pruning. This
    // test calls check_leaf() directly, so pruning first fails it.
    //
    // Both layouts are fully played out: a default, all-empty Deal, which
    // node.hpp's is_terminal describes as "no hand in the node has a card
    // left". Unlike the cutoff test above, no real cards are needed, since no
    // DoubleDummyBound call is made. node.p is 0.9/0.1, so max_layouts = 1
    // would drop the lower layout if pruning ran first.
    be::ObservationState state{};
    state.tricks_needed = 3;
    state.tricks_won_by_declarer = 3;  // already made, common knowledge once terminal

    be::BeliefNode node{};
    node.layouts = {Deal{}, Deal{}};
    node.p = {0.9, 0.1};
    node.root_keys = {0, 1};  // drop_lowest_posterior_layouts indexes this parallel array
    node.kappa = 1.0;
    node.state = state;

    ASSERT_TRUE(be::is_terminal(node));

    be::BruteForceOptions options{};
    options.max_layouts = 1;  // would drop the lower-posterior layout if pruning ran first
    be::LayoutBound const unused_bound;  // never called: is_terminal fires before any bound is needed

    // check_leaf() called as search() calls it. Both layouts count:
    // 0.9 + 0.1 = 1.0 of kappa. So MaximizeProbabilityToMake gives node_mass
    // 1.0, and MaximizeExpectedTricks gives 1.0 * 3 = 3.0, not values pruned
    // to the 0.9 survivor.
    be::LeafCheckResult const p_make_result =
        be::check_leaf(node, /*depth=*/1, options, unused_bound, be::DeclarerObjective::MaximizeProbabilityToMake);
    ASSERT_TRUE(p_make_result.value.has_value()) << "a terminal node must always select the terminal leaf";
    EXPECT_DOUBLE_EQ(*p_make_result.value, 1.0);

    be::LeafCheckResult const expected_tricks_result = be::check_leaf(
        node, /*depth=*/1, options, unused_bound, be::DeclarerObjective::MaximizeExpectedTricks);
    ASSERT_TRUE(expected_tricks_result.value.has_value());
    EXPECT_DOUBLE_EQ(*expected_tricks_result.value, 3.0);

    // What pruning first would give: terminal_leaf_value sees only the 0.9
    // survivor, so node_mass 0.9, not 1.0, and tricks 0.9 * 3 = 2.7, not 3.0.
    // A silent under-count that saves no solve. Illustration only, from
    // calling drop_lowest_posterior_layouts and terminal_leaf_value directly;
    // the assertions above are the check.
    be::BeliefNode const wrongly_pruned_first = be::drop_lowest_posterior_layouts(node, 1);
    double const wrong_p_make = be::terminal_leaf_value(
        wrongly_pruned_first, be::DeclarerObjective::MaximizeProbabilityToMake);
    EXPECT_DOUBLE_EQ(wrong_p_make, 0.9);
    EXPECT_LT(wrong_p_make, *p_make_result.value);

    double const wrong_expected_tricks = be::terminal_leaf_value(
        wrongly_pruned_first, be::DeclarerObjective::MaximizeExpectedTricks);
    EXPECT_DOUBLE_EQ(wrong_expected_tricks, 2.7);
    EXPECT_LT(wrong_expected_tricks, *expected_tricks_result.value);
}
