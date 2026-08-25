#pragma once

// Shared test doubles and fixture-building helpers for the exhaustive
// evaluator's test suite. Kept in one header since node, declarer-node,
// defender-node, BeliefView, evaluate() and oracle tests all need the same
// LayoutSource double and card-holding helpers.

#include <bit>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <api/dll.h>

#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/layout_key.hpp>
#include <belief_evaluation/layout_source.hpp>
#include <belief_evaluation/trick.hpp>
#include <belief_evaluation/types.hpp>
#include <belief_evaluation/validation.hpp>
#include <utility/constants.h>

/// A LayoutSource over a fixed, in-memory list of layouts — the "dumb
/// ordered index space" the production LayoutSource contract describes,
/// with no filtering or observation logic of its own.
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

/// A LayoutSource that cannot report its size — exhaustive evaluation has no
/// bound to enumerate without one, so this exists purely to test that
/// rejection.
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
/// BeliefView) it is called with — the view only by its non-owned summary
/// fields, since the span it carries is not safe to retain past the call —
/// and always returns the same scripted card, regardless of input.
class RecordingDeclarerStrategy
{
public:
    struct Call
    {
        ObservationState state;
        bool view_is_sample;
        std::size_t view_space_size;
        std::size_t view_entry_count;  ///< view.entries.size(); the span itself is not retained
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

/// A deterministic, scripted DefenderStrategy double: a table from
/// (layout, position) to a single card, always returned with probability 1.
/// `layout_key(deal, seat)` is exact only within one node — unique among
/// layouts sharing a node's outstanding pool, not across a whole test tree
/// — so the key also carries the play history, which disambiguates any two
/// nodes that could otherwise collide.
///
/// A missing table entry is a loud test failure (ADD_FAILURE, non-fatal so
/// the test keeps running) rather than a fallback such as "play the lowest
/// legal card": a silent fallback would turn an incomplete script into a
/// passing test against a different defender than the one the expected
/// values were hand-derived from. On a miss, an empty distribution is
/// returned — obviously invalid, and caught by the distribution check below
/// if anything downstream inspects it.
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

    explicit ScriptedDefender(std::map<Key, Card> table) : table_(std::move(table))
    {
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

            std::vector<WeightedCard> const distribution{WeightedCard{entry->second, 1.0}};
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

    std::map<Key, Card> table_;
    std::vector<RecordedQuery> queries_;
};

/// The lowest card `seat` holds in `deal` — following the suit led to the
/// trick in progress if `seat` holds it, else any held suit — for fixtures
/// built so that every decision point has exactly one legal card *per
/// suit*, so no strategy actually has to choose between two cards it could
/// legally play.
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
    return Card{};  // unreachable if the fixture holds its "one legal card per suit" promise
}

/// A DeclarerStrategy::play built on lowest_legal_card(). Finds the seat
/// via seat_on_play(), per DeclarerStrategy's own doxygen: pi is not told
/// its seat any other way.
inline auto single_card_declarer_play(ObservationState const& state, BeliefView const&) -> Card
{
    return lowest_legal_card(state.known_holdings, seat_on_play(state.known_holdings));
}

/// A DefenderStrategy built on lowest_legal_card(), with certainty.
inline auto single_card_defender(DefenderQuery const& query) -> std::vector<WeightedCard>
{
    return {WeightedCard{lowest_legal_card(query.layout, query.seat), 1.0}};
}

/// A bitmask of `ranks` in Deal's own bit convention (bit r for absolute
/// rank r), for building fixture holdings without hand-computed hex
/// literals at every call site.
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

/// Every hand in `deal` holds the same number of cards, compared against
/// hand 0 and reported (via non-fatal ADD_FAILURE, so it composes with
/// gtest rather than throwing) by whichever hand disagrees.
///
/// Applies only to a Deal at a trick boundary (no cards currently in
/// progress): a mid-trick Deal legitimately has one fewer card in whichever
/// hand(s) have already played to the trick in progress, and this helper
/// does not account for that -- every fixture this module builds is at a
/// trick boundary, so the simpler form covers what is actually needed.
///
/// Exists because an unequal hand size does not fail where it is built: play
/// proceeds normally until the short hand empties, and only then does seat
/// rotation land on a hand with nothing legal, several frames into the
/// recursion and pointing at whichever callback happened to be asked rather
/// than at the fixture that caused it.
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

/// The outstanding pool in `suit` -- the union of every hand's holding --
/// for one layout.
inline auto suit_pool(Deal const& deal, int suit) -> unsigned
{
    unsigned mask = 0;
    for (int hand = 0; hand < DDS_HANDS; ++hand)
    {
        mask |= deal.remainCards[hand][suit];
    }
    return mask;
}

/// Every layout in `layouts` shares the same outstanding pool per suit as
/// `layouts.front()`, reporting the first suit and layout index that
/// disagrees. A set of layouts meant to form one belief node must agree on
/// what the pool *is*, even though they may differ in how it splits between
/// the two defenders -- a fixture that gets this wrong does not error on
/// its own, it silently survives as a node with fewer layouts than the test
/// author intended.
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
                return;  // first disagreement named is enough; more would just add noise
            }
        }
    }
}

/// Whether `layouts` would survive `make_root` as a single belief node for
/// `declarer`: same trump, first, currentTrick*, the same declarer and
/// dummy holdings exactly, and the same defender pool per suit -- mirroring
/// make_root's own consistency filter, but asserted at fixture-build time
/// rather than discovered later as a BeliefView with fewer entries than
/// expected.
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
