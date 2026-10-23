#include <gtest/gtest.h>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/validation.hpp>

#include "test_support.hpp"

namespace be = dds::belief_evaluation;

// Tests restricted choice: declarer's belief about where a missing honour
// sits must shift after a defender plays an honour, unless that play
// carries no information. See docs/replenished_belief_evaluation/algorithm.md
// for the theory.

namespace
{
    constexpr int Two = 2;
    constexpr int Three = 3;
    constexpr int Four = 4;
    constexpr int Queen = 12;
    constexpr int King = 13;
    constexpr int Ace = 14;

    constexpr int Diamonds = 2;  // the suit under test
    constexpr int Clubs = 3;     // filler, keeps every hand the same size

    constexpr int North = 0;  // declarer
    constexpr int East = 1;   // the defender who may hold both honours
    constexpr int South = 2;  // dummy
    constexpr int West = 3;   // the other defender

    /// Diamonds: North (declarer) leads the ♦3 toward South's (dummy's) ♦A.
    /// South keeps the ace until the decision point.
    ///
    /// The two layouts split the same pool {K, Q, 4, 2} differently:
    ///   layout A: East holds ♦KQ (a real choice), West ♦42.
    ///   layout B: East holds ♦K4, West ♦Q2 (East plays the king for sure).
    /// A club in North and in South gives every hand two cards, so the ending
    /// plays out to the end.
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

    /// East plays the king or queen with 0.5 each when holding both (layout
    /// A), and the one honour with certainty otherwise (layout B). This is
    /// restricted choice: one honour is always shown, while from two each is
    /// shown half the time. Every other query (West's follow, later forced
    /// plays) falls back to lowest_legal_card, like single_card_defender.
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

    /// A delta with no information: East plays the king with certainty
    /// whenever it holds one, with or without the queen. A defender who always
    /// plays the king from KQ tells declarer nothing about the queen.
    auto deterministic_delta(be::DefenderQuery const& query) -> std::vector<be::WeightedCard>
    {
        unsigned const east_diamonds = query.layout.remainCards[East][Diamonds];
        if (query.seat == East && (east_diamonds & be::holding({King})) != 0)
        {
            return {be::WeightedCard{be::Card{Diamonds, King}, 1.0}};
        }
        return be::single_card_defender(query);
    }

    /// True only at the decision point: South (dummy) is on play, still
    /// holds the ♦A, and East's reply (the second card in history) was the
    /// king. East's queen branch also reaches South on play, but it holds only
    /// layout A, since East never plays the queen in layout B. The two-layout
    /// view this test checks exists only in the king branch.
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

    /// Used by both pi variants below. North leads the ♦3, and every other
    /// play is the lowest legal card. Declarer and dummy never have another
    /// real choice.
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

    /// Tells the two layouts apart at the decision point by East's remaining
    /// diamond, after its honour is played: the queen in layout A, the four in
    /// layout B.
    auto is_layout_a(Deal const& layout) -> bool
    {
        return layout.remainCards[East][Diamonds] == be::holding({Queen});
    }
}

class RestrictedChoiceTest : public ::testing::Test
{
};

// --- the shift under a stochastic delta -------------------------------------

TEST_F(RestrictedChoiceTest, TheShiftAfterAnHonourAppearsFromADoubletonHonourHolding)
{
    Deal const layout_a = make_layout_a();
    Deal const layout_b = make_layout_b();
    be::assert_equal_hand_sizes(layout_a);
    be::assert_equal_hand_sizes(layout_b);
    be::assert_pool_matches({layout_a, layout_b});
    be::assert_forms_one_belief_node({layout_a, layout_b}, North);

    // Derivation (equal priors, kappa = 1/2, p_i = 1 at the root):
    //   layout A: delta plays the king with 0.5, so king-branch p = 0.5.
    //   layout B: delta plays the king with 1.0, so king-branch p = 1.0.
    // North's lead is a declarer node and leaves p and kappa unchanged, so at
    // the decision point:
    //   mass(A) = kappa * 0.5 = 0.25
    //   mass(B) = kappa * 1.0 = 0.50
    //   total   = 0.75
    //   posterior(A) = 0.25 / 0.75 = 1/3
    //   posterior(B) = 0.50 / 0.75 = 2/3
    // East had a choice in layout A, so seeing the king is twice as likely
    // under layout B: the classic 2:1 restricted-choice shift.
    be::DeclarerStrategy const pi{
        .id = 1,
        .play =
            [&](be::ObservationState const& state, be::BeliefView const& view) -> be::Card
        {
            if (is_dummys_decision(state))
            {
                // Check the information set first: the view must show exactly
                // the two layouts above. pi asserts this itself.
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

// --- no shift under a deterministic delta ------------------------------------

TEST_F(RestrictedChoiceTest, ADeterministicDefenceShowsNoShiftOnTheSameFixture)
{
    Deal const layout_a = make_layout_a();
    Deal const layout_b = make_layout_b();
    be::assert_equal_hand_sizes(layout_a);
    be::assert_equal_hand_sizes(layout_b);
    be::assert_pool_matches({layout_a, layout_b});
    be::assert_forms_one_belief_node({layout_a, layout_b}, North);

    // Derivation: East plays the king with certainty in both layouts, so
    // neither layout's mass is split. Both reach the decision point with mass
    // kappa * 1, and the posterior stays at the 0.5/0.5 prior. If both tests
    // showed the same shift, the belief update would not be reading delta.
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
