#pragma once

#include <vector>

#include <belief_evaluation/node.hpp>
#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// Builds the view a declarer strategy reasons over: `node`'s layouts,
/// paired with the normalised posterior `p_i / Sigma_j p_j` within the
/// node — never `w_i`, never `kappa * p_i`, never the raw `p_i`. `kappa` is
/// the evaluator's own sample-weight bookkeeping, not declarer's knowledge,
/// and does not cross into the view; only the normalised posterior does.
///
/// The posterior is derived here rather than stored, because `p` is never
/// renormalised at a defender child (see defender-node expansion) — storing
/// a normalised form back into the node would silently break the mass
/// conservation that relies on `p` staying un-renormalised.
///
/// `scratch` is owned by the caller (the same function that goes on to call
/// a strategy with the returned view) and is overwritten on every call. The
/// returned `BeliefView`'s span references `scratch` and `node.layouts`;
/// both must outlive the call the view is used for, and neither may be
/// mutated while it is live. `node` is `const` so nothing reachable through
/// it can mutate the layouts underneath the view.
///
/// `is_sample` is copied from `node.is_sample` (currently always false — no
/// sampling exists yet) and `space_size` is the node's own layout count —
/// the whole belief space the node genuinely holds, not an estimate, since
/// this evaluator is exhaustive and never samples.
auto make_belief_view(BeliefNode const& node, std::vector<BeliefEntry>& scratch) -> BeliefView;

}  // namespace dds::belief_evaluation
