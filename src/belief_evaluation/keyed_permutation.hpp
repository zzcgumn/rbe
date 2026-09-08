#pragma once

#include <cstdint>

namespace dds::belief_evaluation
{

/// A seeded pseudorandom bijection on `[0, n)`, storing nothing beyond
/// `seed` itself -- the randomised order a `LayoutSource` built on top of
/// `unrank_combination` needs so that scanning from index 0 takes an
/// effectively random sample rather than a systematically biased prefix
/// (see `LayoutSource::at`'s own doxygen for the obligation this
/// discharges). General-purpose: nothing here is about bridge, a `Deal`, or
/// even combinatorics -- it is why this lives in its own file rather than
/// alongside `defender_split.hpp`.
///
/// **Not a materialised shuffle.** `n` reaches `C(26, 13) = 10,400,600`; a
/// Fisher-Yates permutation of that domain is tens of megabytes, built
/// eagerly, before a single layout exists. This computes the same
/// property -- a bijection on `[0, n)` -- on demand, in O(1) expected time
/// per call.
///
/// **The construction: a balanced-as-possible Feistel network over the
/// smallest power-of-two domain `2^b >= n`, with cycle-walking.** The
/// network is applied to `index`, and if the result lands at or past `n`,
/// the network is applied again to that result, repeating until it lands
/// in range. A Feistel network of any round count is an exact bijection on
/// `[0, 2^b)` for *any* round function (each round updates one half by
/// XOR with a function of the other half, and XOR against a fixed value is
/// invertible) -- so is cycle-walking's restriction of a bijection to a
/// subrange (the classic result: iterating a permutation and rejecting
/// out-of-range outputs, on a domain the permutation maps back into
/// itself, remains a bijection on the accepted subset). Bijection is the
/// one design constraint here, not indistinguishability from true
/// randomness -- this is not cryptography, and the bar is "no visible
/// structure in the sample", not resistance to an adversary.
///
/// `b`'s two halves differ by one bit when `b` is odd (`b/2` and
/// `b - b/2`), handled by updating each half with a round function keyed
/// on the *other* half's own current width -- not the classic swap-based
/// Feistel structure (which requires equal widths to exchange), but the
/// simpler alternating-XOR form: odd rounds XOR the high half with a
/// function of the low half, even rounds XOR the low half with a function
/// of the high half. This remains an exact bijection on `[0, 2^b)`
/// regardless of the width split, by the same per-step invertibility
/// argument above.
///
/// **Cycle-walking terminates for every input**: each step moves within a
/// finite permutation's own cycle, and `[0, n)` is non-empty by
/// construction (a `LayoutSource` is never built over an empty space), so
/// the walk cannot leave the cycle without eventually landing in range.
/// The *expected* number of steps is `2^b / n`, which is under 2 since `b`
/// is the smallest power of two at least `n` (so `2^b < 2n`); worst case is
/// unbounded in theory (a cycle could, in principle, revisit
/// out-of-range values many times before landing in range), but no
/// iteration cap is applied -- a cap would break bijectivity by silently
/// giving up on some inputs rather than completing the walk.
///
/// **Round count: 3, chosen by measurement, not copied.** An avalanche
/// test (single input bit flipped, average number of output bits that
/// change, over many random inputs and several domain widths `b` from 4 to
/// 14) shows 1 and 2 rounds measurably under-diffuse -- at `b = 14`, 1
/// round changes an average of 2.6 of 14 bits (versus the ~7 an
/// unstructured map would), 2 rounds reach 5.5; a direct symptom of the
/// same deficiency is round 1 leaving one whole half of the domain
/// completely untouched, so its bits pass through unchanged. 3 rounds
/// reaches 6.8 of 14 -- within measurement noise of the unstructured ideal
/// -- and matches it at every other width tested (4, 7, 10); 4 and 5
/// rounds measure no further improvement. Cross-checked against fixed-point
/// counts (how often `keyed_permutation` maps an index to itself) and
/// inversion counts (how far the output order is from either sorted or
/// reverse-sorted) across several `n`: both settle to the unstructured
/// range at 3 rounds and do not improve further at 4 or 5.
///
/// `index >= n` is a caller error (asserted); the domain this is built
/// over is always `[0, n)`.
auto keyed_permutation(std::uint64_t index, std::uint64_t n, std::uint64_t seed) -> std::uint64_t;

}  // namespace dds::belief_evaluation
