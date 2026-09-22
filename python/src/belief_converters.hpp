#pragma once

/// @file belief_converters.hpp
/// @brief The belief-evaluation half of this directory's pybind converters.
///
/// Split out of dds's own converters.hpp so that the dds3 extension does
/// not depend on the evaluator at all: converters.{hpp,cpp} is one shared
/// translation unit compiled into both extensions, so while the
/// EvaluationResult conversion lived there, dds3 had to link the evaluator
/// to compile a function it never calls.
///
/// The Deal-facing converters this file calls (dict_to_deal, deal_to_dict)
/// stay in converters.hpp: they are genuinely shared, and duplicating them
/// would be a second dict-to-Deal implementation to keep in step.

#include <pybind11/pytypes.h>

#include <belief_evaluation/evaluate.hpp>

namespace dds3_python
{

// dict out, matching converters.hpp's idiom. ObservationState and
// BeliefView are bound as read-only classes instead, because they are
// handed *to* a callback and a dict per call is the belief-set-copy cost
// those bindings exist to avoid; an EvaluationResult is handed out once,
// at the end of one evaluate() call. Never constructed by a caller, so
// there is no matching dict_to_*.
auto evaluation_result_to_dict(const dds::belief_evaluation::EvaluationResult& result) -> pybind11::dict;

}  // namespace dds3_python
