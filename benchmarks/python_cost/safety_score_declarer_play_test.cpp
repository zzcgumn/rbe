// real_work_declarer_play() (strategies.hpp) is the "real work" pi that
// timer.cpp and timer.py time against trivial_declarer_play. Its scoring
// gives that comparison its meaning, so it gets a hand-derived check,
// independent of evaluate() and of any fixture. As in
// tests/belief_evaluation/oracle_test.cpp, a case this small is safe to
// derive by hand: a transcription slip shows up as some other score, not
// as agreement with a wrong one. The Python port in
// python/tests/test_belief_space_local_evaluation_python_cost.py works the
// same case independently.
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

// North holds the Five and Ace of Spades and is on lead, so both are
// legal. Two equally likely layouts: in A, East holds the King (it beats
// the Five, not the Ace); in B, neither defender holds a Spade.
//
//   score(Five) = 0.5 * 1 (East's King beats it in layout A) + 0.5 * 0 = 0.5
//   score(Ace)  = 0.5 * 0 (nothing beats an Ace)             + 0.5 * 0 = 0.0
//
// The Ace scores lower, so real_work_declarer_play must return it, not the
// Five that lowest_legal_card would.
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

// The same two layouts, but North holds only the Five, so there is no
// choice. Tests the path where `enumerate_legal_cards` returns one card,
// apart from the scoring above.
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

// A tie (here the view is empty, so every score is 0) breaks by lowest
// rank, as lowest_legal_card does. So the two strategies agree whenever
// the scoring carries no information.
TEST(RealWorkDeclarerPlayTest, TiesBreakByLowestRank)
{
    be::ObservationState state{};
    state.declarer = North;
    state.known_holdings.first = North;
    state.known_holdings.remainCards[North][Spades] = holding({5, 14});

    std::vector<be::BeliefEntry> const entries{};  // no belief mass, so every score is 0
    be::BeliefView const view{.entries = entries, .is_sample = false, .space_size = 0};

    be::Card const played = strategies::real_work_declarer_play(state, view);
    EXPECT_EQ(played.suit, Spades);
    EXPECT_EQ(played.rank, 5);
}
