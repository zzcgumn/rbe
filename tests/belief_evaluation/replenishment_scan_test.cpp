#include <gtest/gtest.h>

#include <api/dds_data_types.hpp>
#include <utility/constants.h>

#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/replenishment.hpp>

#include "test_support.hpp"

namespace be = dds::belief_evaluation;
using be::BeliefNode;
using be::ExpandDefenderResult;
using be::VectorLayoutSource;
using be::holding;
using be::make_root;
using be::replay_candidate;

namespace
{
    constexpr int King = 13;
    constexpr int Ace = 14;

    constexpr int Spades = 0;
    constexpr int Hearts = 1;
    constexpr int Diamonds = 2;

    constexpr int North = 0;  // declarer
    constexpr int East = 1;   // a defender, on play at depth 0
    constexpr int South = 2;  // dummy
    constexpr int West = 3;   // a defender

    /// A mid-trick root: North (declarer) already led the spade king before
    /// this search began (root's currentTrick* records it, and North's own
    /// holding no longer includes it) -- history_for(root_layout).number is
    /// 1, not 0, so a fixture built from this actually exercises the skip
    /// rather than trivially satisfying it. North and South each hold one
    /// more card (hearts) so the ending has two more plies to go; East and
    /// West hold one diamond each -- East's turn is next (seat_on_play
    /// rotates from North, one card already in), and East's own play is the
    /// ply every test in this file replays across.
    auto make_mid_trick_root() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = North;
        deal.currentTrickSuit[0] = Spades;
        deal.currentTrickRank[0] = King;
        deal.remainCards[North][Hearts] = holding({Ace});
        deal.remainCards[South][Hearts] = holding({King});
        deal.remainCards[East][Diamonds] = holding({King});
        deal.remainCards[West][Diamonds] = holding({Ace});
        return deal;
    }
}

class ReplenishmentScanTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        root_layout_ = make_mid_trick_root();
        VectorLayoutSource const source({root_layout_});
        root_ = *make_root(root_layout_, North, /*tricks_needed=*/1, source).node;
        ASSERT_EQ(root_.state.history.number, 1);  // seeded from the root's own trick in progress

        be::ScriptedDefender::Key const key{be::layout_key(root_layout_, East), "0:13,"};
        be::ScriptedDefender defender({{key, be::Card{Diamonds, King}}});
        ExpandDefenderResult const result = be::expand_defender_node(root_, defender.as_strategy());
        ASSERT_TRUE(result.children.has_value());
        ASSERT_EQ(result.children->size(), 1u);
        child_ = result.children->front();
        ASSERT_EQ(child_.state.history.number, 2);  // the seeded spade king, plus East's diamond king
    }

    Deal root_layout_;
    BeliefNode root_;
    BeliefNode child_;
};

// --- criterion 4: a drawn layout replayed back onto its own node ----------

TEST_F(ReplenishmentScanTest, ADrawnLayoutReplayedBackOntoItsOwnNodeReproducesItExactly)
{
    // root_layout_ itself, fed back through the replay, must reproduce
    // child_.layouts.front() bit for bit -- the same layout the real
    // expansion already advanced by the same card.
    std::optional<Deal> const replayed = replay_candidate(root_layout_, root_layout_, child_.state);
    ASSERT_TRUE(replayed.has_value());
    ASSERT_EQ(child_.layouts.size(), 1u);
    EXPECT_EQ(replayed->remainCards[East][Diamonds], child_.layouts[0].remainCards[East][Diamonds]);
    EXPECT_EQ(replayed->remainCards[West][Diamonds], child_.layouts[0].remainCards[West][Diamonds]);
    EXPECT_EQ(replayed->remainCards[North][Hearts], child_.layouts[0].remainCards[North][Hearts]);
    EXPECT_EQ(replayed->remainCards[South][Hearts], child_.layouts[0].remainCards[South][Hearts]);
    EXPECT_EQ(replayed->currentTrickRank[0], child_.layouts[0].currentTrickRank[0]);
}

// --- the skip is exercised, not vacuous -----------------------------------

TEST_F(ReplenishmentScanTest, TheSkipIsExercisedByAMidTrickRootRatherThanBeingANoOp)
{
    // If the skip were wrong (0 instead of history_for(root_layout).number
    // == 1), the replay would try to re-play the seeded spade king at
    // whatever seat candidate's own already-advanced state computes --
    // and candidate's currentTrick* already reflects that card as played
    // (consistency requires it), so seat_on_play there is already East, who
    // does not hold a spade. A wrong skip would reject this candidate;
    // this test is what would turn red if the skip regressed.
    std::optional<Deal> const replayed = replay_candidate(root_layout_, root_layout_, child_.state);
    EXPECT_TRUE(replayed.has_value());
}

// --- criterion 3: a candidate not holding the recorded card is rejected ---

TEST_F(ReplenishmentScanTest, ACandidateNotHoldingTheRecordedDefenderCardIsRejectedNotAsserted)
{
    Deal candidate = root_layout_;
    candidate.remainCards[East][Diamonds] = holding({Ace});  // does not hold the recorded king

    EXPECT_FALSE(replay_candidate(candidate, root_layout_, child_.state).has_value());
}

// --- criterion 5: the replayed candidate satisfies the node's aggr invariant

TEST_F(ReplenishmentScanTest, AReplayedCandidateSatisfiesTheNodesAggrInvariantAlongsideItsLayouts)
{
    std::optional<Deal> const replayed = replay_candidate(root_layout_, root_layout_, child_.state);
    ASSERT_TRUE(replayed.has_value());
    be::assert_pool_matches({child_.layouts[0], *replayed});
    be::assert_forms_one_belief_node({child_.layouts[0], *replayed}, North);
}
