#include <gtest/gtest.h>

#include <belief_evaluation/node.hpp>

// Tests for terminal_value and is_terminal.

// A namespace alias and targeted `using` declarations, not `using
// namespace dds::belief_evaluation;`. The include of node.hpp brings in the
// global ::Card from api/dds_data_types.hpp, so a bare `Card` would be
// ambiguous instead of a clear compile error.
namespace be = dds::belief_evaluation;
using be::BeliefNode;
using be::is_terminal;
using be::terminal_value;

class TerminalTest : public ::testing::Test
{
};

TEST_F(TerminalTest, ValueIsFullNodeMassWhenContractMade)
{
    BeliefNode node{};
    node.p = {0.25, 0.75};
    node.kappa = 0.5;
    node.state.tricks_needed = 3;
    node.state.tricks_won_by_declarer = 3;

    EXPECT_DOUBLE_EQ(terminal_value(node), 0.5);  // 0.5 * (0.25 + 0.75)
}

TEST_F(TerminalTest, ValueIsZeroWhenContractFails)
{
    BeliefNode node{};
    node.p = {0.25, 0.75};
    node.kappa = 0.5;
    node.state.tricks_needed = 4;
    node.state.tricks_won_by_declarer = 3;

    EXPECT_DOUBLE_EQ(terminal_value(node), 0.0);
}

TEST_F(TerminalTest, ValueUsesUnequalPToRuleOutAnAveragingBug)
{
    // {0.5, 0.5} would also pass if the mass were averaged instead of
    // summed. These values catch that bug.
    BeliefNode node{};
    node.p = {0.1, 0.2, 0.3};
    node.kappa = 2.0;
    node.state.tricks_needed = 5;
    node.state.tricks_won_by_declarer = 5;

    EXPECT_DOUBLE_EQ(terminal_value(node), 1.2);  // 2.0 * (0.1 + 0.2 + 0.3)
}

TEST_F(TerminalTest, ContractExactlyMadeCountsAsMade)
{
    BeliefNode node{};
    node.p = {1.0};
    node.kappa = 1.0;
    node.state.tricks_needed = 6;
    node.state.tricks_won_by_declarer = 6;  // exactly, not more

    EXPECT_DOUBLE_EQ(terminal_value(node), 1.0);
}

// --- is_terminal -------------------------------------------------------

TEST_F(TerminalTest, EmptyHandsAreTerminal)
{
    Deal exhausted{};  // every remainCards entry already zero
    BeliefNode node{};
    node.layouts = {exhausted};

    EXPECT_TRUE(is_terminal(node));
}

TEST_F(TerminalTest, EvenOneCardInOneHandIsNotTerminal)
{
    Deal deal{};
    deal.remainCards[3][0] = 1u << 2;  // West still holds the deuce of spades
    BeliefNode node{};
    node.layouts = {deal};

    EXPECT_FALSE(is_terminal(node));
}
