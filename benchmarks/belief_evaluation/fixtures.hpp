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
