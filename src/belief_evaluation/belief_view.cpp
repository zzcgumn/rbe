#include <belief_evaluation/belief_view.hpp>

#include <cstddef>

#include <belief_evaluation/kahan.hpp>

auto make_belief_view(BeliefNode const& node, std::vector<BeliefEntry>& scratch) -> BeliefView
{
    KahanAccumulator total_p;
    for (Probability const p_i : node.p)
    {
        total_p.add(p_i);
    }
    double const total = total_p.value();

    scratch.clear();
    scratch.reserve(node.layouts.size());
    for (std::size_t i = 0; i < node.layouts.size(); ++i)
    {
        scratch.push_back(BeliefEntry{node.layouts[i], node.p[i] / total});
    }

    return BeliefView{
        std::span<BeliefEntry const>(scratch), node.is_sample, node.layouts.size()};
}
