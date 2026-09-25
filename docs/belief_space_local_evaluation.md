# Belief Space Local Evaluation

## What this is

Given a declarer strategy π and a defender strategy δ, this capability
computes `P_make`: the probability the contract is made, over a *belief
space* — the set of layouts consistent with everything observed so far —
rather than over one assumed layout. It turns declarer play, a partially
observable decision process over a hidden layout, into a search over the
belief space instead. The theory is in
[`docs/replenished_belief_evaluation/algorithm.md`](replenished_belief_evaluation/algorithm.md),
cited throughout below and not restated; this document covers the
vocabulary and both languages' entry points.

It exists as its own capability, separate from the double-dummy solver
proper: a caller solving a single board has no reason to import belief
evaluation to do it, and this capability's own vocabulary (a belief space,
sampling, replenishment) is unrelated to solving one fixed layout.

### The vocabulary

- **A layout** is one fully-specified deal — who holds what, entirely.
- **A belief space** is the set of layouts declarer considers plausible,
  each carrying a probability. At the root, every layout is equally
  likely.
- **A layout source** enumerates candidate layouts, in a fixed order, for
  the evaluator to filter down to the belief space and (when sampling)
  draw a prefix from.
- **π** (declarer's strategy) and **δ** (the shared defender strategy —
  both defenders use it, fed different information) are the two decision
  rules being evaluated against each other.
- **Sampling** draws a bounded prefix of the source's own order for the
  root, rather than enumerating every consistent layout, when the whole
  space is too large to hold in memory.
- **Replenishment** tops a node whose sample has collapsed back up
  towards the target size, from the same source, once its own belief
  space has shrunk below a threshold — node-local, and only to the extent
  the source can still supply matching layouts.
- **`P_make`** is the number this whole capability computes: the
  probability the contract makes under π against δ, over the belief
  space.

Both language surfaces below are built from exactly these pieces: a root
layout, a declarer seat, a layout source, π, δ, and a small set of
options controlling sampling and instrumentation.

## The four obligations nothing here can validate

Each of these is **silently wrong**, not an error — nothing in either
language surface can detect a violation, so getting one wrong produces a
plausible-looking number rather than a crash or an exception.

1. **`state_key`'s coarseness.** π may optionally declare what its `play`
   decision depends on beyond the position the evaluator already keys on,
   for a future reuse cache. A key coarser than `play`'s real dependence
   produces a *wrong probability*, not a slow one. It is currently never
   called — there is no cache yet — so do not spend effort optimising it;
   write it correctly anyway, since the contract is part of the surface.
2. **An injected trick-count bound.** Both languages let a caller supply
   an upper bound on declarer's double-dummy trick count from a given
   layout, to let the evaluator skip subtrees that are provably dead. A
   bound that is too *low* makes that cut fire when it should not,
   silently reporting zero for a contract that in fact makes; a bound
   that is too high only loses pruning, never soundness. **The
   `DoubleDummyBound` shipped here currently violates this** — see "Known
   gaps" below.
3. **The declaration that δ is double-dummy optimal for trick count.**
   Separate from the bound above, deliberately: a caller may want a bound
   for instrumentation while δ does not actually qualify, and collapsing
   the two into one flag would make that unsound configuration the easy
   one to reach. The bound's own soundness (`R <= DD`) depends on this
   declaration being true; nothing checks that it is.
4. **That a caller-supplied layout source presents a randomised order.**
   Sampling takes a *prefix* of the source's own order, on the premise
   that the order is already effectively random with respect to which
   layouts are consistent with any given root. A source that is sorted,
   or grouped by anything correlated with consistency, yields a
   systematically biased sample with no diagnostic anywhere — nothing here
   can distinguish a well-shuffled source from a badly-ordered one, since
   both simply return layouts in whatever order they are asked for. This
   is the one a Python caller is most likely to get wrong: the obvious
   subclass returns `self._deals[i]` from a list built in whatever order
   it was assembled, which is exactly a sorted or grouped order in
   practice. `ExhaustiveLayoutSource` (Python) / `ExhaustiveLayoutSource`
   (C++) is a correct implementation of this obligation, keyed on a seed;
   supply your own source only when you need a belief space narrower than
   the root alone implies (what the bidding ruled out, say), and base its
   ordering on the same kind of keyed shuffle if you do.

## The play history: needed, not merely optional

Both `ExhaustiveLayoutSource` constructors take an optional play history —
a card sequence and the opening leader. It is optional in the sense that
the type accepts its absence. **That is not the same claim as "not
needed", and only the first is true.**

At a root built after trick one, omitting the history gives an answer
computed over a belief space that includes **impossible layouts** —
defenders holding suits they have already shown out of. This is not a
small correction. With a pool of `n` cards and a defender holding `k` of
them, the unconstrained enumeration gives that defender any specific card
with probability `k/n`; if the defender has shown out of a suit with `h`
cards still in the pool, the right probability is `k/(n-h)`. The odds are
wrong for **every** card in the pool, not only the suit shown out of, and
every remaining decision computed at that root inherits the wrong odds.

The parameter is optional because **a play record is not always
available** — not because it is not always needed. A caller with no
history genuinely cannot be helped by any amount of implementation here;
this is a limitation of the input, not a gap awaiting future work.

**Fact versus inference** is the line a caller has to know which side
their own information falls on:

- **fact, applied automatically by `ExhaustiveLayoutSource` given a
  history**: a defender showed out of a suit, so holds none of it.
- **inference, still the caller's own to apply**: the auction suggests a
  defender is short in a suit. Nothing here infers this — a caller with
  bidding-derived information supplies their own, narrower layout source
  reflecting it (the `LayoutSource` injection point, in either language).

Supplying a play history does not cover bidding inference, and a reader
who conflates the two will under-constrain their belief space without any
signal that they have done so.

## Python

```python
import belief_space_local_evaluation as bsle
```

A complete worked example — playing a hand out to a mid-play root, building
the belief space from the play history, and evaluating one declarer line
against three defences — is `examples/guess_6nt_belief_space.py`, runnable
with `bazelisk run //examples:guess_6nt_belief_space`. Every **Python** code
block below is exercised by
`python/tests/test_belief_space_local_evaluation_docs_examples.py`, with the
doc's placeholder names (`root`, `declarer`, `seed`, `tricks_needed`) bound to
concrete values there -- so "exercised", not literally verbatim. The two C++
blocks are not run by that test.

### The building blocks

- **`bsle.Card(suit, rank)`** — `suit` is `0=♠ 1=♥ 2=♦ 3=♣`; `rank` is
  **absolute**, `2..14`, never relative to a node's outstanding pool. This
  is the type π returns and δ's weighted cards carry.
- **A deal** crosses as a plain `dict`, the same shape `dds3` already
  documents: `trump`, `first`, `remain_cards` (a 4×4 array of bitmasks,
  `[hand][suit]`), `current_trick_suit`, `current_trick_rank`.
- **`bsle.ObservationState`** — read-only, handed to π's `play` at every
  call: `trump`, `first`, `history` (a list of `Card`, in order),
  `declarer`, `tricks_needed`, `tricks_won_by_declarer`, `known_holdings`
  (a deal dict; declarer's and dummy's entries are exact, a defender's
  entry is the **union pool** of both defenders' outstanding cards, not
  that defender's own actual holding), and `ranks` (a `bsle.RankMap`:
  `.aggr`, `.to_relative(suit, rank)`, `.to_absolute(suit, ordinal)` — the
  precomputed rank mapping a C++ strategy conditions on directly; a Card
  is always absolute, so a strategy reasoning in relative terms converts
  with `to_absolute` before returning one). `play` and `state_key` share
  this same type, but `state_key` is not itself called yet — see
  obligation 1 above.
- **`bsle.BeliefView`** — what π reasons over: `entries` (a sequence of
  `BeliefEntry`, each with `.layout`, a deal dict, and `.posterior`),
  `is_sample`, `space_size`. **Valid only for the duration of the call it
  was handed to** — accessing it, or anything obtained from it besides an
  already-read `.layout`, afterwards raises `ExpiredBeliefViewError`. Do
  not store a `BeliefView` past the callback that received it.

### A layout source

```python
source = bsle.ExhaustiveLayoutSource(root, declarer, seed)
```

`size()` and `at(i)` enumerate every layout consistent with `root`. Pass a
play history to narrow the space to what it establishes as fact:

```python
source = bsle.ExhaustiveLayoutSource(
    root, declarer, seed, history=[bsle.Card(0, 14), bsle.Card(1, 2)], opening_leader=0)
```

A rejected history (one that does not belong to `root`, or belongs but
leaves no legal split) raises immediately, from the constructor — see
"Where Python is stricter than C++" below.

Subclass `bsle.LayoutSource` and override `size()`/`at(i)` to supply a
narrower space than the root alone implies:

```python
class MySource(bsle.LayoutSource):
    def size(self):
        return len(self._deals)

    def at(self, index):
        return self._deals[index]  # must already be in a randomised order
```

### π and δ

The shape, not a working strategy -- `Card(0, 14)`/`Card(0, 2)` here are
schematic fixed plays, legal only at a node that happens to hold exactly
those cards, not derived from `state`/`layout` the way a real strategy
must be:

```python
def pi(state, view):
    # Pure function of (state, view) alone -- see below.
    return bsle.Card(0, 14)

def delta(layout, seat, state):
    # A list of (Card, probability) pairs. A card the defender will never
    # play must be omitted, not given probability 0 -- the evaluator
    # treats "probability > 0" as the survival test for a layout.
    return [(bsle.Card(0, 2), 1.0)]
```

**Both must be pure functions of their arguments.** The evaluator walks
the tree in its own order and revisits sibling subtrees; a strategy that
accumulates state across calls (most commonly a *seeded* strategy drawing
from one PRNG stream across the whole search) will silently return
different cards for the same node and corrupt the result. If you want
variety without breaking purity, derive the choice from the arguments
themselves — `choice = hash(seed, state) mod n` — rather than from a
stream.

### `evaluate()`

```python
result = bsle.evaluate(root, declarer, tricks_needed, source, pi, delta)
```

Every `EvaluateOptions` field from the C++ type is a keyword-only
argument with the same default: `retain_root`, `collect_counters`,
`bound`, `delta_is_double_dummy_optimal`, `sample_size`, `scan_budget`,
`replenish_below`. `state_key` is also keyword-only here (it is not an
`EvaluateOptions` field in C++ — it belongs to the declarer strategy —
but π crosses as a bare callable rather than a bundled object).

```python
result = bsle.evaluate(
    root, declarer, tricks_needed, source, pi, delta,
    sample_size=200, replenish_below=20)

value = result["by_strategy"][1]  # keyed by pi's strategy id, always 1 here
value["p_make"]           # float
value["root_children"]    # [(Card, float), ...] -- see below
```

`root_children` is alternatives at a declarer root (`p_make` equals
whichever entry π actually chose, not their sum) and a genuine partition
at a defender root (the entries *do* sum to `p_make`, since defender
children partition mass by construction); empty at a terminal root.
Summing them and comparing to `p_make` agrees sometimes and not others —
know which kind of root you are looking at before drawing a conclusion
from a mismatch.

`counters` (present only with `collect_counters=True`) and `retained_root`
(present only with `retain_root=True`, and **the root node alone, never a
tree**) are documented on the returned dict's own keys; see
`test_belief_space_local_evaluation_results.py` for their full shape.

### Exceptions

Every `RootFailure` and every `ValidationError` cause raises a
distinguishable exception, all rooted at
`bsle.BeliefSpaceLocalEvaluationError`. A rejected play history raises
from `ExhaustiveLayoutSource`'s own constructor, under a **separate**
family (`HistoryRejectedError`/`ConstrainedSpaceEmptyError`, still under
the same root) — never catchable as the same thing as an ordinary
`NoLayoutSurvivedError`, since the fix in each case is different (fix your
history, versus fix your source). A Python exception raised inside π
(`play`), δ, or a Python source's `size()`/`at()` propagates out of
`evaluate()` unchanged — own type, own message — rather than being
converted into one of this module's own exceptions. `state_key` is not
listed here: it is never called yet (there is no cache to key), so it has
no exception to propagate — see obligation 1 above.

### The solver seam

```python
import dds3

ctx = dds3.SolverContext()
delta = bsle.DoubleDummyDefender(ctx)                    # usable directly as delta
bound = bsle.DoubleDummyBound(ctx, declarer)              # usable directly as bound
```

A `SolverContext` built through `dds3` works here directly — the two
modules share one type. **`DoubleDummyBound` is currently unsound — see
"Known gaps" below before using it.** It also **fixes `declarer` at
construction and is not reusable across declarers**; reusing one across
two declarers produces a silently wrong bound, which then feeds the
bound-gated cut, whose soundness depends on the bound being right for the
declarer actually being evaluated. `DoubleDummyDefender` **maximises
tricks, not the contract** — it is explicitly not best defence against a
contract, and will sometimes concede the contract to hold the trick count
down — while still satisfying `delta_is_double_dummy_optimal`, since that
declaration is about trick count, which trick-maximising play delivers
for both sides regardless of the contract. `SpreadPolicy.TouchingSequence`
spreads uniformly over the single best card's own touching-sequence group
(restricted choice, the canonical distribution over a genuine equivalence
class); `SpreadPolicy.AllOptimal` spreads over every tied-for-best
candidate's own group, across suits — those cards are equally *good* but
not otherwise equivalent, so this is a more advanced justification, not a
drop-in alternative.

**A `SolverContext` is not thread-safe** (its own C++ contract: one
context per thread), and `DoubleDummyDefender`/`DoubleDummyBound` release
the GIL around the actual solve — so, unlike most of this module, two
Python threads really do run concurrently here if they share one. Build
one `SolverContext` (and one `DoubleDummyDefender`/`DoubleDummyBound`) per
worker; do not share either across threads.

### Where Python is deliberately stricter than C++

Two places, both for the same reason: **C++ chose the weaker behaviour
because the language left it no choice, not because the weaker behaviour
is better.**

- **`replenish_below` without `sample_size`.** In C++ this is a
  documented silent no-op — the field lives on a nested `SamplingOptions`
  type whose *shape* makes the coupling visible to a reader with the
  doxygen open. The Python surface flattens every option to a keyword
  argument, which makes the coupling invisible again to a caller with no
  doxygen at all — so this raises `ValueError` here rather than silently
  doing nothing.
- **A rejected play history.** In C++, `history_verdict()` and
  `constrained_space_status()` are accessors, because a constructor
  cannot report and an earlier review already ruled out a factory
  indirection for this type. Python has no such constraint: a
  non-`Consistent` verdict raises directly from the constructor, so a
  caller who never thinks to call the accessors still finds out.

## C++

```cpp
#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/exhaustive_layout_source.hpp>

namespace be = dds::belief_evaluation;
```

The same pieces, as C++ types: `Card` (`suit`, `rank`, absolute),
`ObservationState`, `BeliefView`/`BeliefEntry`, `LayoutSource` (an
abstract class — `size()`/`at(index)` — implement it directly, no
trampoline needed), `ExhaustiveLayoutSource`, `DeclarerStrategy` (`id`,
`play`, optional `state_key`), `DefenderStrategy` (a
`std::function<std::vector<WeightedCard>(DefenderQuery const&)>`), and
`evaluate()` itself, taking an `EvaluateOptions` whose fields match the
Python keyword arguments above one for one (nested `sampling` for the
three coupled fields).

```cpp
Deal const root = /* ... */;
be::ExhaustiveLayoutSource const source(root, /*declarer=*/0, /*seed=*/1u);

be::DeclarerStrategy const pi{
    .id = 1,
    .play = [](be::ObservationState const& state, be::BeliefView const& view) -> be::Card {
        return be::Card{0, 14};
    },
    .state_key = nullptr,
};
be::DefenderStrategy const delta = [](be::DefenderQuery const& query) -> std::vector<be::WeightedCard> {
    return {be::WeightedCard{be::Card{0, 2}, 1.0}};
};

be::EvaluationResult const result =
    be::evaluate(root, /*declarer=*/0, /*tricks_needed=*/1, source, pi, delta);
if (! result.error.has_value()) {
    double const p_make = result.by_strategy.at(1u).p_make;
}
```

`EvaluationResult::error`, when set, carries `callback` (which of π, δ, or
root construction), `seat`, `layout`, and either `validation` or
`root_failure` — the evaluator **reports** a contract violation here
rather than throwing, since a callback is caller input, not an internal;
a genuine internal invariant failure asserts instead. The Python surface
collapses this same distinction back into ordinary exceptions at its own
boundary instead, since raising is the idiomatic thing to do there and
reporting is not.

`DoubleDummyDefender` and `DoubleDummyBound` (`#include
<belief_evaluation/double_dummy_defender.hpp>` /
`<belief_evaluation/double_dummy_bound.hpp>`) both take `SolverContext&`,
not owned — create, configure and outlive it yourself — and carry the same
two caveats as their Python counterparts above (fixed `declarer`;
trick-maximising, not contract-aware).

## Known gaps

Found by writing `examples/`, and recorded here rather than left in an
example's docstring. Each is a property of this implementation, not of the
approach.

### `DoubleDummyBound` can report a bound of −2, and the cut believes it

`DoubleDummyBound` asks the solver for `solutions = 1`. When the solver can
name the best card without searching it returns `nodes == 0` and
`score == -2`, meaning *not evaluated* rather than a trick count, with a
success status — so the bound returns −2 as though it were a real bound.
Being negative it is below any `tricks_needed`, so the bound-gated cut fires
on a live node.

Measured on a four-card ending, declarer playing low, over 70 layouts:

| configuration | `P_make` |
| --- | --- |
| no options | 0.2000 |
| `bound=` alone | 0.2000 |
| `delta_is_double_dummy_optimal=True` alone | 0.2000 |
| **both** | **0.0000** |
| the 70 layouts evaluated one at a time, averaged | 0.2000 |

So **pairing `DoubleDummyBound` with `DoubleDummyDefender` — the
configuration this document and `double_dummy_bound.hpp` both describe as
the intended one — can report 0.0 for a contract that makes.** Until it is
fixed, run without `bound`; the cost is pruning only, never soundness.

**Which positions trigger it is not established**, and an earlier version of
this section asserted a trigger that turned out to be wrong. What is pinned,
by `tests/belief_evaluation/double_dummy_bound_test.cpp`'s `KnownUnsoundness*`
tests, is the symptom and a refutation of the obvious explanations: a
no-search solve can also return a *correct* score, the same holdings score
correctly when only the seat on lead changes, a two-suit position also fails,
and a forced play does not. `solutions = 3` scores every affected position
correctly.

`DoubleDummyDefender` is unaffected: it reads card identity and `equals`
from the same result, not the score.

### Trick mechanics are not exposed to Python

`seat_on_play`, `legal_cards`, `play` and the trick-winner rule exist in
`src/belief_evaluation/trick.hpp` and are used by the evaluator, but are not
bound. A Python caller who needs to reach a mid-play root has to
re-implement all four — `examples/play_sequence.py` does exactly that. The
risk is not the duplication: a copy that disagrees with the evaluator's own
follow-suit or trick-winner rule produces a wrong *root*, and every number
computed from it is confidently about a different position.

### `ObservationState.first` is the root's leader, not the current trick's

`first` on `ObservationState` is the seat on lead at the root and never
moves. The leader of the trick in progress is
`state.known_holdings["first"]`, which is reassigned to the winner as each
trick resolves. They agree at the root, so a strategy that reaches for
`state.first` is right in testing and wrong in play. Related: an empty trick
has `current_trick_suit == (0, 0, 0)`, and spades are suit 0 — the rank
array, where 0 is the empty-slot sentinel, is what distinguishes "spades
were led" from "nobody has led".

### `Card` is unhashable

`bsle.Card` compares by value but has no `__hash__`, so it cannot be a set
member or a dict key. A caller memoising by played card, deduplicating a δ
distribution, or keying a transposition table has to convert to
`(suit, rank)` first.

### Never exercised by any example

`state_key` (never called — there is no cache), sampling and replenishment
(`sample_size` / `replenish_below`), and any `LayoutSource` narrower than
`ExhaustiveLayoutSource`.

## See also

- [`docs/replenished_belief_evaluation/algorithm.md`](replenished_belief_evaluation/algorithm.md) —
  the theory: the belief-space formulation, why it turns a POMDP into a
  belief MDP, and the derivation `P_make` implements.
- [`docs/module_map.md`](module_map.md) — which header holds what, and the
  conventions that hold across all of them. For changing the library
  rather than using it.
- [`specs/replenished-belief-evaluation.md`](../specs/replenished-belief-evaluation.md) —
  the design record: every capability-wide contract and invariant, with
  the reasoning behind each.
- The double-dummy solver proper, which this capability reaches only
  through the optional solver seam above, documents its own Python and C++
  interfaces in the [dds](https://github.com/dds-bridge/dds) repository.
