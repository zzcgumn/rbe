#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <functional>
#include <set>
#include <utility>
#include <vector>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/belief_view.hpp>
#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/history_verification.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/exhaustive_layout_source.hpp>
#include <belief_evaluation/play_record.hpp>

#include "test_support.hpp"

// One hand evaluated two ways must give the same p_make: a root at trick
// one, where the show-out happens during the search, and a root after that
// trick, with the play history supplied. This exercises void_derivation.hpp,
// history_verification.hpp and constrained_decomposition.hpp end to end
// (see also docs/replenished_belief_evaluation/algorithm.md).

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
using be::PlayRecord;
using be::RootConstructionResult;
using be::ExhaustiveLayoutSource;
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

    /// Builds a root and history that together account for exactly the
    /// 52-card deck (see the copy in history_verification_test.cpp).
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

    /// A PlayRecord from a (history, opening_leader) pair. Each fixture asserts
    /// the pair Consistent with history_verdict() right after building its
    /// ExhaustiveLayoutSource, so that stays the one consistency check, and
    /// this wrapper hides PlayRecord::create()'s two-part return.
    auto make_record(PlayTraceBin const& history, int opening_leader) -> PlayRecord
    {
        return *PlayRecord::create(history, opening_leader).first;
    }
}

class TwoWayExperimentTest : public ::testing::Test
{
};

// --- inside against before, and the no-history contrast --------------------
//
// North (declarer) holds the heart ace and spade nine; South (dummy) the
// heart king and spade eight: a two-trick ending. Trick one: North leads
// the heart ace, East (void in hearts) discards a spade, South plays the
// king and West its lower heart. North wins and leads the spade nine
// towards dummy's eight. Whether the nine wins depends on whether East's
// last card beats it. Once East's heart void is applied, East's pool is
// the six spades {king, three, four, five, six, ten}, without the
// outstanding heart queen.
//
// The "inside" root searches from trick one, where East's trick-one card
// is still uncertain, so its root p_make averages over every way that card
// could go. The test therefore compares "before" with the belief view at
// the one inside node that matches it: North on lead for trick two, having
// seen East discard the spade two. It is captured from pi, as in
// restricted_choice_test.cpp, rather than read from the root aggregate.
//
// East's discard is scripted as uniform over what it holds, not "always
// the lowest". A lowest-card rule would itself say something about East's
// other card (restricted choice), which this void-only model does not
// track: voids are the whole constraint, not which card showed them. A
// uniform discard says nothing, so the captured node and "before" answer
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
    // A representative split for East at this root: any 2-card subset of the
    // pool gives the same C(9, 2) space; West gets the rest.
    inside_root.remainCards[East][Spades] = holding({Two, Three});
    inside_root.remainCards[West][Hearts] = holding({Queen, Jack});
    inside_root.remainCards[West][Spades] = holding({King, Four, Five, Six, Ten});

    ExhaustiveLayoutSource const inside_source(inside_root, North, /*seed=*/1u);
    ASSERT_EQ(inside_source.size(), 36u);  // C(9, 2)

    // A defender strategy whose East discard, when void in hearts, is uniform
    // over what it holds. Every other play (forced follows, West's lowest card
    // at trick two) uses single_card_defender's lowest-legal rule.
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

    // Captured inside pi, at the node that matches "before": North on lead for
    // trick two (history length 4), after East discarded the spade two, the
    // card the "before" history records. Computed from the view's posteriors,
    // as restricted_choice_test.cpp reads a decision node. The root aggregate
    // answers a different question (see the comment above).
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
                // North's real opening-lead choice, scripted to hearts.
                // lowest_legal_card would pick the lower spade nine, since
                // nothing forces hearts.
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
        return South;  // everything else goes to dummy
    };
    auto [before_root, before_history] =
        build_deal({{Hearts, Ace}, {Spades, Two}, {Hearts, King}, {Hearts, Jack}}, hand_for);
    before_root.trump = DDS_NOTRUMP;
    before_root.first = North;

    ExhaustiveLayoutSource const before_source(
        before_root, North, /*seed=*/2u, make_record(before_history, /*opening_leader=*/North));
    ASSERT_EQ(before_source.history_verdict(), HistoryVerdict::Consistent);
    ASSERT_EQ(before_source.size(), 6u);  // C(6, 1): East's void removes the queen from its pool

    // North holds one card at this root (the spade nine), not the rest of the
    // deck. evaluate() explores every legal first card at the root (to fill
    // EvaluationValue::root_children), so a padded North would also lead a
    // spare heart or diamond. Some of those branches reach a second trick
    // where East and West, holding only the enumerated pool, have no cards
    // left. So the padding goes to South (dummy): dummy never makes the
    // root's first-card decision, and its follow at trick two (its only
    // spade) is forced. Every other play here is forced too (North's one
    // card, South's one spade, East's and West's one enumerated card), so the
    // scripted strategies suffice, unlike inside_pi above.
    DeclarerStrategy const before_pi{
        .id = 1, .play = single_card_declarer_play, .state_key = nullptr};
    EvaluationResult const before_result =
        evaluate(before_root, North, /*tricks_needed=*/1, before_source, before_pi, single_card_defender);
    ASSERT_FALSE(before_result.error.has_value());

    // The two constructions agree, at the node the "before" root represents,
    // captured from inside_pi above.
    EXPECT_NEAR(captured_p_make, before_result.by_strategy.at(1u).p_make, 1e-9);
    EXPECT_NEAR(before_result.by_strategy.at(1u).p_make, 2.0 / 3.0, 1e-9);

    // --- the no-history third value: must differ -----------------------------
    ExhaustiveLayoutSource const before_no_history_source(before_root, North, /*seed=*/2u);
    ASSERT_EQ(before_no_history_source.size(), 7u);  // C(7, 1): the outstanding heart is still eligible
    EvaluationResult const before_no_history_result = evaluate(
        before_root, North, /*tricks_needed=*/1, before_no_history_source, before_pi, single_card_defender);
    ASSERT_FALSE(before_no_history_result.error.has_value());

    // The no-history value differs, so applying voids changes the answer.
    EXPECT_NEAR(before_no_history_result.by_strategy.at(1u).p_make, 5.0 / 7.0, 1e-9);
    EXPECT_NE(
        before_result.by_strategy.at(1u).p_make, before_no_history_result.by_strategy.at(1u).p_make);
}

// --- both defenders void, in different suits -------------------------------
//
// Trick one (hearts): North leads the ace, East (void in hearts) discards a
// spade, South plays the king, West the queen (its jack stays out). Trick
// two (diamonds): North leads the ace, East follows with the king, South
// the queen, and West (void in diamonds) discards a spade (its jack stays
// out). North wins both, so North leads trick three (spades, the one that
// decides): the nine towards dummy's eight, as above.
//
// East's void forces the outstanding heart jack to West. West's void
// forces the outstanding diamond jack to East, already one of East's two
// cards. So East's free choice is one card from the four spades {four,
// five, six, ten}.
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

    ExhaustiveLayoutSource const source(root, North, /*seed=*/3u, make_record(history, /*opening_leader=*/North));
    ASSERT_EQ(source.history_verdict(), HistoryVerdict::Consistent);
    ASSERT_EQ(source.size(), 4u);  // C(4, 1): the four free spades, one needed

    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};
    EvaluationResult const constrained =
        evaluate(root, North, /*tricks_needed=*/1, source, pi, single_card_defender);
    ASSERT_FALSE(constrained.error.has_value());
    EXPECT_NEAR(constrained.by_strategy.at(1u).p_make, 3.0 / 4.0, 1e-9);  // only the ten beats nine

    ExhaustiveLayoutSource const no_history_source(root, North, /*seed=*/3u);
    ASSERT_EQ(no_history_source.size(), 15u);  // C(6, 2): both voids ignored
    EvaluationResult const no_history =
        evaluate(root, North, /*tricks_needed=*/1, no_history_source, pi, single_card_defender);
    ASSERT_FALSE(no_history.error.has_value());
    EXPECT_NE(constrained.by_strategy.at(1u).p_make, no_history.by_strategy.at(1u).p_make);
}

// --- sampling over a constrained space, checked once --------------------
//
// Kept apart from the exhaustive comparisons above: with sampling, a
// disagreement could mean the spaces differ or the samples differ. This
// reuses the two-void fixture and checks only that a sample over the
// constrained space runs and, at sample_size >= size(), reproduces the
// exhaustive answer exactly. exhaustive_layout_source_integration_test.cpp
// checks the same bitwise property without a history.
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

    ExhaustiveLayoutSource const source(root, North, /*seed=*/6u, make_record(history, /*opening_leader=*/North));
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

// --- the void changes which play is right, not only the probability -----
//
// The spade king is missing from a six-card spade pool {king, two, three,
// four, five, six}. East holds four of the pool's ten cards; the other
// four are hearts that a trick-one void removes. Without the void,
// P(East holds the king) is East's share of the whole pool, 4/10 = 0.4:
// below even money, so play West for it. With the void, East's four cards
// must all be spades, and P(East holds the king) = 4/6 ~= 0.667: above
// even money. The side declarer should play has flipped, not only the
// number.
//
// This reads the belief view directly (make_root + make_belief_view), the
// posterior a real decision would use. The point is the probability
// crossing even money, which the posterior shows directly.
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

    auto const p_east_holds_the_king = [&root](ExhaustiveLayoutSource const& source) -> double
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

    ExhaustiveLayoutSource const constrained_source(
        root, North, /*seed=*/5u, make_record(history, /*opening_leader=*/North));
    ASSERT_EQ(constrained_source.history_verdict(), HistoryVerdict::Consistent);
    ASSERT_EQ(constrained_source.size(), 15u);  // C(6, 4): East's void removes the four hearts

    ExhaustiveLayoutSource const unconstrained_source(root, North, /*seed=*/5u);
    ASSERT_EQ(unconstrained_source.size(), 210u);  // C(10, 4)

    double const p_constrained = p_east_holds_the_king(constrained_source);
    double const p_unconstrained = p_east_holds_the_king(unconstrained_source);

    EXPECT_NEAR(p_unconstrained, 0.4, 1e-9);
    EXPECT_NEAR(p_constrained, 2.0 / 3.0, 1e-9);
    EXPECT_LT(p_unconstrained, 0.5);  // unconstrained: play West for the king
    EXPECT_GT(p_constrained, 0.5);    // constrained: play East for the king (the flip)
}

// --- a trump contract where the show-out is a ruff --------------------
//
// Hearts are trump. Trick one: North leads the spade ace, East (void in
// spades) ruffs with its one heart, the deuce, South plays the king and
// West the queen. The ruff wins the trick and establishes the same void a
// discard would. A ruff must count as a show-out like any other. East,
// having won, leads trick two: its last card, a club from the pool {two,
// three, four, ten}, towards North's forced nine. The spade jack is still
// out (declarer's and dummy's spades are gone after trick one), so the
// void has a live card to force.
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

    ExhaustiveLayoutSource const source(root, North, /*seed=*/4u, make_record(history, /*opening_leader=*/North));
    ASSERT_EQ(source.history_verdict(), HistoryVerdict::Consistent);
    ASSERT_EQ(source.size(), 4u);  // C(4, 1): East's void removes the spade jack from its pool

    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};
    EvaluationResult const constrained =
        evaluate(root, North, /*tricks_needed=*/1, source, pi, single_card_defender);
    ASSERT_FALSE(constrained.error.has_value());
    EXPECT_NEAR(constrained.by_strategy.at(1u).p_make, 3.0 / 4.0, 1e-9);  // only the ten beats nine

    ExhaustiveLayoutSource const no_history_source(root, North, /*seed=*/4u);
    ASSERT_EQ(no_history_source.size(), 5u);  // C(5, 1): the spade jack still eligible
    EvaluationResult const no_history =
        evaluate(root, North, /*tricks_needed=*/1, no_history_source, pi, single_card_defender);
    ASSERT_FALSE(no_history.error.has_value());
    EXPECT_NE(constrained.by_strategy.at(1u).p_make, no_history.by_strategy.at(1u).p_make);
}
