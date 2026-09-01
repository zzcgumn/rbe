#include <gtest/gtest.h>

#include <api/dds_data_types.hpp>
#include <utility/constants.h>

#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/validation.hpp>

#include "test_support.hpp"

namespace be = dds::belief_evaluation;

// The capability's central acceptance test: declarer's belief about where a
// missing honour sits shifts correctly after a defender produces its
// partner, and does not shift at all when the defender's play carries no
// information (see docs/replenished_belief_evaluation/algorithm.md for the
// theory this exercises).

namespace
{
    constexpr int Two = 2;
    constexpr int Three = 3;
    constexpr int Four = 4;
    constexpr int Queen = 12;
    constexpr int King = 13;
    constexpr int Ace = 14;

    constexpr int Diamonds = 2;  // the suit under test
    constexpr int Clubs = 3;     // harmless filler, keeps every hand equal-sized

    constexpr int North = 0;  // declarer
    constexpr int East = 1;   // the defender who may hold both honours
    constexpr int South = 2;  // dummy
    constexpr int West = 3;   // the other defender

    /// Spades A-J opposite xxx, missing the king and queen -- declarer
    /// leads low from hand (North's 3) toward dummy's ace (South), which
    /// stays untouched until the decision point.
    ///
    /// East's diamonds (and West's complementary holding) are what differ
    /// between the two layouts; both keep the same 4-card pool
    /// {King, Queen, Two, Four} between them, differing only in the split:
    ///   layout A: East holds king AND queen (a genuine choice).
    ///   layout B: East holds king only; West holds queen (forced).
    /// A club filler in North and South keeps every hand at exactly two
    /// cards, so the ending is well-formed to its true end.
    auto make_layout_a() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = North;
        deal.remainCards[North][Diamonds] = be::holding({Three});
        deal.remainCards[North][Clubs] = be::holding({Two});
        deal.remainCards[South][Diamonds] = be::holding({Ace});
        deal.remainCards[South][Clubs] = be::holding({Three});
        deal.remainCards[East][Diamonds] = be::holding({King, Queen});
        deal.remainCards[West][Diamonds] = be::holding({Two, Four});
        return deal;
    }

    auto make_layout_b() -> Deal
    {
        Deal deal = make_layout_a();
        deal.remainCards[East][Diamonds] = be::holding({King, Four});
        deal.remainCards[West][Diamonds] = be::holding({Queen, Two});
        return deal;
    }

    /// East's decision: 0.5/0.5 over king and queen when it genuinely holds
    /// both (layout A); certainty on whichever single honour it holds
    /// otherwise (layout B) -- "restricted choice" itself, that a defender
    /// with only one honour reveals it with certainty while a defender
    /// with both reveals either only half the time. Every other query
    /// (West's forced follow, any later forced play) falls back to
    /// lowest_legal_card, matching single_card_defender.
    auto restricted_choice_delta(be::DefenderQuery const& query) -> std::vector<be::WeightedCard>
    {
        unsigned const east_diamonds = query.layout.remainCards[East][Diamonds];
        bool const east_holds_an_honour =
            query.seat == East && (east_diamonds & be::holding({King, Queen})) != 0;
        if (! east_holds_an_honour)
        {
            return be::single_card_defender(query);
        }
        if (east_diamonds == be::holding({King, Queen}))
        {
            return {
                be::WeightedCard{be::Card{Diamonds, King}, 0.5}, be::WeightedCard{be::Card{Diamonds, Queen}, 0.5}};
        }
        int const honour = (east_diamonds & be::holding({King})) != 0 ? King : Queen;
        return {be::WeightedCard{be::Card{Diamonds, honour}, 1.0}};
    }

    /// The same fixture's delta with no information content: East plays
    /// the king with certainty whenever it holds one, whether or not it
    /// also holds the queen. A defender that always plays the king from KQ
    /// tells declarer nothing about the queen.
    auto deterministic_delta(be::DefenderQuery const& query) -> std::vector<be::WeightedCard>
    {
        unsigned const east_diamonds = query.layout.remainCards[East][Diamonds];
        if (query.seat == East && (east_diamonds & be::holding({King})) != 0)
        {
            return {be::WeightedCard{be::Card{Diamonds, King}, 1.0}};
        }
        return be::single_card_defender(query);
    }

    /// True at the one node this fixture's decision point can be recognised
    /// by: South (dummy) is on play, still holding its ace of diamonds, and
    /// East's reply (the second card in history) was specifically the king
    /// -- not the queen. Both East's card-key branches reach a South-on-
    /// play node (North's lead is a single, layout-independent declarer
    /// node either way), but only the king branch is the two-layout
    /// decision point this test derives; the queen branch holds layout A
    /// alone (East never plays the queen in layout B) and asserting a
    /// two-entry view there would be asserting the wrong node.
    auto is_dummys_decision(be::ObservationState const& state) -> bool
    {
        int const seat = be::seat_on_play(state.known_holdings);
        bool const south_still_holds_the_ace =
            state.known_holdings.remainCards[South][Diamonds] == be::holding({Ace});
        bool const easts_reply_was_the_king =
            state.history.number == 2 && state.history.suit[1] == Diamonds
            && state.history.rank[1] == King;
        return seat == South && south_still_holds_the_ace && easts_reply_was_the_king;
    }

    /// Shared by both pi variants below: North's forced opening lead, and
    /// (once past the decision point) every other play is forced/lowest --
    /// nothing in this fixture ever gives declarer or dummy a second real
    /// choice.
    auto forced_play(be::ObservationState const& state) -> be::Card
    {
        int const seat = be::seat_on_play(state.known_holdings);
        bool const is_declarer_first_lead =
            seat == North && state.known_holdings.remainCards[North][Diamonds] == be::holding({Three});
        if (is_declarer_first_lead)
        {
            return be::Card{Diamonds, Three};
        }
        return be::lowest_legal_card(state.known_holdings, seat);
    }

    /// Distinguishes the two surviving layouts at the decision point by
    /// East's *remaining* diamond (its honour already played): layout A
    /// left the queen behind, layout B left the four.
    auto is_layout_a(Deal const& layout) -> bool
    {
        return layout.remainCards[East][Diamonds] == be::holding({Queen});
    }
}

class RestrictedChoiceTest : public ::testing::Test
{
};

// --- criterion 1: the shift under a genuinely stochastic delta -----------

TEST_F(RestrictedChoiceTest, TheShiftAfterAnHonourAppearsFromADoubletonHonourHolding)
{
    Deal const layout_a = make_layout_a();
    Deal const layout_b = make_layout_b();
    be::assert_equal_hand_sizes(layout_a);
    be::assert_equal_hand_sizes(layout_b);
    be::assert_pool_matches({layout_a, layout_b});
    be::assert_forms_one_belief_node({layout_a, layout_b}, North);

    // Derivation (equal priors, kappa = 1/2, p_i = 1 at the root):
    //   layout A: p = 1, delta plays the king 0.5 -- king-branch p = 0.5.
    //   layout B: p = 1, delta plays the king 1.0 (certain) -- king-branch p = 1.0.
    // Declarer nodes (North's lead, then South's forced follow) pass p and
    // kappa through unchanged, so at the decision point:
    //   mass(A) = kappa * 0.5 = 0.25
    //   mass(B) = kappa * 1.0 = 0.50
    //   total   = 0.75
    //   posterior(A) = 0.25 / 0.75 = 1/3
    //   posterior(B) = 0.50 / 0.75 = 2/3
    // The defender having *had* a choice in layout A is what makes the
    // observation (the king) twice as likely under layout B -- the classic
    // 2:1 restricted-choice shift.
    be::DeclarerStrategy const pi{
        .id = 1,
        .play =
            [&](be::ObservationState const& state, be::BeliefView const& view) -> be::Card
        {
            if (is_dummys_decision(state))
            {
                // Info set first: the recorded view must show exactly the
                // two layouts above before any probability is trusted --
                // asserted from inside pi, not assumed by the test.
                EXPECT_EQ(view.entries.size(), 2u);
                Deal const& first = view.entries[0].layout;
                Deal const& second = view.entries[1].layout;
                for (int suit = 0; suit < DDS_SUITS; ++suit)
                {
                    EXPECT_EQ(first.remainCards[North][suit], second.remainCards[North][suit]);
                    EXPECT_EQ(first.remainCards[South][suit], second.remainCards[South][suit]);
                }
                EXPECT_EQ(first.currentTrickSuit[0], second.currentTrickSuit[0]);
                EXPECT_EQ(first.currentTrickRank[0], second.currentTrickRank[0]);

                be::BeliefEntry const& entry_a =
                    is_layout_a(view.entries[0].layout) ? view.entries[0] : view.entries[1];
                be::BeliefEntry const& entry_b =
                    is_layout_a(view.entries[0].layout) ? view.entries[1] : view.entries[0];
                EXPECT_NEAR(entry_a.posterior, 1.0 / 3.0, 1e-9);
                EXPECT_NEAR(entry_b.posterior, 2.0 / 3.0, 1e-9);
                be::KahanAccumulator total;
                total.add(entry_a.posterior);
                total.add(entry_b.posterior);
                EXPECT_NEAR(total.value(), 1.0, 1e-9);
            }
            return forced_play(state);
        },
        .state_key = nullptr,
    };

    be::VectorLayoutSource source({layout_a, layout_b});
    be::EvaluationResult const result =
        be::evaluate(layout_a, North, /*tricks_needed=*/1, source, pi, restricted_choice_delta);

    ASSERT_FALSE(result.error.has_value());
}

// --- criterion 2: no shift under a deterministic delta --------------------

TEST_F(RestrictedChoiceTest, ADeterministicDefenceShowsNoShiftOnTheSameFixture)
{
    Deal const layout_a = make_layout_a();
    Deal const layout_b = make_layout_b();
    be::assert_equal_hand_sizes(layout_a);
    be::assert_equal_hand_sizes(layout_b);
    be::assert_pool_matches({layout_a, layout_b});
    be::assert_forms_one_belief_node({layout_a, layout_b}, North);

    // Derivation: East now plays the king with certainty in *both* layouts
    // (never the queen from layout A), so neither layout's mass is ever
    // split -- both arrive at the decision point with their full prior
    // mass, kappa * 1 each, and the posterior is exactly the 0.5/0.5 prior
    // it started as. If both fixtures showed the same shift, the belief
    // update would not be reading delta at all.
    be::DeclarerStrategy const pi{
        .id = 1,
        .play =
            [](be::ObservationState const& state, be::BeliefView const& view) -> be::Card
        {
            if (is_dummys_decision(state))
            {
                EXPECT_EQ(view.entries.size(), 2u);
                for (be::BeliefEntry const& entry : view.entries)
                {
                    EXPECT_NEAR(entry.posterior, 0.5, 1e-9);
                }
            }
            return forced_play(state);
        },
        .state_key = nullptr,
    };

    be::VectorLayoutSource source({layout_a, layout_b});
    be::EvaluationResult const result =
        be::evaluate(layout_a, North, /*tricks_needed=*/1, source, pi, deterministic_delta);

    ASSERT_FALSE(result.error.has_value());
}
