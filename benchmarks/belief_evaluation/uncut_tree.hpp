#pragma once

#include <cstdint>
#include <optional>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/layout_source.hpp>

// How many nodes the search tree would have with tier 1 and tier 2 both
// removed -- the denominator for "how much of the tree does a cut remove",
// which evaluate() cannot answer itself: already_made() and is_dead() are
// unconditional, with no option to disable either. Built from the same
// public pieces evaluate() is built from (make_root, is_terminal, the two
// expand functions), so it needs no change under src/.
namespace dds::belief_evaluation::benchmarks
{

/// Walks the full tree over `source` for `(root, declarer, tricks_needed)`,
/// calling `pi`/`delta` at every node and never checking already_made(),
/// is_dead() or tier2_dead() -- so a subtree a real evaluate() call would
/// have cut here gets expanded in full. Stops recursing when is_terminal()
/// holds (evaluate()'s own stopping rule, not a cut), or when the seat on
/// play is void in every layout the node holds -- not a cut either, since
/// a void seat cannot be asked to choose. Most fixtures here give declarer
/// or dummy far fewer cards than the defenders' pool, so that second
/// boundary is usually reached first. Checked against `node.layouts`, not
/// `node.state.known_holdings`, whose defender entries hold the aggregate
/// pool rather than each seat's own split and so read nonzero for a seat
/// that is actually void.
///
/// Returns the total node count, or nullopt if `pi`/`delta` ever returns
/// something expand_declarer_node/expand_defender_node rejects (this
/// walker has no error-reporting path beyond that -- a caller wanting the
/// specific ValidationError should call evaluate() itself).
///
/// A declarer-led root is handled as evaluate() handles one: every legal
/// root card is expanded, through the same two pieces evaluate() uses
/// (expand_declarer_node for pi's choice, make_declarer_children for the
/// rest) -- not only pi's choice, unlike every declarer node below the
/// root. Missing this silently undercounted a declarer-led root.
///
/// **Not an upper bound on evaluate()'s own node count in general --
/// confirmed empirically, not assumed.** This is a single linear traversal,
/// visiting every node object once, while the real evaluator revisits
/// sibling subtrees (see `DeclarerStrategy::play`). Where no revisiting
/// happens this count is a true upper bound on evaluate()'s, which holds
/// for every rung of fixtures.hpp's finesse and solver ladders. The pool
/// and realistic ladders do trigger revisiting, and evaluate()'s count
/// there can exceed this one -- they are counting different things. Do
/// not report a "tree fraction removed" for those two ladders.
auto count_uncut_nodes(
    Deal const& root, int declarer, int tricks_needed, LayoutSource const& source,
    DeclarerStrategy const& pi, DefenderStrategy const& delta) -> std::optional<std::uint64_t>;

}  // namespace dds::belief_evaluation::benchmarks
