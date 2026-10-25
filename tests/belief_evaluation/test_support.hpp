#pragma once

// Shared test doubles and fixture helpers for the evaluator's tests. The
// node, declarer-node, defender-node, BeliefView, evaluate() and oracle
// tests all need the same LayoutSource double and card-holding
// helpers.

#include <bit>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/layout_key.hpp>
#include <belief_evaluation/layout_source.hpp>
#include <belief_evaluation/trick.hpp>
#include <belief_evaluation/types.hpp>
#include <belief_evaluation/validation.hpp>

namespace dds::belief_evaluation
{

/// A LayoutSource over a fixed list of layouts in memory: the plain ordered
/// index space the LayoutSource contract describes, with no filtering or
/// observation logic.
class VectorLayoutSource : public LayoutSource
{
public:
    explicit VectorLayoutSource(std::vector<Deal> layouts) : layouts_(std::move(layouts))
    {
    }

    auto size() const -> std::optional<std::uint64_t> override
    {
        return layouts_.size();
    }

    auto at(std::uint64_t index) const -> Deal override
    {
        return layouts_.at(index);
    }

private:
    std::vector<Deal> layouts_;
};

/// A LayoutSource that wraps another and counts every at() call: the unit
/// scan_budget and the node-local scan cost are defined in (see
/// RootOptions::scan_budget). at() is const on the interface, so the
/// counter is mutable. Counting does not change what at() returns.
///
/// size() is counted separately. Showing that a caller never queried
/// size(), as opposed to querying it and making no at() calls, needs its
/// own counter. The wanted == 0 early return of scan_for_replenishment is
/// the case it exists for.
class CountingLayoutSource : public LayoutSource
{
public:
    explicit CountingLayoutSource(LayoutSource const& wrapped) : wrapped_(wrapped)
    {
    }

    auto size() const -> std::optional<std::uint64_t> override
    {
        ++size_calls_;
        return wrapped_.size();
    }

    auto at(std::uint64_t index) const -> Deal override
    {
        ++at_calls_;
        return wrapped_.at(index);
    }

    auto at_calls() const -> std::uint64_t
    {
        return at_calls_;
    }

    auto size_calls() const -> std::uint64_t
    {
        return size_calls_;
    }

private:
    LayoutSource const& wrapped_;
    mutable std::uint64_t at_calls_ = 0;
    mutable std::uint64_t size_calls_ = 0;
};

/// A LayoutSource that cannot report its size. Exhaustive evaluation has no
/// bound to enumerate without one, so this tests that rejection.
class UnboundedLayoutSource : public LayoutSource
{
public:
    auto size() const -> std::optional<std::uint64_t> override
    {
        return std::nullopt;
    }

    auto at(std::uint64_t /*index*/) const -> Deal override
    {
        return Deal{};  // never legitimately reached
    }
};

/// A DeclarerStrategy double that records every (ObservationState,
/// BeliefView) it is called with, and always returns the same scripted
/// card. It records the view's summary fields only: the span is not safe
/// to keep past the call.
class RecordingDeclarerStrategy
{
public:
    struct Call
    {
        ObservationState state;
        bool view_is_sample;
        std::size_t view_space_size;
        std::size_t view_entry_count;  ///< view.entries.size(); the span is not kept
    };

    explicit RecordingDeclarerStrategy(Card scripted_card) : scripted_card_(scripted_card)
    {
    }

    auto as_strategy() -> DeclarerStrategy
    {
        return DeclarerStrategy{
            .id = 0,
            .play =
                [this](ObservationState const& state, BeliefView const& view) -> Card
            {
                calls_.push_back(
                    Call{state, view.is_sample, view.space_size, view.entries.size()});
                return scripted_card_;
            },
            .state_key = nullptr,
        };
    }

    auto calls() const -> std::vector<Call> const&
    {
        return calls_;
    }

private:
    Card scripted_card_;
    std::vector<Call> calls_;
};

/// A scripted DefenderStrategy double: a table from (layout, position) to
/// one card with probability 1, or, through `stochastic()`, to a full
/// distribution for a defender with a real choice. `layout_key(deal, seat)`
/// is unique only among layouts sharing one node's pool, not across a test
/// tree, so the key also carries the play history, which tells apart any
/// two nodes that could collide.
///
/// A missing entry is a loud failure (ADD_FAILURE, non-fatal so the test
/// keeps running), not a fallback such as "play the lowest legal card". A
/// silent fallback would let an incomplete script pass against a different
/// defender than the one the expected values were derived for. On a miss
/// it returns an empty distribution, which is invalid and caught by the
/// distribution check if anything inspects it.
class ScriptedDefender
{
public:
    struct Key
    {
        std::uint64_t layout;
        std::string position;

        auto operator<(Key const& other) const -> bool
        {
            return layout != other.layout ? layout < other.layout : position < other.position;
        }
    };

    struct RecordedQuery
    {
        int seat;
        std::uint64_t layout;
        std::string position;
    };

    explicit ScriptedDefender(std::map<Key, Card> table)
    {
        for (auto const& [key, card] : table)
        {
            table_.emplace(key, std::vector<WeightedCard>{WeightedCard{card, 1.0}});
        }
    }

    /// The stochastic form: a table from (layout, position) to a full
    /// distribution, for a scripted defender with more than one reply. It uses
    /// the same lookup, recording and validation as the constructor above;
    /// only the table's value type differs.
    ///
    /// A named factory, not a second constructor. A constructor taking
    /// `std::map<Key, std::vector<WeightedCard>>` would make an empty table
    /// literal `{}` ambiguous against the constructor above. Making it private
    /// does not help: overload resolution picks the best candidate before
    /// access control, so an inaccessible equal match is still an ambiguity
    /// error. A private default constructor does not help either: it makes the
    /// implicit copy and move constructors viable for the same `{}` call, so
    /// the ambiguity comes back. So this builds through the one existing
    /// constructor, with an empty table, and then sets `table_` directly,
    /// which a member function may always do.
    static auto stochastic(std::map<Key, std::vector<WeightedCard>> table) -> ScriptedDefender
    {
        ScriptedDefender defender({});
        defender.table_ = std::move(table);
        return defender;
    }

    auto as_strategy() -> DefenderStrategy
    {
        return [this](DefenderQuery const& query) -> std::vector<WeightedCard>
        {
            Key const key{layout_key(query.layout, query.seat), position_string(query.state)};
            queries_.push_back(RecordedQuery{query.seat, key.layout, key.position});

            auto const entry = table_.find(key);
            if (entry == table_.end())
            {
                ADD_FAILURE() << "ScriptedDefender: no scripted entry for seat " << query.seat
                              << ", layout_key " << key.layout << ", position \"" << key.position
                              << "\"";
                return {};
            }

            std::vector<WeightedCard> const& distribution = entry->second;
            EXPECT_EQ(
                validate_defender_distribution(query.layout, query.seat, distribution),
                ValidationError::None)
                << "ScriptedDefender's table scripted an illegal defence for seat " << query.seat;
            return distribution;
        };
    }

    auto queries() const -> std::vector<RecordedQuery> const&
    {
        return queries_;
    }

private:
    static auto position_string(ObservationState const& state) -> std::string
    {
        std::string result;
        for (int i = 0; i < state.history.number; ++i)
        {
            result += std::to_string(state.history.suit[i]);
            result += ':';
            result += std::to_string(state.history.rank[i]);
            result += ',';
        }
        return result;
    }

    std::map<Key, std::vector<WeightedCard>> table_;
    std::vector<RecordedQuery> queries_;
};

/// The lowest card `seat` holds in `deal`: in the suit led to the trick in
/// progress if `seat` holds it, else in any suit. For fixtures where every
/// decision has exactly one legal card per suit, so no strategy chooses
/// between two legal cards.
inline auto lowest_legal_card(Deal const& deal, int seat) -> Card
{
    int led = -1;
    if (deal.currentTrickRank[0] != 0)
    {
        led = deal.currentTrickSuit[0];
    }
    if (led != -1 && deal.remainCards[seat][led] != 0)
    {
        for (int rank = 2; rank <= 14; ++rank)
        {
            if ((deal.remainCards[seat][led] & (1u << rank)) != 0)
            {
                return Card{led, rank};
            }
        }
    }
    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        unsigned const suit_holding = deal.remainCards[seat][suit];
        for (int rank = 2; rank <= 14; ++rank)
        {
            if ((suit_holding & (1u << rank)) != 0)
            {
                return Card{suit, rank};
            }
        }
    }
    return Card{};  // unreachable if the fixture has one legal card per suit
}

/// A DeclarerStrategy::play built on lowest_legal_card(). It finds its seat
/// with seat_on_play(), since pi is not told its seat any other way (see
/// DeclarerStrategy).
inline auto single_card_declarer_play(ObservationState const& state, BeliefView const&) -> Card
{
    return lowest_legal_card(state.known_holdings, seat_on_play(state.known_holdings));
}

/// A DefenderStrategy built on lowest_legal_card(), with certainty.
inline auto single_card_defender(DefenderQuery const& query) -> std::vector<WeightedCard>
{
    return {WeightedCard{lowest_legal_card(query.layout, query.seat), 1.0}};
}

/// A bitmask of `ranks` in Deal's bit convention (bit r for rank r), so
/// fixtures need no hand-computed hex literals.
inline auto holding(std::initializer_list<int> ranks) -> unsigned
{
    unsigned mask = 0;
    for (int rank : ranks)
    {
        mask |= 1u << rank;
    }
    return mask;
}

/// Cards `hand` holds in `deal`, summed across suits via std::popcount.
inline auto card_count(Deal const& deal, int hand) -> int
{
    int count = 0;
    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        count += std::popcount(deal.remainCards[hand][suit]);
    }
    return count;
}

/// Every hand in `deal` holds as many cards as hand 0. A hand that differs
/// is reported with a non-fatal ADD_FAILURE, which composes with gtest.
///
/// Only for a Deal at a trick boundary. Mid-trick, the hands that have
/// played hold one card fewer, which this does not allow for. Every
/// fixture in this module is at a trick boundary.
///
/// An unequal hand does not fail where it is built. Play runs normally
/// until the short hand empties; then seat rotation reaches a hand with
/// nothing legal, several frames deep, and the error points at whichever
/// callback was asked, not at the fixture.
inline auto assert_equal_hand_sizes(Deal const& deal) -> void
{
    int const expected = card_count(deal, 0);
    for (int hand = 1; hand < DDS_HANDS; ++hand)
    {
        int const actual = card_count(deal, hand);
        if (actual != expected)
        {
            ADD_FAILURE() << "assert_equal_hand_sizes: hand " << hand << " holds " << actual
                          << " cards, hand 0 holds " << expected;
        }
    }
}

/// The outstanding pool in `suit` for one layout: the union of every hand's
/// holding.
inline auto suit_pool(Deal const& deal, int suit) -> unsigned
{
    unsigned mask = 0;
    for (int hand = 0; hand < DDS_HANDS; ++hand)
    {
        mask |= deal.remainCards[hand][suit];
    }
    return mask;
}

/// Every layout in `layouts` has the same pool per suit as
/// `layouts.front()`. Reports the first suit and layout index that differ.
/// Layouts meant to form one node must agree on the pool, though they may
/// split it differently between the defenders. A fixture that gets this
/// wrong does not fail by itself: it silently becomes a node with fewer
/// layouts than intended.
inline auto assert_pool_matches(std::vector<Deal> const& layouts) -> void
{
    if (layouts.empty())
    {
        return;
    }
    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        unsigned const expected = suit_pool(layouts.front(), suit);
        for (std::size_t i = 1; i < layouts.size(); ++i)
        {
            unsigned const actual = suit_pool(layouts[i], suit);
            if (actual != expected)
            {
                ADD_FAILURE() << "assert_pool_matches: layout " << i << " suit " << suit
                              << " pool " << actual << " != layout 0's pool " << expected;
                return;  // naming the first difference is enough
            }
        }
    }
}

/// Whether `layouts` would survive `make_root` as one belief node for
/// `declarer`: the same trump, first and currentTrick*, the same declarer
/// and dummy holdings, and the same defender pool per suit. It mirrors
/// make_root's consistency filter, so a mistake fails when the fixture is
/// built, not later as a BeliefView with too few entries.
inline auto assert_forms_one_belief_node(std::vector<Deal> const& layouts, int declarer) -> void
{
    if (layouts.size() < 2)
    {
        return;
    }
    int const dummy = (declarer + 2) % DDS_HANDS;
    Deal const& root = layouts.front();

    auto const defender_pool = [&](Deal const& deal, int suit) -> unsigned
    {
        unsigned mask = 0;
        for (int hand = 0; hand < DDS_HANDS; ++hand)
        {
            if (hand != declarer && hand != dummy)
            {
                mask |= deal.remainCards[hand][suit];
            }
        }
        return mask;
    };

    for (std::size_t i = 1; i < layouts.size(); ++i)
    {
        Deal const& candidate = layouts[i];
        if (candidate.trump != root.trump)
        {
            ADD_FAILURE() << "assert_forms_one_belief_node: layout " << i << " trump "
                          << candidate.trump << " != layout 0's trump " << root.trump;
        }
        if (candidate.first != root.first)
        {
            ADD_FAILURE() << "assert_forms_one_belief_node: layout " << i << " first "
                          << candidate.first << " != layout 0's first " << root.first;
        }
        for (int t = 0; t < 3; ++t)
        {
            if (candidate.currentTrickSuit[t] != root.currentTrickSuit[t]
                || candidate.currentTrickRank[t] != root.currentTrickRank[t])
            {
                ADD_FAILURE() << "assert_forms_one_belief_node: layout " << i << " currentTrick["
                              << t << "] differs from layout 0's";
            }
        }
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            if (candidate.remainCards[declarer][suit] != root.remainCards[declarer][suit])
            {
                ADD_FAILURE() << "assert_forms_one_belief_node: layout " << i << " suit " << suit
                              << " declarer holding differs from layout 0's";
            }
            if (candidate.remainCards[dummy][suit] != root.remainCards[dummy][suit])
            {
                ADD_FAILURE() << "assert_forms_one_belief_node: layout " << i << " suit " << suit
                              << " dummy holding differs from layout 0's";
            }
            if (defender_pool(candidate, suit) != defender_pool(root, suit))
            {
                ADD_FAILURE() << "assert_forms_one_belief_node: layout " << i << " suit " << suit
                              << " defender pool differs from layout 0's";
            }
        }
    }
}

/// A scripted LayoutBound double: a table from a whole `Deal` to a claimed
/// bound, recording every layout it is asked about. Like ScriptedDefender,
/// but keyed on the whole Deal, not layout_key(): a LayoutBound
/// (`Deal -> int`) has no seat or position to key on. Equality is
/// field-by-field over trump, remainCards and the current-trick state,
/// which is everything make_root's consistency filter reads. A linear scan
/// is fine; no fixture scripts more than a handful of layouts.
///
/// A missing entry is a loud failure (ADD_FAILURE, non-fatal), not a
/// fallback, for the reason given for ScriptedDefender.
class ScriptedBound
{
public:
    explicit ScriptedBound(std::vector<std::pair<Deal, int>> table) : table_(std::move(table))
    {
    }

    auto as_bound() -> LayoutBound
    {
        return [this](Deal const& layout) -> int
        {
            queries_.push_back(layout);
            for (auto const& [scripted_layout, bound] : table_)
            {
                if (same_layout(scripted_layout, layout))
                {
                    return bound;
                }
            }
            ADD_FAILURE() << "ScriptedBound: no scripted entry for this layout";
            return 0;
        };
    }

    auto queries() const -> std::vector<Deal> const&
    {
        return queries_;
    }

private:
    static auto same_layout(Deal const& a, Deal const& b) -> bool
    {
        if (a.trump != b.trump)
        {
            return false;
        }
        for (int hand = 0; hand < DDS_HANDS; ++hand)
        {
            for (int suit = 0; suit < DDS_SUITS; ++suit)
            {
                if (a.remainCards[hand][suit] != b.remainCards[hand][suit])
                {
                    return false;
                }
            }
        }
        for (int i = 0; i < 3; ++i)
        {
            if (a.currentTrickSuit[i] != b.currentTrickSuit[i]
                || a.currentTrickRank[i] != b.currentTrickRank[i])
            {
                return false;
            }
        }
        return true;
    }

    std::vector<std::pair<Deal, int>> table_;
    std::vector<Deal> queries_;
};

/// A predicate-table LayoutBound double: pairs of a predicate over the
/// whole Deal and a claimed bound, tried in order, first match wins. For
/// fixtures over more than one ply, which ScriptedBound cannot serve:
/// tier2_dead() runs at every node, and each play produces a new Deal (see
/// cuts_test.cpp), so an exact-Deal table answers only the node it was
/// built for. The fixture author writes a predicate instead ("North still
/// holds two cards", "the club filler is still with East").
///
/// A predicate table, not a table keyed on cards remaining. A
/// cards-remaining table is one predicate here (a fixture in cuts_test.cpp
/// does this), but a bound that depends on something else, such as which
/// defender holds a filler, cannot be keyed on cards remaining.
///
/// It records every layout it is asked about and fails loudly (ADD_FAILURE,
/// non-fatal) when no predicate matches, as ScriptedBound does.
/// ScriptedBound remains the choice where the check happens at one node.
/// This serves the case it cannot.
class PredicateBound
{
public:
    explicit PredicateBound(std::vector<std::pair<std::function<bool(Deal const&)>, int>> table)
        : table_(std::move(table))
    {
    }

    auto as_bound() -> LayoutBound
    {
        return [this](Deal const& layout) -> int
        {
            queries_.push_back(layout);
            for (auto const& [predicate, bound] : table_)
            {
                if (predicate(layout))
                {
                    return bound;
                }
            }
            ADD_FAILURE() << "PredicateBound: no predicate matched this layout";
            return 0;
        };
    }

    auto queries() const -> std::vector<Deal> const&
    {
        return queries_;
    }

private:
    std::vector<std::pair<std::function<bool(Deal const&)>, int>> table_;
    std::vector<Deal> queries_;
};

}  // namespace dds::belief_evaluation
