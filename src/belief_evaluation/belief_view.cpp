#include <belief_evaluation/belief_view.hpp>

#include <cassert>
#include <cstddef>

#include <belief_evaluation/kahan.hpp>

namespace dds::belief_evaluation
{

auto make_belief_view(BeliefNode const& node, std::vector<BeliefEntry>& scratch) -> BeliefView
{
    KahanAccumulator total_p;
    for (Probability const p_i : node.p)
    {
        total_p.add(p_i);
    }
    double const total = total_p.value();
    // Never zero: the root has p_i = 1, and later p_i are products of
    // validated, positive probabilities. An internal invariant, so asserted.
    assert(total > 0.0);

    scratch.clear();
    scratch.reserve(node.layouts.size());
    for (std::size_t i = 0; i < node.layouts.size(); ++i)
    {
        scratch.push_back(BeliefEntry{node.layouts[i], node.p[i] / total});
    }

    return BeliefView{
        std::span<BeliefEntry const>(scratch),
        node.is_sample,
        node.is_sample ? 0 : node.layouts.size()};
}

}  // namespace dds::belief_evaluation
