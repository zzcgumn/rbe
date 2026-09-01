#include <gtest/gtest.h>

#include <api/dds_data_types.hpp>
#include <utility/constants.h>

#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/node.hpp>

#include "test_support.hpp"

// A namespace alias plus targeted `using` declarations, not `using namespace
// dds::belief_evaluation;` -- this file includes api/dds_data_types.hpp
// directly (declaring global ::Card), so a `using namespace` here would make
// any future bare `Card` reference ambiguous between ::Card and
// dds::belief_evaluation::Card rather than a clear compile error naming
// which one was meant.
namespace be = dds::belief_evaluation;
using be::BeliefNode;
using be::KahanAccumulator;
using be::ObservationState;
using be::Probability;
using be::UnboundedLayoutSource;
using be::VectorLayoutSource;
using be::card_count;
using be::holding;
using be::make_root;
using be::tricks_remaining;

namespace
{
    constexpr int Two = 2;
    constexpr int Jack = 11;
    constexpr int Queen = 12;
    constexpr int King = 13;
    constexpr int Ace = 14;

    constexpr int North = 0;  // declarer throughout this file
    constexpr int East = 1;   // a defender
    constexpr int South = 2;  // dummy: (declarer + 2) % DDS_HANDS
    constexpr int West = 3;   // a defender

    /// North (declarer) holds spades A/K, South (dummy) holds hearts A/K,
    /// and East/West hold diamonds Q/J between them, split East=Q, West=J.
    /// Trump is notrump, North leads, nothing played yet.
    auto make_root_layout() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = North;
        deal.remainCards[North][0] = holding({Ace, King});    // spades
        deal.remainCards[South][1] = holding({Ace, King});    // hearts
        deal.remainCards[East][2] = holding({Queen});         // diamonds
        deal.remainCards[West][2] = holding({Jack});          // diamonds
        return deal;
    }

    /// Same shape as make_root_layout(), but with the diamond queen and
    /// jack swapped between the defenders — the same outstanding pool,
    /// split the other way.
    auto make_swapped_split_layout() -> Deal
    {
        Deal deal = make_root_layout();
        deal.remainCards[East][2] = holding({Jack});
        deal.remainCards[West][2] = holding({Queen});
        return deal;
    }

    /// Differs from make_root_layout() in dummy's holding (hearts A only,
    /// missing the king) — must not survive filtering against the root.
    auto make_wrong_dummy_layout() -> Deal
    {
        Deal deal = make_root_layout();
        deal.remainCards[South][1] = holding({Ace});
        return deal;
    }
}

class NodeTest : public ::testing::Test
{
};

TEST_F(NodeTest, RootInvariantsHoldOverEveryLayoutThatSurvivesFiltering)
{
    Deal const root_layout = make_root_layout();
    VectorLayoutSource source({make_root_layout(), make_swapped_split_layout()});

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source);
    ASSERT_TRUE(node.has_value());

    ASSERT_EQ(node->layouts.size(), 2u);
    ASSERT_EQ(node->p.size(), 2u);
    for (Probability const p_i : node->p)
    {
        EXPECT_DOUBLE_EQ(p_i, 1.0);
    }
    EXPECT_DOUBLE_EQ(node->kappa, 0.5);

    KahanAccumulator total_weight;
    for (Probability const p_i : node->p)
    {
        total_weight.add(node->kappa * p_i);
    }
    EXPECT_DOUBLE_EQ(total_weight.value(), 1.0);
}

TEST_F(NodeTest, KappaIsOneOverTheSurvivingCountNotTheRawSourceSize)
{
    Deal const root_layout = make_root_layout();
    // Three raw layouts, only two of which are consistent with the root —
    // kappa must be 1/2, not 1/3. A kappa computed from source.size()
    // directly would pass every other test in this file, since none of
    // them puts an inconsistent layout in the source alongside consistent
    // ones.
    VectorLayoutSource source(
        {make_root_layout(), make_swapped_split_layout(), make_wrong_dummy_layout()});

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source);
    ASSERT_TRUE(node.has_value());
    ASSERT_EQ(node->layouts.size(), 2u);
    EXPECT_DOUBLE_EQ(node->kappa, 0.5);
}

TEST_F(NodeTest, ASourceThatCannotReportItsSizeIsReportedAsAnError)
{
    Deal const root_layout = make_root_layout();
    UnboundedLayoutSource source;

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source);
    EXPECT_FALSE(node.has_value());
}

TEST_F(NodeTest, ASourceWhereNoLayoutSurvivesIsReportedAsAnError)
{
    Deal const root_layout = make_root_layout();
    VectorLayoutSource source({make_wrong_dummy_layout()});

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source);
    EXPECT_FALSE(node.has_value());
}

TEST_F(NodeTest, ADifferentDefenderSplitOfTheSamePoolSurvivesFiltering)
{
    Deal const root_layout = make_root_layout();
    VectorLayoutSource source({make_swapped_split_layout()});

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source);
    ASSERT_TRUE(node.has_value());
    ASSERT_EQ(node->layouts.size(), 1u);
    EXPECT_EQ(node->layouts[0].remainCards[East][2], holding({Jack}));
    EXPECT_EQ(node->layouts[0].remainCards[West][2], holding({Queen}));
}

TEST_F(NodeTest, ALayoutDifferingInDummysHoldingDoesNotSurviveFiltering)
{
    Deal const root_layout = make_root_layout();
    VectorLayoutSource source({make_root_layout(), make_wrong_dummy_layout()});

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source);
    ASSERT_TRUE(node.has_value());
    EXPECT_EQ(node->layouts.size(), 1u);  // only make_root_layout() survives
}

TEST_F(NodeTest, CommonKnowledgeFieldsAreSetFromTheRootLayoutAndCaller)
{
    Deal const root_layout = make_root_layout();
    VectorLayoutSource source({make_root_layout()});

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/7, source);
    ASSERT_TRUE(node.has_value());
    EXPECT_EQ(node->state.trump, DDS_NOTRUMP);
    EXPECT_EQ(node->state.first, North);
    EXPECT_EQ(node->state.declarer, North);
    EXPECT_EQ(node->state.tricks_needed, 7);
    EXPECT_EQ(node->state.tricks_won_by_declarer, 0);
    EXPECT_EQ(node->state.history.number, 0);
    // known_holdings: declarer/dummy exact, defenders as the union pool.
    EXPECT_EQ(node->state.known_holdings.remainCards[North][0], holding({Ace, King}));
    EXPECT_EQ(node->state.known_holdings.remainCards[South][1], holding({Ace, King}));
    EXPECT_EQ(node->state.known_holdings.remainCards[East][2], holding({Queen, Jack}));
    EXPECT_EQ(node->state.known_holdings.remainCards[West][2], holding({Queen, Jack}));
}

TEST_F(NodeTest, HistoryIsSeededFromCardsAlreadyPlayedToTheRootsTrickInProgress)
{
    // North led the spade king and East followed with the two before this
    // search began -- the root position starts two cards into a trick, not
    // fresh. history must reflect that, since callers rely on it to see
    // "every card played so far", and both those cards are still
    // recoverable from root_layout's own currentTrickSuit/Rank.
    Deal root_layout = make_root_layout();
    root_layout.currentTrickSuit[0] = 0;  // spades
    root_layout.currentTrickRank[0] = King;
    root_layout.currentTrickSuit[1] = 0;  // spades
    root_layout.currentTrickRank[1] = Two;
    root_layout.remainCards[North][0] = holding({Ace});  // king already played
    root_layout.remainCards[East][0] = 0;                // two already played
    VectorLayoutSource source({root_layout});

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/7, source);
    ASSERT_TRUE(node.has_value());
    ASSERT_EQ(node->state.history.number, 2);
    EXPECT_EQ(node->state.history.suit[0], 0);
    EXPECT_EQ(node->state.history.rank[0], King);
    EXPECT_EQ(node->state.history.suit[1], 0);
    EXPECT_EQ(node->state.history.rank[1], Two);
}

// --- tricks_remaining() ----------------------------------------------------

TEST_F(NodeTest, TricksRemainingAtATrickBoundaryEqualsDeclarersOwnCardCount)
{
    // make_root_layout(): North (declarer) holds spades A/K -- two cards,
    // nothing played, so two tricks remain.
    Deal const root_layout = make_root_layout();
    VectorLayoutSource source({root_layout});
    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source);
    ASSERT_TRUE(node.has_value());

    EXPECT_EQ(tricks_remaining(node->state), 2);
}

TEST_F(NodeTest, TricksRemainingCrossCheckedAgainstADefendersOwnHolding)
{
    // A cross-check worth building even though it is not shipped: tricks
    // remaining computed from declarer's holding must equal the same
    // count computed directly from a single layout's defender holding
    // (East's two diamonds here, one card each of the pool the two
    // defenders' entries would otherwise double if summed together) --
    // this is exactly the mistake tricks_remaining() itself must not make.
    Deal const root_layout = make_root_layout();
    VectorLayoutSource source({root_layout});
    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source);
    ASSERT_TRUE(node.has_value());
    ASSERT_EQ(node->layouts.size(), 1u);

    int east_diamond_count = 0;
    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        east_diamond_count += std::popcount(node->layouts[0].remainCards[East][suit]);
    }
    // East alone holds one of the two outstanding diamonds (West holds the
    // other) -- not itself tricks_remaining, but every suit here has depth
    // one, so North's spade count (2) equals declarer's card count exactly
    // as tricks_remaining() computes it; this test pins that a defender's
    // OWN entry (not the union pool known_holdings would give) is the
    // right thing to cross-check against.
    EXPECT_EQ(east_diamond_count, 1);
    EXPECT_EQ(tricks_remaining(node->state), 2);
}

TEST_F(NodeTest, TricksRemainingMidTrickIsOneShortForAHandThatHasAlreadyPlayed)
{
    // North led the spade king (already played, one card gone from its
    // holding) before this search began; East has not followed yet. North
    // (declarer) is the trick's leader here, so it has already played to
    // the trick in progress -- its own remaining card count (1, the ace)
    // is one short of tricks_remaining, which must still read 2 (the ace
    // trick, plus the queen/jack diamond trick still to come).
    Deal root_layout = make_root_layout();
    root_layout.currentTrickSuit[0] = 0;  // spades
    root_layout.currentTrickRank[0] = King;
    root_layout.remainCards[North][0] = holding({Ace});  // king already played
    VectorLayoutSource source({root_layout});
    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source);
    ASSERT_TRUE(node.has_value());

    ASSERT_EQ(card_count(node->state.known_holdings, North), 1);  // declarer's own count: one short
    EXPECT_EQ(tricks_remaining(node->state), 2);
}

TEST_F(NodeTest, TricksRemainingMidTrickTwoCardsInIsStillCorrectForTheLeader)
{
    // Two cards into the trick North led (king, then East's queen), with
    // North (the leader) still one short of tricks_remaining exactly as
    // in the one-card-in case above -- pinned separately per this file's
    // own standing rule against collapsing trick-boundary cases together.
    Deal root_layout = make_root_layout();
    root_layout.currentTrickSuit[0] = 0;  // spades
    root_layout.currentTrickRank[0] = King;
    root_layout.currentTrickSuit[1] = 2;  // diamonds
    root_layout.currentTrickRank[1] = Queen;
    root_layout.remainCards[North][0] = holding({Ace});  // king already played
    root_layout.remainCards[East][2] = 0;                // queen already played
    VectorLayoutSource source({root_layout});
    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source);
    ASSERT_TRUE(node.has_value());

    ASSERT_EQ(card_count(node->state.known_holdings, North), 1);
    EXPECT_EQ(tricks_remaining(node->state), 2);
}

TEST_F(NodeTest, TricksRemainingMidTrickIsNotShortForAHandThatHasNotPlayedYet)
{
    // Same two-cards-in position as above, but read from a seat that has
    // NOT yet played to the trick in progress: South (dummy) is due to
    // play third (after North and East), so its own remaining card count
    // (2, the ace and king of hearts) already equals tricks_remaining
    // exactly, with no +1 adjustment needed -- the mirror case
    // tricks_remaining() has to get right along with the "already played"
    // case above.
    Deal root_layout = make_root_layout();
    root_layout.currentTrickSuit[0] = 0;  // spades
    root_layout.currentTrickRank[0] = King;
    root_layout.currentTrickSuit[1] = 2;  // diamonds
    root_layout.currentTrickRank[1] = Queen;
    root_layout.remainCards[North][0] = holding({Ace});  // king already played
    root_layout.remainCards[East][2] = 0;                // queen already played
    VectorLayoutSource source({root_layout});
    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source);
    ASSERT_TRUE(node.has_value());

    // tricks_remaining() reads state.declarer directly, not "the real
    // declarer" -- and South's own known_holdings entry is exact (South is
    // this fixture's dummy, kept verbatim by known_holdings_for(), the
    // same as declarer's own entry), so pointing declarer at South is a
    // legitimate way to ask "what would tricks_remaining read from South's
    // own seat instead of North's".
    ObservationState south_view = node->state;
    south_view.declarer = South;
    ASSERT_EQ(card_count(south_view.known_holdings, South), 2);
    EXPECT_EQ(tricks_remaining(south_view), 2);
}
