#include <belief_evaluation/expand.hpp>

#include <map>

#include <belief_evaluation/belief_view.hpp>
#include <belief_evaluation/rank_map.hpp>
#include <belief_evaluation/trick.hpp>
#include <utility/constants.h>

namespace
{
    /// state advanced by seat playing card: known_holdings and ranks updated
    /// via trick.hpp's play(), the card appended to history, and
    /// tricks_won_by_declarer incremented if this play resolved a trick in
    /// declarer's or dummy's favour. Trump, declarer, tricks_needed and
    /// `first` (the root's leader, not the current trick's — see
    /// ObservationState's own doxygen) are unaffected by a single card.
    ///
    /// known_holdings' declarer and dummy entries are each hand's exact
    /// holding, but its two defender entries are both set to the *union*
    /// pool the two defenders share (see known_holdings_for in node.cpp) —
    /// so play(), which clears the card from only seat_on_play's own slot,
    /// is correct for a declarer or dummy play but leaves a defender's
    /// played card sitting in the *other* defender's identical pool entry.
    /// Clearing the card from every hand's slot first — safe, since a card
    /// can only ever be set in the slot(s) that actually hold it — fixes
    /// this uniformly for both cases without needing to know which kind of
    /// seat played.
    auto advance_state(ObservationState const& state, Card const& card) -> ObservationState
    {
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
        bool const trick_resolved = advanced.currentTrickRank[0] == 0
            && advanced.currentTrickRank[1] == 0 && advanced.currentTrickRank[2] == 0;
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

    /// A total order over Card, for grouping defender children by the card
    /// played — Card itself declares no comparison, and doesn't need one
    /// anywhere else in the module.
    auto card_key(Card const& card) -> int
    {
        return card.suit * 100 + card.rank;
    }
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
        child.kappa = parent.kappa;  // every child gets the parent's full
                                      // mass, not a share of it.
        child.is_sample = parent.is_sample;
        children.push_back(std::move(child));
    }
    return children;
}

auto expand_declarer_node(BeliefNode const& node, DeclarerStrategy const& pi) -> ExpandResult
{
    int const seat = seat_on_play(node.state.known_holdings);

    std::vector<BeliefEntry> scratch;
    BeliefView const view = make_belief_view(node, scratch);
    Card const card = pi.play(node.state, view);

    ValidationError const error = validate_declarer_card(node.state.known_holdings, seat, card);
    if (error != ValidationError::None)
    {
        return ExpandResult{std::nullopt, error};
    }

    std::vector<BeliefNode> children = make_declarer_children(node, {card});
    return ExpandResult{std::move(children.front()), ValidationError::None};
}

auto expand_defender_node(BeliefNode const& node, DefenderStrategy const& delta)
    -> ExpandDefenderResult
{
    int const seat = seat_on_play(node.state.known_holdings);

    // Grouped by card (keyed via card_key): the card itself, the surviving
    // layouts that card belongs to (advanced by it), and their reweighted
    // p. std::map keeps first-seen order stable and both maps share the
    // same key space, so lookups below never need a not-found branch.
    std::map<int, Card> card_by_key;
    std::map<int, std::vector<Deal>> layouts_by_key;
    std::map<int, std::vector<Probability>> p_by_key;

    for (std::size_t i = 0; i < node.layouts.size(); ++i)
    {
        Deal const& layout = node.layouts[i];
        DefenderQuery const query{layout, seat, node.state};
        std::vector<WeightedCard> const distribution = delta(query);  // once per layout

        ValidationError const error = validate_defender_distribution(layout, seat, distribution);
        if (error != ValidationError::None)
        {
            return ExpandDefenderResult{std::nullopt, error};
        }

        for (WeightedCard const& entry : distribution)
        {
            int const key = card_key(entry.card);
            card_by_key.emplace(key, entry.card);
            layouts_by_key[key].push_back(layout);
            // p is never renormalised here — p_i' = p_i * delta(...) and
            // nothing else. The normalised posterior pi sees is a view
            // computed from this, never stored back (task 07).
            p_by_key[key].push_back(node.p[i] * entry.probability);
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
        child.kappa = node.kappa;  // kappa is untouched; defender children
                                    // partition p, not kappa.
        child.is_sample = node.is_sample;
        children.push_back(std::move(child));
    }

    return ExpandDefenderResult{std::move(children), ValidationError::None};
}
