#include "belief_converters.hpp"

#include <cstdint>

#include <pybind11/pybind11.h>

#include <dds3/converters.hpp>

namespace py = pybind11;
namespace be = dds::belief_evaluation;

namespace dds3_python
{

namespace
{
    auto depth_sample_stats_to_dict(const be::DepthSampleStats& stats) -> py::dict
    {
        py::dict result;
        result["nodes"] = stats.nodes;
        result["layout_sum"] = stats.layout_sum;
        result["layout_min"] = stats.layout_min;
        return result;
    }

    auto depth_replenishment_stats_to_dict(const be::DepthReplenishmentStats& stats) -> py::dict
    {
        py::dict result;
        result["attempted"] = stats.attempted;
        result["succeeded"] = stats.succeeded;
        result["layouts_added"] = stats.layouts_added;
        result["at_calls"] = stats.at_calls;
        return result;
    }

    auto evaluation_counters_to_dict(const be::EvaluationCounters& counters) -> py::dict
    {
        py::dict result;
        result["nodes_visited"] = counters.nodes_visited;
        result["tier1_made_cuts"] = counters.tier1_made_cuts;
        result["tier1_dead_cuts"] = counters.tier1_dead_cuts;
        result["tier2_cuts"] = counters.tier2_cuts;

        // Raw counts, not ratios: counts can be summed across depths and
        // runs. The list is exactly as long as the C++ vector. An index past
        // the end means no node was visited at that depth. That differs from
        // an entry with nodes == 0.
        py::list sample_size_by_depth;
        for (const be::DepthSampleStats& depth : counters.sample_size_by_depth) {
            sample_size_by_depth.append(depth_sample_stats_to_dict(depth));
        }
        result["sample_size_by_depth"] = sample_size_by_depth;

        py::list replenishment_by_depth;
        for (const be::DepthReplenishmentStats& depth : counters.replenishment_by_depth) {
            replenishment_by_depth.append(depth_replenishment_stats_to_dict(depth));
        }
        result["replenishment_by_depth"] = replenishment_by_depth;

        return result;
    }

    // The root node only, never a tree (see EvaluateOptions::retain_root).
    // kappa is internal sample-weight bookkeeping, so it is not exposed,
    // as in BeliefView (see belief_view.hpp).
    auto belief_node_to_dict(const be::BeliefNode& node) -> py::dict
    {
        py::dict result;

        py::list layouts;
        for (const Deal& layout : node.layouts) {
            layouts.append(deal_to_dict(layout));
        }
        result["layouts"] = layouts;

        py::list p;
        for (const be::Probability& weight : node.p) {
            p.append(weight);
        }
        result["p"] = p;

        py::list root_keys;
        for (const std::uint64_t& key : node.root_keys) {
            root_keys.append(key);
        }
        result["root_keys"] = root_keys;

        result["is_sample"] = node.is_sample;
        result["no_more_available"] = node.no_more_available;
        return result;
    }
}  // namespace

// Converts a successful result to a dict. EvaluationResult::error is not
// converted: the evaluate() binding raises before it calls this. So
// by_strategy is always populated here.
auto evaluation_result_to_dict(const be::EvaluationResult& result) -> py::dict
{
    py::dict out;
    py::dict by_strategy;
    for (const auto& [id, value] : result.by_strategy) {
        py::dict entry;
        entry["p_make"] = value.p_make;

        // root_children (see EvaluationValue::root_children):
        // - at a declarer root, alternatives: p_make is the one pi chose;
        // - at a defender root, a partition that sums to p_make;
        // - empty at a terminal root, or when the contract is already made.
        py::list root_children;
        for (const be::RootChildValue& child : value.root_children) {
            root_children.append(py::make_tuple(child.card, child.value));
        }
        entry["root_children"] = root_children;
        // Which shape root_children has. Present on every result, even when
        // root_children is empty.
        entry["root_is_declaring_side"] = value.root_is_declaring_side;

        if (value.retained_root.has_value()) {
            entry["retained_root"] = belief_node_to_dict(*value.retained_root);
        }
        if (value.counters.has_value()) {
            entry["counters"] = evaluation_counters_to_dict(*value.counters);
        }

        by_strategy[py::cast(id)] = entry;
    }
    out["by_strategy"] = by_strategy;
    return out;
}

}  // namespace dds3_python
