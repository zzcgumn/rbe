# Belief Space Local Evaluation

## What this is

This library computes `P_make`, the probability that a contract makes.
It takes a declarer strategy π and a defender strategy δ. It averages
over a *belief space*: every layout consistent with what has been seen so
far.

Declarer play is a decision process over a hidden layout. The library
turns it into a search over the belief space. The theory is in
[`algorithm.md`](replenished_belief_evaluation/algorithm.md). This
document does not repeat it. It covers the terms and the entry points in
Python and C++.

### The vocabulary

These are the canonical terms. The rest of the documentation uses them.

- **Layout:** one complete deal. It says who holds every card.
- **Belief space:** the set of layouts declarer thinks possible. Each
  layout has a probability. At the root, all layouts are equally likely.
- **Layout source:** lists candidate layouts in a fixed order. The
  evaluator keeps the consistent ones. When it samples, it takes a prefix
  of that order.
- **π (declarer strategy):** chooses declarer's and dummy's cards.
- **δ (defender strategy):** chooses the defenders' cards. Both defenders
  use the same δ, with different information.
- **Sampling:** use only a prefix of the source order at the root. Use it
  when the full belief space is too large to hold in memory.
- **Replenishment:** add layouts from the same source to a node whose
  sample has shrunk below a threshold. It works per node. It adds only as
  many layouts as the source can still supply.
- **`P_make`:** the probability that the contract makes, under π against
  δ, over the belief space.

Both language surfaces use the same inputs: a root layout, a declarer
seat, a layout source, π, δ, and options for sampling and instrumentation.

## Four obligations the library cannot check

If you break one of these, you get no error. You get a wrong number that
looks plausible.

1. **`state_key` must not be too coarse.** π can declare a `state_key`.
   It names what `play` depends on, beyond the position the evaluator
   already keys on. It is for a future cache. A key that is too coarse
   gives a *wrong probability*, not a slow one. Nothing calls `state_key`
   yet, so do not optimise it. Write it correctly anyway: it is part of
   the contract.
2. **An injected trick bound must not be too low.** You can supply an
   upper bound on declarer's double-dummy tricks. The evaluator uses it to
   skip subtrees that cannot make. A bound that is too *low* makes the cut
   fire wrongly: it reports zero for a contract that makes. A bound that
   is too high only loses pruning. **The shipped `DoubleDummyBound` is
   safe.** When the solver will not score a position, it answers too high.
   See "Known gaps".
3. **Declare δ double-dummy optimal only if it is.** This declaration is
   separate from the bound. You may want a bound for instrumentation with
   a δ that does not qualify. The bound-gated cut is sound (`R <= DD`) only
   if the declaration is true. Nothing checks it.
4. **A layout source you write must return layouts in random order.**
   Sampling takes a prefix of the source order. A sorted or grouped order
   gives a biased sample, and nothing reports it. This is the easiest one
   to get wrong in Python: a subclass that returns `self._deals[i]` uses
   the order the list was built in. `ExhaustiveLayoutSource` meets this
   obligation, with a seeded shuffle. Write your own source only when you
   need a narrower belief space than the root gives, for example to apply
   the bidding. If you do, use a seeded shuffle too.

## Supply the play history

`ExhaustiveLayoutSource` takes an optional `PlayRecord`: a card sequence
and the opening leader. The type lets you omit it. **Omit it only if you
do not have it.**

After trick one, a belief space built without the history includes
**impossible layouts**. In them, a defender holds a suit they have shown
out of. This distorts the odds for **every** card in the pool, not only
that suit. Example: a defender holds `k` of a pool of `n` cards. Without
the history, they hold a given card with probability `k/n`. If they have
shown out of a suit with `h` cards left in the pool, the correct value is
`k/(n-h)`. Every decision at that root inherits the error.

A caller with no history cannot get a better answer. That limit is in the
input, not in the library.

Know which kind of information you have:

- **Fact:** a defender showed out of a suit, so holds none of it.
  `ExhaustiveLayoutSource` applies this when you pass a `PlayRecord`.
- **Inference:** the bidding suggests a defender is short in a suit. The
  library never infers this. Apply it with your own, narrower
  `LayoutSource`.

A play history does not cover inferences from the bidding. If you mix the
two up, your belief space is too wide, with no warning.

You can also pass a `PlayRecord` to `evaluate()`. That has a different
purpose. The source uses the history to narrow the belief space. π reads
the record from `state.play_record` to see what happened before the root.
Every played card is public, so this reveals nothing hidden.
`state.play_record` is not `state.history`: `history` holds only the
root's trick in progress and the cards played since the root. See the
doxygen on those fields.

## Python

```python
import belief_space_local_evaluation as bsle
```

For a complete example, see `examples/guess_6nt_belief_space.py`. It plays
a hand to a mid-play root, builds the belief space from the history, and
evaluates one declarer line against three defences. Run it with
`bazelisk run //examples:guess_6nt_belief_space`.

`python/tests/test_belief_space_local_evaluation_docs_examples.py` runs
each Python code block below. It gives the placeholder names (`root`,
`declarer`, `seed`, `tricks_needed`) concrete values. It does not run the
C++ blocks.

### The building blocks

- **`bsle.Card(suit, rank)`.** `suit` is `0=♠ 1=♥ 2=♦ 3=♣`. `rank` is
  **absolute**, `2..14`, never relative to a node's outstanding cards. π
  returns a `Card`, and δ returns weighted `Card`s. A `Card` compares by
  value and is hashable, so it can be a `dict` key or `set` member. It is
  also mutable. If you change a `Card` that is already a key, the lookup
  fails.
- **A deal** is a plain `dict`, in the shape `dds3` documents: `trump`,
  `first`, `remain_cards` (a 4×4 array of bitmasks, `[hand][suit]`),
  `current_trick_suit`, `current_trick_rank`.
- **`bsle.ObservationState`** is read-only. π's `play` receives it on
  every call. Stored fields:
  - `trump`, `first`, `declarer`, `tricks_needed`,
    `tricks_won_by_declarer`.
  - `history`: the `Card`s of the root's trick in progress, then those
    played since the root, in order.
  - `known_holdings`: a deal dict. Declarer's and dummy's entries are
    exact. Each defender entry is the **union** of both defenders' cards,
    not that defender's actual hand.
  - `ranks`: a `bsle.RankMap` with `.aggr`, `.to_relative(suit, rank)`
    and `.to_absolute(suit, ordinal)`. A strategy that reasons in relative
    ranks must convert with `to_absolute` before it returns a `Card`.

  `state_key` takes the same type. Nothing calls it yet (obligation 1).

  Seven more fields are computed on access, so a strategy pays only for
  what it reads:
  - `seat_on_play`: the seat this call is for.
  - `trick_leader`: the leader of the current trick. **Do not use
    `first` for this.** `first` is the root leader and never changes, so
    it is wrong from the second trick on.
  - `current_trick`: the `Card`s already played to this trick. Empty when
    leading.
  - `position_in_trick`: 0 when leading.
  - `legal_cards`: `Card`s with follow-suit applied, sorted by suit, then
    rank ascending.
  - `can_follow_led_suit`: false when leading.
  - `is_declaring_side`: true for declarer *or* dummy.
- **`bsle.BeliefView`** is what π reasons over. It has `entries` (a
  sequence of `BeliefEntry`, each with `.layout`, a deal dict, and
  `.posterior`), `is_sample` and `space_size`. **It is valid only during
  the call that received it.** After that call, any access raises
  `ExpiredBeliefViewError`. A `.layout` you already read stays valid. Do
  not store a `BeliefView`.

### A layout source

```python
source = bsle.ExhaustiveLayoutSource(root, declarer, seed)
```

`size()` and `at(i)` list every layout consistent with `root`. Pass a
`PlayRecord` to remove the layouts that the history rules out:

```python
record = bsle.PlayRecord([bsle.Card(0, 14), bsle.Card(1, 2)], opening_leader=0)
source = bsle.ExhaustiveLayoutSource(root, declarer, seed, record=record)
```

The constructor raises if it rejects the record. That happens when the
record does not belong to `root`, or leaves no legal layout. See "Where
Python is stricter than C++".

`bsle.SingleLayoutSource(layout)` holds exactly one layout: `size()` is 1
and `at(0)` is `layout`. Use it to evaluate one layout alone. For example,
check that `P_make` over a belief space equals the mean of `P_make` over
each layout. A single layout has only one order, so obligation 4 does not
apply.

For any other source, subclass `bsle.LayoutSource` and override
`size()` and `at(i)`:

```python
class MySource(bsle.LayoutSource):
    def size(self):
        return len(self._deals)

    def at(self, index):
        return self._deals[index]  # must already be in a randomised order
```

### Playing a hand out

Four free functions apply the same follow-suit and trick-winner rules as
the evaluator. They are bound from `src/belief_evaluation/trick.hpp`. Use
them to reach a mid-play root:

```python
seat = bsle.seat_on_play(deal)              # int, 0..3
legal = bsle.legal_cards(deal, seat)        # [Card, ...], follow-suit applied
winner = bsle.trick_complete_winner(deal, card)  # deal must carry 3 played cards
after = bsle.play(deal, card)               # a new deal; the one passed in is unchanged
```

`bsle.play_out(deal, history, opening_leader, declarer)` replays a whole
history. It returns `(root, tricks_won_by_declarer)`. It returns both so
that they cannot disagree. A wrong trick count evaluates a different
contract, with no warning.

- Dummy's tricks count for declarer.
- An incomplete last trick is replayed but counts for nobody.
- An illegal card raises `ValueError` with its index in `history`. This
  catches a card in the wrong place in a transcribed hand. It also catches
  an `opening_leader` that does not match the history.

`PlaySequence` wraps these functions:

```python
from belief_space_local_evaluation import PlaySequence

sequence = PlaySequence(deal, declarer=2, trump=4, level=6)  # South, notrump
sequence.play_trick("C6 C4 C9 CQ")
root, tricks_needed = sequence.current_deal, sequence.tricks_needed
```

It plays cards with `seat_on_play`, `legal_cards` and `play`. It computes
`tricks_needed` with `play_out`. It records the history and the opening
leader that `ExhaustiveLayoutSource` needs. So one object gives you all
three inputs to `evaluate()`, and they cannot disagree. The docstring in
`belief_space_local_evaluation/play_sequence.py` lists the full surface
(`play`, `play_trick`, `history`, `tricks_won_by_declarer`,
`format_tricks`).

`sequence.declarer_view()` shows a position safely outside `evaluate()`.
Use it to try a declarer heuristic, or to print what declarer knows. It
returns a small, frozen snapshot of what declarer may see: declarer's and
dummy's remaining cards, the current trick, the legal cards, the play
history, and more.

- **The snapshot never holds `sequence.current_deal`.** That deal holds
  all four hands, including the defenders'.
- **It raises `ValueError` when a defender is on play.** The legal cards
  would come from a defender's hand, which declarer cannot see.

### π and δ

This shows the shape only. The cards are fixed for illustration. They
are legal only at a node that holds exactly those cards. A real strategy
derives its card from `state` or `layout`.

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

**π and δ must be pure functions of their arguments.** The evaluator
visits nodes in its own order, and visits some more than once. A strategy
that keeps state between calls can return different cards for the same
node. That corrupts the result silently. The common case is a seeded
strategy that draws from one random stream for the whole search. For
variety, derive the choice from the arguments instead, for example
`choice = hash(seed, state) mod n`.

### `evaluate()`

```python
result = bsle.evaluate(root, declarer, tricks_needed, source, pi, delta)
```

Each field of the C++ `EvaluateOptions` is a keyword-only argument with
the same default: `retain_root`, `collect_counters`, `bound`,
`delta_is_double_dummy_optimal`, `sample_size`, `scan_budget`,
`replenish_below`. `state_key` is keyword-only too. In C++ it belongs to
the declarer strategy, but in Python π is a bare callable.

```python
result = bsle.evaluate(
    root, declarer, tricks_needed, source, pi, delta,
    sample_size=200, replenish_below=20)

value = result["by_strategy"][1]  # keyed by pi's strategy id, always 1 here
value["p_make"]                  # float
value["root_children"]           # [(Card, float), ...] -- see below
value["root_is_declaring_side"]  # bool -- which shape root_children has
```

`root_children` has three shapes:

- **At a declarer root**, the entries are alternatives. `p_make` equals
  the entry π chose, not the sum.
- **At a defender root**, the entries partition the mass. They sum to
  `p_make`.
- **At a terminal root, or when a cut fires at the root**, it is empty.

**Use `root_is_declaring_side` to tell which shape you have.** Do not
work it out from the root seat. It is true when declarer *or dummy* is on
lead, which is the part callers get wrong. It is present even when
`root_children` is empty.

`counters` is present only with `collect_counters=True`. `retained_root`
is present only with `retain_root=True`. It is **the root node only,
never a tree**. For their full shape, see
`test_belief_space_local_evaluation_results.py`.

### Exceptions

All exceptions derive from `bsle.BeliefSpaceLocalEvaluationError`. Each
`RootFailure` cause and each `ValidationError` cause has its own
exception.

A rejected play record raises from a constructor:

- `PlayRecord` raises for a bad shape or a duplicate card.
- `ExhaustiveLayoutSource` raises `HistoryRejectedError` or
  `ConstrainedSpaceEmptyError` for the other causes.

These are separate from `NoLayoutSurvivedError`, because the fix is
different. For a rejected record, fix the history. For
`NoLayoutSurvivedError`, fix the source.

An exception raised in π (`play`), in δ, or in a Python source's `size()`
or `at()` passes out of `evaluate()` unchanged. `state_key` cannot raise
here, because nothing calls it yet.

### The solver seam

```python
import dds3

ctx = dds3.SolverContext()
delta = bsle.DoubleDummyDefender(ctx)                    # usable directly as delta
bound = bsle.DoubleDummyBound(ctx, declarer)              # usable directly as bound

chain = bsle.make_default_defender_heuristics(trump)     # convenience: the shipped order
delta = bsle.HeuristicDefender(ctx, chain)                # also usable directly as delta
```

A `SolverContext` from `dds3` works here directly. The two modules share
the type.

**`DoubleDummyBound`:**

- It **fixes `declarer` when you construct it.** Do not reuse it for
  another declarer. The bound would be wrong, and the bound-gated cut
  would give a wrong answer.
- It **prunes nothing on positions the solver will not score.** That is
  safe but weaker than it looks. See "Known gaps".

**`DoubleDummyDefender`:**

- It **maximises tricks, not the contract.** It is not best defence
  against a contract. Sometimes it gives up the contract to hold the
  trick count down.
- It still satisfies `delta_is_double_dummy_optimal`, because that
  declaration is about trick count.
- `SpreadPolicy.TouchingSequence` spreads evenly over the touching
  sequence of the single best card. This is restricted choice.
- `SpreadPolicy.AllOptimal` spreads over the touching sequences of every
  card tied for best, across suits. Those cards are equally good but not
  equivalent. This is an advanced choice, not a drop-in alternative.

**`HeuristicDefender`** is a second δ. It does not replace
`DoubleDummyDefender`. Use either one as `evaluate()`'s `delta`. It
solves like `DoubleDummyDefender`. Then it passes the double-dummy-optimal
cards to a chain of rules: second hand low, third hand high or low,
fourth hand low, ruff small, and discards that keep winners. If every
rule defers, it falls back to the even spread of `DoubleDummyDefender`.

Why it exists: the even spread sometimes includes a card that no
competent defender would play. Tied cards score the same double-dummy.
But normal defensive technique picks one of them, for reasons that trick
counting cannot see.

**You build the chain: any order, any subset of the built-in rules, and
your own rules.** `make_default_defender_heuristics(trump,
randomize_touching_honors=True)` only builds the shipped order for
convenience. Nothing treats it as special. To build a chain by hand:

```python
chain = bsle.DefenderHeuristicChain()
chain.add(bsle.high_in_third())            # any order --
chain.add(bsle.second_seat_low(False))      # -- not the shipped one
chain.add(my_own_rule)                      # a plain Python function,
                                             # (context, best_cards) -> Card | None,
                                             # on equal footing with the built-ins
chain.add(bsle.discard_keep_winners())
```

`second_seat_low` has a `randomize_touching_honors` flag, default `True`.
It controls the restricted choice that `SpreadPolicy.TouchingSequence`
allows. With `False`, the rule always plays the lower of two touching
cards. That shows which card the defender held. When no touching pair
reaches the rule, both settings give the same result.

**Put `second_seat_low` after any other rule that can fire at the same
point.** The chain tries every later rule before the fallback spread. A
later rule that fires wins, whatever this flag says. See the doxygen in
`second_seat_low.hpp`.

**`BruteForceDeclarer`** is a lookahead π. Use it directly as
`evaluate()`'s `pi`:

```python
pi = bsle.BruteForceDeclarer(
    ctx, bsle.DeclarerObjective.MaximizeProbabilityToMake)   # opponent_model defaults to
                                                              # an owned DoubleDummyDefender
```

- At a declarer or dummy node, it tries every legal card. It keeps the
  best one for its `DeclarerObjective`: `MaximizeExpectedTricks` or
  `MaximizeProbabilityToMake`. The objective is fixed for the life of the
  instance.
- At a defender node, it sums the response of each surviving layout. It
  gets each response from its own `opponent_model`, a δ-shaped callable.
  That is separate from the `delta` you pass to `evaluate()`.
- `ctx` solves positions directly, through an internal
  `DoubleDummyBound`, when the search reaches `max_depth`. If you leave
  `opponent_model` unset, `ctx` also backs the default
  `DoubleDummyDefender`.

**It plays as "the double-dummy-optimal declarer" only when
`opponent_model` matches the outer `delta`.** With any other pairing,
`evaluate()` still returns a well-defined `P_make`, but only for that
pairing.

**An `opponent_model` you supply must answer from the position only.** It
must not read `state.history` or `state.play_record`, although an
ordinary δ may. A scripted defender that replays a fixed history is an
example that reads them. The internal cache key leaves those fields out,
so two `pi` calls on one line of play can share a value. Nothing checks
this at run time.

`max_depth` and `max_layouts` limit the search. Both default to `None`,
which searches to the end of play over the whole belief space. That is
affordable only on a small ending. On one ending with 252 layouts, the
unlimited search took over 100 seconds and 1.7 million calls to
`opponent_model`.

`max_layouts` keeps a node's highest-posterior layouts and drops the rest.
It drops them before the solver cost of a depth cutoff is paid. The
dropped mass is not given to the other layouts. So the result is a
deliberate under-count, never an over-count. The cap does not apply at a
terminal node. A terminal node calls no solver, so the cap would only
lose mass.

**The internal cache belongs to one instance.**

- With `max_depth` set to `None`, the cache is kept between `evaluate()`
  calls and never returns a wrong value. But hits between two calls are
  rare. Each call scales its root to sum to 1, and the search never
  rescales below that. So the same position usually has different values
  in two calls. A hit between calls happens only along a chain of
  probability-1 choices on both sides.
- Within one call, a position reached by two move orders still hits the
  cache.
- With `max_depth` set, a cutoff depends on how deep the current call
  searches. So each `play()` call starts with an empty cache. This costs
  little, because hits between calls are rare anyway.
- The objective and `opponent_model` are fixed per instance, so one cache
  never mixes them. Mixing them would corrupt the backed-up values.
- `evaluate()` cannot see the cache. The whole search happens inside one
  `play()` call.

**A `SolverContext` is not thread-safe.** Its C++ contract is one context
per thread. `DoubleDummyDefender`, `DoubleDummyBound` and
`HeuristicDefender` release the GIL during each solve.
`BruteForceDeclarer` releases it for its whole search. So, unlike most of
this module, two Python threads sharing a context really do run at the
same time. Create one `SolverContext`, and one of each defender, bound or
declarer, per worker thread. Do not share them between threads.

### Where Python is stricter than C++

In two places, Python reports an error where C++ does not. C++ is weaker
here only because the language forces it.

- **`replenish_below` without `sample_size`.** In C++ this does nothing,
  as documented. The field is inside a nested `SamplingOptions`, so the
  dependency is visible. Python flattens every option into a keyword
  argument, which hides the dependency. So Python raises `ValueError`.
- **A rejected play history.** In C++, you read the result from the
  `history_verdict()` and `constrained_space_status()` accessors, because a
  constructor cannot report it. In Python, the constructor raises for any
  verdict other than `Consistent`. A caller who never calls the accessors
  still finds out.

## C++

```cpp
#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/exhaustive_layout_source.hpp>

namespace be = dds::belief_evaluation;
```

The C++ types match the Python ones:

- `Card` (`suit`, `rank`, absolute).
- `ObservationState`, `BeliefView`, `BeliefEntry`.
- `LayoutSource`: an abstract class with `size()` and `at(index)`.
  Implement it directly.
- `ExhaustiveLayoutSource`.
- `DeclarerStrategy`: `id`, `play`, and an optional `state_key`.
- `DefenderStrategy`: a
  `std::function<std::vector<WeightedCard>(DefenderQuery const&)>`.
- `evaluate()`, which takes an `EvaluateOptions`. Its fields match the
  Python keyword arguments one for one. The three sampling fields are in a
  nested `sampling` struct.

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

`evaluate()` **reports** a contract violation by a callback in
`EvaluationResult::error`. It does not throw, because a callback is
caller input. The error holds `callback` (π, δ, or root construction),
`seat`, `layout`, and either `validation` or `root_failure`. A broken
internal invariant asserts instead. Python turns these reports into
exceptions, because raising is normal there.

`DoubleDummyDefender` (`<belief_evaluation/double_dummy_defender.hpp>`)
and `DoubleDummyBound` (`<belief_evaluation/double_dummy_bound.hpp>`)
take a `SolverContext&`. They do not own it. Create and configure it
yourself, and keep it alive longer than they are. The two caveats from
Python apply: the bound fixes `declarer`, and the defender maximises
tricks, not the contract.

## Known gaps

These are limits of this implementation, not of the approach.

### `DoubleDummyBound` cannot always get a score

On some positions, the solver returns `score[0] == -2` ("not evaluated")
with a success status. `DoubleDummyBound` checks the range of every
score. If a score is out of range, it asks again with `solutions = 3`.
That has scored every position measured so far. If the retry also fails,
the bound returns `SolverFailureSentinel` (14). That is higher than any
trick count, so it costs pruning but never correctness.

Results stay correct, and `DoubleDummyBound` with `DoubleDummyDefender` is
a sound pair. On the four-card ending in `examples/`, the pair cuts nodes
visited from 6283 to 3159, with 197 tier-2 cuts. It is also faster than
tier 1 alone.

Measured on a four-card ending, declarer playing low, over 70 layouts:

| configuration | `P_make` |
| --- | --- |
| no options | 0.2000 |
| `bound=` alone | 0.2000 |
| `delta_is_double_dummy_optimal=True` alone | 0.2000 |
| both | 0.2000 |
| the 70 layouts evaluated one at a time, averaged | 0.2000 |

**A wrong bound does not raise. It silently answers a different
question.** For example, a bound of 0 reports a contract that makes as
certain to fail. This is why a missing score must map to a value that is
too high. A value that is merely in range is not safe.

Which positions the solver declines to score is not known.
`tests/belief_evaluation/double_dummy_bound_test.cpp` pins the solver
behaviour and the guard. The doxygen in `double_dummy_bound.hpp` has the
details. `DoubleDummyDefender` is not affected, because it does not read
the score.

### Not used by any example

- `state_key`. Nothing calls it, because there is no cache.
- Sampling and replenishment (`sample_size`, `replenish_below`).

### Missing from the Python surface

**Bridge notation on `Card`.** Callers must convert a `Card` or a deal to
text such as `♠Q` or PBN themselves. The `bridge_notation` package does
most of this. A `__str__` and a parse/format pair on `Card` would remove
the need.

## See also

- [`algorithm.md`](replenished_belief_evaluation/algorithm.md): the
  theory. It covers the belief-space formulation, why it turns a POMDP
  into a belief MDP, and the derivation of `P_make`.
- [`module_map.md`](module_map.md): which header holds what, and the
  conventions for all headers. Read it to change the library.
- [`specs/replenished-belief-evaluation.md`](../specs/replenished-belief-evaluation.md):
  the design record. It holds every capability-wide contract and
  invariant, and the reason for each.
- The [dds](https://github.com/dds-bridge/dds) repository documents the
  double-dummy solver's Python and C++ interfaces. This library reaches
  the solver only through the solver seam.
