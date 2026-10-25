#pragma once

#include <cstdint>
#include <vector>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/history_verification.hpp>

// Roots of known, increasing belief-space size for the benchmarks in this
// directory. fixtures.cpp derives each rung's expected_size, and
// fixtures_test.cpp pins it.
namespace dds::belief_evaluation::benchmarks
{

/// One position: a root, its declarer (dummy is always
/// `(declarer + 2) % DDS_HANDS`), and the tricks declarer needs. `history`
/// (empty for the unconstrained form) and `opening_leader` are what an
/// `ExhaustiveLayoutSource` needs at this root. `expected_size` is the
/// fixture's `size()`, derived by hand and pinned by fixtures_test.cpp.
struct RungFixture
{
    Deal root{};
    int declarer = 0;
    int tricks_needed = 0;
    PlayTraceBin history{};
    int opening_leader = 0;
    std::uint64_t expected_size = 0;
};

/// One rung in both forms, sharing one `Deal`:
/// - `without_history`: an empty `PlayTraceBin`, so no void constraint;
/// - `with_history`: the same root, with a history whose void narrows the
///   space.
///
/// The forms differ only in `history`, `opening_leader` and
/// `expected_size`. So comparing them compares the history parameter alone,
/// not two positions.
struct Rung
{
    char const* name = nullptr;
    RungFixture without_history;
    RungFixture with_history;
};

// --- The pool ladder: five rungs of increasing unconstrained size,
// C(2k, k) for k = 4..8: 70, 252, 924, 3,432, 12,870. The point is the
// defender pool's combinatorics. Declarer holds a whole suit as harmless
// filler, as in exhaustive_layout_source_test.cpp. See fixtures.cpp. ---

auto make_pool4_rung() -> Rung;  // C(8, 4)   = 70
auto make_pool5_rung() -> Rung;  // C(10, 5)  = 252
auto make_pool6_rung() -> Rung;  // C(12, 6)  = 924
auto make_pool7_rung() -> Rung;  // C(14, 7)  = 3,432
auto make_pool8_rung() -> Rung;  // C(16, 8)  = 12,870

// --- Two positions a bridge player would recognise. They use the pool
// ladder's void mechanism, but declarer and dummy hold believable
// multi-suit hands, not one whole suit each. The pools are an order of
// magnitude or more above a four-card ending. See fixtures.cpp. ---

auto make_realistic_rung_a() -> Rung;  // pool k=6: C(12, 6)=924 -> C(10,6)=210
auto make_realistic_rung_b() -> Rung;  // pool k=8: C(16, 8)=12,870 -> C(14,8)=3,003

/// Every rung above, in ascending unconstrained N. fixtures_test.cpp and
/// every instrument here iterate this, so a new rung needs no second
/// edit.
auto all_rungs() -> std::vector<Rung>;

// --- The finesse ladder: real per-layout uncertainty. Every pool and
// realistic rung makes with p_make exactly 1 in every layout, because
// declarer holds a whole suit. That leaves a convergence study nothing to
// converge on.
//
// Each rung is close to the two-card finesse of
// exhaustive_layout_source_integration_test.cpp: declarer Nine, dummy
// Eight, and the fixed seat's single card against a five-card pool that
// includes the Ten. It is repeated in 1..DDS_SUITS independent suits, and
// declarer needs every trick. The pool is C(6s, s) for s = 1..4: 6, 66,
// 816, 10,626.
//
// There is no history form, since these fixtures model no void. The
// instrument returns the same form for `--history with`. See
// fixtures.cpp. ---

auto make_finesse_rung(int suits) -> RungFixture;  // suits in 1..DDS_SUITS

/// make_finesse_rung(1..DDS_SUITS), ascending N: 6, 66, 816, 10,626.
auto all_finesse_rungs() -> std::vector<RungFixture>;

// --- The solver ladder: positions solve_board() accepts. Every rung above
// gives some hand an unequal card count (the pool ladder's whole declarer
// suit, the finesse ladder's 1-against-5 split). solve_board rejects that:
// DoubleDummyDefender then fails with ValidationError::DistributionEmpty.
//
// Two traps shape these rungs (see fixtures.cpp):
// - all four hands must hold the same number of cards;
// - defender_pool_decomposition pools every suit either defender holds,
//   not only the contested one. So East and West hold only Spades.
//
// East and West hold `pool_size / 2` Spades each, the only uncertain suit.
// North and South carry `pool_size / 2 - 1` Diamonds each to equalise card
// counts; those never enter the pool. tricks_needed = pool_size / 2 is
// every remaining trick, so a tier-1 dead cut fires as soon as the Spade
// finesse loses. ---

auto make_solver_rung(int pool_size) -> RungFixture;  // pool_size in {4, 6}, even

auto make_solver_rung_a() -> RungFixture;  // pool_size=4: C(4,2)=6
auto make_solver_rung_b() -> RungFixture;  // pool_size=6: C(6,3)=20

/// Exhaustive evaluation cost, measured 2026-09-15: scripted single-card
/// strategies (fixtures_test.cpp, `exhaustively_evaluate`), fastbuild,
/// this platform. Every rung, both forms, completes in under 50ms. pool8
/// without history (12,870 layouts), the largest, took 49ms. pool6 with
/// history (210 layouts) took under 1ms.
///
/// **This is not the ceiling of exhaustive evaluation.** With
/// `tricks_needed = 1` every tree here is one trick deep, so cost grows
/// roughly linearly with `size()`. The real limit depends on tree depth and
/// branching: more tricks needed, and strategies that vary by layout.

}  // namespace dds::belief_evaluation::benchmarks
