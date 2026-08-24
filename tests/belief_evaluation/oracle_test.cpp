#include <gtest/gtest.h>

#include <api/dll.h>
#include <utility/constants.h>

#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/layout_key.hpp>
#include <belief_evaluation/validation.hpp>

#include "test_support.hpp"

// The tests whose expected values are derived by hand rather than read off
// the code — see plans/02_exhaustive_evaluator task 09. Plans 4, 5 and 6
// all have "reproduces plan 2 exactly" as their acceptance criterion, so
// these cases are what certifies the rest of the sequence.

namespace
{
    constexpr int Two = 2;
    constexpr int Five = 5;
    constexpr int Six = 6;
    constexpr int Seven = 7;
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

    /// ♠A opposite North's ♠2/♠3/♠4/♠J — North's side holds every remaining
    /// winner in the suit. Certain, for any belief space over the split of
    /// the low cards among the defenders.
    auto make_certain_win_layout(int east_low, int west_low) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = North;
        deal.remainCards[North][Spades] = holding({Ace});
        deal.remainCards[East][Spades] = holding({east_low});
        deal.remainCards[South][Spades] = holding({3});
        deal.remainCards[West][Spades] = holding({west_low});
        return deal;
    }

    auto strategy(StrategyId id) -> DeclarerStrategy
    {
        return DeclarerStrategy{.id = id, .play = single_card_declarer_play, .state_key = nullptr};
    }
}

class OracleTest : public ::testing::Test
{
};

// --- 9a: certainty ---------------------------------------------------------

TEST_F(OracleTest, CertaintyOverASeveralLayoutBeliefSpace)
{
    // North's ace is the only winner needed and it is unconditionally the
    // highest card in the suit, regardless of how the two low cards
    // Two/Four are split between the defenders. P_make == 1 for any of
    // three distinct splits, proving the belief machinery does not
    // perturb a determined answer.
    VectorLayoutSource source(
        {make_certain_win_layout(Two, /*west=*/4),
         make_certain_win_layout(4, /*west=*/Two),
         make_certain_win_layout(Two, /*west=*/Two)});
    // (the third layout is degenerate -- both defenders "holding" rank 2 is
    // not a real deal, but is_consistent() and the recursion do not care;
    // it only needs to be self-consistent as a Deal, and demonstrates the
    // machinery tolerates an arbitrary extra layout in the space.)

    EvaluationResult const result =
        evaluate(source.at(0), North, /*tricks_needed=*/1, source, strategy(1), single_card_defender);

    ASSERT_FALSE(result.error.has_value());
    EXPECT_DOUBLE_EQ(result.by_strategy.at(1u).p_make, 1.0);
}

// --- 9b: impossibility -------------------------------------------------

TEST_F(OracleTest, Impossibility)
{
    Deal const layout = make_certain_win_layout(Two, 4);
    VectorLayoutSource source({layout});

    // Only one trick exists in the whole ending; two is unreachable no
    // matter what anyone does. Worth having even though it is trivially
    // true: a mass-accounting bug that inflates everything would show up
    // here immediately.
    EvaluationResult const result =
        evaluate(layout, North, /*tricks_needed=*/2, source, strategy(1), single_card_defender);

    ASSERT_FALSE(result.error.has_value());
    EXPECT_DOUBLE_EQ(result.by_strategy.at(1u).p_make, 0.0);
}

// --- 9c: a single defender choice ---------------------------------------

TEST_F(OracleTest, ASingleDefenderChoiceCarriesNoMassOnTheUnchosenBranch)
{
    // East, on lead, holds the king and the two of a suit where North (on
    // this trick's far side) holds the ace. Whichever card East leads,
    // North's ace wins the trick -- but the test's point is not the
    // winner, it is that delta's unchosen branch is *absent*, not present
    // with zero mass.
    Deal layout{};
    layout.trump = DDS_NOTRUMP;
    layout.first = East;
    layout.remainCards[North][Spades] = holding({Ace});
    layout.remainCards[East][Spades] = holding({King, Two});
    layout.remainCards[South][Spades] = holding({3});
    layout.remainCards[West][Spades] = holding({4});
    VectorLayoutSource source({layout});
    BeliefNode const node = *make_root(layout, North, /*tricks_needed=*/1, source);

    ScriptedDefender::Key const key{layout_key(layout, East), ""};
    ScriptedDefender defender({{key, Card{Spades, King}}});  // scripts the king, never the two

    ExpandDefenderResult const result = expand_defender_node(node, defender.as_strategy());

    ASSERT_TRUE(result.children.has_value());
    ASSERT_EQ(result.children->size(), 1u);  // exactly one child: the king's
    EXPECT_EQ((*result.children)[0].layouts.size(), 1u);
    EXPECT_DOUBLE_EQ((*result.children)[0].p[0], 1.0);
    // The two's branch does not exist at all -- there is no second child
    // to inspect, which is the assertion.
}

// --- 9d: the two-way guess -- the most important test in the project ----

namespace
{
    /// North: Spade Two, Club Two (the club is a harmless filler so every
    /// hand holds two cards and the ending stays well-formed to its true
    /// end). South (dummy): Spade Ace, Spade Jack -- the deciding tenace.
    /// East always holds two spades, West always one (plus a club filler)
    /// -- fixed *seats*, not fixed cards; which specific ranks each holds
    /// is what differs between the two layouts below.
    ///
    /// East's two-spade holding is what makes its play uninformative: with
    /// a "play the lowest held" defender, East plays Six whether its other
    /// card is the King (Layout 0) or the Seven (Layout 1) -- Six is lower
    /// than both, so East's visible card is identical either way. West,
    /// holding only one spade, has no such cover: whatever West holds is
    /// revealed the moment it is forced to follow.
    ///
    /// Hand-derivation of P_make == 0.5 for the fixed policy "always play
    /// the jack": North leads its spade Two. East plays Six (see above --
    /// identical in both layouts, so dummy's decision below is genuinely
    /// blind). Dummy commits to the Jack:
    ///
    ///   Layout 0 (East: King+Six, West: Seven): West, forced (its only
    ///   spade), follows with Seven. Jack (11) beats North's Two, East's
    ///   Six and West's Seven -- trick 1 to declarer's side, with the king
    ///   still concealed in East's hand. Dummy leads trick 2 with its
    ///   remaining Ace; North discards its club filler (void in spades);
    ///   East is forced to follow with its remaining King; West discards
    ///   its club filler (void in spades). Ace (14) beats King (13) --
    ///   trick 2 to declarer's side too. 2 of 2 tricks: contract made.
    ///
    ///   Layout 1 (East: Six+Seven, West: King): West, forced (its only
    ///   spade), is *now* holding the king and must play it: King (13)
    ///   beats Jack (11) -- trick 1 to the defence. West (won trick 1)
    ///   leads trick 2 with its remaining club filler; North follows suit
    ///   with its own club filler; East and South, void in clubs, discard
    ///   their remaining spades (East's Seven, South's Ace) uselessly.
    ///   West's own club is the only card that actually follows the led
    ///   suit, so West wins its own trick. 0 of 2 tricks: contract fails.
    ///
    /// Layout 0 and Layout 1 are equally likely (kappa = 1/2, p_i = 1
    /// each), so P_make = 0.5*1 + 0.5*0 = 0.5. An evaluator that solved
    /// each layout independently would find "always jack" wins Layout 0
    /// and "always ace" wins Layout 1, and -- illegitimately, since it
    /// cannot see which layout is real -- report 1.0 by picking whichever
    /// line wins in each. That is strategy fusion, the failure this whole
    /// project exists to avoid.
    auto make_two_way_guess_layout0() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = North;
        deal.remainCards[North][Spades] = holding({Two});
        deal.remainCards[North][Clubs] = holding({Two});
        deal.remainCards[South][Spades] = holding({Ace, Jack});
        deal.remainCards[East][Spades] = holding({King, Six});
        deal.remainCards[West][Spades] = holding({Seven});
        deal.remainCards[West][Clubs] = holding({Five});
        return deal;
    }

    auto make_two_way_guess_layout1() -> Deal
    {
        Deal deal = make_two_way_guess_layout0();
        deal.remainCards[East][Spades] = holding({Six, Seven});
        deal.remainCards[West][Spades] = holding({King});
        return deal;
    }

    /// pi for the two-way-guess test: forced/lowest-legal everywhere,
    /// except at dummy's one real decision (South, still holding both
    /// Ace and Jack), where it verifies the information set is genuine --
    /// two equally-likely, observationally-identical entries -- before
    /// committing, blind to the hidden layout, to the Jack.
    auto play_two_way_guess(ObservationState const& state, BeliefView const& view) -> Card
    {
        int const seat = seat_on_play(state.known_holdings);
        bool const is_dummys_decision =
            seat == South && state.known_holdings.remainCards[South][Spades] == holding({Ace, Jack});

        if (! is_dummys_decision)
        {
            return lowest_legal_card(state.known_holdings, seat);
        }

        EXPECT_EQ(view.entries.size(), 2u);
        KahanAccumulator total_posterior;
        for (BeliefEntry const& entry : view.entries)
        {
            EXPECT_NEAR(entry.posterior, 0.5, 1e-12);
            total_posterior.add(entry.posterior);
        }
        EXPECT_NEAR(total_posterior.value(), 1.0, 1e-12);

        // Declarer's and dummy's holdings -- the observable part of the
        // position -- must be identical *between* the two entries (not
        // against some fixed pre-play constant: North has already led by
        // this point, so its own spade holding is legitimately 0 here,
        // the same in both entries). If the two entries disagreed on any
        // observable field, the ambiguity would already be resolved and
        // 1.0 would be the *correct* answer, not a bug.
        Deal const& first = view.entries[0].layout;
        Deal const& second = view.entries[1].layout;
        for (int hand : {North, South})
        {
            for (int suit = 0; suit < DDS_SUITS; ++suit)
            {
                EXPECT_EQ(first.remainCards[hand][suit], second.remainCards[hand][suit])
                    << "hand=" << hand << " suit=" << suit;
            }
        }
        EXPECT_EQ(first.currentTrickSuit[0], second.currentTrickSuit[0]);
        EXPECT_EQ(first.currentTrickRank[0], second.currentTrickRank[0]);

        return Card{Spades, Jack};
    }

    /// The same fixed "always play the jack" policy, without the info-set
    /// assertions -- for reuse against a *singleton* source (see
    /// TheTwoWayGuessCollapsesToOneIfTheLinePerLayoutIsAllowedToDiffer),
    /// where a genuine two-entry belief view never arises and asserting
    /// one would be asserting the wrong thing.
    auto play_jack_forced(ObservationState const& state, BeliefView const&) -> Card
    {
        int const seat = seat_on_play(state.known_holdings);
        bool const is_dummys_decision =
            seat == South && state.known_holdings.remainCards[South][Spades] == holding({Ace, Jack});
        if (is_dummys_decision)
        {
            return Card{Spades, Jack};
        }
        return lowest_legal_card(state.known_holdings, seat);
    }
}

TEST_F(OracleTest, TheTwoWayGuess)
{
    Deal const layout0 = make_two_way_guess_layout0();
    Deal const layout1 = make_two_way_guess_layout1();
    VectorLayoutSource source({layout0, layout1});

    DeclarerStrategy const pi{.id = 1, .play = play_two_way_guess, .state_key = nullptr};
    EvaluationResult const result =
        evaluate(layout0, North, /*tricks_needed=*/2, source, pi, single_card_defender);

    ASSERT_FALSE(result.error.has_value())
        << "callback=" << static_cast<int>(result.error->callback)
        << " validation=" << static_cast<int>(result.error->validation);
    EXPECT_DOUBLE_EQ(result.by_strategy.at(1u).p_make, 0.5);
}

TEST_F(OracleTest, TheTwoWayGuessCollapsesToOneIfTheLinePerLayoutIsAllowedToDiffer)
{
    // The mutation this guards against: an evaluator that (incorrectly)
    // solved each layout independently and picked whichever line wins in
    // that specific layout would report 1.0 here instead of 0.5. Confirmed
    // by literally doing that -- using a pi that inspects the *true*
    // layout via delta's side channel is not possible (delta and pi don't
    // share layout identity), so instead this recomputes P_make per layout
    // directly against a *singleton* source, which is exactly what
    // strategy fusion amounts to: solving each layout as if it were the
    // whole world.
    Deal const layout0 = make_two_way_guess_layout0();
    Deal const layout1 = make_two_way_guess_layout1();

    DeclarerStrategy const pi{.id = 1, .play = play_jack_forced, .state_key = nullptr};

    VectorLayoutSource source0({layout0});
    EvaluationResult const result0 =
        evaluate(layout0, North, /*tricks_needed=*/2, source0, pi, single_card_defender);
    VectorLayoutSource source1({layout1});
    EvaluationResult const result1 =
        evaluate(layout1, North, /*tricks_needed=*/2, source1, pi, single_card_defender);

    ASSERT_FALSE(result0.error.has_value());
    ASSERT_FALSE(result1.error.has_value());
    // pi's fixed jack policy wins layout 0 alone outright...
    EXPECT_DOUBLE_EQ(result0.by_strategy.at(1u).p_make, 1.0);
    // ...and loses layout 1 alone outright -- (1.0 + 0.0) / 2 != 1.0, so
    // averaging single-layout results is not the same computation as
    // evaluating the joint belief space, which is exactly the point.
    EXPECT_DOUBLE_EQ(result1.by_strategy.at(1u).p_make, 0.0);
}

// --- 9e: delta actually matters -------------------------------------------

TEST_F(OracleTest, DeltaActuallyMatters)
{
    // Same layout, same pi; East (on lead) holds both the King and the Two
    // (a club filler on every other hand keeps the ending well-formed to
    // its true end, though it is irrelevant here since tricks_needed = 1
    // means the outcome is already decided once trick 1 resolves). Two
    // different deltas force East's choice explicitly -- one lets North's
    // side win the trick, the other does not -- confirming delta's answer
    // is actually used, not merely consulted (which every earlier test in
    // this suite would also pass even if delta were consulted and then
    // ignored).
    Deal layout{};
    layout.trump = DDS_NOTRUMP;
    layout.first = East;
    layout.remainCards[North][Spades] = holding({Queen});
    layout.remainCards[North][Clubs] = holding({Two});
    layout.remainCards[East][Spades] = holding({King, Two});
    layout.remainCards[South][Spades] = holding({3});
    layout.remainCards[South][Clubs] = holding({3});
    layout.remainCards[West][Spades] = holding({4});
    layout.remainCards[West][Clubs] = holding({4});
    VectorLayoutSource source({layout});

    // Each only overrides East's *leading* decision, i.e. only while East
    // still holds both cards -- every other query (West following, or
    // East's own forced follow-up in trick 2 once one of its two spades
    // is already gone) falls back to the forced/lowest response.
    auto const east_still_has_the_choice = [](Deal const& layout)
    { return layout.remainCards[East][Spades] == holding({King, Two}); };
    auto const delta_plays_king = [=](DefenderQuery const& query) -> std::vector<WeightedCard>
    {
        if (query.seat == East && east_still_has_the_choice(query.layout))
        {
            return {WeightedCard{Card{Spades, King}, 1.0}};
        }
        return single_card_defender(query);
    };
    auto const delta_plays_two = [=](DefenderQuery const& query) -> std::vector<WeightedCard>
    {
        if (query.seat == East && east_still_has_the_choice(query.layout))
        {
            return {WeightedCard{Card{Spades, Two}, 1.0}};
        }
        return single_card_defender(query);
    };

    EvaluationResult const with_king =
        evaluate(layout, North, /*tricks_needed=*/1, source, strategy(1), delta_plays_king);
    EvaluationResult const with_two =
        evaluate(layout, North, /*tricks_needed=*/1, source, strategy(1), delta_plays_two);

    ASSERT_FALSE(with_king.error.has_value())
        << "callback=" << static_cast<int>(with_king.error->callback)
        << " validation=" << static_cast<int>(with_king.error->validation)
        << " seat=" << with_king.error->seat;
    ASSERT_FALSE(with_two.error.has_value())
        << "callback=" << static_cast<int>(with_two.error->callback)
        << " validation=" << static_cast<int>(with_two.error->validation)
        << " seat=" << with_two.error->seat;
    EXPECT_DOUBLE_EQ(with_king.by_strategy.at(1u).p_make, 0.0);  // King beats North's Queen
    EXPECT_DOUBLE_EQ(with_two.by_strategy.at(1u).p_make, 1.0);   // Queen beats the Two
    EXPECT_NE(with_king.by_strategy.at(1u).p_make, with_two.by_strategy.at(1u).p_make);
}

// --- 9f: determinism ------------------------------------------------------

TEST_F(OracleTest, DeterminismAcrossRepeatedEvaluationAndLayoutOrder)
{
    Deal const layout0 = make_two_way_guess_layout0();
    Deal const layout1 = make_two_way_guess_layout1();
    DeclarerStrategy const pi{.id = 1, .play = play_two_way_guess, .state_key = nullptr};

    VectorLayoutSource source_ab({layout0, layout1});
    EvaluationResult const first =
        evaluate(layout0, North, /*tricks_needed=*/2, source_ab, pi, single_card_defender);
    EvaluationResult const second =
        evaluate(layout0, North, /*tricks_needed=*/2, source_ab, pi, single_card_defender);

    ASSERT_FALSE(first.error.has_value());
    ASSERT_FALSE(second.error.has_value());
    // Bitwise, not EXPECT_DOUBLE_EQ -- this is also the purity check on pi
    // and delta: a strategy accumulating state across calls fails here.
    EXPECT_EQ(first.by_strategy.at(1u).p_make, second.by_strategy.at(1u).p_make);

    VectorLayoutSource source_ba({layout1, layout0});  // layouts in the opposite order
    EvaluationResult const reordered =
        evaluate(layout0, North, /*tricks_needed=*/2, source_ba, pi, single_card_defender);
    ASSERT_FALSE(reordered.error.has_value());
    EXPECT_EQ(first.by_strategy.at(1u).p_make, reordered.by_strategy.at(1u).p_make);
}
