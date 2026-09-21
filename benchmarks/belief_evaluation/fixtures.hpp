#pragma once

#include <cstdint>
#include <vector>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/history_verification.hpp>

// Roots of known, increasing belief-space size, for the benchmarks under
// this directory to measure on. See fixtures.cpp for the combinatorics
// behind each rung's expected_size and fixtures_test.cpp for the guard
// that pins it.
namespace dds::belief_evaluation::benchmarks
{

/// One position: a root, the declarer it is built for (dummy is always
/// `(declarer + 2) % DDS_HANDS`), how many tricks declarer needs, and
/// everything an `ExhaustiveLayoutSource` needs to enumerate the space at
/// this exact root -- `history` (empty for the unconstrained form) and the
/// `opening_leader` it is played from. `expected_size` is this exact
/// fixture's own `size()`, hand-derived and pinned by fixtures_test.cpp.
struct RungFixture
{
    Deal root{};
    int declarer = 0;
    int tricks_needed = 0;
    PlayTraceBin history{};
    int opening_leader = 0;
    std::uint64_t expected_size = 0;
};

/// A rung of the ladder in both of its forms, sharing one underlying
/// `Deal`: `without_history` (the default, empty `PlayTraceBin` -- no void
/// constraint applied) and `with_history` (the same root, with a history
/// that narrows the space by the void it implies). The two differ *only*
/// in `history`/`opening_leader`/`expected_size` -- same root, same
/// declarer, same tricks_needed -- so a comparison between them is a
/// comparison of the history parameter alone, not of two different
/// positions -- not a separately-constructed "equivalent" root.
struct Rung
{
    char const* name = nullptr;
    RungFixture without_history;
    RungFixture with_history;
};

// --- the pool ladder: five rungs of increasing unconstrained size,
// C(2k, k) for k = 4..8 -- 70, 252, 924, 3,432, 12,870. Declarer holds an
// entire suit as filler (so is not itself the point of these fixtures: the
// point is the defender pool's own combinatorics), matching the existing
// exhaustive_layout_source_test.cpp fixtures' own precedent of an
// unrealistically large, harmless declarer holding. See fixtures.cpp. ---

auto make_pool4_rung() -> Rung;  // C(8, 4)   = 70
auto make_pool5_rung() -> Rung;  // C(10, 5)  = 252
auto make_pool6_rung() -> Rung;  // C(12, 6)  = 924
auto make_pool7_rung() -> Rung;  // C(14, 7)  = 3,432
auto make_pool8_rung() -> Rung;  // C(16, 8)  = 12,870

// --- two positions a bridge player would recognise: the same void
// mechanism as the pool ladder, but declarer and dummy hold a believable
// multi-suit spread rather than one whole suit each, at pool sizes an
// order of magnitude and more above a four-card ending. See
// fixtures.cpp. ---

auto make_realistic_rung_a() -> Rung;  // pool k=6: C(12, 6)=924 -> C(10,6)=210
auto make_realistic_rung_b() -> Rung;  // pool k=8: C(16, 8)=12,870 -> C(14,8)=3,003

/// Every rung above, in ascending unconstrained-N order -- what
/// fixtures_test.cpp and every instrument under this directory iterate,
/// so a rung added here is picked up everywhere without a second edit.
auto all_rungs() -> std::vector<Rung>;

// --- a second, separate ladder: genuine per-layout uncertainty, not the
// pool ladder's own combinatorial-size focus. Every pool/realistic rung
// above makes with p_make exactly 1 in *every* layout (declarer holds an
// entire suit outright), which is correct for what earlier tasks needed
// from it but leaves nothing for a convergence study to converge on --
// discovered directly, not assumed, while building the sweep this ladder
// exists for. Close to exhaustive_layout_source_integration_test.cpp's
// own two-card finesse (declarer Nine, dummy Eight, the fixed seat's
// single card against a five-card pool including the Ten), replicated
// across 1..DDS_SUITS independent suits so declarer needs every trick;
// the pool spans C(6s, s) for s = 1..4 -- 6, 66, 816, 10,626. No history
// form: these fixtures do not model a void, so `with_history` is not
// offered for them -- a caller passing `--history with` to the
// instrument for one of these gets the same (only) form back. See
// fixtures.cpp. ---

auto make_finesse_rung(int suits) -> RungFixture;  // suits in 1..DDS_SUITS

/// make_finesse_rung(1..DDS_SUITS), ascending N: 6, 66, 816, 10,626.
auto all_finesse_rungs() -> std::vector<RungFixture>;

// --- a third, separate ladder: the one shape the two above cannot stand
// in for -- a position solve_board() will actually accept. Every rung
// above intentionally gives some hand an unequal, unrealistic card count
// (the pool ladder's whole declarer suit; the finesse ladder's own
// 1-vs-5 split per suit); solve_board rejects that -- found directly, and
// twice over: every pool/realistic/finesse rung handed to
// DoubleDummyDefender fails with ValidationError::DistributionEmpty (a
// non-zero solve_board status), and this rung's own first two drafts did
// too, for two different reasons neither anticipated (unequal hand
// counts outright, then -- after equalising them -- a pool that turned
// out to include East/West's supposedly-fixed cards in other suits too,
// since defender_pool_decomposition flattens across every suit either
// defender holds anything in, not just the contested one). See
// fixtures.cpp's own comment on both. All four hands hold the same
// total card count, and East/West hold *only* Spades: `pool_size / 2`
// each, split between them -- the only suit with real uncertainty, the
// same construction this file's other rungs already use elsewhere --
// while North and South alone carry Diamonds as the card-count
// equaliser (`pool_size / 2 - 1` cards each there), never entering the
// enumerated pool regardless of suit. tricks_needed = pool_size / 2
// (every remaining trick, so a tier-1 dead cut can fire the moment the
// Spades finesse is lost, not only at the very end). See fixtures.cpp. ---

auto make_solver_rung(int pool_size) -> RungFixture;  // pool_size in {4, 6}, even

auto make_solver_rung_a() -> RungFixture;  // pool_size=4: C(4,2)=6
auto make_solver_rung_b() -> RungFixture;  // pool_size=6: C(6,3)=20

/// Where exhaustive evaluation (evaluate() over the *whole* space, no
/// sampling) stops being affordable is a measurement, not a guess.
/// Measured 2026-09-15, scripted single-card strategies (this file's own
/// fixtures_test.cpp, `exhaustively_evaluate`), fastbuild, this platform:
/// every rung here, both forms, completes in under 50ms -- pool8's own
/// 12,870-layout without-history form, the largest, took 49ms; pool6's
/// with-history form (210 layouts) took under 1ms.
///
/// That is not this ladder's own ceiling, and reporting it as one would
/// overclaim: `tricks_needed = 1` throughout keeps every search tree here
/// one trick deep regardless of pool size, so cost stays roughly linear in
/// `size()` rather than growing with a search tree the way a multi-trick
/// position with real declarer/defender choices would. This fixture shape
/// does not, by itself, locate the boundary the plan asks for -- that is a
/// property of tree depth and branching (more tricks, a strategy that
/// actually varies its answer by layout), which later tasks' own richer
/// strategies (a real double-dummy delta, several tricks needed) are
/// positioned to find. Recorded here rather than silently assumed away.

}  // namespace dds::belief_evaluation::benchmarks
