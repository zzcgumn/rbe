#pragma once

// Shared test doubles and fixture-building helpers for the exhaustive
// evaluator (plan 2). Kept in one header because tasks 02, 06, 07, 08 and 09
// all need the same LayoutSource double and card-holding helpers.

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
#include <belief_evaluation/types.hpp>
#include <belief_evaluation/validation.hpp>

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
                calls_.push_back(Call{state, view.is_sample, view.space_size});
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
