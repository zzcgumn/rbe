#include <belief_evaluation/defender_split.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>

#include <api/dds_constants.hpp>

namespace dds::belief_evaluation
{

auto defender_pool_decomposition(Deal const& root, int declarer) -> DefenderPool
{
    // declarer and dummy are not read: the pool is the defenders' cards only.
    int const fixed_seat = (declarer + 1) % DDS_HANDS;
    int const other_seat = (declarer + 3) % DDS_HANDS;

    DefenderPool pool;
    // std::popcount of the fixed seat, not half the pool: mid-trick the two
    // defenders' counts differ by one.
    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        pool.fixed_seat_count += std::popcount(root.remainCards[fixed_seat][suit]);
    }

    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        unsigned const suit_pool = root.remainCards[fixed_seat][suit] | root.remainCards[other_seat][suit];
        // Ranks ascending, bit r for absolute rank r (the Deal convention, no
        // >> 2).
        for (int rank = 2; rank <= 14; ++rank)
        {
            if ((suit_pool & (1u << rank)) != 0)
            {
                pool.cards.push_back(Card{suit, rank});
            }
        }
    }
    return pool;
}

namespace
{
    // At most 26 cards are outstanding between two defenders.
    constexpr int MaxOutstandingCards = 26;

    constexpr auto make_pascals_triangle()
    {
        std::array<std::array<std::uint64_t, MaxOutstandingCards + 1>, MaxOutstandingCards + 1> table{};
        for (int n = 0; n <= MaxOutstandingCards; ++n)
        {
            table[n][0] = 1;
            for (int k = 1; k <= n; ++k)
            {
                // When k == n, table[n - 1][k] is still 0 from initialisation, which is
                // C(n - 1, n). No bounds check is needed.
                table[n][k] = table[n - 1][k - 1] + table[n - 1][k];
            }
        }
        return table;
    }

    constexpr auto PascalsTriangle = make_pascals_triangle();
}  // namespace

auto binomial_coefficient(int n, int k) -> std::uint64_t
{
    // n outside [0, MaxOutstandingCards] is a caller error and would read past
    // the table. Asserted; the range check below stops the read when asserts
    // are off.
    assert(n >= 0 && n <= MaxOutstandingCards);
    if (n < 0 || n > MaxOutstandingCards || k < 0 || k > n)
    {
        return 0;
    }
    return PascalsTriangle[static_cast<std::size_t>(n)][static_cast<std::size_t>(k)];
}

auto unrank_combination(std::uint64_t index, int n, int k) -> std::vector<int>
{
    // Colex unranking: for each position i from k down to 1, take the largest
    // v with C(v, i) <= index and subtract. Builds descending, then reverses.
    std::vector<int> result;
    result.reserve(static_cast<std::size_t>(k));
    int v = n - 1;
    for (int i = k; i >= 1; --i)
    {
        while (binomial_coefficient(v, i) > index)
        {
            --v;
        }
        result.push_back(v);
        index -= binomial_coefficient(v, i);
        --v;
    }
    std::reverse(result.begin(), result.end());
    return result;
}

auto apply_defender_split(
    Deal const& root, int declarer, DefenderPool const& pool, std::vector<int> const& fixed_seat_cards) -> Deal
{
    int const fixed_seat = (declarer + 1) % DDS_HANDS;
    int const other_seat = (declarer + 3) % DDS_HANDS;

    Deal result = root;  // copy-and-overwrite: only the defenders' holdings change
    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        result.remainCards[fixed_seat][suit] = 0;
        result.remainCards[other_seat][suit] = 0;
    }

    // The complement comes from pool, not from root (see the header). A
    // fixed-size array, not std::vector<bool>: this is the hot path of
    // ExhaustiveLayoutSource::at(), and the pool is at most 26 cards.
    assert(pool.cards.size() <= MaxOutstandingCards);
    std::array<bool, MaxOutstandingCards> is_fixed_seat_card{};
    for (int index : fixed_seat_cards)
    {
        // A bad index is a caller error. Asserted; the range check below stops an
        // out-of-bounds write when asserts are off.
        assert(index >= 0 && static_cast<std::size_t>(index) < pool.cards.size());
        if (index < 0 || static_cast<std::size_t>(index) >= pool.cards.size())
        {
            continue;
        }
        is_fixed_seat_card[static_cast<std::size_t>(index)] = true;
    }
    for (std::size_t i = 0; i < pool.cards.size(); ++i)
    {
        Card const& card = pool.cards[i];
        int const seat = is_fixed_seat_card[i] ? fixed_seat : other_seat;
        result.remainCards[seat][card.suit] |= (1u << card.rank);
    }
    return result;
}

}  // namespace dds::belief_evaluation
