#include <gtest/gtest.h>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/layout_key.hpp>
#include <belief_evaluation/validation.hpp>

#include "test_support.hpp"

namespace be = dds::belief_evaluation;

// Tests whose expected values are derived by hand, not read off the code.
// The sampled, cut and replenishing evaluators are all measured against
// this exhaustive evaluator, so a wrong value here would certify every one
// of them wrongly.

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
        deal.remainCards[North][Spades] = be::holding({Ace});
        deal.remainCards[East][Spades] = be::holding({east_low});
        deal.remainCards[South][Spades] = be::holding({3});
        deal.remainCards[West][Spades] = be::holding({west_low});
        return deal;
    }

    auto strategy(be::StrategyId id) -> be::DeclarerStrategy
    {
        return be::DeclarerStrategy{.id = id, .play = be::single_card_declarer_play, .state_key = nullptr};
    }
}

class OracleTest : public ::testing::Test
{
};

// --- 9a: certainty ---------------------------------------------------------

TEST_F(OracleTest, CertaintyOverASeveralLayoutBeliefSpace)
{
    // North's ace is the only winner needed, and it is the highest card in
    // the suit however the Two and Four are split. P_make == 1 for each of
    // three splits, so the belief machinery does not disturb a determined
    // answer.
    be::VectorLayoutSource source(
        {make_certain_win_layout(Two, /*west=*/4),
         make_certain_win_layout(4, /*west=*/Two),
         make_certain_win_layout(Two, /*west=*/Two)});
    // The third layout is degenerate: both defenders "hold" the Two, which is
    // not a real deal. is_consistent() and the recursion accept it; it only
    // has to be a self-consistent Deal, and it shows that an extra arbitrary
    // layout is tolerated. Its pool ({2}) differs from the others' ({2, 4}),
    // so assert_pool_matches and assert_forms_one_belief_node are not applied
    // across all three. Each layout is still well-formed, which is all
    // assert_equal_hand_sizes checks.
    be::assert_equal_hand_sizes(source.at(0));
    be::assert_equal_hand_sizes(source.at(1));
    be::assert_equal_hand_sizes(source.at(2));

    be::EvaluationResult const result =
        be::evaluate(source.at(0), North, /*tricks_needed=*/1, source, strategy(1), be::single_card_defender);

    ASSERT_FALSE(result.error.has_value());
    EXPECT_DOUBLE_EQ(result.by_strategy.at(1u).p_make, 1.0);
}

// --- 9b: impossibility -------------------------------------------------

TEST_F(OracleTest, Impossibility)
{
    Deal const layout = make_certain_win_layout(Two, 4);
    be::assert_equal_hand_sizes(layout);
    be::VectorLayoutSource source({layout});

    // The ending has one trick, so two is unreachable. Trivial, but a
    // mass-accounting bug that inflates everything would show here at once.
    be::EvaluationResult const result =
        be::evaluate(layout, North, /*tricks_needed=*/2, source, strategy(1), be::single_card_defender);

    ASSERT_FALSE(result.error.has_value());
    EXPECT_DOUBLE_EQ(result.by_strategy.at(1u).p_make, 0.0);
}

// --- 9c: a single defender choice ---------------------------------------

TEST_F(OracleTest, ASingleDefenderChoiceCarriesNoMassOnTheUnchosenBranch)
{
    // East, on lead, holds the king and two of a suit in which North holds the
    // ace. North's ace wins whichever card East leads. The point is not the
    // winner: delta's unchosen branch must be absent, not present with zero
    // mass.
    Deal layout{};
    layout.trump = DDS_NOTRUMP;
    layout.first = East;
    layout.remainCards[North][Spades] = be::holding({Ace});
    layout.remainCards[East][Spades] = be::holding({King, Two});
    layout.remainCards[South][Spades] = be::holding({3});
    layout.remainCards[West][Spades] = be::holding({4});
    // Not assert_equal_hand_sizes(layout): East holds one card more than the
    // others, to give East a real choice, so that check would fail. It is not
    // the unplayable-ending trap the check guards against: this test calls
    // expand_defender_node() for one node and never recurses far enough for
    // the imbalance to matter.
    be::VectorLayoutSource source({layout});
    be::BeliefNode const node = *be::make_root(layout, North, /*tricks_needed=*/1, source).node;

    be::ScriptedDefender::Key const key{be::layout_key(layout, East), ""};
    be::ScriptedDefender defender({{key, be::Card{Spades, King}}});  // scripts the king, never the two

    be::ExpandDefenderResult const result = be::expand_defender_node(node, defender.as_strategy());

    ASSERT_TRUE(result.children.has_value());
    ASSERT_EQ(result.children->size(), 1u);  // exactly one child: the king's
    EXPECT_EQ((*result.children)[0].layouts.size(), 1u);
    EXPECT_DOUBLE_EQ((*result.children)[0].p[0], 1.0);
    // The Two's branch does not exist: there is no second child, which is
    // the assertion.
}

// --- 9d: the two-way guess -- the most important test in the project ----

namespace
{
    /// North: spade Two and club Two (the club is filler so every hand holds
    /// two cards and the ending stays well-formed). South (dummy): spade Ace
    /// and Jack, the tenace that decides. East always holds two spades, West
    /// one (plus a club filler). The seats are fixed; the two layouts differ in
    /// which ranks each holds.
    ///
    /// East's second spade hides its play. A "lowest held" defender makes East
    /// play the Six whether its other card is the King (layout 0) or the Seven
    /// (layout 1), since the Six is lower than both. West, with one spade, has
    /// no such cover: its card shows when it must follow.
    ///
    /// P_make == 0.5 for the fixed policy "always play the jack". North leads
    /// the spade Two, East plays the Six (the same in both layouts, so dummy
    /// decides blind), and dummy plays the Jack:
    ///
    ///   Layout 0 (East: King+Six, West: Seven): West must follow with the
    ///   Seven. The Jack (11) beats the Two, Six and Seven, so declarer's side
    ///   wins trick 1, with the King still hidden in East's hand. Dummy leads
    ///   the Ace; North discards its club (void in spades); East must play the
    ///   King; West discards its club. The Ace (14) beats the King (13), so
    ///   declarer's side wins trick 2 too. 2 of 2 tricks: made.
    ///
    ///   Layout 1 (East: Six+Seven, West: King): West must play its only
    ///   spade, the King, which beats the Jack: trick 1 to the defence. West
    ///   leads its club; North follows with its club; East and South, void in
    ///   clubs, discard their last spades (East's Seven, South's Ace). West's
    ///   club is the only card in the led suit, so West wins. 0 of 2 tricks:
    ///   down.
    ///
    /// The layouts are equally likely (kappa = 1/2, p_i = 1 each), so
    /// P_make = 0.5*1 + 0.5*0 = 0.5. An evaluator that solved each layout
    /// separately would find "always jack" wins layout 0 and "always ace" wins
    /// layout 1, and report 1.0 by picking the winning line in each, though it
    /// cannot see which layout is real. That is strategy fusion, the failure
    /// this project exists to avoid.
    auto make_two_way_guess_layout0() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = North;
        deal.remainCards[North][Spades] = be::holding({Two});
        deal.remainCards[North][Clubs] = be::holding({Two});
        deal.remainCards[South][Spades] = be::holding({Ace, Jack});
        deal.remainCards[East][Spades] = be::holding({King, Six});
        deal.remainCards[West][Spades] = be::holding({Seven});
        deal.remainCards[West][Clubs] = be::holding({Five});
        return deal;
    }

    auto make_two_way_guess_layout1() -> Deal
    {
        Deal deal = make_two_way_guess_layout0();
        deal.remainCards[East][Spades] = be::holding({Six, Seven});
        deal.remainCards[West][Spades] = be::holding({King});
        return deal;
    }

    /// pi for the two-way guess: forced or lowest-legal everywhere except
    /// dummy's one real decision (South, holding Ace and Jack). There it checks
    /// that the information set is real (two equally likely entries that look
    /// the same) before committing to the Jack, blind to the layout.
    auto play_two_way_guess(be::ObservationState const& state, be::BeliefView const& view) -> be::Card
    {
        int const seat = be::seat_on_play(state.known_holdings);
        bool const is_dummys_decision =
            seat == South && state.known_holdings.remainCards[South][Spades] == be::holding({Ace, Jack});

        if (! is_dummys_decision)
        {
            return be::lowest_legal_card(state.known_holdings, seat);
        }

        EXPECT_EQ(view.entries.size(), 2u);
        be::KahanAccumulator total_posterior;
        for (be::BeliefEntry const& entry : view.entries)
        {
            EXPECT_NEAR(entry.posterior, 0.5, 1e-12);
            total_posterior.add(entry.posterior);
        }
        EXPECT_NEAR(total_posterior.value(), 1.0, 1e-12);

        // The observable part of the position, declarer's and dummy's holdings,
        // must be the same in both entries. The comparison is between entries,
        // not against a pre-play constant: North has already led, so its spade
        // holding is 0 in both. If the entries differed on anything observable,
        // the ambiguity would be resolved and 1.0 would be correct.
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

        return be::Card{Spades, Jack};
    }

    /// The same "always play the jack" policy without the information-set
    /// checks, for a singleton source (see
    /// TheTwoWayGuessCollapsesToOneIfTheLinePerLayoutIsAllowedToDiffer), where
    /// no two-entry view arises and the checks would be wrong.
    auto play_jack_forced(be::ObservationState const& state, be::BeliefView const&) -> be::Card
    {
        int const seat = be::seat_on_play(state.known_holdings);
        bool const is_dummys_decision =
            seat == South && state.known_holdings.remainCards[South][Spades] == be::holding({Ace, Jack});
        if (is_dummys_decision)
        {
            return be::Card{Spades, Jack};
        }
        return be::lowest_legal_card(state.known_holdings, seat);
    }
}

TEST_F(OracleTest, TheTwoWayGuess)
{
    Deal const layout0 = make_two_way_guess_layout0();
    Deal const layout1 = make_two_way_guess_layout1();
    be::assert_equal_hand_sizes(layout0);
    be::assert_equal_hand_sizes(layout1);
    be::assert_pool_matches({layout0, layout1});
    be::assert_forms_one_belief_node({layout0, layout1}, North);
    be::VectorLayoutSource source({layout0, layout1});

    be::DeclarerStrategy const pi{.id = 1, .play = play_two_way_guess, .state_key = nullptr};
    be::EvaluationResult const result =
        be::evaluate(layout0, North, /*tricks_needed=*/2, source, pi, be::single_card_defender);

    ASSERT_FALSE(result.error.has_value())
        << "callback=" << static_cast<int>(result.error->callback)
        << " validation=" << static_cast<int>(result.error->validation);
    EXPECT_DOUBLE_EQ(result.by_strategy.at(1u).p_make, 0.5);
}

TEST_F(OracleTest, TheTwoWayGuessCollapsesToOneIfTheLinePerLayoutIsAllowedToDiffer)
{
    // The mutation this guards against: an evaluator that solved each layout
    // separately and picked the winning line in each would report 1.0, not
    // 0.5. pi cannot see the true layout (delta and pi share no layout
    // identity), so this computes P_make per layout against a singleton
    // source instead. That is what strategy fusion amounts to: solving each
    // layout as if it were the whole world.
    Deal const layout0 = make_two_way_guess_layout0();
    Deal const layout1 = make_two_way_guess_layout1();
    be::assert_equal_hand_sizes(layout0);
    be::assert_equal_hand_sizes(layout1);
    be::assert_pool_matches({layout0, layout1});
    be::assert_forms_one_belief_node({layout0, layout1}, North);

    be::DeclarerStrategy const pi{.id = 1, .play = play_jack_forced, .state_key = nullptr};

    be::VectorLayoutSource source0({layout0});
    be::EvaluationResult const result0 =
        be::evaluate(layout0, North, /*tricks_needed=*/2, source0, pi, be::single_card_defender);
    be::VectorLayoutSource source1({layout1});
    be::EvaluationResult const result1 =
        be::evaluate(layout1, North, /*tricks_needed=*/2, source1, pi, be::single_card_defender);

    ASSERT_FALSE(result0.error.has_value());
    ASSERT_FALSE(result1.error.has_value());
    // pi's fixed jack policy wins layout 0 alone outright...
    EXPECT_DOUBLE_EQ(result0.by_strategy.at(1u).p_make, 1.0);
    // ...and loses layout 1 alone. (1.0 + 0.0) / 2 != 1.0, so averaging
    // single-layout results is not the same as evaluating the joint belief
    // space.
    EXPECT_DOUBLE_EQ(result1.by_strategy.at(1u).p_make, 0.0);
}

// --- 9e: delta actually matters -------------------------------------------

TEST_F(OracleTest, DeltaActuallyMatters)
{
    // One layout, one pi. East, on lead, holds the King and the Two (a club
    // filler on the other hands keeps the ending well-formed, though
    // tricks_needed = 1 settles the result at trick 1). Two deltas force East's
    // choice: one lets North's side win the trick, the other does not. So
    // delta's answer is used, not only consulted; the earlier tests would pass
    // even if delta were consulted and ignored.
    Deal layout{};
    layout.trump = DDS_NOTRUMP;
    layout.first = East;
    layout.remainCards[North][Spades] = be::holding({Queen});
    layout.remainCards[North][Clubs] = be::holding({Two});
    layout.remainCards[East][Spades] = be::holding({King, Two});
    layout.remainCards[South][Spades] = be::holding({3});
    layout.remainCards[South][Clubs] = be::holding({3});
    layout.remainCards[West][Spades] = be::holding({4});
    layout.remainCards[West][Clubs] = be::holding({4});
    be::assert_equal_hand_sizes(layout);
    be::VectorLayoutSource source({layout});

    // Each overrides only East's lead, while East still holds both cards.
    // Every other query (West following, East's forced card in trick 2) falls
    // back to the forced or lowest response.
    auto const east_still_has_the_choice = [](Deal const& layout)
    { return layout.remainCards[East][Spades] == be::holding({King, Two}); };
    auto const delta_plays_king = [=](be::DefenderQuery const& query) -> std::vector<be::WeightedCard>
    {
        if (query.seat == East && east_still_has_the_choice(query.layout))
        {
            return {be::WeightedCard{be::Card{Spades, King}, 1.0}};
        }
        return be::single_card_defender(query);
    };
    auto const delta_plays_two = [=](be::DefenderQuery const& query) -> std::vector<be::WeightedCard>
    {
        if (query.seat == East && east_still_has_the_choice(query.layout))
        {
            return {be::WeightedCard{be::Card{Spades, Two}, 1.0}};
        }
        return be::single_card_defender(query);
    };

    be::EvaluationResult const with_king =
        be::evaluate(layout, North, /*tricks_needed=*/1, source, strategy(1), delta_plays_king);
    be::EvaluationResult const with_two =
        be::evaluate(layout, North, /*tricks_needed=*/1, source, strategy(1), delta_plays_two);

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
    be::assert_equal_hand_sizes(layout0);
    be::assert_equal_hand_sizes(layout1);
    be::assert_pool_matches({layout0, layout1});
    be::assert_forms_one_belief_node({layout0, layout1}, North);
    be::DeclarerStrategy const pi{.id = 1, .play = play_two_way_guess, .state_key = nullptr};

    be::VectorLayoutSource source_ab({layout0, layout1});
    be::EvaluationResult const first =
        be::evaluate(layout0, North, /*tricks_needed=*/2, source_ab, pi, be::single_card_defender);
    be::EvaluationResult const second =
        be::evaluate(layout0, North, /*tricks_needed=*/2, source_ab, pi, be::single_card_defender);

    ASSERT_FALSE(first.error.has_value());
    ASSERT_FALSE(second.error.has_value());
    // Bitwise, not EXPECT_DOUBLE_EQ. This is also the purity check on pi and
    // delta: a strategy that keeps state across calls fails here.
    EXPECT_EQ(first.by_strategy.at(1u).p_make, second.by_strategy.at(1u).p_make);

    be::VectorLayoutSource source_ba({layout1, layout0});  // layouts in the opposite order
    be::EvaluationResult const reordered =
        be::evaluate(layout0, North, /*tricks_needed=*/2, source_ba, pi, be::single_card_defender);
    ASSERT_FALSE(reordered.error.has_value());
    EXPECT_EQ(first.by_strategy.at(1u).p_make, reordered.by_strategy.at(1u).p_make);
}
