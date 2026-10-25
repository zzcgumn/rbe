#pragma once

#include <cstdint>

#include <api/dds_data_types.hpp>

namespace dds::belief_evaluation
{

/// The exact identity of a layout within one belief node: the holding of
/// `defender_seat`, packed as four 13-bit suits (52 bits). Every layout at
/// a node has the same declarer and dummy holdings, so one defender's
/// holding fixes the other's.
///
/// **Unique only within one node.** It is not a global layout identity.
///
/// `defender_seat` outside `0..DDS_HANDS` returns 0. Each suit is masked
/// to 13 bits, so stray bits in a malformed `Deal` cannot reach the next
/// suit.
auto layout_key(Deal const& deal, int defender_seat) -> std::uint64_t;

}  // namespace dds::belief_evaluation
