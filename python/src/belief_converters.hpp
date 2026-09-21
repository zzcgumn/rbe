#pragma once

/// @file belief_converters.hpp
/// @brief The belief-evaluation half of this directory's pybind converters.
///
/// Split out of converters.hpp so that the dds3 extension does not depend on
/// //library/src/belief_evaluation at all. converters.{hpp,cpp} is one shared
/// translation unit compiled into both extensions; while the EvaluationResult
/// conversion lived there, dds3 had to link the evaluator to compile a
/// function bindings.cpp never calls. Everything here is used only by
/// belief_space_local_evaluation.cpp.
///
/// The Deal-facing converters this file's implementation calls
/// (dict_to_deal, deal_to_dict) stay in converters.hpp: they are genuinely
/// shared, and duplicating them would be a second dict-to-Deal
/// implementation to keep in step with the first.

#include <pybind11/pytypes.h>

#include <belief_evaluation/evaluate.hpp>

namespace dds3_python
{

// dict out, matching converters.hpp's own idiom -- unlike
// ObservationState or BeliefView (bound as read-only classes, since they
// are handed *to* a callback and materialising a dict per call would be
// the belief-set-copy cost those bindings exist to avoid), an
// EvaluationResult is handed *out*, once, at the end of one evaluate()
// call: no per-call cost to avoid, and a caller of the existing bindings
// already expects a dict back. Never constructed by a caller, so no
// matching dict_to_* exists or is needed.
auto evaluation_result_to_dict(const dds::belief_evaluation::EvaluationResult& result) -> pybind11::dict;

}  // namespace dds3_python
