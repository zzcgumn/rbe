#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
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

    // North (declarer) on lead, one legal card -- enough to prove play()
    // returns a legal card and that state_key is wired up, without
    // depending on any search logic yet.
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
        be::BruteForceDeclarer declarer(ctx, be::DeclarerObjective::MaximiseExpectedTricks, spy));
}

TEST_F(BruteForceDeclarerTest, ConstructionRejectsZeroMaxLayouts)
{
    // max_layouts = 0 means "keep the 0 highest-posterior layouts" --
    // every non-terminal node search() ever prunes would collapse to zero
    // layouts, and make_brute_force_cache_key's own layouts.front() would
    // then read past an empty vector. Rejected at construction instead,
    // the same way make_root() rejects sample_size == 0 (node.cpp).
    SolverContext ctx;
    be::BruteForceOptions options{};
    options.max_layouts = 0;
    EXPECT_THROW(
        be::BruteForceDeclarer declarer(ctx, be::DeclarerObjective::MaximiseExpectedTricks, nullptr, options),
        std::invalid_argument);
}

TEST_F(BruteForceDeclarerTest, ReusingOneInstanceForASecondDeclarerSeatThrows)
{
    // This class's own doxygen already documents "one declarer seat for
    // its whole lifetime" -- this is the runtime check for that
    // precondition, not assert() (which compiles out entirely under
    // NDEBUG, this repo's own --config=opt, and would otherwise silently
    // keep the first declarer's DoubleDummyBound and compute wrong cutoff
    // values for the second rather than failing at all).
    SolverContext ctx;
    be::BruteForceDeclarer declarer(ctx);
    be::DeclarerStrategy const pi = declarer.as_strategy();

    // Named, outliving the pi.play() call below -- BeliefEntry::layout is
    // a Deal const&, so a braced temporary passed straight into
    // make_entries() would dangle the moment make_entries() returns (see
    // PicksTheCardThatWinsTheTrickOverTheOneThatLosesIt's own comment on
    // this same hazard).
    be::ObservationState const first_root = make_one_card_root();  // declarer = North
    std::vector<Deal> const first_layouts = {first_root.known_holdings};
    std::vector<be::BeliefEntry> const first_entries = make_entries(first_layouts, {1.0});
    pi.play(first_root, be::BeliefView{first_entries, false, first_entries.size()});

    // A second root for a *different* declarer seat -- East/West as
    // declarer/dummy this time, North/South as the defenders.
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
    node.state.tricks_won_by_declarer = 2;  // made, in every layout -- common knowledge at a terminal node

    EXPECT_DOUBLE_EQ(
        be::terminal_leaf_value(node, be::DeclarerObjective::MaximiseProbabilityToMake), 1.0);
}

TEST_F(BruteForceDeclarerTest, TerminalLeafGivesTrickCountForExpectedTricks)
{
    be::BeliefNode node{};
    node.p = {0.3, 0.7};
    node.kappa = 1.0;
    node.state.tricks_won_by_declarer = 5;

    EXPECT_DOUBLE_EQ(
        be::terminal_leaf_value(node, be::DeclarerObjective::MaximiseExpectedTricks), 5.0);
}

TEST_F(BruteForceDeclarerTest, CutoffLeafUsesPerLayoutBoundsNotANodeWideNumber)
{
    // North (declarer) holds King, Eight, Five of spades. In deal_a, East
    // alone holds the one dangerous spade (the Nine) and is forced to
    // spend it on North's first-round King lead, going void before
    // North's Eight is ever at risk (the same mechanism
    // suit_top_tricks_test.cpp's own
    // UndercountsWhenTheShorterOpponentHoldsTheOnlyDangerousCard fixture
    // describes). In deal_b, the Nine moves to the *longer* defending hand
    // (West, holding Nine and Two), which can duck the first round with
    // its Two and keep the Nine in reserve to beat North's Eight on the
    // second round instead. Same North/South spade holdings, a different
    // defender split: the two deals' own double-dummy bounds are read off
    // the solver directly below, not asserted by hand, and need only
    // differ from each other for this test to exercise per-layout
    // branching at all.
    // Every hand must hold the same total card count for solve_board to
    // report a real score rather than its own failure sentinel (measured
    // directly against this solver build, not assumed -- see
    // double_dummy_bound_test.cpp's own "filler suit" note); North's own
    // 3-card spade holding sets that count, so South/East/West are padded
    // with filler clubs nobody has a real choice over, identically in both
    // deals, so any filler-suit effect on the result is the same constant
    // in both and isolates the spades difference between them.
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

    // Confirmed by a direct solve_board call -- not assumed from the
    // comment above -- per the per-layout bound each deal actually
    // yields.
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
    // The higher of the two bounds, so the layout with that bound makes
    // and the other does not -- a P(make) fixture that actually
    // discriminates between the two layouts, whatever their real bounds
    // turn out to be.
    node.state.tricks_needed = std::max(bound_a, bound_b);

    double const expected_expected_tricks = 0.4 * (0 + bound_a) + 0.6 * (0 + bound_b);
    EXPECT_DOUBLE_EQ(
        be::cutoff_leaf_value(node, bound.as_bound(), be::DeclarerObjective::MaximiseExpectedTricks),
        expected_expected_tricks);

    double const expected_p_make =
        0.4 * ((0 + bound_a) >= node.state.tricks_needed ? 1.0 : 0.0)
        + 0.6 * ((0 + bound_b) >= node.state.tricks_needed ? 1.0 : 0.0);
    EXPECT_DOUBLE_EQ(
        be::cutoff_leaf_value(node, bound.as_bound(), be::DeclarerObjective::MaximiseProbabilityToMake),
        expected_p_make);
}

// --- the actual search: branching, both sides -----------------------------
//
// Trick mechanics always need all four seats to play, so no realistic
// fixture reaches a declarer decision without also reaching a defender's
// turn soon after -- the two branches below are therefore exercised
// together, not in isolation.

TEST_F(BruteForceDeclarerTest, PicksTheCardThatWinsTheTrickOverTheOneThatLosesIt)
{
    // Every hand holds the same card count (2) throughout -- a real
    // bridge ending always does, since every trick removes exactly one
    // card from each hand, and the engine's own trick mechanics assume
    // it: an unbalanced fixture leaves some seat void mid-ending with no
    // legal card to offer. North (declarer) holds Jack, Two; South
    // (dummy) Three, Five; East King, Six; West Four, Seven.
    // single_card_defender always offers each defender's own *lowest*
    // remaining card first, so East's King only ever appears once East's
    // low Six is already spent.
    //
    // Leading the Jack first: trick 1 is {Jack(North), Six(East, forced
    // low), dummy's low filler, Four(West)} -- the Jack beats every one
    // of those, so declarer wins trick 1 outright and leads trick 2 too;
    // trick 2 is {Two(North, forced), King(East, forced -- only card
    // left), dummy's other filler, Seven(West)} -- King wins trick 2 for
    // East. One trick for declarer.
    //
    // Leading the Two first: trick 1 is {Two(North), Six(East, forced
    // low), dummy's low filler, Four(West)} -- East's own low Six already
    // beats North's Two, so East wins trick 1 (without even needing the
    // King) and leads trick 2; trick 2 is {King(East, forced -- only card
    // left), Jack(North, forced), dummy's other filler, Seven(West)} --
    // King wins trick 2 too. Zero tricks for declarer.
    //
    // tricks_needed = 1: leading the Jack makes the contract, leading the
    // Two does not -- a clean, hand-traced, discriminating choice reached
    // only by actually searching both legal cards through two real
    // tricks, both sides.
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

    // Named, outliving the pi.play() call below -- BeliefEntry::layout is
    // a Deal const&, so a braced temporary passed straight into
    // make_entries() would dangle the moment make_entries() returns.
    std::vector<Deal> const layouts = {layout};
    std::vector<be::BeliefEntry> const entries = make_entries(layouts, {1.0});

    SolverContext ctx;
    be::BruteForceDeclarer declarer(
        ctx, be::DeclarerObjective::MaximiseProbabilityToMake, be::single_card_defender);
    be::DeclarerStrategy const pi = declarer.as_strategy();

    be::Card const chosen = pi.play(state, be::BeliefView{entries, false, entries.size()});

    EXPECT_EQ(chosen.suit, Spades);
    EXPECT_EQ(chosen.rank, Jack);
}

TEST_F(BruteForceDeclarerTest, ACacheHitSkipsRecomputingASubtreeReachedByTransposition)
{
    // Three independent one-round suits, North holding the Ace and South
    // (dummy) a low filler in each, East/West low fillers too -- North's
    // Ace always wins whichever suit it leads, so North stays on lead and
    // the root decision recurs at every trick: which of the remaining
    // suits to lead next.
    //
    // This is a tree with shared prefixes, not six independent flat
    // paths -- counting nodes, not paths, is what gets the baseline
    // right: trick 1's East/West queries are reached once per ROOT choice
    // (3 queries each, 6 total); trick 2's are reached once per (root
    // choice x which-of-the-remaining-two-suits), i.e. 3*2 = 6 times each
    // (12 total); trick 3 is forced (only one suit left) but still reached
    // once per (root x trick-2 choice) = 6 times each (12 total). Baseline
    // with no caching at all: 6 + 12 + 12 = 30 opponent-model queries.
    //
    // A genuine transposition exists at trick 3: for any one of the three
    // suits, the other two can have been played in either order, and both
    // orders leave an identical residual position (same declarer/dummy
    // holdings, same single surviving layout, same tricks_won_by_declarer)
    // -- the same BruteForceCacheKey either way. Three such "X remains"
    // shapes exist, each reached by exactly two of the six (root, trick-2)
    // combinations; the first visit computes and caches it (2 queries),
    // the second is a pure cache hit (0 queries). That is 3 shapes * 2
    // queries saved = 6 queries saved, for an exact expected total of
    // 30 - 6 = 24 -- not merely "fewer than 30", a specific, derived count.
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

    // Named, outliving the pi.play() call below -- see the same note in
    // PicksTheCardThatWinsTheTrickOverTheOneThatLosesIt above.
    std::vector<Deal> const layouts = {layout};
    std::vector<be::BeliefEntry> const entries = make_entries(layouts, {1.0});

    int call_count = 0;
    be::DefenderStrategy const spy = [&](be::DefenderQuery const& query) -> std::vector<be::WeightedCard>
    {
        ++call_count;
        return be::single_card_defender(query);
    };

    SolverContext ctx;
    be::BruteForceDeclarer declarer(ctx, be::DeclarerObjective::MaximiseExpectedTricks, spy);
    be::DeclarerStrategy const pi = declarer.as_strategy();

    pi.play(state, be::BeliefView{entries, false, entries.size()});

    EXPECT_EQ(call_count, 24);
}

TEST_F(BruteForceDeclarerTest, AMalformedOpponentModelThrowsRatherThanMiscomputing)
{
    // East holds a card (so a real defender decision is reached), but the
    // scripted opponent model offers an empty distribution -- the same
    // ValidationError::DistributionEmpty case expand_defender_node already
    // detects at evaluate()'s own outer level. DeclarerStrategy::play has
    // no error channel of its own to report this through, so it must
    // throw; the point of this test is that it throws a *specific*,
    // inspectable type carrying the real cause, not a generic one.
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
    be::BruteForceDeclarer declarer(ctx, be::DeclarerObjective::MaximiseExpectedTricks, broken);
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
    // deal_a (p=0.1, root_key=10) is dropped -- order among survivors is
    // not asserted, only membership, via the parallel root_keys array.
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
    // Two layouts tied on posterior -- the survivor must be chosen by each
    // layout's own content (layout_key), not by its position in
    // node.layouts, so that two belief spaces that are pure permutations
    // of each other (the same set of (deal, posterior) pairs, differently
    // ordered -- which state_key/make_brute_force_cache_key's own sorted
    // key already treats as identical) keep the same survivor regardless
    // of which order the caller happened to enumerate them in.
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
    // Re-run PicksTheCardThatWinsTheTrickOverTheOneThatLosesIt's own
    // fixture with max_layouts left default (absent) -- identical result,
    // proving this task introduced no regression to the unbounded path.
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
        ctx, be::DeclarerObjective::MaximiseProbabilityToMake, be::single_card_defender);
    be::DeclarerStrategy const pi = declarer.as_strategy();

    be::Card const chosen = pi.play(state, be::BeliefView{entries, false, entries.size()});

    EXPECT_EQ(chosen.rank, Jack);
}

TEST_F(BruteForceDeclarerTest, MaxLayoutsCapsHowManyLayoutsTheOpponentModelIsQueriedOver)
{
    // North holds exactly one card -- no declarer-side choice at all, so
    // this test isolates max_layouts's own effect from declarer branching
    // entirely. Two layouts in the belief space, differing only in which
    // defender holds which filler (irrelevant to the outcome; only their
    // count matters here). The one trick still has *two* defender turns
    // (East, then West) before it resolves -- expand_defender_node
    // queries the opponent model once per surviving layout, at each.
    // Without a cap: 2 layouts x 2 defender turns = 4 queries. With
    // max_layouts = 1: exactly one layout should survive pruning before
    // either defender node is reached -- 1 layout x 2 turns = 2 queries.
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
    be::BruteForceDeclarer declarer(ctx, be::DeclarerObjective::MaximiseProbabilityToMake, spy, options);
    be::DeclarerStrategy const pi = declarer.as_strategy();

    pi.play(state, be::BeliefView{entries, false, entries.size()});

    EXPECT_EQ(call_count, 2);
}

// --- self-consistency: BruteForceDeclarer against DoubleDummyBound's
// own ceiling, run through evaluate() end to end ---------------------------

TEST_F(BruteForceDeclarerTest, EndToEndMatchesDoubleDummyBoundsOwnCeiling)
{
    // The same balanced, 2-card-per-hand fixture PicksTheCardThatWinsThe
    // TrickOverTheOneThatLosesIt uses, now paired with a *real*
    // DoubleDummyDefender (not the scripted single_card_defender) as both
    // BruteForceDeclarer's own internal opponent model and evaluate()'s
    // outer delta -- the intended, self-consistent configuration. One
    // SolverContext, one DoubleDummyDefender instance reused for both
    // roles: safe here since this test is single-threaded and
    // DoubleDummyDefender's own non-thread-safety contract only concerns
    // concurrent use, not reuse.
    //
    // Under genuine double-dummy defence (not the earlier scripted
    // "always lowest" model), East sees through either lead and denies
    // declarer a trick either way: rising with the King when North leads
    // the Jack (forcing it, rather than ducking with the Six as the
    // scripted model always did), and refusing to waste the King on
    // North's Two (playing the Six, which already beats it, and keeping
    // the King to also beat North's remaining Jack next). Declarer's true
    // double-dummy trick count from this layout is therefore 0, not 1 --
    // verified below via DoubleDummyBound directly, not assumed.
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
        ctx, be::DeclarerObjective::MaximiseProbabilityToMake, dd.as_strategy());

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

    // Stronger than just the aggregate: both of North's candidate leads,
    // not only whichever one pi actually chose, should independently
    // report the same value -- a genuine tie at the true ceiling, not a
    // coincidence of which one happened to be picked.
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
    // The same three-independent-suits fixture
    // ACacheHitSkipsRecomputingASubtreeReachedByTransposition uses, now
    // with max_depth set. The cutoff a finite max_depth triggers depends
    // on *call-relative* recursion depth: the same logical node reached
    // at a different depth by a different play() call has a genuinely
    // different correct cutoff-or-not answer, so carrying the cache
    // across calls is only sound when max_depth is absent (see
    // BruteForceDeclarer's own class doxygen). This test does not need to
    // construct two *different* roots at two different depths to catch a
    // regression here -- running the *identical* call twice is already
    // decisive: if the cache were (wrongly) carried over, the second call
    // would hit a cache entry for literally every node it visits (same
    // root, same tree, same depths both times -- a transposition that
    // really would be safe to reuse, which is exactly why this case alone
    // cannot distinguish "reuse" from "correct reuse", only "reuse
    // happened or did not"), collapsing its own opponent-model call count
    // to near zero; with the fix, the second call recomputes everything
    // fresh and its count matches the first call's exactly.
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
    options.max_depth = 5;  // cuts off at trick 2's East response -- see the fixture's own
                             // sibling test for the exact depth-by-depth layout
    SolverContext ctx;
    be::BruteForceDeclarer declarer(ctx, be::DeclarerObjective::MaximiseExpectedTricks, spy, options);
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
    // A regression for a cost-bound gap, not a correctness one: with
    // max_depth set small enough to reach a cutoff before any node had a
    // chance to prune (max_depth of 0 or 1 reaches the cutoff at the very
    // first node search() visits), cutoff_leaf_value must still see at
    // most max_layouts layouts -- it calls DoubleDummyBound, a real
    // solver call, once per layout in whatever node it is handed.
    //
    // There is no way to observe this through play()'s own returned card:
    // every legal card at one declarer node shares the exact same belief
    // space, undivided (node.hpp's own "declarer nodes pass weight
    // through undivided") -- make_declarer_children() gives every
    // candidate the identical, full set of layouts, so a layout that
    // contaminates one candidate's own eventual value contaminates every
    // sibling candidate's by the same additive amount, and can therefore
    // never change *which* one search()'s own max picks, only the
    // absolute value nothing public exposes (search() itself is private;
    // evaluate()'s own root_children, for a declarer root, are computed by
    // evaluate()'s own outer recursion, not by this strategy's internal
    // one). This test instead calls check_leaf() directly -- the exact
    // free function search() itself calls, not a parallel
    // reimplementation of its ordering -- for the depth-cutoff case
    // specifically (prune, then check the depth cutoff; is_terminal is
    // checked separately, before pruning, since a terminal leaf has no
    // solver cost for max_layouts to bound; see the companion test
    // MaxLayoutsMustNotBeAppliedBeforeATerminalLeaf below for that case).
    // Because search() calls check_leaf() rather than reimplementing this
    // ordering inline, a regression in check_leaf()'s own ordering (prune
    // after the cutoff, or only in the branching case below it) would
    // fail this test directly, not just a hand-copied simulation of it.
    //
    // One well-formed layout (East holds the Clubs Ace; South/West hold
    // low fillers; East's ace trivially wins the only remaining trick, so
    // declarer's own continuation from here is 0 tricks -- confirmed below
    // via a direct DoubleDummyBound call, not assumed) at the higher
    // posterior (0.9), and one garbage layout (a default-constructed,
    // all-empty Deal -- the same malformed input
    // double_dummy_bound_test.cpp's own
    // AMalformedLayoutSurfacesAsTheSentinelNotARealBound fixture uses,
    // which DoubleDummyBound answers with its own too-high sentinel, 14,
    // precisely because there is nothing there for the solver to analyse)
    // at the lower posterior (0.1). max_layouts = 1 keeps only the
    // well-formed one.
    Deal well_formed{};
    well_formed.trump = DDS_NOTRUMP;
    well_formed.first = North;
    well_formed.currentTrickSuit[0] = Clubs;
    well_formed.currentTrickRank[0] = Two;  // North's own already-played forced lead
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

    // check_leaf() itself, called the exact way search() calls it: depth
    // at the configured max_depth, so the depth-cutoff branch fires.
    be::BruteForceOptions options{};
    options.max_depth = 1;
    options.max_layouts = 1;

    be::LeafCheckResult const result = be::check_leaf(
        node, /*depth=*/1, options, bound.as_bound(), be::DeclarerObjective::MaximiseExpectedTricks);

    ASSERT_TRUE(result.value.has_value()) << "depth 1 >= max_depth 1 must select the cutoff leaf";
    EXPECT_DOUBLE_EQ(*result.value, 0.0);  // only the well-formed layout's own mass contributes

    // What the regression this test is for would have produced: the wrong
    // order (cutoff first, prune only in the branching case reached
    // further down) runs cutoff_leaf_value over the *unpruned* node --
    // the garbage layout's own sentinel-derived value (0 + 14 = 14) still
    // contributes 0.1 * 14 = 1.4, a stark, unmissable difference from the
    // correctly-pruned 0.0 that check_leaf() above now gives, not a
    // rounding-level one. (This is illustrative color, computed by
    // calling cutoff_leaf_value directly on the unpruned node -- not
    // itself a check on check_leaf()'s own ordering, which the assertion
    // above already is.)
    double const wrong_order_value = be::cutoff_leaf_value(
        node, bound.as_bound(), be::DeclarerObjective::MaximiseExpectedTricks);
    EXPECT_NEAR(wrong_order_value, 1.4, 1e-9);
    EXPECT_GT(wrong_order_value, 1.0);
}

TEST_F(BruteForceDeclarerTest, MaxLayoutsMustNotBeAppliedBeforeATerminalLeaf)
{
    // The companion regression to the test above, for the opposite
    // mistake: pruning *before* checking is_terminal. Unlike the
    // depth-cutoff case, a terminal leaf calls no solver --
    // terminal_leaf_value is O(1), reading only node.state's own
    // tricks_won_by_declarer (common knowledge once nothing is left to
    // play) and summing node.p -- so there is no cost here for max_layouts
    // to bound, only mass it would silently lose with nothing gained in
    // return. search() therefore checks is_terminal on the node exactly
    // as given, before any pruning. This test calls check_leaf() directly
    // -- the exact function search() calls -- so a regression in its own
    // ordering (pruning before the terminal check) fails this test
    // directly.
    //
    // Both layouts below are already fully played out (a default,
    // all-empty Deal, same as node.hpp's own is_terminal doxygen
    // describes: "no hand in the node has a card left" -- checking one
    // representative layout suffices, so neither layout here needs any
    // real cards at all, unlike the depth-cutoff test above, which goes
    // through DoubleDummyBound and does). node.p is split 0.9/0.1, so
    // max_layouts = 1 would drop the lower-posterior layout if pruning
    // ran first.
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

    // check_leaf() itself, called the exact way search() calls it: both
    // layouts' mass counts, 0.9 + 0.1 = 1.0 of kappa, so
    // MaximiseProbabilityToMake's own node_mass is 1.0 and
    // MaximiseExpectedTricks's own mass-weighted trick count is
    // 1.0 * 3 = 3.0 -- neither pruned down to just the 0.9 survivor.
    be::LeafCheckResult const p_make_result =
        be::check_leaf(node, /*depth=*/1, options, unused_bound, be::DeclarerObjective::MaximiseProbabilityToMake);
    ASSERT_TRUE(p_make_result.value.has_value()) << "a terminal node must always select the terminal leaf";
    EXPECT_DOUBLE_EQ(*p_make_result.value, 1.0);

    be::LeafCheckResult const expected_tricks_result = be::check_leaf(
        node, /*depth=*/1, options, unused_bound, be::DeclarerObjective::MaximiseExpectedTricks);
    ASSERT_TRUE(expected_tricks_result.value.has_value());
    EXPECT_DOUBLE_EQ(*expected_tricks_result.value, 3.0);

    // What the regression this test is for would have produced: pruning
    // before the terminal check drops the lower-posterior layout first,
    // so terminal_leaf_value only ever sees the 0.9 that survives --
    // node_mass 0.9, not 1.0; mass-weighted tricks 0.9 * 3 = 2.7, not 3.0
    // -- a silent under-count with no solver call saved to justify it.
    // (Illustrative color, computed by calling drop_lowest_posterior_layouts
    // and terminal_leaf_value directly -- not itself a check on
    // check_leaf()'s own ordering, which the assertions above already are.)
    be::BeliefNode const wrongly_pruned_first = be::drop_lowest_posterior_layouts(node, 1);
    double const wrong_p_make = be::terminal_leaf_value(
        wrongly_pruned_first, be::DeclarerObjective::MaximiseProbabilityToMake);
    EXPECT_DOUBLE_EQ(wrong_p_make, 0.9);
    EXPECT_LT(wrong_p_make, *p_make_result.value);

    double const wrong_expected_tricks = be::terminal_leaf_value(
        wrongly_pruned_first, be::DeclarerObjective::MaximiseExpectedTricks);
    EXPECT_DOUBLE_EQ(wrong_expected_tricks, 2.7);
    EXPECT_LT(wrong_expected_tricks, *expected_tricks_result.value);
}
