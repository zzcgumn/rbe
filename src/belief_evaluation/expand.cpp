#include <belief_evaluation/expand.hpp>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <map>

#include <belief_evaluation/belief_view.hpp>
#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/rank_map.hpp>
#include <belief_evaluation/trick.hpp>
#include <utility/constants.h>

namespace dds::belief_evaluation
{

namespace
{
    /// A total order over Card, for grouping defender children by the card
    /// played — Card itself declares no comparison, and doesn't need one
    /// anywhere else in the module.
    auto card_key(Card const& card) -> int
    {
        return card.suit * 100 + card.rank;
    }

    /// Mass conservation (algorithm.md's Sigma_c w_i^(...,b,c) = w_i^(...,b)):
    /// summing every child's mass must reproduce the parent's. Every
    /// distribution delta returned was already validated per-layout by its
    /// caller, each within ProbabilitySumTolerance of summing to one, so a
    /// violation here is not user input — it is a genuine internal
    /// invariant failure (a bug in the grouping/reweighting that built
    /// children, not a contract violation delta committed), and this is
    /// called only inside an assert() rather than reported through
    /// ExpandDefenderResult.
    ///
    /// The tolerance is ProbabilitySumTolerance scaled by node.layouts.size(),
    /// not used verbatim: this check sums a quantity derived from every
    /// layout's distribution, and each layout independently contributes up
    /// to ProbabilitySumTolerance of error (kappa * p_i * that layout's own
    /// sum-of-probabilities deviation), so the worst case (every layout's
    /// error the same sign) scales with how many layouts there are, not
    /// with a single distribution's own tolerance. p_i <= 1 for every
    /// layout at every node (it only ever shrinks from the root's 1 via
    /// multiplication by further probabilities <= 1), so scaling by the
    /// layout count rather than by Sigma_i p_i is a safe, if slightly
    /// looser, bound.
    ///
    /// Re-derived, not assumed, now that node.kappa may have come from a
    /// replenishment's rescale (kappa *= E / E') rather than only from
    /// make_root's 1/N or a plain copy: that division introduces its own
    /// rounding, but it does not add an uncounted error term to *this*
    /// check. node.kappa and every child's kappa are the same double value
    /// (copy-assigned, never recomputed in expand_defender_node), so
    /// whatever rounding the rescale baked into it multiplies both sides of
    /// the comparison identically and cancels to that one value's own
    /// relative precision (order 1e-16), utterly below
    /// ProbabilitySumTolerance (1e-6). What is left is exactly the
    /// pre-existing per-layout term above, now summed over however many
    /// layouts node.layouts.size() currently reports -- replenished layouts
    /// included, each subject to the identical ProbabilitySumTolerance bound
    /// on its own delta query this ply, no differently from a drawn layout.
    /// The count already reflects any replenishment automatically, which is
    /// the "safe direction" a larger node.layouts.size() pushes the bound;
    /// the division pushes it nowhere, being common to both sides.
    ///
    /// Called only inside assert(): under NDEBUG the whole check --
    /// tolerance, accumulator and loop alike -- disappears with it, rather
    /// than computing and discarding a value nothing then reads. [[maybe_unused]]
    /// is correct here, unlike on the tolerance it replaces: nothing about
    /// this function runs when it isn't called, so there is no discarded
    /// work left behind for the attribute to paper over -- only the
    /// definition itself goes unused, in exactly the build where that is
    /// the point.
    [[maybe_unused]] auto defender_children_conserve_mass(
        BeliefNode const& node, std::vector<BeliefNode> const& children) -> bool
    {
        double const mass_conservation_tolerance =
            ProbabilitySumTolerance * static_cast<double>(node.layouts.size());
        KahanAccumulator total_child_mass;
        for (BeliefNode const& child : children)
        {
            total_child_mass.add(node_mass(child));
        }
        return std::abs(total_child_mass.value() - node_mass(node)) <= mass_conservation_tolerance;
    }
}

auto advance_state(ObservationState const& state, Card const& card) -> ObservationState
{
    // known_holdings' declarer and dummy entries are each hand's exact
    // holding, but its two defender entries are both set to the *union*
    // pool the two defenders share (see known_holdings_for in node.cpp) —
    // so play(), which clears the card from only seat_on_play's own slot,
    // is correct for a declarer or dummy play but leaves a defender's
    // played card sitting in the *other* defender's identical pool entry.
    // Clearing the card from every hand's slot first — safe, since a card
    // can only ever be set in the slot(s) that actually hold it — fixes
    // this uniformly for both cases without needing to know which kind of
    // seat played.
    ObservationState next = state;
    Deal known = state.known_holdings;
    for (int hand = 0; hand < DDS_HANDS; ++hand)
    {
        known.remainCards[hand][card.suit] &= ~(1u << card.rank);
    }
    Deal const advanced = play(known, card);

    // play() clears currentTrick* only on the branch that resolves a
    // trick; the append branch always leaves at least one slot
    // non-zero. So "every slot zero after" is exactly "this play
    // resolved the trick", with no need to also inspect the state
    // before the play.
    bool const trick_resolved = advanced.currentTrickRank[0] == 0 && advanced.currentTrickRank[1] == 0
        && advanced.currentTrickRank[2] == 0;
    if (trick_resolved)
    {
        int const dummy = (state.declarer + 2) % DDS_HANDS;
        int const winner = advanced.first;
        if (winner == state.declarer || winner == dummy)
        {
            next.tricks_won_by_declarer += 1;
        }
    }

    next.known_holdings = advanced;
    next.ranks = make_rank_map(advanced);
    next.history.suit[next.history.number] = card.suit;
    next.history.rank[next.history.number] = card.rank;
    next.history.number += 1;
    return next;
}

auto make_declarer_children(BeliefNode const& parent, std::vector<Card> const& cards)
    -> std::vector<BeliefNode>
{
    std::vector<BeliefNode> children;
    children.reserve(cards.size());
    for (Card const& card : cards)
    {
        BeliefNode child{};
        child.state = advance_state(parent.state, card);

        child.layouts.reserve(parent.layouts.size());
        for (Deal const& layout : parent.layouts)
        {
            child.layouts.push_back(play(layout, card));
        }

        child.p = parent.p;          // untouched: declarer's play does not
                                      // filter or reweight the belief space.
        child.root_keys = parent.root_keys;  // copied whole: declarer's play
                                              // neither filters nor renames
                                              // any layout's root-space identity.
        child.kappa = parent.kappa;  // every child gets the parent's full
                                      // mass, not a share of it.
        child.is_sample = parent.is_sample;
        child.no_more_available = parent.no_more_available;
        children.push_back(std::move(child));
    }
    return children;
}

auto expand_declarer_node(BeliefNode const& node, DeclarerStrategy const& pi) -> ExpandResult
{
    // layouts/p/root_keys same length: node.hpp's own doxygen says this is
    // "relied on wherever the node is read" -- cheap insurance that a
    // hand-built fixture violating it (root_keys left default-empty, say)
    // fails loudly here rather than through an unchecked operator[] further
    // down, the same style is_terminal()'s own assert already uses.
    assert(node.p.size() == node.layouts.size());
    assert(node.root_keys.size() == node.layouts.size());

    int const seat = seat_on_play(node.state.known_holdings);

    std::vector<BeliefEntry> scratch;
    BeliefView const view = make_belief_view(node, scratch);
    Card const card = pi.play(node.state, view);

    ValidationError const error = validate_declarer_card(node.state.known_holdings, seat, card);
    if (error != ValidationError::None)
    {
        return ExpandResult{std::nullopt, Card{}, error};
    }

    std::vector<BeliefNode> children = make_declarer_children(node, {card});
    return ExpandResult{std::move(children.front()), card, ValidationError::None};
}

auto expand_defender_node(BeliefNode const& node, DefenderStrategy const& delta)
    -> ExpandDefenderResult
{
    // Same invariant, same reason as expand_declarer_node's own assert above
    // -- this function additionally indexes node.root_keys[i] directly
    // (below), so a mismatch here is exactly the unchecked-operator[]
    // out-of-bounds this pair of asserts exists to catch before it happens.
    assert(node.p.size() == node.layouts.size());
    assert(node.root_keys.size() == node.layouts.size());

    int const seat = seat_on_play(node.state.known_holdings);

    // Grouped by card (keyed via card_key): the card itself, the surviving
    // layouts that card belongs to (advanced by it), and their reweighted
    // p. std::map orders by key (card_key), giving a stable, deterministic
    // iteration order regardless of which layout each card was first seen
    // from — the determinism oracle case relies on this. All three maps
    // share the same key space, so lookups below never need a not-found
    // branch.
    std::map<int, Card> card_by_key;
    std::map<int, std::vector<Deal>> layouts_by_key;
    std::map<int, std::vector<Probability>> p_by_key;
    std::map<int, std::vector<std::uint64_t>> root_keys_by_key;

    for (std::size_t i = 0; i < node.layouts.size(); ++i)
    {
        Deal const& layout = node.layouts[i];
        DefenderQuery const query{layout, seat, node.state};
        std::vector<WeightedCard> const distribution = delta(query);  // once per layout

        // Drawn here, before delegating, rather than inside
        // validate_defender_distribution: see ValidationError::DistributionEmpty's
        // own doxygen for why the two functions deliberately disagree on
        // this one input.
        if (distribution.empty())
        {
            return ExpandDefenderResult{std::nullopt, ValidationError::DistributionEmpty, layout};
        }
        ValidationError const error = validate_defender_distribution(layout, seat, distribution);
        if (error != ValidationError::None)
        {
            return ExpandDefenderResult{std::nullopt, error, layout};
        }

        for (WeightedCard const& entry : distribution)
        {
            int const key = card_key(entry.card);
            card_by_key.emplace(key, entry.card);
            layouts_by_key[key].push_back(layout);
            // p is never renormalised here — p_i' = p_i * delta(...) and
            // nothing else. The normalised posterior pi sees (belief_view.hpp)
            // is computed from this on demand, never stored back.
            p_by_key[key].push_back(node.p[i] * entry.probability);
            // Pushed in this same loop, in this same order, into a third
            // map alongside layouts_by_key and p_by_key -- see
            // BeliefNode::root_keys' own doxygen for why a root-space key
            // must never be recomputed from a node-depth Deal (distinct
            // root-space layouts can replay to the same Deal here), which
            // is what pushing in a second pass or deriving it from the
            // child's own layouts afterwards would silently do.
            root_keys_by_key[key].push_back(node.root_keys[i]);
        }
    }

    std::vector<BeliefNode> children;
    children.reserve(card_by_key.size());
    for (auto const& [key, card] : card_by_key)
    {
        BeliefNode child{};
        child.state = advance_state(node.state, card);
        child.layouts.reserve(layouts_by_key[key].size());
        for (Deal const& layout : layouts_by_key[key])
        {
            child.layouts.push_back(play(layout, card));
        }
        child.p = p_by_key[key];
        child.root_keys = root_keys_by_key[key];
        child.kappa = node.kappa;  // kappa is untouched; defender children
                                    // partition p, not kappa.
        child.is_sample = node.is_sample;
        child.no_more_available = node.no_more_available;
        children.push_back(std::move(child));
    }

    // Mass conservation (algorithm.md's Sigma_c w_i^(...,b,c) = w_i^(...,b)):
    // see defender_children_conserve_mass's own doxygen for the tolerance
    // derivation and why this is only ever called inside assert().
    assert(defender_children_conserve_mass(node, children));

    return ExpandDefenderResult{std::move(children), ValidationError::None};
}

}  // namespace dds::belief_evaluation
