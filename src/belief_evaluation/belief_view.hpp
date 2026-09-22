#pragma once

#include <vector>

#include <belief_evaluation/node.hpp>
#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// Builds the view a declarer strategy reasons over: `node`'s layouts
/// paired with the normalised posterior `p_i / Sigma_j p_j` — never
/// `kappa * p_i` and never the raw `p_i`. `kappa` is the evaluator's own
/// bookkeeping, not declarer's knowledge, and does not cross into the view.
///
/// The posterior is derived here rather than stored: `p` is never
/// renormalised at a defender child, and storing a normalised form back
/// into the node would break the mass conservation that relies on it.
///
/// `scratch` is the caller's, overwritten on every call. The returned
/// view's span references `scratch` and `node.layouts`; both must outlive
/// the call the view is used for, and neither may be mutated while it is
/// live.
///
/// `space_size` is `node.layouts.size()` **only when `node.is_sample` is
/// false**, and 0 otherwise. On a sampled node the true size genuinely is
/// unknown, and reporting the drawn count would hand a strategy false
/// certainty — a node down to one layout announcing a belief space of size
/// one, which is the strategy-fusion-by-the-back-door failure algorithm.md
/// warns about. A strategy wanting what it is actually reasoning over has
/// `entries.size()`.
auto make_belief_view(BeliefNode const& node, std::vector<BeliefEntry>& scratch) -> BeliefView;

}  // namespace dds::belief_evaluation
