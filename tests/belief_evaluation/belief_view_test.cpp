#include <gtest/gtest.h>

#include <vector>

#include <utility/constants.h>

#include <belief_evaluation/belief_view.hpp>
#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/node.hpp>

// A namespace alias plus targeted `using` declarations, not `using namespace
// dds::belief_evaluation;` -- this file's include chain (belief_view.hpp /
// node.hpp) already makes api/dds_data_types.hpp's global ::Card visible, so
// a `using namespace` here would make any future bare `Card` reference
// ambiguous between ::Card and dds::belief_evaluation::Card rather than a
// clear compile error naming which one was meant.
namespace be = dds::belief_evaluation;
using be::BeliefEntry;
using be::BeliefNode;
using be::BeliefView;
using be::KahanAccumulator;
using be::Probability;
using be::SampleWeight;
using be::make_belief_view;

namespace
{
    auto make_node_with_p(std::vector<Probability> p, SampleWeight kappa) -> BeliefNode
    {
        BeliefNode node{};
        node.layouts.resize(p.size());  // content is irrelevant to this file
        node.p = std::move(p);
        node.kappa = kappa;
        return node;
    }
}

class BeliefViewTest : public ::testing::Test
{
};

TEST_F(BeliefViewTest, PosteriorsAreNormalisedNotRawPNotWNotKappaTimesP)
{
    // p = {0.6, 0.1} does not sum to 1, and kappa != 1, so posterior == p_i,
    // posterior == w_i (= kappa * p_i), and posterior == the raw p_i are all
    // simultaneously false unless the view actually normalises within the
    // node — 0.6 / 0.7 = 0.857142..., 0.1 / 0.7 = 0.142857...
    BeliefNode const node = make_node_with_p({0.6, 0.1}, /*kappa=*/0.5);

    std::vector<BeliefEntry> scratch;
    BeliefView const view = make_belief_view(node, scratch);

    ASSERT_EQ(view.entries.size(), 2u);
    EXPECT_NEAR(view.entries[0].posterior, 0.6 / 0.7, 1e-12);
    EXPECT_NEAR(view.entries[1].posterior, 0.1 / 0.7, 1e-12);

    KahanAccumulator total;
    for (BeliefEntry const& entry : view.entries)
    {
        total.add(entry.posterior);
    }
    EXPECT_NEAR(total.value(), 1.0, 1e-12);
}

TEST_F(BeliefViewTest, PosteriorReflectsReweightingAfterADefenderPlay)
{
    // Simulates what a defender child's p looks like after unequal
    // reweighting: a stale view (one built before the reweighting) would
    // still report the old {0.5, 0.5} split.
    BeliefNode node = make_node_with_p({0.5, 0.5}, /*kappa=*/1.0);
    node.p = {0.9, 0.3};  // unequally reweighted, e.g. by a defender's play

    std::vector<BeliefEntry> scratch;
    BeliefView const view = make_belief_view(node, scratch);

    ASSERT_EQ(view.entries.size(), 2u);
    EXPECT_NEAR(view.entries[0].posterior, 0.9 / 1.2, 1e-12);
    EXPECT_NEAR(view.entries[1].posterior, 0.3 / 1.2, 1e-12);
}

TEST_F(BeliefViewTest, IsSampleIsFalseAndSpaceSizeIsTheNodesLayoutCount)
{
    BeliefNode const node = make_node_with_p({0.4, 0.3, 0.3}, /*kappa=*/1.0);

    std::vector<BeliefEntry> scratch;
    BeliefView const view = make_belief_view(node, scratch);

    EXPECT_FALSE(view.is_sample);
    EXPECT_EQ(view.space_size, 3u);
}

TEST_F(BeliefViewTest, IsSampleIsCopiedFromTheNodeNotHardcoded)
{
    // This evaluator never sets is_sample true, but make_belief_view must
    // still read it from the node rather than writing the literal false,
    // so a future sampling evaluator has one place (BeliefNode::is_sample)
    // to change rather than every call site.
    BeliefNode node = make_node_with_p({1.0}, /*kappa=*/1.0);
    node.is_sample = true;

    std::vector<BeliefEntry> scratch;
    BeliefView const view = make_belief_view(node, scratch);

    EXPECT_TRUE(view.is_sample);
}

TEST_F(BeliefViewTest, SpaceSizeIsZeroOnASampledNodeEvenThoughEntriesStillCarriesEveryDrawnLayout)
{
    // The false-certainty failure this exists to prevent: on a sampled
    // node the true belief-space size is genuinely unknown (the evaluator
    // has seen a prefix of the source, not the whole of it), so
    // space_size reports 0 -- BeliefView::space_size's own documented
    // meaning for "unknown" -- rather than the drawn count, which would
    // hand a strategy a false certainty about how determined the position
    // is. Nothing else about the view changes: entries still carries every
    // one of the three drawn layouts and its normalised posterior, exactly
    // as the unsampled case above does.
    BeliefNode node = make_node_with_p({0.4, 0.3, 0.3}, /*kappa=*/1.0);
    node.is_sample = true;

    std::vector<BeliefEntry> scratch;
    BeliefView const view = make_belief_view(node, scratch);

    EXPECT_TRUE(view.is_sample);
    EXPECT_EQ(view.space_size, 0u);
    ASSERT_EQ(view.entries.size(), 3u);
    EXPECT_NEAR(view.entries[0].posterior, 0.4, 1e-12);
    EXPECT_NEAR(view.entries[1].posterior, 0.3, 1e-12);
    EXPECT_NEAR(view.entries[2].posterior, 0.3, 1e-12);
}
