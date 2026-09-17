# Replenished Belief Evaluation: Benchmarks

This document reports what a set of measurements found about the
replenished-belief-evaluation capability's own behaviour: how sampled
answers relate to the true one, what a search costs and why, which early
cuts pay for themselves, and what crossing into Python costs a caller who
writes π or δ there.

**Who this is for.** If you have built a hand through this implementation
and gotten a number, this document is where you check whether that number
is surprising — a wide spread is normal at a given sample size; a
narrow spread around the wrong answer is not, and Behaviour & spread below
is where to look for that distinction. Every measurement here reports raw
counts, never only a ratio, and is labelled with which build mode and
platform produced it, so a number can be compared rather than merely read.

**Method, once, rather than per table.** Two build modes exist and they
differ only by `-DNDEBUG` — `-O3` is on in both, on every platform
(`CPPVARIABLES.bzl`), so "default" is not "unoptimised" against "optimised
`-c opt`". What `-c opt` actually removes is the module's own 21 runtime
asserts, including the mass-conservation invariants. Every counting-based
measurement below (all of them except the Python-cost section) was run in
the **default** build, deliberately, so those invariants stayed live while
the numbers were produced; `-c opt` would report the same counts, faster,
with less internal checking. The one wall-clock measurement (Python vs
C++ cost) reports both modes explicitly, since assert cost is exactly what
a caller comparing the two modes' timings needs to know is or is not in
the number. All measurements ran on Linux, one machine, one session —
absolute wall-clock times are not portable to another machine; the ratios
between two numbers taken in the same run are the more robust thing to
read across environments.

Every fixture named below is a member of one of three ladders of
increasing belief-space size: a **pool** ladder (five rungs, `N` = 70 to
12,870, declarer holding a whole side-suit as harmless filler) and two
**realistic** rungs sharing its combinatorics but a believable multi-suit
declarer hand instead; a **finesse** ladder (four rungs, `N` = 6 to
10,626, replicated independent finesses, the only ladder with genuine
per-layout uncertainty — every pool/realistic rung makes with `p_make = 1`
in every layout by construction); and a two-rung **solver** ladder
(`N` = 6, 20), the only fixtures small and evenly-dealt enough for
`solve_board` to accept, used wherever a measurement needed a real
double-dummy defender rather than a scripted one.

## Behaviour & spread: does a sampled answer converge to the truth, and does it converge honestly?

This is the most consequential question in this document, and the reason
it comes first: a spread that is merely wide is an inconvenience: a spread
that is narrow around the *wrong* answer is a bias, and would be the most
serious finding this capability's own testing could produce, since nothing
else in the project can detect it directly. So the question is answered in
that order below — where the true answer sits, before how wide the spread
is.

**Where the truth sits, at every sample size tried: inside the observed
spread, never outside it.** Measured on the finesse ladder (the only one
with real per-layout uncertainty), the exhaustive answer for each fixture
computed in the *same run* as the sampled ones (never a stored constant),
at four sample fractions of `N` (10%, 25%, 50%, 75%), 15-30 seeds per
cell:

| fixture | N | true p_make | M=10%N | M=25%N | M=50%N | M=75%N |
|---|---:|---:|---|---|---|---|
| finesse1 | 6 | 0.8333 | [0.667, 1.000] | [0.667, 1.000] | [0.667, 1.000] | [0.750, 1.000] |
| finesse2 | 66 | 0.8333 | [0.429, 1.000] | [0.688, 0.938] | [0.727, 0.909] | [0.780, 0.860] |
| finesse3 | 816 | 0.7169 | [0.634, 0.829] | [0.647, 0.784] | [0.706, 0.755] | [0.701, 0.734] |
| finesse4 | 10,626 | 0.6432 | [0.605, 0.668] | [0.623, 0.654] | [0.632, 0.648] | [0.638, 0.647] |

(Linux, default build.) Sixteen (fixture, `M`) cells, no exceptions — the
true value fell inside the observed range at every single one, including
the smallest-`M`, widest-spread cells where a bias would be most visible.
This is the first check in this capability's own testing of whether
`ExhaustiveLayoutSource`'s keyed-Feistel shuffling is actually unbiased
with respect to layout consistency, rather than merely asserted to be by
construction.

**Width shrinks monotonically with `M` at every fixture**, roughly as
`1/sqrt(M)` by eye (not fit statistically — see Limits below): finesse4's
own spread narrows from 0.063 (10%N) to 0.009 (75%N), a sevenfold
tightening for a 7.5x sampling increase. finesse1's own spread barely
narrows across `M` — expected, not a defect: even at 75% of `N=6` that is
4 or 5 of 6 total layouts, still close to the whole space.

**Recommendation: `M` around half of `N`** is a reasonable default for a
reader who cannot afford exhaustive evaluation and wants the estimator's
own noise comfortably below single-digit percentage points. Evidence:
finesse3/finesse4 (the two largest, most representative fixtures here)
are already under a 0.05 spread at `M=50%N`, and `M=75%N` buys
comparatively little more (0.049→0.033, 0.016→0.009) for 50% more
sampling cost. **Limits: drawn from four fixtures of one shape** (a
replicated finesse), not a general result across fixture geometry, and it
does not by itself account for sampling cost — Cost of sampling and
replenishment below covers that, and a bigger `M` is a more expensive one
there too.

One counter carried alongside every convergence run, `layout_min` (the
smallest live layout count at any depth reached), read 1 — the minimum
possible — at every cell measured, both sampled and exhaustive. **This is
not itself a red flag**: a search narrows toward single-layout certainty
near the end of a resolved line, which is expected at a leaf, not evidence
of premature collapse partway through. The measurement taken only
`layout_min`'s own minimum across all depths in one number, which cannot
distinguish "collapsed early" from "narrowed exactly where it should
have" — see Limits.

## Scan-to-hit by depth, against N — and whether a narrowing hint still looks necessary

`at_calls / layouts_added` at a node-local replenishment scan, against the
belief-space size `N`, across the whole pool/realistic ladder, both
history forms, swept two ways (Linux, default build, 20 seeds):

**Fixed absolute sample size** (`sample_size=5`, the operationally
realistic case — a caller who picks one practical sample size regardless
of the position) is roughly flat to mildly rising with `N`: a 184x
increase in `N` (70 → 12,870) produces about a 1.5-1.8x increase in
scan-to-hit, from 7.2 to 13.3 at depth 2. Read at face value against the
matching with-history row, this looks *worse* in 4 of 5 pool rungs (7.22
→ 8.67 at the smallest) — **this reading is a confound, not a finding**:
at a fixed absolute sample size, a small constrained space gets
over-sampled relative to its own size, driving up collision pressure
(drawn-and-excluded candidates) for a reason that has nothing to do with
whether history narrows the space usefully.

**Fixed fraction of N** (`sample_size = 20%` of each rung's own size, the
comparison that removes that confound) rises clearly with `N` (7.4 at
N=70 to 17.7 at N=12,870, without history), and **history improves or
matches scan-to-hit in 4 of 5 pool rungs** at matched fraction (only the
smallest rung is very slightly worse, within small-integer noise).

**Answer: a narrowing hint does not look necessary, on this evidence.**
At matched sampling fraction, a supplied history already buys most of
what a hint would — the direction the capability's own design predicted,
for the predicted reason (a history-constrained space is denser in
consistent layouts, not merely smaller). **Limits: seven rungs, one
family of fixture shapes** — pool6 and the corresponding realistic rung
give literally identical numbers (same defender-pool combinatorics,
different declarer filler only), a consistency check rather than a second
independent data point. Read the direction as evidence, not as validated
across a wide range of fixture shapes.

`attempted` vs `succeeded` stayed close (within a few percent) except at
the smallest with-history fixture, where 35% of scan attempts added
nothing — the same collision-pressure story: a 15-layout space with a
5-sample scan empties its node-local pool fast.

## What a play history buys, at realistic positions

Constrained (`with_history`) against unconstrained `size()`, same
underlying deal, across the pool/realistic ladder (Linux, no search run —
these are direct `ExhaustiveLayoutSource::size()` calls, no sampling, no
seed):

| rung | unconstrained | constrained | ratio |
|---|---:|---:|---:|
| pool4 | 70 | 15 | 4.67x |
| pool5 | 252 | 56 | 4.50x |
| pool6 | 924 | 210 | 4.40x |
| pool7 | 3,432 | 792 | 4.33x |
| pool8 | 12,870 | 3,003 | 4.29x |
| realistic_a | 924 | 210 | 4.40x |
| realistic_b | 12,870 | 3,003 | 4.29x |

The two realistic rungs are genuine post-trick-one positions, not small
endings with a synthetic history stapled on: declarer leads a top winner
and wins it, one defender discards (showing a real void), and the history
supplied replays exactly that. The ratio falls slowly toward 4, not
toward 1, as the pool grows (a void always removes the same two forced
cards, so its relative bite shrinks with pool size but never vanishes) —
**a supplied history keeps mattering at any pool size this ladder reaches
or plausibly could.**

**How wrong an unconstrained answer would have been, concretely, at the
two realistic rungs:** with `n` outstanding pool cards and a defender
holding `k`, the unconstrained enumeration gives that defender any
specific card with probability `k/n`; a defender who has shown void in a
suit with `h` cards still in the pool should instead see `k/(n-h)`.

| rung | k | unconstrained P(defender holds a given pool card) | constrained P | swing |
|---|---:|---:|---:|---:|
| realistic_a | 6 | 6/12 = 0.500 | 6/10 = 0.600 | +10.0pp |
| realistic_b | 8 | 8/16 = 0.500 | 8/14 = 0.571 | +7.1pp |

That swing lands on **every one of the ten (realistic_a) or fourteen
(realistic_b) outstanding non-void cards**, not on a single decision — the
error from an unconstrained answer is not confined to the voided suit.
The two forced-void cards themselves are the sharper number: an
unconstrained answer reports a flat 50% chance either defender holds one,
when the true answer is a certainty (0%) — the defender already showed
out of them.

Scan-to-hit improves at these same two rungs' own sizes too, not merely
space size: at matched sampling fraction (Scan-to-hit above, sweep 2),
depth-2 scan-to-hit went from 13.44 (N=924, no history) to 11.54 (N=210,
with history) at the smaller size, and from 17.69 to 17.62 at the larger
— flat to improving, never worse. **The size ratio alone understates what
a history buys a sampling run**, because the layouts a void removes are
disproportionately the ones a sampling scan would otherwise have drawn
and discarded as inconsistent.

## Cost of sampling and replenishment, especially when it fails

Two purpose-built cases (no existing fixture happens to exercise both;
Linux, default build):

- **A replenishment attempt that fails outright** (a near-exhausted
  source): 6.0 `at_calls` per attempt, **zero layouts added** — cheap per
  attempt, and buys literally nothing.
- **An attempt that always succeeds** (fresh candidates never scarce): 76
  to 105 `at_calls` per attempt, 100% success.

A failed attempt being *cheaper per call* is not a contradiction — it is
a scan giving up fast against an exhausted space; a larger nearly-exhausted
space could cost far more per failed attempt before conceding.

**`delta_calls` and wall time, `replenish_below` set against not set**,
swept across three rungs of increasing scale, three thresholds each
(10/25/50% of sample size), 10 seeds, best of 3 repeats:

| rung | sample_size | threshold | delta_calls | wall time | share of total |
|---|---:|---:|---:|---:|---:|
| finesse2 | 30 | 10% | +380% | 6.49x | 85% |
| finesse2 | 30 | 25% | +157% | 3.63x | 72% |
| finesse2 | 30 | 50% | +157% | 3.67x | 73% |
| finesse3 | 204 | 10% | +1353% | 27.38x | 96% |
| finesse3 | 204 | 25% | +403% | 8.01x | 88% |
| finesse3 | 204 | 50% | +403% | 8.13x | 88% |
| pool7 | 343 | 10% | +2194% | 118.47x | 99% |
| pool7 | 343 | 25% | +2754% | 152.48x | 99% |
| pool7 | 343 | 50% | +2878% | 158.49x | 99% |

**Recommendation: leaving `replenish_below` absent by default is right.**
Evidence: every configuration tested with it set showed replenishment
dominating total wall time (72-99% of the with-replenishment run), and
the cost grows sharply, not linearly, with fixture scale — two to nearly
three orders of magnitude of wall-time multiplier as fixture scale grows
across the rungs tested. Leaving it absent costs nothing (unchanged
behaviour); setting it without accounting for this scale dependence risks
a wall-time regression of two orders of magnitude, not a modest one.
**Limits: three rungs across two fixture shapes**, not a systematic sweep
of sample size at fixed `N` that would isolate scale from shape more
cleanly.

## Cut rates: how much tier 1 removes, and whether tier 2 pays

**Tier 1 (unconditional arithmetic cuts), made and dead cut rates as a
percentage of nodes visited**, across every fixture (Linux, default
build, 10 seeds, median):

| fixture | N | made_cuts | dead_cuts |
|---|---:|---:|---:|
| pool4..8, without history | 70-12,870 | 42.0-45.7% | 0.0% |
| pool4..8, with history | 15-3,003 | 31.8-33.6% | 0.0% |
| realistic_a/b, without history | 924, 12,870 | 44.4%, 45.6% | 0.0% |
| realistic_a/b, with history | 210, 3,003 | 32.3%, 31.7% | 0.0% |
| finesse1..4 | 6-10,626 | 20.0-22.8% | 2.5-4.7% |

Dead cuts read zero across the whole pool/realistic ladder by
construction, not by defect — those fixtures make with `p_make=1` in
every single layout, so there is never a losing line for `is_dead()` to
prune. The finesse ladder is the only one where declarer can lose, and is
the only one with nonzero dead-cut activity.

**How much of the tree a cut actually removes** (not merely what fraction
of *visited* nodes were cuts — a cut high in the tree removes a whole
subtree from ever being visited, which a per-visited-node rate
systematically understates), measured against a walker that removes both
tiers and counts the resulting tree — valid only on the finesse and
solver ladders, confirmed directly rather than assumed (the real
evaluator revisits sibling subtrees for belief-view renormalisation in a
way this single-pass walker cannot replicate, and the walker's own count
comes back smaller than the real tree's on every pool/realistic rung,
which is provably impossible where the property holds — so it is reported
only where checked to hold):

| fixture | N | tree fraction removed |
|---|---:|---:|
| finesse1 | 6 | 0.0% |
| finesse2 | 66 | 9.1% |
| finesse3 | 816 | 16.2% |
| finesse4 | 10,626 | 23.4% |

**Compounding grows with `N`, not flat** — independent cut opportunities
remove a growing share of the tree as it grows, across three orders of
magnitude on one fixture shape. **Limits: one fixture shape** (a
replicated finesse), not a systematic sweep of shape independent of
scale; the pool/realistic ladder's own compounding cannot be measured
with a walker built from the evaluator's own public pieces, for the
structural reason above.

**Tier 2 (the injected-bound cut), on the two solver-ladder rungs** — the
only fixtures small and evenly-dealt enough for a real double-dummy
defender, exhaustive, 10 seeds:

| fixture | N | nodes: without/with tier 2 | delta_calls: without/with | bound_calls (with) |
|---|---:|---|---|---:|
| solver_a | 6 | 25 / 5 | 14 / 6 | 10 |
| solver_b | 20 | 49 / 7 | 50 / 20 | 40 |

Clear benefit on both: 80% and 86% fewer nodes visited, 57% and 60% fewer
delta calls. Cost, re-measured directly: roughly 14-18 µs per bound call
against roughly 80-108 µs per delta call — but this is a **net** figure
(the wall-time delta between the two runs, divided by bound calls), not
an isolated per-call cost: it already has tier 2's own downstream savings
(fewer nodes, fewer delta calls) folded into it working the other way.

**Recommendation: on this evidence, tier 2 pays, clearly, on both
fixtures it was tested against.** **Limits: the qualifying-δ sample size
is exactly one** (a double-dummy defender paired with a double-dummy
bound — the only pairing that satisfies the soundness obligation this
tier's own injected bound carries), and both fixtures are small (`N` = 6
and 20). This is a real finding at that scale, not a general claim about
tier 2 at production scale.

**Checked against the documentation's own claim that early cuts "speed up
the search significantly": not contradicted.** Made-cut rates are
substantial everywhere measured (20-46% of visited nodes), tier 1's own
tree-fraction-removed grows with `N` rather than staying flat, and tier 2
shows a clear net benefit on both fixtures tested. Nothing found here
argues for softening or qualifying that claim.

## `TouchingSequence` against `AllOptimal`: how often the two spread policies actually diverge

Both halves of this question need a real double-dummy solve, restricting
the measurement to the two solver-ladder rungs (Linux, default build,
three seeds, confirmed bitwise identical across seeds — these are
exhaustive fixtures, so seed cannot affect the answer).

**Choice divergence** — do the two policies pick different candidate card
sets at the same node — run once with each policy driving the tree (the
two trees genuinely differ, since which children a defender node gets
depends on which policy is live):

| fixture | driven by | delta_calls | divergences | rate |
|---|---|---:|---:|---:|
| solver_a | TouchingSequence | 14 | 4 | 28.6% |
| solver_a | AllOptimal | 18 | 8 | 44.4% |
| solver_b | TouchingSequence | 50 | 23 | 46.0% |
| solver_b | AllOptimal | 80 | 51 | 63.7% |

**This is not rare** — between a quarter and two-thirds of defender calls
on these two small, unremarkable fixtures (not built to engineer a tie)
see the two policies diverge.

**Answer divergence** — does the choice change the final `p_make` — read
`p_make(TouchingSequence)` against `p_make(AllOptimal)` on the same
fixture: both rungs report `0.0` under either policy. **This is not
evidence the choice never matters** — traced rather than reported flat,
both solver rungs turn out to be positions declarer cannot make against
*any* defence at all (by their own construction: exactly as many defender
winners in the pool as tricks needed), so the zero is a ceiling on what
these two fixtures can show, not a finding about the parameter itself.

**Conclusion: a real choice to the search, unresolved for the final
answer.** The two policies choose differently often enough that a caller
selecting between them is selecting a materially different search tree,
not a corner case. Whether that choice ever changes which hands are
recommended makeable remains open — closing it needs a fixture where
declarer's success genuinely depends on the defence, under the equal hand
counts a real double-dummy solve requires, which the existing fixture
ladder does not have.

## What a Python π and δ cost against their C++ equivalents

Same fixture, same seed, same rule on both sides for the shared parts —
`single_card_declarer_play`'s own "lowest legal card" rule (identical to
the existing parity test's own π, already proven bitwise-identical
between languages) crossed with a "real work" π that scores every legal
card by the posterior-weighted count of defender cards that would beat it
(entries × candidates work, proven correct independently in both
languages via a hand-derived case, not through a fixture) — and a
replenishing sampling configuration against a non-replenishing one
differing only in `replenish_below`. 2000 iterations per cell, one run
per build mode, Linux:

| π | config | build mode | C++ ms/iter | Python ms/iter | ratio |
|---|---|---|---:|---:|---:|
| trivial | non-replenishing | default | 0.020412 | 0.345747 | 16.94x |
| trivial | non-replenishing | opt | 0.021938 | 0.261476 | 11.92x |
| trivial | replenishing | default | 0.056528 | 0.582060 | 10.30x |
| trivial | replenishing | opt | 0.053599 | 0.479204 | 8.94x |
| real-work | non-replenishing | default | 0.019605 | 0.472462 | 24.10x |
| real-work | non-replenishing | opt | 0.017293 | 0.402853 | 23.30x |
| real-work | replenishing | default | 0.055005 | 0.866256 | 15.75x |
| real-work | replenishing | opt | 0.053723 | 0.702686 | 13.08x |

**Reproduces an earlier order-of-magnitude check's own ratio closely**
(trivial-π/replenishing: 10.30x default against a prior ~9.0x, 8.94x
`-c opt` against a prior ~7.8x — same order of magnitude, same direction,
opt lower than default in both). Absolute times do not reproduce and
should not be expected to — both languages run roughly 4.5-5x slower here
than the earlier check's own figures, consistently across both languages
and both build modes, which is what different underlying hardware running
the same relative workload produces, not a methodology difference (which
would distort the ratio, not scale both sides by close to the same
constant).

**Does a Python π dominate the profile? Yes — more emphatically than the
trivial-rule ratio alone suggests, for a specific, checked reason.** A
real-work π was predicted to *narrow* the ratio (fixed crossing cost
amortising over more work); measured here it **widens** instead, at both
sampling configurations and both build modes. Traced to a fact about the
fixture, confirmed from its own construction rather than inferred: it
gives declarer exactly one legal card at every decision, so the real-work
scoring's own `entries × candidates` work collapses to `entries × 1` — a
handful of floating-point multiply-adds C++ absorbs for free at these
timescales (real-work measures at or below trivial on the C++ side, every
cell). **On the Python side that same work is not free**: real-work costs
0.126-0.284ms/iteration more than trivial, holding the sampling
configuration fixed — the entire size of the gap the ratio widens by. A
Python callback's own cost is not only the fixed cost of crossing the
boundary; it is also the raw interpreter cost of whatever the callback
does once there, and that second cost is visible in Python at a scale C++
cannot measure at all. **Condition this can state precisely**: it holds
whenever the belief view being reasoned over carries more than a handful
of entries. **Condition it cannot speak to**: this fixture's own
single-candidate-per-suit construction means the *candidates* side of
that product was never exercised above 1.

**Replenishment's cost is dominated by the Python side, read as raw
milliseconds rather than a percentage.** A percentage reading looks
backwards — replenishment appears *proportionally* larger on the C++ side
(+176.9% default) than the Python side (+68.4%) — because the two
percentages are computed against very different baselines (a near-free
C++ call, an already-expensive Python one), and a percentage against
different baselines obscures rather than reveals the comparison. The raw
figures say the opposite: replenishment adds 0.036ms/iteration in C++
against 0.236ms/iteration in Python (trivial π, default) — **Python's own
replenishment tax is roughly 6.5x the size of C++'s in absolute wall
time**, growing to roughly 11x at the real-work π. A Python δ does sit on
a hot inner loop once replenishment is enabled, and its cost is not a
rounding error next to the trivial-π baseline.

## Limits: what this document could not measure, and what would close each gap

- **The exhaustive-evaluation ceiling** (a measurement, not a guess, from
  building the fixture ladder itself): every pool/realistic rung, both
  history forms, completes an exhaustive evaluation in under 50ms on this
  platform — the largest (12,870 layouts) took 49ms. This is not this
  ladder's own general ceiling: every rung here needs only one trick, so
  cost stays roughly linear in belief-space size rather than growing with
  a search tree the way a multi-trick position with real declarer
  choices would. Locating that boundary needs a richer fixture (more
  tricks needed, a strategy that actually varies its answer by layout),
  not attempted here.
- **No defect surfaced by any fixture in this document that no existing
  test already covers.** Checked explicitly, not merely unmentioned —
  running positions nothing had run before was the most likely thing in
  this whole effort to surface one by accident, and none did.
- **The pool/realistic ladder's own convergence behaviour** is not
  measured (Behaviour & spread above covers only the finesse ladder,
  which has genuine per-layout uncertainty; the pool/realistic ladder
  makes with `p_make=1` in every layout by construction and has nothing
  to converge on). A non-degenerate strategy over that ladder would be
  needed to extend it.
- **Whether `TouchingSequence`/`AllOptimal` divergence ever changes the
  final answer** is open (`TouchingSequence` against `AllOptimal` above)
  — needs a fixture genuinely winnable in some layouts and not others,
  under the equal hand counts a real double-dummy solve requires.
- **No Python δ was measured with real work** — only π got the two-shape
  treatment; δ stayed the trivial rule throughout on both languages. The
  replenishment-cost finding above still bears on a Python δ's own risk,
  since it is δ's *call frequency* the replenishing/non-replenishing
  contrast isolates, not δ's own per-call complexity.
- **Whether a bound provider and a defender interleaving on the same node
  defeat the solver's own transposition-table warmth** (the `similarDeal`
  question) was not attempted — it would need solver-internal
  instrumentation beyond what this capability's own public interface
  exposes.
