#pragma once

#include <cstdint>

#include <belief_evaluation/dds_types.hpp>

/// Exact identity of a layout within one belief node: `defender_seat`'s
/// holding, packed as four 13-bit suits (52 significant bits). Unique only
/// among layouts that share a node's outstanding-card pool — every layout at
/// a node has bit-identical declarer and dummy holdings and differs only in
/// how the pool splits between the two defenders, so one defender's holding
/// determines the other's by complement and identifies the layout completely
/// within that node. This is **not** a cross-node or global layout identity.
///
/// `defender_seat` outside `0..DDS_HANDS` returns 0 rather than indexing out
/// of bounds. Each suit's holding is masked to its 13 significant bits after
/// shifting, so a malformed `Deal` with stray bits set above rank 14 cannot
/// leak into an adjacent suit's field of the packed key.
auto layout_key(Deal const& deal, int defender_seat) -> std::uint64_t;
