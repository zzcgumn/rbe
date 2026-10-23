#pragma once

#include <vector>

#include <belief_evaluation/node.hpp>
#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// Builds the view a declarer strategy reads: the layouts of `node`, each
/// with the normalised posterior `p_i / Sigma_j p_j`. Never `kappa * p_i`
/// and never the raw `p_i`: `kappa` is the evaluator's bookkeeping, not
/// declarer's knowledge.
///
/// The posterior is computed here, not stored. Storing it in the node
/// would break mass conservation, which needs the raw `p`.
///
/// `scratch` belongs to the caller and is overwritten on every call. The
/// view refers to `scratch` and `node.layouts`. **Both must outlive the
/// view, and neither may change while it is in use.**
///
/// `space_size` is `node.layouts.size()` **only when `node.is_sample` is
/// false**, and 0 otherwise. On a sample the true size is unknown. The
/// drawn count would give a strategy false certainty: a node down to one
/// layout would claim a space of one. algorithm.md warns about this. Use
/// `entries.size()` for the number of layouts in the view.
auto make_belief_view(BeliefNode const& node, std::vector<BeliefEntry>& scratch) -> BeliefView;

}  // namespace dds::belief_evaluation
