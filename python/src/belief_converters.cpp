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

        // Not summarised, not turned into a ratio on the way out -- a
        // ratio cannot be summed across depths or runs, which is exactly
        // why the C++ type stores counts rather than a scan-to-hit figure
        // itself. An index beyond either list's own length means "no node
        // was ever visited at this depth", distinct from an entry present
        // with nodes == 0 -- the vectors are grown on demand for exactly
        // this reason, and that distinction survives here unchanged: this
        // list is exactly as long as the C++ vector, no padding either way.
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

    // The root node only -- EvaluateOptions::retain_root never holds a
    // retained tree (a deliberate choice, not an oversight: see that
    // field's own doxygen), and a caller seeing "retained root" in a
    // result would otherwise reasonably assume a tree is reachable from
    // it. kappa never crosses, for the same reason a BeliefView's own
    // posterior is the normalised form and not kappa (see belief_view.hpp)
    // -- it is the evaluator's own sample-weight bookkeeping, not
    // something declarer (or a caller reading the result) ever needs.
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

// Dict out for a *successful* result, matching converters.cpp's idiom
// (see this function's own doxygen in belief_converters.hpp for why the
// direction matters here and not for a Deal or an ObservationState).
// EvaluationResult::error is not represented here at all: the evaluate()
// binding that is this function's only caller raises before ever
// reaching this call when result.error is set (every RootFailure and
// every ValidationError cause raises a distinguishable exception, rather
// than the evaluator's own "reported, not thrown" posture surviving
// unchanged all the way to the Python boundary) -- so by_strategy is
// always populated by the time this runs.
auto evaluation_result_to_dict(const be::EvaluationResult& result) -> py::dict
{
    py::dict out;
    py::dict by_strategy;
    for (const auto& [id, value] : result.by_strategy) {
        py::dict entry;
        entry["p_make"] = value.p_make;

        // root_children: alternatives, not a partition, at a declarer
        // root (p_make equals whichever entry pi actually chose, not
        // their sum); a genuine partition summing to p_make at a
        // defender root; empty at a terminal root, or one where declarer
        // has already banked every trick the contract needs. Summing
        // these and comparing to p_make agrees sometimes and not others
        // -- read EvaluationValue::root_children's own doxygen before
        // drawing a conclusion from a mismatch.
        py::list root_children;
        for (const be::RootChildValue& child : value.root_children) {
            root_children.append(py::make_tuple(child.card, child.value));
        }
        entry["root_children"] = root_children;
        // Which of the two shapes root_children has -- see
        // EvaluationValue::root_is_declaring_side. Present on every result,
        // including those where root_children is empty.
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
