#include <belief_evaluation/expand.hpp>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <map>

#include <api/dds_constants.hpp>
#include <belief_evaluation/belief_view.hpp>
#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/rank_map.hpp>
#include <belief_evaluation/trick.hpp>

namespace dds::belief_evaluation
{

namespace
{
    /// A total order over Card, for grouping defender children by card.
    auto card_key(Card const& card) -> int
    {
        return card.suit * 100 + card.rank;
    }

    /// Mass conservation (algorithm.md: Sigma_c w_i^(...,b,c) = w_i^(...,b)):
    /// the children's masses sum to the parent's. Each delta distribution was
    /// already validated, so a failure here is an internal bug, not caller
    /// input. Called only inside assert(), so it vanishes under NDEBUG.
    ///
    /// The tolerance is ProbabilitySumTolerance times node.layouts.size().
    /// Each layout can add up to ProbabilitySumTolerance of error, and every
    /// p_i <= 1, so the worst case grows with the layout count.
    ///
    /// A replenishment rescale of kappa adds no error term here. Parent and
    /// children share the same kappa value, so its rounding cancels.
    /// Replenished layouts are counted in node.layouts.size() like any other.
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
    // The two defender entries of known_holdings both hold the shared pool
    // (see known_holdings_for in node.cpp). play() clears the card only from
    // the seat on play, so a defender's card would stay in the other
    // defender's entry. Clear it from every hand first. A card is only set
    // where it is held, so this is safe for every seat.
    ObservationState next = state;
    Deal known = state.known_holdings;
    for (int hand = 0; hand < DDS_HANDS; ++hand)
    {
        known.remainCards[hand][card.suit] &= ~(1u << card.rank);
    }
    Deal const advanced = play(known, card);

    // play() clears currentTrick* only when the trick resolves. So all slots
    // zero means the trick resolved.
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

        child.p = parent.p;          // declarer's play does not reweight
        child.root_keys = parent.root_keys;  // or filter layouts
        child.kappa = parent.kappa;  // every child gets the full mass
        child.is_sample = parent.is_sample;
        child.no_more_available = parent.no_more_available;
        children.push_back(std::move(child));
    }
    return children;
}

auto expand_declarer_node(BeliefNode const& node, DeclarerStrategy const& pi) -> ExpandResult
{
    // layouts, p and root_keys are parallel (node.hpp). Fail here on a
    // hand-built fixture that breaks this, not on a later operator[].
    assert(node.p.size() == node.layouts.size());
    assert(node.root_keys.size() == node.layouts.size());

    int const seat = seat_on_play(node.state.known_holdings);

    std::vector<BeliefEntry> scratch;
    BeliefView const view = make_belief_view(node, scratch);

    // pi may throw DeclarerStrategyContractViolation when a delegate breaks
    // its contract. Convert it to the same ValidationError as a bad card, so
    // it never leaves evaluate(). Keep the delegate's seat and layout as
    // nested_seat and nested_layout. See declarer_strategy.hpp.
    Card card;
    try
    {
        card = pi.play(node.state, view);
    }
    catch (DeclarerStrategyContractViolation const& violation)
    {
        return ExpandResult{
            std::nullopt, Card{}, violation.validation, violation.seat, violation.offending_layout};
    }

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
    // As in expand_declarer_node. node.root_keys[i] is indexed below.
    assert(node.p.size() == node.layouts.size());
    assert(node.root_keys.size() == node.layouts.size());

    int const seat = seat_on_play(node.state.known_holdings);

    // Grouped by card_key: the card, its surviving layouts (advanced by it),
    // and their reweighted p. std::map gives a deterministic order, which the
    // determinism test relies on. All three maps share one key space.
    std::map<int, Card> card_by_key;
    std::map<int, std::vector<Deal>> layouts_by_key;
    std::map<int, std::vector<Probability>> p_by_key;
    std::map<int, std::vector<std::uint64_t>> root_keys_by_key;

    for (std::size_t i = 0; i < node.layouts.size(); ++i)
    {
        Deal const& layout = node.layouts[i];
        DefenderQuery const query{layout, seat, node.state};
        std::vector<WeightedCard> const distribution = delta(query);  // once per layout

        // Checked here, not in validate_defender_distribution. See
        // ValidationError::DistributionEmpty.
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
            // p is never renormalised: p_i' = p_i * delta(...). belief_view.hpp
            // normalises on demand.
            p_by_key[key].push_back(node.p[i] * entry.probability);
            // Pushed in the same loop and order as layouts and p. A root-space key
            // must never be recomputed from a node Deal: different root layouts can
            // replay to the same Deal. See BeliefNode::root_keys.
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
        child.kappa = node.kappa;  // defender children split p, not kappa
        child.is_sample = node.is_sample;
        child.no_more_available = node.no_more_available;
        children.push_back(std::move(child));
    }

    // See defender_children_conserve_mass.
    assert(defender_children_conserve_mass(node, children));

    return ExpandDefenderResult{std::move(children), ValidationError::None};
}

}  // namespace dds::belief_evaluation
