#include <gtest/gtest.h>

#include <vector>

#include <api/dds_constants.hpp>

#include <belief_evaluation/belief_view.hpp>
#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/node.hpp>

// Tests for make_belief_view: normalised posteriors, the is_sample flag
// and space_size.

// A namespace alias and targeted `using` declarations, not `using
// namespace dds::belief_evaluation;`. The includes (belief_view.hpp,
// node.hpp) already make the global ::Card from api/dds_data_types.hpp
// visible, so a bare `Card` would be ambiguous instead of a clear compile
// error.
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
    // p = {0.6, 0.1} does not sum to 1, and kappa != 1. So posterior == p_i
    // and posterior == w_i (= kappa * p_i) are both false unless the view
    // normalises within the node: 0.6 / 0.7 = 0.857142..., 0.1 / 0.7 =
    // 0.142857...
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
    // A defender child's p after unequal reweighting. A stale view, built
    // before the reweighting, would still report the old {0.5, 0.5} split.
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
    // make_root sets is_sample on a sampled root, and expand() copies it to
    // the children. make_belief_view must read it from the node, not write a
    // literal false.
    BeliefNode node = make_node_with_p({1.0}, /*kappa=*/1.0);
    node.is_sample = true;

    std::vector<BeliefEntry> scratch;
    BeliefView const view = make_belief_view(node, scratch);

    EXPECT_TRUE(view.is_sample);
}

TEST_F(BeliefViewTest, SpaceSizeIsZeroOnASampledNodeEvenThoughEntriesStillCarriesEveryDrawnLayout)
{
    // On a sampled node the true belief-space size is unknown: the evaluator
    // has seen a prefix of the source, not all of it. So space_size reports 0,
    // BeliefView's value for "unknown". The drawn count would give a strategy
    // a false certainty about how determined the position is. Nothing else
    // changes: entries still carries all three drawn layouts and their
    // normalised posteriors, as in the unsampled case above.
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
