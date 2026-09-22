#pragma once

#include <cstdint>

namespace dds::belief_evaluation
{

/// A seeded pseudorandom bijection on `[0, n)`, storing nothing beyond
/// `seed` — the randomised order a `LayoutSource` needs so that scanning
/// from index 0 takes an effectively random sample rather than a
/// systematically biased prefix (see `LayoutSource::at` for the obligation
/// this discharges). General-purpose: nothing here is about bridge, which
/// is why it lives in its own file.
///
/// **Not a materialised shuffle.** `n` reaches `C(26, 13)`, tens of
/// megabytes as a Fisher-Yates permutation, built eagerly before a single
/// layout exists. This computes the same property on demand, in O(1)
/// expected time per call.
///
/// **The construction: a balanced-as-possible Feistel network over the
/// smallest power-of-two domain `2^b >= n`, with cycle-walking.** Both
/// steps preserve bijectivity: a Feistel round updates one half by XOR with
/// a function of the other, which is invertible for *any* round function,
/// and cycle-walking restricts a bijection to a subrange it maps into
/// itself. Bijection is the design constraint — this is not cryptography,
/// and the bar is "no visible structure in the sample", not resistance to
/// an adversary.
///
/// An odd `b` splits into halves differing by one bit, so rounds alternate
/// XOR direction rather than swapping halves, which would need equal
/// widths. The invertibility argument is unchanged.
///
/// **Cycle-walking terminates for every input**: each step moves within a
/// finite permutation's cycle, and `[0, n)` is non-empty. Expected steps
/// are `2^b / n`, under 2 since `2^b < 2n`; the worst case is unbounded in
/// theory, and no iteration cap is applied — a cap would break bijectivity
/// by giving up on some inputs.
///
/// **Round count: 3, chosen by measurement.** An avalanche test over
/// domain widths 4 to 14 shows 1 and 2 rounds measurably under-diffusing
/// (at `b = 14`: 2.6 and 5.5 output bits changed per input bit flipped,
/// against the ~7 an unstructured map gives); 1 round leaves one whole half
/// untouched. 3 rounds reaches 6.8, within noise of the ideal, and 4 and 5
/// measure no further improvement. Cross-checked against fixed-point and
/// inversion counts, which settle at 3 as well.
///
/// `index >= n` is a caller error (asserted). `n == 0` likewise, but is
/// handled as a hard case returning `0` rather than only asserted:
/// computing `n - 1` would underflow and cycle-walking could then never
/// exit, hanging in exactly the release build where the assert is gone.
auto keyed_permutation(std::uint64_t index, std::uint64_t n, std::uint64_t seed) -> std::uint64_t;

}  // namespace dds::belief_evaluation
