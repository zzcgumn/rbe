// real_work_declarer_play() (strategies.hpp) is the "real work" pi
// timer.{cpp,py} time against trivial_declarer_play's O(1) rule -- its own
// scoring is what makes that comparison mean anything, so it gets a
// hand-derived correctness check independent of evaluate() and of any
// fixture (tests/belief_evaluation/oracle_test.cpp's own precedent
// for why a small enough case is safe to hand-derive rather than build a
// "same construction, once" generator for: a transcription slip here would
// show up as *some other score*, not as an agreement with the wrong one).
// The Python port in
// python/tests/test_belief_space_local_evaluation_python_cost.py works the
// identical case independently, the same way oracle_test.cpp's own cases do
// on the Python side.
#include <vector>

#include <gtest/gtest.h>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/types.hpp>

#include "strategies.hpp"

namespace be = dds::belief_evaluation;
namespace strategies = dds::belief_evaluation::benchmarks::python_cost;

namespace
{
    constexpr int Spades = 0;

    constexpr int North = 0;
    constexpr int East = 1;

    auto holding(std::initializer_list<int> ranks) -> unsigned
    {
        unsigned bits = 0;
        for (int rank : ranks)
        {
            bits |= (1u << rank);
        }
        return bits;
    }
}  // namespace

// North holds the Five and the Ace of Spades, on lead (no trick in
// progress) -- both are legal. Two equally likely layouts: one where East
// holds the King (beats the Five, not the Ace), one where neither defender
// holds anything in Spades at all.
//
//   score(Five) = 0.5 * 1 (East's King beats it in layout A) + 0.5 * 0 = 0.5
//   score(Ace)  = 0.5 * 0 (nothing beats an Ace)             + 0.5 * 0 = 0.0
//
// The Ace's score is strictly lower -- real_work_declarer_play must return
// it, not the Five lowest_legal_card would.
TEST(RealWorkDeclarerPlayTest, PlaysTheCardNoDefenderLayoutCanBeat)
{
    be::ObservationState state{};
    state.declarer = North;
    state.known_holdings.first = North;
    state.known_holdings.remainCards[North][Spades] = holding({5, 14});

    Deal layout_a{};
    layout_a.remainCards[East][Spades] = holding({13});

    Deal layout_b{};  // both defenders void in Spades

    std::vector<be::BeliefEntry> const entries{
        be::BeliefEntry{.layout = layout_a, .posterior = 0.5},
        be::BeliefEntry{.layout = layout_b, .posterior = 0.5},
    };
    be::BeliefView const view{.entries = entries, .is_sample = false, .space_size = 2};

    be::Card const played = strategies::real_work_declarer_play(state, view);
    EXPECT_EQ(played.suit, Spades);
    EXPECT_EQ(played.rank, 14);
}

// Same two layouts, but North holds only the Five -- no choice to make.
// Guards the "only one legal card" path (`enumerate_legal_cards` returning
// a single entry) separately from the scoring comparison above.
TEST(RealWorkDeclarerPlayTest, ReturnsTheOnlyLegalCardWhenThereIsNoChoice)
{
    be::ObservationState state{};
    state.declarer = North;
    state.known_holdings.first = North;
    state.known_holdings.remainCards[North][Spades] = holding({5});

    Deal layout_a{};
    layout_a.remainCards[East][Spades] = holding({13});
    std::vector<be::BeliefEntry> const entries{be::BeliefEntry{.layout = layout_a, .posterior = 1.0}};
    be::BeliefView const view{.entries = entries, .is_sample = false, .space_size = 1};

    be::Card const played = strategies::real_work_declarer_play(state, view);
    EXPECT_EQ(played.suit, Spades);
    EXPECT_EQ(played.rank, 5);
}

// A tie (every candidate's score is identical, here because the belief
// view is empty and every score is vacuously 0) breaks by lowest rank --
// the same deterministic tie-break lowest_legal_card uses, so the two
// strategies agree whenever the safety scoring itself carries no
// information, rather than picking arbitrarily.
TEST(RealWorkDeclarerPlayTest, TiesBreakByLowestRank)
{
    be::ObservationState state{};
    state.declarer = North;
    state.known_holdings.first = North;
    state.known_holdings.remainCards[North][Spades] = holding({5, 14});

    std::vector<be::BeliefEntry> const entries{};  // no belief mass anywhere -- every score is 0
    be::BeliefView const view{.entries = entries, .is_sample = false, .space_size = 0};

    be::Card const played = strategies::real_work_declarer_play(state, view);
    EXPECT_EQ(played.suit, Spades);
    EXPECT_EQ(played.rank, 5);
}
