#pragma once

#include <cstdint>
#include <optional>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/layout_source.hpp>

// How many nodes the search tree would have, with tier 1 and tier 2 both
// removed entirely -- for "how much of the tree does a cut actually
// remove", which evaluate() itself cannot answer directly: already_made() and
// is_dead() are called unconditionally in evaluate.cpp, with no
// EvaluateOptions flag to disable either, so there is no way to ask the
// real evaluator for an uncut count. Built entirely from the public
// pieces evaluate() itself is built from -- make_root, is_terminal,
// expand_declarer_node, expand_defender_node -- so this needs no change
// under library/.
namespace dds::belief_evaluation::benchmarks
{

/// Walks the full tree over `source` for `(root, declarer, tricks_needed)`,
/// calling `pi`/`delta` at every node and never checking already_made(),
/// is_dead() or tier2_dead() -- so a subtree a real evaluate() call would
/// have cut here gets expanded in full. Stops recursing at a node exactly
/// when either is_terminal() holds (no hand anywhere has a card left,
/// evaluate()'s own stopping rule, not a cut), or the seat on play at that
/// node is void in every layout the node holds -- not a cut either: a
/// void seat cannot be asked to choose (this project's own fixtures
/// routinely give declarer or dummy far fewer cards than the defenders'
/// own pool, by design -- see fixtures.hpp -- so this boundary is reached
/// well before is_terminal() would be on most of them). Checked against
/// `node.layouts` directly rather than `node.state.known_holdings`: the
/// latter is exact for declarer and dummy but is the *aggregate pool* for
/// a defender seat (both defenders' entries hold the same union value,
/// not each one's own split), so it can read nonzero for a defender who
/// is actually void everywhere this node's own belief has already
/// narrowed to.
///
/// Returns the total node count, or nullopt if `pi`/`delta` ever returns
/// something expand_declarer_node/expand_defender_node rejects (this
/// walker has no error-reporting path beyond that -- a caller wanting the
/// specific ValidationError should call evaluate() itself).
auto count_uncut_nodes(
    Deal const& root, int declarer, int tricks_needed, LayoutSource const& source,
    DeclarerStrategy const& pi, DefenderStrategy const& delta) -> std::optional<std::uint64_t>;

}  // namespace dds::belief_evaluation::benchmarks
