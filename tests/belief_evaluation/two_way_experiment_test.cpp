#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <functional>
#include <set>
#include <utility>
#include <vector>

#include <api/dds_data_types.hpp>
#include <utility/constants.h>

#include <belief_evaluation/belief_view.hpp>
#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/history_verification.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/unconstrained_layout_source.hpp>

#include "test_support.hpp"

// The plan's headline: one hand, evaluated two ways -- a root at trick one
// (the show-out occurring during the search) against a root built after
// that trick (the play history supplied) -- giving the same p_make. See
// docs/replenished_belief_evaluation/algorithm.md and this module's own
// void_derivation.hpp / history_verification.hpp / constrained_decomposition.hpp
// for the machinery this exercises end to end.

namespace be = dds::belief_evaluation;

using be::BeliefEntry;
using be::BeliefView;
using be::DeclarerStrategy;
using be::EvaluateOptions;
using be::EvaluationResult;
using be::HistoryVerdict;
using be::make_belief_view;
using be::make_root;
using be::ObservationState;
using be::RootConstructionResult;
using be::UnconstrainedLayoutSource;
using be::WeightedCard;
using be::evaluate;
using be::holding;
using be::lowest_legal_card;
using be::seat_on_play;
using be::single_card_declarer_play;
using be::single_card_defender;

namespace
{
    constexpr int Two = 2;
    constexpr int Three = 3;
    constexpr int Four = 4;
    constexpr int Five = 5;
    constexpr int Six = 6;
    constexpr int Eight = 8;
    constexpr int Nine = 9;
    constexpr int Ten = 10;
    constexpr int Jack = 11;
    constexpr int Queen = 12;
    constexpr int King = 13;
    constexpr int Ace = 14;

    constexpr int Spades = 0;
    constexpr int Hearts = 1;
    constexpr int Diamonds = 2;
    constexpr int Clubs = 3;

    constexpr int North = 0;  // declarer in every fixture below
    constexpr int East = 1;   // the fixed seat: (declarer + 1) % DDS_HANDS
    constexpr int South = 2;  // dummy
    constexpr int West = 3;

    using CardPair = std::pair<int, int>;  // (suit, rank)

    auto all_52_cards() -> std::vector<CardPair>
    {
        std::vector<CardPair> cards;
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            for (int rank = 2; rank <= 14; ++rank)
            {
                cards.emplace_back(suit, rank);
            }
        }
        return cards;
    }

    /// Builds a root + history pair that together account for exactly the
    /// 52-card deck -- see history_verification_test.cpp's own copy of this
    /// helper for the full rationale.
    auto build_deal(
        std::vector<CardPair> const& played, std::function<int(int, int)> const& hand_for)
        -> std::pair<Deal, PlayTraceBin>
    {
        std::set<CardPair> const played_set(played.begin(), played.end());
        Deal root{};
        for (auto const& [suit, rank] : all_52_cards())
        {
            if (played_set.count({suit, rank}) != 0)
            {
                continue;
            }
            root.remainCards[hand_for(suit, rank)][suit] |= (1u << rank);
        }
        PlayTraceBin history{};
        history.number = static_cast<int>(played.size());
        for (std::size_t i = 0; i < played.size(); ++i)
        {
            history.suit[i] = played[i].first;
            history.rank[i] = played[i].second;
        }
        return {root, history};
    }
}

class TwoWayExperimentTest : public ::testing::Test
{
};

// --- the headline: inside vs before, and the no-history contrast ----------
//
// North (declarer) holds the ace of hearts and the nine of spades; South
// (dummy) the king of hearts and the eight of spades -- a two-trick
// ending. Trick one: North leads the heart ace, East -- void in hearts --
// discards a spade, South follows the king, West follows its lower heart.
// North's ace wins, so North leads trick two: the nine of spades, low
// toward dummy's eight. Whether North's nine wins now depends on whether
// East's one remaining card beats it -- and East's pool, once its void in
// hearts is applied, is the six spades {king, three, four, five, six,
// ten} rather than those six plus the outstanding heart queen.
//
// The "inside" construction searches from trick one, where East's actual
// trick-one card is still uncertain -- so its root-level p_make averages
// over every way that card could go, not only the branch that matches
// what the "before" root's history records. What this test actually
// compares against "before" is the belief view at the one node inside
// that search which *does* match -- North on lead for trick two, having
// just watched East discard specifically the two of spades -- captured
// directly from pi, in restricted_choice_test.cpp's own style, rather
// than trusted to survive intact through the root-level aggregate.
//
// East's discard on trick one is scripted to be uniform among whatever it
// holds when void, not "always the lowest": a deterministic "lowest
// legal" choice would itself carry information about East's other card
// (a restricted-choice-shaped effect), which the void-only model built
// here deliberately does not track -- voids are the whole residual
// constraint; which specific card showed the void is not. A uniform
// discard carries none, so the captured node and "before" are answering
// the same question.
TEST_F(TwoWayExperimentTest, InsideAndBeforeAgreeAndNoHistoryDiffers)
{
    // --- the "inside" root: trick one, nothing played yet ------------------
    Deal inside_root{};
    inside_root.trump = DDS_NOTRUMP;
    inside_root.first = North;
    inside_root.remainCards[North][Hearts] = holding({Ace});
    inside_root.remainCards[North][Spades] = holding({Nine});
    inside_root.remainCards[South][Hearts] = holding({King});
    inside_root.remainCards[South][Spades] = holding({Eight});
    // East's own representative split at this root -- any 2-card subset of
    // the pool fixes the same C(9, 2) space; West gets the rest.
    inside_root.remainCards[East][Spades] = holding({Two, Three});
    inside_root.remainCards[West][Hearts] = holding({Queen, Jack});
    inside_root.remainCards[West][Spades] = holding({King, Four, Five, Six, Ten});

    UnconstrainedLayoutSource const inside_source(inside_root, North, /*seed=*/1u);
    ASSERT_EQ(inside_source.size(), 36u);  // C(9, 2)

    // A defender strategy where East's void-in-hearts discard is uniform
    // among whatever it holds; every other play (East's and West's forced
    // follows, West's own forced-lowest at trick two) is single_card_defender's
    // ordinary lowest-legal rule -- neither informative nor in question here.
    auto const east_uniform_discard = [](be::DefenderQuery const& query) -> std::vector<WeightedCard>
    {
        bool const led_hearts = query.layout.currentTrickSuit[0] == Hearts
            && query.layout.currentTrickRank[0] != 0;
        bool const east_void_in_hearts = query.layout.remainCards[East][Hearts] == 0;
        if (query.seat == East && led_hearts && east_void_in_hearts)
        {
            std::vector<WeightedCard> distribution;
            int total = 0;
            for (int suit = 0; suit < DDS_SUITS; ++suit)
            {
                total += std::popcount(query.layout.remainCards[East][suit]);
            }
            for (int suit = 0; suit < DDS_SUITS; ++suit)
            {
                unsigned const suit_holding = query.layout.remainCards[East][suit];
                for (int rank = 2; rank <= 14; ++rank)
                {
                    if ((suit_holding & (1u << rank)) != 0)
                    {
                        distribution.push_back(WeightedCard{be::Card{suit, rank}, 1.0 / total});
                    }
                }
            }
            return distribution;
        }
        return single_card_defender(query);
    };

    // Captured from inside pi, at the one node this fixture's decision
    // point can be recognised by: North on lead for trick two (history
    // length 4), with East's second-card reply specifically the two of
    // spades -- the exact discard the "before" root's own history records
    // below. Computed directly from the belief view's own posteriors,
    // exactly as restricted_choice_test.cpp inspects a decision node's
    // view rather than trusting the root-level aggregate to isolate one
    // branch -- the root-level aggregate is a different question (see this
    // file's own commit message for why).
    double captured_p_make = -1.0;
    std::size_t captured_entry_count = 0;

    DeclarerStrategy const inside_pi{
        .id = 1,
        .play =
            [&](ObservationState const& state, BeliefView const& view) -> be::Card
        {
            int const seat = seat_on_play(state.known_holdings);
            if (seat == North && state.history.number == 0)
            {
                // North's genuine opening-lead choice, scripted to hearts:
                // lowest_legal_card would pick the lower-ranked spade nine
                // instead, since nothing forces hearts here.
                return be::Card{Hearts, Ace};
            }
            if (seat == North && state.history.number == 4 && state.history.suit[1] == Spades
                && state.history.rank[1] == Two)
            {
                captured_entry_count = view.entries.size();
                double success_mass = 0.0;
                for (BeliefEntry const& entry : view.entries)
                {
                    unsigned const east_spades = entry.layout.remainCards[East][Spades];
                    bool const east_beats_nine = (east_spades & holding({King, Ten})) != 0;
                    if (! east_beats_nine)
                    {
                        success_mass += entry.posterior;
                    }
                }
                captured_p_make = success_mass;
            }
            return lowest_legal_card(state.known_holdings, seat);
        },
        .state_key = nullptr,
    };

    EvaluationResult const inside_result =
        evaluate(inside_root, North, /*tricks_needed=*/2, inside_source, inside_pi, east_uniform_discard);
    ASSERT_FALSE(inside_result.error.has_value());
    ASSERT_GT(captured_entry_count, 0u);
    ASSERT_GE(captured_p_make, 0.0);

    // --- the "before" root: trick one already played, history supplied -----
    auto const hand_for = [](int suit, int rank) -> int
    {
        if (suit == Spades && rank == Nine)
        {
            return North;
        }
        if (suit == Spades && rank == Eight)
        {
            return South;
        }
        if (suit == Spades && rank == Three)
        {
            return East;
        }
        if (suit == Spades && (rank == King || rank == Four || rank == Five || rank == Six || rank == Ten))
        {
            return West;
        }
        if (suit == Hearts && rank == Queen)
        {
            return West;
        }
        return South;  // everything else -- dump on dummy's own pile
    };
    auto [before_root, before_history] =
        build_deal({{Hearts, Ace}, {Spades, Two}, {Hearts, King}, {Hearts, Jack}}, hand_for);
    before_root.trump = DDS_NOTRUMP;
    before_root.first = North;

    UnconstrainedLayoutSource const before_source(
        before_root, North, /*seed=*/2u, before_history, /*opening_leader=*/North);
    ASSERT_EQ(before_source.history_verdict(), HistoryVerdict::Consistent);
    ASSERT_EQ(before_source.size(), 6u);  // C(6, 1): East's void removes the queen from its pool

    // North holds exactly one card at this root (spade-nine) rather than
    // being padded with the rest of the deck: evaluate() explores every
    // legal first card at the root, not only pi's chosen one (to populate
    // EvaluationValue::root_children), so a padded North would have the
    // search explore leading a dumped heart or diamond too -- reaching,
    // in some of those branches, a second trick East and West (whose own
    // hands are exactly the enumerated pool, not padded) have no cards
    // left for. Padding goes on South (dummy) instead: dummy is never the
    // root's own first-card decision, and its forced follow at trick two
    // (its only spade) is unambiguous regardless of what else it holds.
    // Both single_card_declarer_play and single_card_defender are
    // unambiguous for every other play in this fixture -- North's single
    // legal card, South's single legal spade, East's and West's single
    // enumerated cards -- so no custom declarer strategy is needed here,
    // unlike inside_pi above.
    DeclarerStrategy const before_pi{
        .id = 1, .play = single_card_declarer_play, .state_key = nullptr};
    EvaluationResult const before_result =
        evaluate(before_root, North, /*tricks_needed=*/1, before_source, before_pi, single_card_defender);
    ASSERT_FALSE(before_result.error.has_value());

    // The headline: the two constructions agree -- at the node the "before"
    // root actually represents, captured from inside_pi above.
    EXPECT_NEAR(captured_p_make, before_result.by_strategy.at(1u).p_make, 1e-9);
    EXPECT_NEAR(before_result.by_strategy.at(1u).p_make, 2.0 / 3.0, 1e-9);

    // --- the no-history third value: must differ -----------------------------
    UnconstrainedLayoutSource const before_no_history_source(before_root, North, /*seed=*/2u);
    ASSERT_EQ(before_no_history_source.size(), 7u);  // C(7, 1): the outstanding heart is still eligible
    EvaluationResult const before_no_history_result = evaluate(
        before_root, North, /*tricks_needed=*/1, before_no_history_source, before_pi, single_card_defender);
    ASSERT_FALSE(before_no_history_result.error.has_value());

    // The no-history value differs: the fix was necessary, not decorative.
    EXPECT_NEAR(before_no_history_result.by_strategy.at(1u).p_make, 5.0 / 7.0, 1e-9);
    EXPECT_NE(
        before_result.by_strategy.at(1u).p_make, before_no_history_result.by_strategy.at(1u).p_make);
}

// --- both defenders void, in different suits -------------------------------
//
// Trick one (hearts): North leads the ace, East -- void in hearts --
// discards a spade, South follows the king, West follows the queen (its
// jack remains outstanding). Trick two (diamonds): North leads the ace
// again, East follows the king (not void here), South follows the queen,
// West -- void in diamonds -- discards a spade (its jack remains
// outstanding). North wins both, so trick three (spades, the decisive
// one) is North's own lead, exactly as in the headline fixture above:
// North's nine, low toward dummy's eight.
//
// East's void forces the outstanding heart jack to West; West's void
// forces the outstanding diamond jack to East, which is already one of
// East's two remaining cards -- so East's free choice is over the four
// remaining spades {four, five, six, ten}, needing exactly one.
TEST_F(TwoWayExperimentTest, BothDefendersVoidInDifferentSuits)
{
    auto const hand_for = [](int suit, int rank) -> int
    {
        if (suit == Spades && rank == Nine)
        {
            return North;
        }
        if (suit == Spades && rank == Eight)
        {
            return South;
        }
        if (suit == Spades && rank == Four)
        {
            return East;
        }
        if (suit == Spades && (rank == Five || rank == Six || rank == Ten))
        {
            return West;
        }
        if (suit == Diamonds && rank == Jack)
        {
            return East;  // forced to East anyway, once West's void applies
        }
        if (suit == Hearts && rank == Jack)
        {
            return West;  // forced to West anyway, once East's void applies
        }
        return South;
    };
    auto [root, history] = build_deal(
        {{Hearts, Ace},
         {Spades, Two},
         {Hearts, King},
         {Hearts, Queen},
         {Diamonds, Ace},
         {Diamonds, King},
         {Diamonds, Queen},
         {Spades, Three}},
        hand_for);
    root.trump = DDS_NOTRUMP;
    root.first = North;

    UnconstrainedLayoutSource const source(root, North, /*seed=*/3u, history, /*opening_leader=*/North);
    ASSERT_EQ(source.history_verdict(), HistoryVerdict::Consistent);
    ASSERT_EQ(source.size(), 4u);  // C(4, 1): the four free spades, one needed

    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};
    EvaluationResult const constrained =
        evaluate(root, North, /*tricks_needed=*/1, source, pi, single_card_defender);
    ASSERT_FALSE(constrained.error.has_value());
    EXPECT_NEAR(constrained.by_strategy.at(1u).p_make, 3.0 / 4.0, 1e-9);  // only the ten beats nine

    UnconstrainedLayoutSource const no_history_source(root, North, /*seed=*/3u);
    ASSERT_EQ(no_history_source.size(), 15u);  // C(6, 2): both voids ignored
    EvaluationResult const no_history =
        evaluate(root, North, /*tricks_needed=*/1, no_history_source, pi, single_card_defender);
    ASSERT_FALSE(no_history.error.has_value());
    EXPECT_NE(constrained.by_strategy.at(1u).p_make, no_history.by_strategy.at(1u).p_make);
}

// --- sampling composes with a constrained space, checked once -------------
//
// Not part of the exhaustive two-way comparison above -- sampling belongs
// after it, since a sampled run would leave any disagreement ambiguous
// between "the spaces differ" and "the samples differ". This reuses the
// two-void fixture and asks only that a bounded sample over the
// constrained space runs and, at sample_size >= size(), reproduces the
// exhaustive answer exactly -- the same bitwise property
// unconstrained_layout_source_integration_test.cpp already establishes
// for the unconstrained case, re-checked here for a source built with a
// history.
TEST_F(TwoWayExperimentTest, SamplingComposesWithAConstrainedSpace)
{
    auto const hand_for = [](int suit, int rank) -> int
    {
        if (suit == Spades && rank == Nine)
        {
            return North;
        }
        if (suit == Spades && rank == Eight)
        {
            return South;
        }
        if (suit == Spades && rank == Four)
        {
            return East;
        }
        if (suit == Spades && (rank == Five || rank == Six || rank == Ten))
        {
            return West;
        }
        if (suit == Diamonds && rank == Jack)
        {
            return East;
        }
        if (suit == Hearts && rank == Jack)
        {
            return West;
        }
        return South;
    };
    auto [root, history] = build_deal(
        {{Hearts, Ace},
         {Spades, Two},
         {Hearts, King},
         {Hearts, Queen},
         {Diamonds, Ace},
         {Diamonds, King},
         {Diamonds, Queen},
         {Spades, Three}},
        hand_for);
    root.trump = DDS_NOTRUMP;
    root.first = North;

    UnconstrainedLayoutSource const source(root, North, /*seed=*/6u, history, /*opening_leader=*/North);
    ASSERT_EQ(source.history_verdict(), HistoryVerdict::Consistent);
    ASSERT_EQ(source.size(), 4u);

    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};
    EvaluationResult const exhaustive =
        evaluate(root, North, /*tricks_needed=*/1, source, pi, single_card_defender);
    EvaluationResult const sampled = evaluate(
        root, North, /*tricks_needed=*/1, source, pi, single_card_defender,
        EvaluateOptions{.sampling = {.sample_size = 1000u}});  // 1000 >= size() == 4

    ASSERT_FALSE(exhaustive.error.has_value());
    ASSERT_FALSE(sampled.error.has_value());
    EXPECT_EQ(exhaustive.by_strategy.at(1u).p_make, sampled.by_strategy.at(1u).p_make);
}

// --- the void changes which play is right, not merely the probability -----
//
// A spade king is missing from a six-card spade pool {king, two, three,
// four, five, six}; East holds four of the pool's ten cards (the other
// four are the hearts a trick-one void removes). Unconstrained, P(East
// holds the king) is East's plain share of the whole pool, 4/10 = 0.4 --
// below even money, so the odds favour playing West for it. Once the
// void removes the four hearts from contention, East's four cards must
// all come from the six spades, and P(East holds the king) becomes
// 4/6 ~= 0.667 -- above even money the other way. The same missing card,
// the same defender, and the *side declarer should play* has flipped, not
// merely the number attached to it.
//
// This inspects the belief view directly (make_root + make_belief_view),
// the same posterior a real decision would be read from, rather than
// running a scripted two-line comparison through evaluate() -- the point
// under test is the probability crossing even money, which the posterior
// states directly.
TEST_F(TwoWayExperimentTest, TheVoidFlipsWhichDefenderIsFavouriteForTheMissingKing)
{
    auto const hand_for = [](int suit, int rank) -> int
    {
        if (suit == Spades && rank == Nine)
        {
            return East;  // East's sacrificial discard on the heart trick
        }
        if (suit == Spades && (rank == Two || rank == Three || rank == Four || rank == Five))
        {
            return East;  // East's own four-card spade holding
        }
        if (suit == Spades && (rank == King || rank == Six))
        {
            return West;
        }
        if (suit == Hearts && (rank == Jack || rank == Ten || rank == Nine || rank == Eight))
        {
            return West;  // the four outstanding hearts, forced to West once void applies
        }
        return South;
    };
    auto [root, history] =
        build_deal({{Hearts, Ace}, {Spades, Nine}, {Hearts, King}, {Hearts, Queen}}, hand_for);
    root.trump = DDS_NOTRUMP;
    root.first = North;

    auto const p_east_holds_the_king = [&root](UnconstrainedLayoutSource const& source) -> double
    {
        RootConstructionResult const result = make_root(root, North, /*tricks_needed=*/1, source);
        if (! result.node.has_value())
        {
            ADD_FAILURE() << "make_root failed";
            return -1.0;
        }
        std::vector<BeliefEntry> scratch;
        BeliefView const view = make_belief_view(*result.node, scratch);
        double mass = 0.0;
        for (BeliefEntry const& entry : view.entries)
        {
            if ((entry.layout.remainCards[East][Spades] & holding({King})) != 0)
            {
                mass += entry.posterior;
            }
        }
        return mass;
    };

    UnconstrainedLayoutSource const constrained_source(
        root, North, /*seed=*/5u, history, /*opening_leader=*/North);
    ASSERT_EQ(constrained_source.history_verdict(), HistoryVerdict::Consistent);
    ASSERT_EQ(constrained_source.size(), 15u);  // C(6, 4): East's void removes the four hearts

    UnconstrainedLayoutSource const unconstrained_source(root, North, /*seed=*/5u);
    ASSERT_EQ(unconstrained_source.size(), 210u);  // C(10, 4)

    double const p_constrained = p_east_holds_the_king(constrained_source);
    double const p_unconstrained = p_east_holds_the_king(unconstrained_source);

    EXPECT_NEAR(p_unconstrained, 0.4, 1e-9);
    EXPECT_NEAR(p_constrained, 2.0 / 3.0, 1e-9);
    EXPECT_LT(p_unconstrained, 0.5);  // unconstrained: play West for the king
    EXPECT_GT(p_constrained, 0.5);    // constrained: play East for the king -- the flip
}

// --- a trump contract, the show-out a ruff -----------------------------
//
// Hearts are trump. Trick one: North leads the ace of spades, East --
// void in spades -- ruffs with its one heart (the deuce), South follows
// the king, West follows the queen. The ruff wins the trick like any
// other trump over a plain suit, and establishes exactly the same void a
// discard would: the trap this task exists to catch is treating the ruff
// as a special case that does not register as a show-out. East, having
// won, leads trick two -- its one remaining card, a club, from a
// four-card pool {two, three, four, ten}, low toward North's forced nine.
// A spade jack remains outstanding (declarer's and dummy's own spades are
// exhausted after trick one), so the void has a live card to force.
TEST_F(TwoWayExperimentTest, ATrumpContractWhereTheShowOutIsARuff)
{
    auto const hand_for = [](int suit, int rank) -> int
    {
        if (suit == Clubs && rank == Nine)
        {
            return North;
        }
        if (suit == Clubs && rank == Two)
        {
            return East;
        }
        if (suit == Clubs && (rank == Three || rank == Four || rank == Ten))
        {
            return West;
        }
        if (suit == Spades && rank == Jack)
        {
            return West;  // forced to West anyway, once East's void applies
        }
        return South;
    };
    auto [root, history] = build_deal(
        {{Spades, Ace}, {Hearts, Two}, {Spades, King}, {Spades, Queen}}, hand_for);
    root.trump = Hearts;
    root.first = East;  // East's ruff won trick one

    UnconstrainedLayoutSource const source(root, North, /*seed=*/4u, history, /*opening_leader=*/North);
    ASSERT_EQ(source.history_verdict(), HistoryVerdict::Consistent);
    ASSERT_EQ(source.size(), 4u);  // C(4, 1): East's void removes the spade jack from its pool

    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};
    EvaluationResult const constrained =
        evaluate(root, North, /*tricks_needed=*/1, source, pi, single_card_defender);
    ASSERT_FALSE(constrained.error.has_value());
    EXPECT_NEAR(constrained.by_strategy.at(1u).p_make, 3.0 / 4.0, 1e-9);  // only the ten beats nine

    UnconstrainedLayoutSource const no_history_source(root, North, /*seed=*/4u);
    ASSERT_EQ(no_history_source.size(), 5u);  // C(5, 1): the spade jack still eligible
    EvaluationResult const no_history =
        evaluate(root, North, /*tricks_needed=*/1, no_history_source, pi, single_card_defender);
    ASSERT_FALSE(no_history.error.has_value());
    EXPECT_NE(constrained.by_strategy.at(1u).p_make, no_history.by_strategy.at(1u).p_make);
}
