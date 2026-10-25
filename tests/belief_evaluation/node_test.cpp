#include <cstdint>

#include <gtest/gtest.h>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/play_record.hpp>

#include "test_support.hpp"

// Tests for make_root, root_keys, tricks_remaining and the play_record
// field of ObservationState.

// A namespace alias and targeted `using` declarations, not `using
// namespace dds::belief_evaluation;`. This file includes
// api/dds_data_types.hpp, which declares a global ::Card, so a bare `Card`
// would be ambiguous instead of a clear compile error.
namespace be = dds::belief_evaluation;
using be::BeliefNode;
using be::HistoryVerdict;
using be::KahanAccumulator;
using be::ObservationState;
using be::PlayRecord;
using be::Probability;
using be::UnboundedLayoutSource;
using be::VectorLayoutSource;
using be::advance_state;
using be::card_count;
using be::holding;
using be::layout_key;
using be::make_root;
using be::root_observation_state;
using be::RootFailure;
using be::tricks_remaining;

namespace
{
    constexpr int Two = 2;
    constexpr int Jack = 11;
    constexpr int Queen = 12;
    constexpr int King = 13;
    constexpr int Ace = 14;

    constexpr int North = 0;  // declarer in every test
    constexpr int East = 1;   // a defender
    constexpr int South = 2;  // dummy: (declarer + 2) % DDS_HANDS
    constexpr int West = 3;   // a defender

    /// North (declarer) holds ♠AK and South (dummy) holds ♥AK. East holds
    /// the ♦Q and West the ♦J. Notrump, North to lead, nothing played.
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

    /// make_root_layout() with the ♦Q and ♦J swapped between the defenders:
    /// the same pool, split the other way.
    auto make_swapped_split_layout() -> Deal
    {
        Deal deal = make_root_layout();
        deal.remainCards[East][2] = holding({Jack});
        deal.remainCards[West][2] = holding({Queen});
        return deal;
    }

    /// make_root_layout() with dummy holding only the ♥A. It must not
    /// survive filtering against the root.
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

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source).node;
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
    // Three raw layouts, two of them consistent with the root, so kappa must
    // be 1/2. A kappa computed from source.size() would give 1/3.
    VectorLayoutSource source(
        {make_root_layout(), make_swapped_split_layout(), make_wrong_dummy_layout()});

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source).node;
    ASSERT_TRUE(node.has_value());
    ASSERT_EQ(node->layouts.size(), 2u);
    EXPECT_DOUBLE_EQ(node->kappa, 0.5);
}

TEST_F(NodeTest, ASourceThatCannotReportItsSizeIsReportedAsAnError)
{
    Deal const root_layout = make_root_layout();
    UnboundedLayoutSource source;

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source).node;
    EXPECT_FALSE(node.has_value());
}

TEST_F(NodeTest, ASourceWhereNoLayoutSurvivesIsReportedAsAnError)
{
    Deal const root_layout = make_root_layout();
    VectorLayoutSource source({make_wrong_dummy_layout()});

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source).node;
    EXPECT_FALSE(node.has_value());
}

TEST_F(NodeTest, TheTwoFailureCausesAreDistinguishable)
{
    // "source.size() is nullopt" and "no layout survived filtering" both
    // leave RootConstructionResult::node empty. ::failure tells them apart.
    Deal const root_layout = make_root_layout();
    UnboundedLayoutSource unbounded_source;
    VectorLayoutSource empty_result_source({make_wrong_dummy_layout()});

    auto const unenumerable = make_root(root_layout, North, /*tricks_needed=*/2, unbounded_source);
    auto const nothing_survived =
        make_root(root_layout, North, /*tricks_needed=*/2, empty_result_source);

    EXPECT_FALSE(unenumerable.node.has_value());
    EXPECT_EQ(unenumerable.failure, RootFailure::SourceNotEnumerable);
    EXPECT_FALSE(nothing_survived.node.has_value());
    EXPECT_EQ(nothing_survived.failure, RootFailure::NoLayoutSurvived);
    EXPECT_NE(unenumerable.failure, nothing_survived.failure);
}

TEST_F(NodeTest, ADifferentDefenderSplitOfTheSamePoolSurvivesFiltering)
{
    Deal const root_layout = make_root_layout();
    VectorLayoutSource source({make_swapped_split_layout()});

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source).node;
    ASSERT_TRUE(node.has_value());
    ASSERT_EQ(node->layouts.size(), 1u);
    EXPECT_EQ(node->layouts[0].remainCards[East][2], holding({Jack}));
    EXPECT_EQ(node->layouts[0].remainCards[West][2], holding({Queen}));
}

TEST_F(NodeTest, ALayoutDifferingInDummysHoldingDoesNotSurviveFiltering)
{
    Deal const root_layout = make_root_layout();
    VectorLayoutSource source({make_root_layout(), make_wrong_dummy_layout()});

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source).node;
    ASSERT_TRUE(node.has_value());
    EXPECT_EQ(node->layouts.size(), 1u);  // only make_root_layout() survives
}

TEST_F(NodeTest, CommonKnowledgeFieldsAreSetFromTheRootLayoutAndCaller)
{
    Deal const root_layout = make_root_layout();
    VectorLayoutSource source({make_root_layout()});

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/7, source).node;
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
    // The root position starts two cards into a trick: North led the ♠K and
    // East followed with the ♠2. history must include both cards, because
    // callers rely on it for every card played so far. Both are in
    // root_layout's currentTrickSuit and currentTrickRank.
    Deal root_layout = make_root_layout();
    root_layout.currentTrickSuit[0] = 0;  // spades
    root_layout.currentTrickRank[0] = King;
    root_layout.currentTrickSuit[1] = 0;  // spades
    root_layout.currentTrickRank[1] = Two;
    root_layout.remainCards[North][0] = holding({Ace});  // king already played
    root_layout.remainCards[East][0] = 0;                // two already played
    VectorLayoutSource source({root_layout});

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/7, source).node;
    ASSERT_TRUE(node.has_value());
    ASSERT_EQ(node->state.history.number, 2);
    EXPECT_EQ(node->state.history.suit[0], 0);
    EXPECT_EQ(node->state.history.rank[0], King);
    EXPECT_EQ(node->state.history.suit[1], 0);
    EXPECT_EQ(node->state.history.rank[1], Two);
}

// --- root_keys ---------------------------------------------------------

TEST_F(NodeTest, MakeRootPopulatesRootKeysOneForEachSurvivingLayoutWithTheFixedSeat)
{
    Deal const root_layout = make_root_layout();
    VectorLayoutSource source({make_root_layout(), make_swapped_split_layout()});

    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source).node;
    ASSERT_TRUE(node.has_value());
    ASSERT_EQ(node->root_keys.size(), node->layouts.size());

    // The fixed seat is (declarer + 1) % DDS_HANDS = East. See the doxygen
    // of BeliefNode::root_keys for why the seat must never vary.
    int const fixed_seat = (North + 1) % DDS_HANDS;
    for (std::size_t i = 0; i < node->layouts.size(); ++i)
    {
        EXPECT_EQ(node->root_keys[i], layout_key(node->layouts[i], fixed_seat));
    }
    // The two layouts split the diamonds in opposite ways, so their keys
    // must differ.
    EXPECT_NE(node->root_keys[0], node->root_keys[1]);
}

TEST_F(NodeTest, RootSpaceKeysStayDistinctEvenWhenNodeDepthDealsHaveConverged)
{
    // Two root layouts with the diamond honours swapped: East holds the ♦K
    // and West the ♦Q in one, the reverse in the other. Once both honours are
    // played, the two Deals in a node are identical. A key taken at node depth
    // would collide; root_keys must not. See BeliefNode::root_keys and
    // node.hpp for why the exclusion set keys root space.
    Deal root_a = make_root_layout();
    root_a.remainCards[East][2] = holding({King});
    root_a.remainCards[West][2] = holding({Queen});
    Deal root_b = make_root_layout();
    root_b.remainCards[East][2] = holding({Queen});
    root_b.remainCards[West][2] = holding({King});

    int const fixed_seat = (North + 1) % DDS_HANDS;
    std::uint64_t const key_a = layout_key(root_a, fixed_seat);
    std::uint64_t const key_b = layout_key(root_b, fixed_seat);
    ASSERT_NE(key_a, key_b);  // the swap is genuinely visible before any card is played

    // The Deal a node shows once both diamond honours are gone, whichever
    // root layout it came from.
    Deal after_play = make_root_layout();
    after_play.remainCards[East][2] = 0;
    after_play.remainCards[West][2] = 0;

    BeliefNode node{};
    node.layouts = {after_play, after_play};
    node.p = {0.5, 0.5};
    node.root_keys = {key_a, key_b};

    ASSERT_EQ(node.root_keys.size(), node.layouts.size());
    EXPECT_EQ(node.layouts[0].remainCards[East][2], node.layouts[1].remainCards[East][2]);
    EXPECT_EQ(node.layouts[0].remainCards[West][2], node.layouts[1].remainCards[West][2]);
    EXPECT_NE(node.root_keys[0], node.root_keys[1]);  // a key taken at node depth would collide
}

// --- tricks_remaining() ----------------------------------------------------

TEST_F(NodeTest, TricksRemainingAtATrickBoundaryEqualsDeclarersOwnCardCount)
{
    // make_root_layout(): North (declarer) holds ♠AK, two cards, and nothing
    // has been played, so two tricks remain.
    Deal const root_layout = make_root_layout();
    VectorLayoutSource source({root_layout});
    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source).node;
    ASSERT_TRUE(node.has_value());

    EXPECT_EQ(tricks_remaining(node->state), 2);
}

TEST_F(NodeTest, TricksRemainingCrossCheckedAgainstADefendersOwnHolding)
{
    // Counts a defender's cards in one layout, not in known_holdings.
    // known_holdings gives each defender the union pool of both diamonds, so
    // summing defender entries there would double count.
    Deal const root_layout = make_root_layout();
    VectorLayoutSource source({root_layout});
    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source).node;
    ASSERT_TRUE(node.has_value());
    ASSERT_EQ(node->layouts.size(), 1u);

    int east_diamond_count = 0;
    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        east_diamond_count += std::popcount(node->layouts[0].remainCards[East][suit]);
    }
    // East holds one of the two outstanding diamonds and West the other.
    // tricks_remaining() counts declarer's cards instead: North's two spades.
    EXPECT_EQ(east_diamond_count, 1);
    EXPECT_EQ(tricks_remaining(node->state), 2);
}

TEST_F(NodeTest, TricksRemainingMidTrickIsOneShortForAHandThatHasAlreadyPlayed)
{
    // North led the ♠K before the search began, and East has not followed.
    // North, the leader, has already played to the trick, so it holds one
    // card (the ♠A). tricks_remaining must still read 2: the trick in progress
    // and one more.
    Deal root_layout = make_root_layout();
    root_layout.currentTrickSuit[0] = 0;  // spades
    root_layout.currentTrickRank[0] = King;
    root_layout.remainCards[North][0] = holding({Ace});  // king already played
    VectorLayoutSource source({root_layout});
    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source).node;
    ASSERT_TRUE(node.has_value());

    ASSERT_EQ(card_count(node->state.known_holdings, North), 1);  // one short
    EXPECT_EQ(tricks_remaining(node->state), 2);
}

TEST_F(NodeTest, TricksRemainingMidTrickTwoCardsInIsStillCorrectForTheLeader)
{
    // Two cards into the trick: North led the ♠K and East discarded the ♦Q.
    // North, the leader, is still one card short of tricks_remaining. A
    // separate test from the one-card case, because trick-boundary cases are
    // easy to get wrong.
    Deal root_layout = make_root_layout();
    root_layout.currentTrickSuit[0] = 0;  // spades
    root_layout.currentTrickRank[0] = King;
    root_layout.currentTrickSuit[1] = 2;  // diamonds
    root_layout.currentTrickRank[1] = Queen;
    root_layout.remainCards[North][0] = holding({Ace});  // king already played
    root_layout.remainCards[East][2] = 0;                // queen already played
    VectorLayoutSource source({root_layout});
    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source).node;
    ASSERT_TRUE(node.has_value());

    ASSERT_EQ(card_count(node->state.known_holdings, North), 1);
    EXPECT_EQ(tricks_remaining(node->state), 2);
}

TEST_F(NodeTest, TricksRemainingMidTrickIsNotShortForAHandThatHasNotPlayedYet)
{
    // The same position, read from a seat that has not played to the trick.
    // South (dummy) plays third, after North and East. South still holds two
    // cards (♥AK), which equals tricks_remaining with no adjustment.
    Deal root_layout = make_root_layout();
    root_layout.currentTrickSuit[0] = 0;  // spades
    root_layout.currentTrickRank[0] = King;
    root_layout.currentTrickSuit[1] = 2;  // diamonds
    root_layout.currentTrickRank[1] = Queen;
    root_layout.remainCards[North][0] = holding({Ace});  // king already played
    root_layout.remainCards[East][2] = 0;                // queen already played
    VectorLayoutSource source({root_layout});
    std::optional<BeliefNode> const node = make_root(root_layout, North, /*tricks_needed=*/2, source).node;
    ASSERT_TRUE(node.has_value());

    // tricks_remaining() reads state.declarer. South is dummy, so its
    // known_holdings entry is exact, like declarer's. Setting declarer to South
    // asks what tricks_remaining reads from South's seat.
    ObservationState south_view = node->state;
    south_view.declarer = South;
    ASSERT_EQ(card_count(south_view.known_holdings, South), 2);
    EXPECT_EQ(tricks_remaining(south_view), 2);
}

// --- play_record: set by root_observation_state, unchanged by
// advance_state ----------------------------------------------------------

TEST_F(NodeTest, RootObservationStateCarriesTheSuppliedPlayRecord)
{
    Deal const root_layout = make_root_layout();

    PlayTraceBin cards{};
    cards.number = 2;
    cards.suit[0] = 0;  // spades
    cards.rank[0] = 6;
    cards.suit[1] = 0;  // spades
    cards.rank[1] = 4;
    auto [record, verdict] = PlayRecord::create(cards, /*opening_leader=*/West);
    ASSERT_EQ(verdict, HistoryVerdict::Consistent);

    ObservationState const state = root_observation_state(root_layout, North, /*tricks_needed=*/2, &*record);

    EXPECT_EQ(state.play_record, &*record);
}

TEST_F(NodeTest, RootObservationStateDefaultsPlayRecordToNull)
{
    Deal const root_layout = make_root_layout();

    ObservationState const state = root_observation_state(root_layout, North, /*tricks_needed=*/2);

    EXPECT_EQ(state.play_record, nullptr);
}

TEST_F(NodeTest, PlayRecordSurvivesAdvanceStateUnchanged)
{
    Deal const root_layout = make_root_layout();

    PlayTraceBin cards{};
    cards.number = 1;
    cards.suit[0] = 2;  // diamonds
    cards.rank[0] = 11;
    auto [record, verdict] = PlayRecord::create(cards, /*opening_leader=*/West);
    ASSERT_EQ(verdict, HistoryVerdict::Consistent);

    ObservationState const root_state =
        root_observation_state(root_layout, North, /*tricks_needed=*/2, &*record);

    // North (declarer) is on lead in make_root_layout() and holds ♠AK, so
    // the ace is a legal lead.
    ObservationState const child = advance_state(root_state, be::Card{0, Ace});

    EXPECT_EQ(child.play_record, root_state.play_record);  // same address
    EXPECT_EQ(child.history.number, root_state.history.number + 1);
}
