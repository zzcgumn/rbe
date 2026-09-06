#include <gtest/gtest.h>

#include <api/dds_data_types.hpp>
#include <utility/constants.h>

#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/replenishment.hpp>

#include "test_support.hpp"

namespace be = dds::belief_evaluation;
using be::BeliefNode;
using be::DeclarerStrategy;
using be::ExpandDefenderResult;
using be::ExpandResult;
using be::ReplayResult;
using be::ScanResult;
using be::VectorLayoutSource;
using be::holding;
using be::make_root;
using be::replay_candidate;
using be::scan_for_replenishment;

namespace
{
    constexpr int Two = 2;
    constexpr int Nine = 9;
    constexpr int Jack = 11;
    constexpr int Ten = 10;
    constexpr int King = 13;
    constexpr int Ace = 14;

    constexpr int Spades = 0;
    constexpr int Hearts = 1;
    constexpr int Diamonds = 2;
    constexpr int Clubs = 3;

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
    /// ply every test in this section replays across.
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
        defender_.emplace(
            std::map<be::ScriptedDefender::Key, be::Card>{{key, be::Card{Diamonds, King}}});
        ExpandDefenderResult const result = be::expand_defender_node(root_, defender_->as_strategy());
        ASSERT_TRUE(result.children.has_value());
        ASSERT_EQ(result.children->size(), 1u);
        child_ = result.children->front();
        ASSERT_EQ(child_.state.history.number, 2);  // the seeded spade king, plus East's diamond king
    }

    Deal root_layout_;
    BeliefNode root_;
    BeliefNode child_;
    std::optional<be::ScriptedDefender> defender_;
};

// --- a drawn layout replayed back onto its own node

TEST_F(ReplenishmentScanTest, ADrawnLayoutReplayedBackOntoItsOwnNodeReproducesItExactly)
{
    // root_layout_ itself, fed back through the replay, must reproduce
    // child_.layouts.front() bit for bit -- the same layout the real
    // expansion already advanced by the same card.
    ReplayResult const result =
        replay_candidate(root_layout_, root_layout_, child_.state, defender_->as_strategy());
    ASSERT_TRUE(result.layout.has_value());
    EXPECT_EQ(result.error, be::ValidationError::None);
    ASSERT_EQ(child_.layouts.size(), 1u);
    Deal const& replayed = *result.layout;
    EXPECT_EQ(replayed.remainCards[East][Diamonds], child_.layouts[0].remainCards[East][Diamonds]);
    EXPECT_EQ(replayed.remainCards[West][Diamonds], child_.layouts[0].remainCards[West][Diamonds]);
    EXPECT_EQ(replayed.remainCards[North][Hearts], child_.layouts[0].remainCards[North][Hearts]);
    EXPECT_EQ(replayed.remainCards[South][Hearts], child_.layouts[0].remainCards[South][Hearts]);
    EXPECT_EQ(replayed.currentTrickRank[0], child_.layouts[0].currentTrickRank[0]);
}

// --- p_j of a drawn layout equals its own p, bitwise ----------------------

TEST_F(ReplenishmentScanTest, PjOfADrawnLayoutReplayedBackOntoItsOwnNodeEqualsItsOwnPBitwise)
{
    // East's own play here is certain (probability 1), so p_j must come
    // back exactly 1.0 -- the same p the drawn layout already carries.
    ASSERT_EQ(child_.p.size(), 1u);
    ReplayResult const result =
        replay_candidate(root_layout_, root_layout_, child_.state, defender_->as_strategy());
    ASSERT_TRUE(result.layout.has_value());
    EXPECT_EQ(result.p_j, child_.p[0]);
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
    ReplayResult const result =
        replay_candidate(root_layout_, root_layout_, child_.state, defender_->as_strategy());
    EXPECT_TRUE(result.layout.has_value());
}

// --- a candidate not holding the recorded defender card is rejected ------

TEST_F(ReplenishmentScanTest, ACandidateNotHoldingTheRecordedDefenderCardIsRejectedNotAsserted)
{
    Deal candidate = root_layout_;
    candidate.remainCards[East][Diamonds] = holding({Ace});  // does not hold the recorded king

    ReplayResult const result =
        replay_candidate(candidate, root_layout_, child_.state, defender_->as_strategy());
    EXPECT_FALSE(result.layout.has_value());
    EXPECT_EQ(result.error, be::ValidationError::None);  // an ordinary rejection, not a contract violation
}

// --- the replayed candidate satisfies the node's aggr invariant ----------

TEST_F(ReplenishmentScanTest, AReplayedCandidateSatisfiesTheNodesAggrInvariantAlongsideItsLayouts)
{
    ReplayResult const result =
        replay_candidate(root_layout_, root_layout_, child_.state, defender_->as_strategy());
    ASSERT_TRUE(result.layout.has_value());
    be::assert_pool_matches({child_.layouts[0], *result.layout});
    be::assert_forms_one_belief_node({child_.layouts[0], *result.layout}, North);
}

// ===========================================================================
// A three-ply fixture (defender, dummy, defender) with genuine defender
// choices at both ends, for p_j's multiplicative accumulation across more
// than one defender ply, and for pinning that a declarer/dummy ply
// contributes nothing to it.
// ===========================================================================

namespace
{
    /// East on lead, holding a genuine choice of two diamonds; South
    /// (dummy) holds a single forced heart (void in diamonds, so any
    /// discard is legal, and this is South's only card); West holds a
    /// genuine choice of two clubs (void in diamonds too, so West's second
    /// card is a free discard, not a forced follow). North (declarer) holds
    /// nothing relevant -- this fixture never asks North to play; the
    /// three-entry history it builds stops one ply short of that.
    auto make_three_ply_root() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[East][Diamonds] = holding({King, Two});
        deal.remainCards[South][Hearts] = holding({Ace});
        deal.remainCards[West][Clubs] = holding({Jack, Ten});
        return deal;
    }
}

class ThreePlyReplayTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        root_layout_ = make_three_ply_root();
        VectorLayoutSource const source({root_layout_});
        root_ = *make_root(root_layout_, North, /*tricks_needed=*/1, source).node;
        ASSERT_EQ(root_.state.history.number, 0);  // no pre-existing trick -- the skip is 0 here

        // East plays the king 0.6, the two 0.4 -- a genuine choice, not a
        // certainty, so criterion 3's bitwise check cannot pass "for free".
        be::ScriptedDefender::Key const east_key{be::layout_key(root_layout_, East), ""};
        east_defender_.emplace(be::ScriptedDefender::stochastic(
            {{east_key,
              {be::WeightedCard{be::Card{Diamonds, King}, 0.6},
               be::WeightedCard{be::Card{Diamonds, Two}, 0.4}}}}));
        ExpandDefenderResult const east_result =
            be::expand_defender_node(root_, east_defender_->as_strategy());
        ASSERT_TRUE(east_result.children.has_value());
        ASSERT_EQ(east_result.children->size(), 2u);
        // Identify the king's child: East's own diamond entry still shows
        // the two, having played the king.
        for (BeliefNode const& child : *east_result.children)
        {
            if (child.layouts.front().remainCards[East][Diamonds] == holding({Two}))
            {
                king_child_ = child;
            }
        }
        ASSERT_EQ(king_child_.p.size(), 1u);
        EXPECT_DOUBLE_EQ(king_child_.p[0], 0.6);

        // South (dummy), forced to the only card it holds -- contributes
        // nothing to p_j.
        DeclarerStrategy const pi{.id = 0, .play = be::single_card_declarer_play, .state_key = nullptr};
        ExpandResult const south_result = be::expand_declarer_node(king_child_, pi);
        ASSERT_TRUE(south_result.child.has_value());
        south_child_ = *south_result.child;
        ASSERT_EQ(south_child_.state.history.number, 2);
        ASSERT_DOUBLE_EQ(south_child_.p[0], 0.6);  // unchanged by a declarer-side ply

        // West plays the jack 0.7, the ten 0.3.
        be::ScriptedDefender::Key const west_key{be::layout_key(root_layout_, West), "2:13,1:14,"};
        west_defender_.emplace(be::ScriptedDefender::stochastic(
            {{west_key,
              {be::WeightedCard{be::Card{Clubs, Jack}, 0.7},
               be::WeightedCard{be::Card{Clubs, Ten}, 0.3}}}}));
        ExpandDefenderResult const west_result =
            be::expand_defender_node(south_child_, west_defender_->as_strategy());
        ASSERT_TRUE(west_result.children.has_value());
        ASSERT_EQ(west_result.children->size(), 2u);
        for (BeliefNode const& child : *west_result.children)
        {
            if (child.layouts.front().remainCards[West][Clubs] == holding({Ten}))
            {
                jack_child_ = child;
            }
        }
        ASSERT_EQ(jack_child_.state.history.number, 3);
        ASSERT_EQ(jack_child_.p.size(), 1u);
        // Hand-derived: 0.6 (East's king) * 1.0 (South's forced ace,
        // contributing nothing) * 0.7 (West's jack) = 0.42.
        ASSERT_DOUBLE_EQ(jack_child_.p[0], 0.42);
    }

    /// A single DefenderStrategy answering both East's and West's queries,
    /// for the replay call -- both scripted tables are consulted through
    /// the same lookup, so this is the delta a replenishment scan would
    /// actually be given.
    auto combined_delta() -> be::DefenderStrategy
    {
        be::DefenderStrategy const east_strategy = east_defender_->as_strategy();
        be::DefenderStrategy const west_strategy = west_defender_->as_strategy();
        return [east_strategy, west_strategy](be::DefenderQuery const& query) -> std::vector<be::WeightedCard>
        {
            return query.seat == East ? east_strategy(query) : west_strategy(query);
        };
    }

    Deal root_layout_;
    BeliefNode root_;
    BeliefNode king_child_;
    BeliefNode south_child_;
    BeliefNode jack_child_;
    std::optional<be::ScriptedDefender> east_defender_;
    std::optional<be::ScriptedDefender> west_defender_;
};

// --- criterion 3: p_j across two defender plies matches the drawn p ------
// --- criterion 4: the hand-derived value, from the SetUp comment above ---

TEST_F(ThreePlyReplayTest, PjAccumulatesAcrossTwoDefenderPliesAndMatchesTheHandDerivedValue)
{
    ReplayResult const result =
        replay_candidate(root_layout_, root_layout_, jack_child_.state, combined_delta());
    ASSERT_TRUE(result.layout.has_value());
    EXPECT_EQ(result.error, be::ValidationError::None);
    EXPECT_DOUBLE_EQ(result.p_j, 0.42);           // the hand derivation in SetUp
    EXPECT_EQ(result.p_j, jack_child_.p[0]);      // bitwise agreement with the drawn layout's own p
}

// --- criterion 2: a candidate delta assigns zero probability is rejected -

TEST_F(ThreePlyReplayTest, ACandidateDeltaAssignsZeroProbabilityToIsRejectedNotAsserted)
{
    // A candidate whose East holds {king, nine} instead of {king, two} --
    // still holds the recorded king (so the structural check passes) but
    // is scripted, under its own distinct layout_key, to never play it.
    Deal candidate = root_layout_;
    candidate.remainCards[East][Diamonds] = holding({King, Nine});

    be::ScriptedDefender::Key const key{be::layout_key(candidate, East), ""};
    be::ScriptedDefender never_plays_king(
        {{key, be::Card{Diamonds, Nine}}});  // omits the king entirely from its one certain reply

    ReplayResult const result =
        replay_candidate(candidate, root_layout_, king_child_.state, never_plays_king.as_strategy());
    EXPECT_FALSE(result.layout.has_value());
    EXPECT_EQ(result.error, be::ValidationError::None);  // rejection, not a contract violation
}

// --- criterion 5: delta's own contract violations are reported, not asserted

TEST_F(ThreePlyReplayTest, ADeltaContractViolationDuringReplayIsReportedNotAsserted)
{
    auto const broken_delta = [](be::DefenderQuery const&) -> std::vector<be::WeightedCard> { return {}; };

    ReplayResult const result = replay_candidate(root_layout_, root_layout_, king_child_.state, broken_delta);
    EXPECT_FALSE(result.layout.has_value());
    EXPECT_EQ(result.error, be::ValidationError::DistributionEmpty);
    EXPECT_EQ(result.seat, East);
}

// ===========================================================================
// scan_for_replenishment: a one-ply-deep node, and a source holding one
// root-space candidate already in the node, one genuinely new one, and one
// inconsistent with the root altogether.
// ===========================================================================

namespace
{
    /// East on lead, holding the recorded king plus one more diamond that
    /// varies between the two root-space candidates below (the pool stays
    /// {king, two, ace} either way); West holds the complement. North and
    /// South hold nothing relevant, per this file's established convention
    /// for fixtures that exercise expansion directly.
    auto make_scan_root(int east_second_card, int west_card) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[East][Diamonds] = holding({King, east_second_card});
        deal.remainCards[West][Diamonds] = holding({west_card});
        return deal;
    }

    /// Consistent with neither scan-root candidate: North holds a card
    /// (the two of spades) the two candidates above do not give it at all
    /// (they give North nothing), so `is_consistent`'s declarer-holdings
    /// comparison rejects it before anything else does.
    auto make_inconsistent_layout() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[North][Spades] = holding({Two});
        deal.remainCards[East][Diamonds] = holding({King, Two});
        deal.remainCards[West][Diamonds] = holding({Ace});
        return deal;
    }
}

class ScanForReplenishmentTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        candidate_a_ = make_scan_root(/*east_second=*/Two, /*west=*/Ace);
        candidate_b_ = make_scan_root(/*east_second=*/Ace, /*west=*/Two);
        root_layout_ = candidate_a_;

        VectorLayoutSource const root_source({candidate_a_});
        root_ = *make_root(root_layout_, North, /*tricks_needed=*/1, root_source).node;
        ASSERT_EQ(root_.state.history.number, 0);

        be::ScriptedDefender::Key const key_a{be::layout_key(candidate_a_, East), ""};
        be::ScriptedDefender::Key const key_b{be::layout_key(candidate_b_, East), ""};
        defender_.emplace(std::map<be::ScriptedDefender::Key, be::Card>{
            {key_a, be::Card{Diamonds, King}}, {key_b, be::Card{Diamonds, King}}});

        ExpandDefenderResult const result = be::expand_defender_node(root_, defender_->as_strategy());
        ASSERT_TRUE(result.children.has_value());
        ASSERT_EQ(result.children->size(), 1u);  // both candidates would play the king; only A is drawn here
        node_ = result.children->front();
        ASSERT_EQ(node_.state.history.number, 1);
        ASSERT_EQ(node_.root_keys.size(), 1u);
        ASSERT_EQ(node_.root_keys[0], be::layout_key(candidate_a_, East));
    }

    Deal candidate_a_;
    Deal candidate_b_;
    Deal root_layout_;
    BeliefNode root_;
    BeliefNode node_;
    std::optional<be::ScriptedDefender> defender_;
};

// --- criterion 1: a scan finds a new candidate, with its p_j and outcome -

TEST_F(ScanForReplenishmentTest, FindsTheOneNewConsistentCandidateAndReportsSampleFilled)
{
    // A third entry after the wanted candidate, so filling the want stops
    // the scan strictly before the source itself runs out -- matching
    // make_root's own rule that reaching a cap exactly as the source also
    // ends is SourceExhausted, not SampleFilled (there was nothing left to
    // find regardless). candidate_a_ here is never even reached.
    VectorLayoutSource const scan_source({candidate_a_, candidate_b_, candidate_a_});
    ScanResult const result =
        scan_for_replenishment(node_, root_layout_, scan_source, defender_->as_strategy(), /*wanted=*/1, std::nullopt);

    EXPECT_EQ(result.error, be::ValidationError::None);
    ASSERT_EQ(result.candidates.size(), 1u);
    EXPECT_EQ(result.candidates[0].root_key, be::layout_key(candidate_b_, East));
    EXPECT_DOUBLE_EQ(result.candidates[0].p_j, 1.0);  // East's king is certain in this script
    EXPECT_EQ(result.outcome, be::ScanOutcome::SampleFilled);
}

// --- criterion 4: an already-present candidate costs no delta call -------

TEST_F(ScanForReplenishmentTest, AnAlreadyPresentCandidateIsSkippedWithoutCallingDelta)
{
    int delta_calls = 0;
    be::DefenderStrategy const counting_delta = [&](be::DefenderQuery const& query) -> std::vector<be::WeightedCard>
    {
        ++delta_calls;
        return defender_->as_strategy()(query);
    };

    VectorLayoutSource const scan_source({candidate_a_, candidate_b_});
    ScanResult const result =
        scan_for_replenishment(node_, root_layout_, scan_source, counting_delta, /*wanted=*/2, std::nullopt);

    ASSERT_EQ(result.candidates.size(), 1u);  // only B is new; A is already in node_
    EXPECT_EQ(delta_calls, 1);                // exactly one call, for B -- none spent rejecting A
}

// --- the consistency filter rejects a layout outside the belief space ----

TEST_F(ScanForReplenishmentTest, AnInconsistentLayoutIsSkippedBeforeAnyReplayOrDeltaCall)
{
    Deal const inconsistent = make_inconsistent_layout();
    int delta_calls = 0;
    be::DefenderStrategy const counting_delta = [&](be::DefenderQuery const& query) -> std::vector<be::WeightedCard>
    {
        ++delta_calls;
        return defender_->as_strategy()(query);
    };

    VectorLayoutSource const scan_source({inconsistent, candidate_b_});
    ScanResult const result =
        scan_for_replenishment(node_, root_layout_, scan_source, counting_delta, /*wanted=*/2, std::nullopt);

    ASSERT_EQ(result.candidates.size(), 1u);  // only B; the inconsistent layout never reached delta
    EXPECT_EQ(delta_calls, 1);
}

// --- criterion 5: the budget counts source.at() calls ---------------------

TEST_F(ScanForReplenishmentTest, ABudgetOfOneStopsAfterASingleAtCallAndReportsBudgetExhausted)
{
    // candidate_a_ at index 0 is already present (skipped, no delta), but
    // still costs the one at() call the budget allows -- index 1
    // (candidate_b_, genuinely new) is never reached.
    VectorLayoutSource const scan_source({candidate_a_, candidate_b_});
    ScanResult const result = scan_for_replenishment(
        node_, root_layout_, scan_source, defender_->as_strategy(), /*wanted=*/5, /*budget=*/1u);

    EXPECT_TRUE(result.candidates.empty());
    EXPECT_EQ(result.outcome, be::ScanOutcome::BudgetExhausted);
}

// --- criterion 6: the source runs out and nothing was accepted -----------

TEST_F(ScanForReplenishmentTest, ExhaustingTheSourceWithNothingAcceptedIsStillSourceExhausted)
{
    // Both entries are already present in node_'s exclusion set (B is
    // added to it by hand here, simulating an already-fully-replenished
    // node), so the scan accepts nothing yet still reaches the end.
    node_.root_keys.push_back(be::layout_key(candidate_b_, East));

    VectorLayoutSource const scan_source({candidate_a_, candidate_b_});
    ScanResult const result = scan_for_replenishment(
        node_, root_layout_, scan_source, defender_->as_strategy(), /*wanted=*/5, std::nullopt);

    EXPECT_TRUE(result.candidates.empty());
    EXPECT_EQ(result.outcome, be::ScanOutcome::SourceExhausted);
}

// --- a delta contract violation during the scan is reported ---------------

TEST_F(ScanForReplenishmentTest, ADeltaContractViolationDuringTheScanIsReportedNotAsserted)
{
    auto const broken_delta = [](be::DefenderQuery const&) -> std::vector<be::WeightedCard> { return {}; };
    VectorLayoutSource const scan_source({candidate_b_});

    ScanResult const result =
        scan_for_replenishment(node_, root_layout_, scan_source, broken_delta, /*wanted=*/1, std::nullopt);

    EXPECT_TRUE(result.candidates.empty());
    EXPECT_EQ(result.error, be::ValidationError::DistributionEmpty);
    EXPECT_EQ(result.seat, East);
}
