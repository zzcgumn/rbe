#pragma once

/// @file belief_converters.hpp
/// @brief The belief-evaluation converters for the pybind extension.
///
/// Separate from converters.hpp, which both extensions compile. So dds3
/// does not need to link the evaluator. The Deal converters (dict_to_deal,
/// deal_to_dict) stay in converters.hpp, because both extensions use them.

#include <pybind11/pytypes.h>

#include <belief_evaluation/evaluate.hpp>

namespace dds3_python
{

// Returns a dict, like converters.hpp. ObservationState and BeliefView are
// bound classes instead: a callback receives them on every call, and a dict
// per call would cost too much. A result is returned once per evaluate()
// call. Callers never build one, so there is no dict_to_* function.
auto evaluation_result_to_dict(const dds::belief_evaluation::EvaluationResult& result) -> pybind11::dict;

}  // namespace dds3_python
