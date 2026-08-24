#include <gtest/gtest.h>

#include <vector>

#include <api/dll.h>
#include <utility/constants.h>

#include <belief_evaluation/belief_view.hpp>
#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/node.hpp>

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
    // Plan 2 never sets is_sample true, but make_belief_view must still
    // read it from the node rather than writing the literal false, so
    // plan 5 has one place (BeliefNode::is_sample) to change rather than
    // every call site.
    BeliefNode node = make_node_with_p({1.0}, /*kappa=*/1.0);
    node.is_sample = true;

    std::vector<BeliefEntry> scratch;
    BeliefView const view = make_belief_view(node, scratch);

    EXPECT_TRUE(view.is_sample);
}
