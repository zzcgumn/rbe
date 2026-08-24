#include <belief_evaluation/expand.hpp>

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
    auto advance_state(ObservationState const& state, Card const& card) -> ObservationState
    {
        ObservationState next = state;
        Deal const advanced = play(state.known_holdings, card);

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
        children.push_back(std::move(child));
    }
    return children;
}

auto expand_declarer_node(BeliefNode const& node, DeclarerStrategy const& pi) -> ExpandResult
{
    int const seat = seat_on_play(node.state.known_holdings);

    BeliefView const view{{}, false, node.layouts.size()};  // task 07 replaces this placeholder
    Card const card = pi.play(node.state, view);

    ValidationError const error = validate_declarer_card(node.state.known_holdings, seat, card);
    if (error != ValidationError::None)
    {
        return ExpandResult{std::nullopt, error};
    }

    std::vector<BeliefNode> children = make_declarer_children(node, {card});
    return ExpandResult{std::move(children.front()), ValidationError::None};
}
