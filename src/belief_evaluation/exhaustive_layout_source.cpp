#include <belief_evaluation/exhaustive_layout_source.hpp>

#include <cassert>
#include <cstddef>
#include <vector>

#include <belief_evaluation/keyed_permutation.hpp>
#include <belief_evaluation/void_derivation.hpp>

namespace dds::belief_evaluation
{

namespace
{
    // At most 26 cards are outstanding between two defenders. A private copy
    // of the bound in defender_split.cpp.
    constexpr int MaxOutstandingCards = 26;

    /// Builds the `Deal` for one layout: `root` with the two defenders'
    /// `remainCards` replaced by the forced cards of `decomposition`, plus a
    /// split of the free cards. `free_subset` holds indices into
    /// `decomposition.free_cards` for the fixed seat; the rest go to the
    /// other seat. Copy-and-overwrite, as in `apply_defender_split`, so every
    /// other field equals `root`.
    ///
    /// Writes straight from `decomposition` and a fixed-size `is_chosen`
    /// array, with no heap allocation: this is the hot path of `at()`. The
    /// no-history case runs through here too.
    auto build_layout(
        Deal const& root,
        int fixed_seat,
        int other_seat,
        ConstrainedDecomposition const& decomposition,
        std::vector<int> const& free_subset) -> Deal
    {
        assert(decomposition.free_cards.size() <= MaxOutstandingCards);
        std::array<bool, MaxOutstandingCards> is_chosen{};
        for (int i : free_subset)
        {
            is_chosen[static_cast<std::size_t>(i)] = true;
        }

        Deal result = root;
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            result.remainCards[fixed_seat][suit] = 0;
            result.remainCards[other_seat][suit] = 0;
        }
        for (Card const& card : decomposition.forced_to_fixed_seat)
        {
            result.remainCards[fixed_seat][card.suit] |= (1u << card.rank);
        }
        for (Card const& card : decomposition.forced_to_other_seat)
        {
            result.remainCards[other_seat][card.suit] |= (1u << card.rank);
        }
        for (std::size_t i = 0; i < decomposition.free_cards.size(); ++i)
        {
            Card const& card = decomposition.free_cards[i];
            int const seat = is_chosen[i] ? fixed_seat : other_seat;
            result.remainCards[seat][card.suit] |= (1u << card.rank);
        }
        return result;
    }
}

ExhaustiveLayoutSource::ExhaustiveLayoutSource(
    Deal const& root, int declarer, std::uint64_t seed, std::optional<PlayRecord> const& record)
    : root_(root)
    , seed_(seed)
    , fixed_seat_((declarer + 1) % DDS_HANDS)
    , other_seat_((declarer + 3) % DDS_HANDS)
    , pool_(defender_pool_decomposition(root, declarer))
{
    // No record and an empty record behave the same: no constraint, and no
    // check against root.
    PlayTraceBin const history = record.has_value() ? record->cards() : PlayTraceBin{};
    int const opening_leader = record.has_value() ? record->opening_leader() : 0;

    verdict_ =
        history.number == 0 ? HistoryVerdict::Consistent : verify_history(root, declarer, history, opening_leader);

    if (verdict_ == HistoryVerdict::Consistent)
    {
        // With an empty history, derive_voids finds no voids. Then nothing is
        // forced, free_cards is the whole pool, and fixed_seat_needed is
        // pool_.fixed_seat_count.
        VoidsBySeat const voids = derive_voids(history, opening_leader, root.trump);
        decomposition_ = decompose_constrained(pool_, voids[fixed_seat_], voids[other_seat_]);
    }
    // else: decomposition_ stays default (status Ok, lists empty). That status
    // means nothing here: callers check history_verdict() first.
}

auto ExhaustiveLayoutSource::size() const -> std::optional<std::uint64_t>
{
    if (verdict_ != HistoryVerdict::Consistent)
    {
        // Not constrained_space_size(decomposition_), which is the default here
        // and would give C(0, 0) == 1. This branch is a rejected history: the
        // space is empty.
        return std::uint64_t{0};
    }
    return constrained_space_size(decomposition_);
}

auto ExhaustiveLayoutSource::at(std::uint64_t index) const -> Deal
{
    std::uint64_t const total = *size();
    assert(index < total);

    // Permute the index, then unrank. Permuting the output instead would mean
    // storing it. This keeps memory O(1).
    std::uint64_t const permuted_index = keyed_permutation(index, total, seed_);
    std::vector<int> const free_subset = unrank_combination(
        permuted_index, static_cast<int>(decomposition_.free_cards.size()), decomposition_.fixed_seat_needed);

    return build_layout(root_, fixed_seat_, other_seat_, decomposition_, free_subset);
}

auto ExhaustiveLayoutSource::history_verdict() const -> HistoryVerdict
{
    return verdict_;
}

auto ExhaustiveLayoutSource::constrained_space_status() const -> ConstrainedSpaceStatus
{
    return decomposition_.status;
}

}  // namespace dds::belief_evaluation
