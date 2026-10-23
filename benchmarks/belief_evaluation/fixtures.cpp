#include "fixtures.hpp"

#include <algorithm>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

namespace dds::belief_evaluation::benchmarks
{

namespace
{
    constexpr int Spades = 0;
    constexpr int Hearts = 1;
    constexpr int Diamonds = 2;
    constexpr int Clubs = 3;

    constexpr int North = 0;  // declarer throughout this file
    constexpr int East = 1;   // the fixed seat: (declarer + 1) % DDS_HANDS
    constexpr int South = 2;  // dummy
    constexpr int West = 3;

    using CardPair = std::pair<int, int>;  // (suit, rank)

    /// `C(n, k)`, a small local copy. defender_split.hpp's
    /// binomial_coefficient is private to the core library and not exported.
    /// Copying it for the small arguments used here is simpler than exporting
    /// it.
    auto choose(int n, int k) -> std::uint64_t
    {
        if (k < 0 || k > n)
        {
            return 0;
        }
        std::uint64_t result = 1;
        for (int i = 0; i < k; ++i)
        {
            result = result * static_cast<std::uint64_t>(n - i) / static_cast<std::uint64_t>(i + 1);
        }
        return result;
    }

    /// The first `count` cards of Clubs, then Spades, each ascending. This is
    /// the free pool the two defenders split once the voided suit (Diamonds)
    /// is set aside. Two suits, so `count` can exceed 13 (k=8 needs
    /// 2k-2 = 14).
    auto free_pool_cards(int count) -> std::vector<CardPair>
    {
        std::vector<CardPair> cards;
        for (int const suit : {Clubs, Spades})
        {
            for (int rank = 2; rank <= 14 && static_cast<int>(cards.size()) < count; ++rank)
            {
                cards.emplace_back(suit, rank);
            }
        }
        return cards;
    }

    /// Builds one rung, scaled up from exhaustive_layout_source_test.cpp's
    /// make_void_ending. A completed trick shows East void in Diamonds. Two
    /// Diamonds stay outstanding, and the void forces both to West. The free
    /// pool has `2k - 2` Clubs and Spades: East needs all `k` of its cards from
    /// it, and West needs `k - 2` more. Without a history the space is
    /// `C(2k, k)`; with it, `C(2k - 2, k)`.
    ///
    /// `realistic`:
    /// - false gives declarer (North) every leftover card as filler. That is
    ///   unrealistic but harmless, as in exhaustive_layout_source_test.cpp's
    ///   make_ten_card_pool_root. These fixtures are about the defender pool.
    /// - true (make_realistic_rung_a and _b) alternates leftover cards between
    ///   North and South, so both get a believable multi-suit hand.
    auto make_rung(char const* name, int k, bool realistic) -> Rung
    {
        int const c = 2 * k - 2;  // size of the free pool
        std::vector<CardPair> const free_cards = free_pool_cards(c);
        std::vector<CardPair> const east_cards(free_cards.begin(), free_cards.begin() + k);
        std::vector<CardPair> const west_free_cards(free_cards.begin() + k, free_cards.end());

        auto const contains = [](std::vector<CardPair> const& cards, int suit, int rank) -> bool
        {
            return std::find(cards.begin(), cards.end(), CardPair{suit, rank}) != cards.end();
        };

        // Trick 1: North leads the Diamond Ace and wins it. East discards,
        // showing a Diamond void; South and West follow. The four cards are
        // seated North, East, South, West, as `opening_leader = North` replays.
        // After the trick North leads again and no trick is in progress.
        std::vector<CardPair> const played = {{Diamonds, 14}, {Hearts, 10}, {Diamonds, 13}, {Diamonds, 12}};
        std::set<CardPair> const played_set(played.begin(), played.end());

        int leftover_to_north = 0;  // only consulted when realistic is true

        auto const hand_for = [&](int suit, int rank) -> int
        {
            if (suit == Diamonds && (rank == 2 || rank == 3))
            {
                return West;  // the two Diamonds the void forces
            }
            if (suit == Hearts && (rank == 14 || rank == 13))
            {
                return North;
            }
            if (suit == Hearts && (rank == 12 || rank == 11))
            {
                return South;
            }
            if (contains(east_cards, suit, rank))
            {
                return East;
            }
            if (contains(west_free_cards, suit, rank))
            {
                return West;
            }
            // Everything else is filler for declarer and dummy. The fixture's
            // combinatorics do not depend on it.
            if (! realistic)
            {
                return North;
            }
            leftover_to_north = ! leftover_to_north;
            return leftover_to_north != 0 ? North : South;
        };

        Deal root{};
        root.trump = DDS_NOTRUMP;
        root.first = North;
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            for (int rank = 2; rank <= 14; ++rank)
            {
                if (played_set.count({suit, rank}) != 0)
                {
                    continue;
                }
                root.remainCards[hand_for(suit, rank)][suit] |= (1u << rank);
            }
        }

        PlayTraceBin history{};
        history.number = static_cast<int>(played.size());
        for (std::size_t i = 0; i < played.size(); ++i)
        {
            history.suit[i] = played[i].first;
            history.rank[i] = played[i].second;
        }

        Rung rung{};
        rung.name = name;
        rung.without_history = RungFixture{
            .root = root,
            .declarer = North,
            .tricks_needed = 1,
            .history = PlayTraceBin{},
            .opening_leader = North,
            .expected_size = choose(2 * k, k),
        };
        rung.with_history = RungFixture{
            .root = root,
            .declarer = North,
            .tricks_needed = 1,
            .history = history,
            .opening_leader = North,
            .expected_size = choose(c, k),
        };
        return rung;
    }
}  // namespace

auto make_pool4_rung() -> Rung
{
    return make_rung("pool4", 4, /*realistic=*/false);
}

auto make_pool5_rung() -> Rung
{
    return make_rung("pool5", 5, /*realistic=*/false);
}

auto make_pool6_rung() -> Rung
{
    return make_rung("pool6", 6, /*realistic=*/false);
}

auto make_pool7_rung() -> Rung
{
    return make_rung("pool7", 7, /*realistic=*/false);
}

auto make_pool8_rung() -> Rung
{
    return make_rung("pool8", 8, /*realistic=*/false);
}

auto make_realistic_rung_a() -> Rung
{
    return make_rung("realistic_a", 6, /*realistic=*/true);
}

auto make_realistic_rung_b() -> Rung
{
    return make_rung("realistic_b", 8, /*realistic=*/true);
}

auto all_rungs() -> std::vector<Rung>
{
    return {
        make_pool4_rung(),
        make_pool5_rung(),
        make_pool6_rung(),
        make_pool7_rung(),
        make_pool8_rung(),
        make_realistic_rung_a(),
        make_realistic_rung_b(),
    };
}

// --- the finesse ladder: genuine per-layout uncertainty ------------------

auto make_finesse_rung(int suits) -> RungFixture
{
    // Close to make_two_card_finesse_root of
    // exhaustive_layout_source_integration_test.cpp, in each of `suits`
    // suits: North (declarer) holds the Nine, South (dummy) the Eight, and
    // East and West split a pool. That fixture's pool is {2..7, 10}, 7 cards.
    // This one drops the Seven ({2..6, 10}, 6 cards) so that four suits fit
    // binomial_coefficient's domain: 24 outstanding cards is under the
    // 26-card ceiling, and 28 would not be. The root's East/West split per
    // suit only sets the counts ExhaustiveLayoutSource reads
    // (fixed_seat_count, by popcount). The enumeration still varies which
    // pool cards East holds.
    Deal root{};
    root.trump = DDS_NOTRUMP;
    root.first = East;  // a defender leads, as the fixture this mirrors does
    for (int suit = 0; suit < suits; ++suit)
    {
        root.remainCards[North][suit] |= (1u << 9);
        root.remainCards[South][suit] |= (1u << 8);
        root.remainCards[East][suit] |= (1u << 2);
        for (int const rank : {3, 4, 5, 6, 10})
        {
            root.remainCards[West][suit] |= (1u << rank);
        }
    }

    RungFixture fixture{};
    fixture.root = root;
    fixture.declarer = North;
    fixture.tricks_needed = suits;  // one trick per suit, all of them needed
    fixture.history = PlayTraceBin{};
    fixture.opening_leader = East;
    fixture.expected_size = choose(6 * suits, suits);
    return fixture;
}

auto all_finesse_rungs() -> std::vector<RungFixture>
{
    std::vector<RungFixture> rungs;
    for (int suits = 1; suits <= DDS_SUITS; ++suits)
    {
        rungs.push_back(make_finesse_rung(suits));
    }
    return rungs;
}

// --- the solver rung: a position solve_board() actually accepts --------

namespace
{
    /// Spade pool ranks for make_solver_rung(pool_size). Chosen by hand so the
    /// pool always has ranks both below and above North's Nine. A pool all
    /// below it would let the Nine win every layout: the degenerate
    /// p_make == 1 case the finesse ladder exists to avoid.
    auto solver_rung_pool_ranks(int pool_size) -> std::vector<int>
    {
        if (pool_size == 4)
        {
            return {2, 3, 10, 11};
        }
        if (pool_size == 6)
        {
            return {2, 3, 4, 10, 11, 12};
        }
        return {};  // caller error; the two wrappers never pass anything else
    }
}  // namespace

auto make_solver_rung(int pool_size) -> RungFixture
{
    // solve_board rejects a deal unless all four hands hold the same number of
    // cards (the three rules in table_deal_validate.hpp, which solve_board
    // also enforces). Two earlier versions failed:
    //
    //   1. East and West held `half` Spades plus one card each in Hearts,
    //      Diamonds and Clubs. North and South held one Spade plus the same
    //      three suits. The counts were unequal (RETURN_CARD_COUNT).
    //   2. With extra Diamonds for North and South to equalise the counts,
    //      ExhaustiveLayoutSource::size() was 252, not C(pool_size, half).
    //      defender_pool_decomposition pools every suit East or West holds,
    //      so their "fixed" Hearts and Clubs were enumerated too.
    //
    // The fix for both: East and West hold only Spades. Every other card,
    // including the ones that equalise the counts, goes to North and South.
    // They are declarer's side and never enter the pool.
    std::vector<int> const pool_ranks = solver_rung_pool_ranks(pool_size);
    int const half = pool_size / 2;

    Deal root{};
    root.trump = DDS_NOTRUMP;
    root.first = East;
    root.remainCards[North][Spades] |= (1u << 9);
    root.remainCards[South][Spades] |= (1u << 8);
    for (int i = 0; i < half; ++i)
    {
        root.remainCards[East][Spades] |= (1u << pool_ranks[i]);
    }
    for (int i = half; i < pool_size; ++i)
    {
        root.remainCards[West][Spades] |= (1u << pool_ranks[i]);
    }

    // Diamonds go to North and South only: `half - 1` each, the top
    // `2 * (half - 1)` ranks split between them. They equalise card counts and
    // keep the defenders' pool to Spades. North's top Diamonds win in every
    // layout, so the uncertainty is in Spades alone.
    int const extra = half - 1;
    for (int i = 0; i < extra; ++i)
    {
        root.remainCards[North][Diamonds] |= (1u << (14 - i));
    }
    for (int i = 0; i < extra; ++i)
    {
        root.remainCards[South][Diamonds] |= (1u << (14 - extra - i));
    }

    RungFixture fixture{};
    fixture.root = root;
    fixture.declarer = North;
    fixture.tricks_needed = half;  // every remaining trick
    fixture.history = PlayTraceBin{};
    fixture.opening_leader = East;
    fixture.expected_size = choose(pool_size, half);
    return fixture;
}

auto make_solver_rung_a() -> RungFixture
{
    return make_solver_rung(4);
}

auto make_solver_rung_b() -> RungFixture
{
    return make_solver_rung(6);
}

}  // namespace dds::belief_evaluation::benchmarks
