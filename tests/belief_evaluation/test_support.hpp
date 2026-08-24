#pragma once

// Shared test doubles and fixture-building helpers for the exhaustive
// evaluator (plan 2). Kept in one header because tasks 02, 06, 07, 08 and 09
// all need the same LayoutSource double and card-holding helpers.

#include <cstdint>
#include <optional>
#include <vector>

#include <api/dll.h>

#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/layout_source.hpp>
#include <belief_evaluation/types.hpp>

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
