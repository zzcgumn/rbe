#pragma once

#include <cstdint>

namespace dds::belief_evaluation
{

/// A seeded pseudorandom bijection on `[0, n)` that stores only `seed`.
/// It gives a `LayoutSource` the random order it needs, so that a prefix
/// is a fair sample. See `LayoutSource::at`.
///
/// **Not a stored shuffle.** `n` reaches `C(26, 13)`, which would take tens
/// of megabytes. Each call takes O(1) expected time.
///
/// **The construction:** a Feistel network over the smallest `2^b >= n`,
/// with cycle-walking. Both steps keep it a bijection. A Feistel round
/// XORs one half with a function of the other, which is invertible for any
/// function. Cycle-walking restricts a bijection to a subrange. This is not
/// cryptography: the goal is no visible structure in the sample.
///
/// An odd `b` gives halves that differ by one bit. The rounds alternate
/// the XOR direction instead of swapping halves.
///
/// **Cycle-walking always ends.** Expected steps are `2^b / n`, under 2.
/// There is no step cap: a cap would break the bijection.
///
/// **3 rounds, chosen by measurement.** An avalanche test over widths 4 to
/// 14 shows 1 and 2 rounds mixing too little. At `b = 14`, 3 rounds change
/// 6.8 output bits per input bit, close to the ideal of about 7. 4 and 5
/// rounds are no better.
///
/// `index >= n` is a caller error (asserted). `n == 0` is also a caller
/// error, but it is not asserted. It returns `0`: otherwise `n - 1` would
/// underflow and the loop would never end in a release build.
auto keyed_permutation(std::uint64_t index, std::uint64_t n, std::uint64_t seed) -> std::uint64_t;

}  // namespace dds::belief_evaluation
