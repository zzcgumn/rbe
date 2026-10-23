#pragma once

#include <cstdint>
#include <optional>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/layout_source.hpp>

// The node count with tier 1 and tier 2 both removed: the denominator for
// "how much of the tree does a cut remove". evaluate() cannot report it,
// because already_made() and is_dead() always run; no option disables
// them. This is built from the public pieces evaluate() uses (make_root,
// is_terminal, the two expand functions), so nothing under src/ changes.
namespace dds::belief_evaluation::benchmarks
{

/// Walks the full tree over `source` for `(root, declarer, tricks_needed)`.
/// It calls `pi` and `delta` at every node and never checks already_made(),
/// is_dead() or tier2_dead(), so a subtree evaluate() would cut is expanded
/// in full. It stops recursing in two cases, neither of them a cut:
/// - is_terminal() holds (evaluate()'s stopping rule);
/// - the seat on play is void in a layout the node holds, so it cannot be
///   asked for a card. Most fixtures give declarer or dummy far fewer cards
///   than the defenders' pool, so this is usually reached first. It checks
///   `node.layouts`, not `node.state.known_holdings`: a defender's entry
///   there is the whole pool, which reads non-zero for a void seat.
///
/// Returns the total node count. Returns nullopt if `pi` or `delta` returns
/// something expand_declarer_node or expand_defender_node rejects. For the
/// specific ValidationError, call evaluate().
///
/// A declarer-led root is handled as evaluate() handles it: every legal
/// root card is expanded, through expand_declarer_node for pi's choice and
/// make_declarer_children for the rest. Every declarer node below the root
/// follows only pi's choice.
///
/// **Not an upper bound on evaluate()'s node count in general.** This
/// walker visits each node once. The real evaluator can revisit sibling
/// subtrees (see `DeclarerStrategy::play`). Where nothing is revisited this
/// count is an upper bound, which holds on every finesse and solver rung.
/// The pool and realistic ladders do revisit, and evaluate()'s count there
/// can exceed this one. Do not report a "tree fraction removed" for those
/// two ladders.
auto count_uncut_nodes(
    Deal const& root, int declarer, int tricks_needed, LayoutSource const& source,
    DeclarerStrategy const& pi, DefenderStrategy const& delta) -> std::optional<std::uint64_t>;

}  // namespace dds::belief_evaluation::benchmarks
