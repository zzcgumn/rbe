# The `belief_evaluation` module: map and conventions

Where each header sits on the path from a root position to `P_make`, and
the nine rules that hold across all of them. The header doxygen does not
repeat these rules.

The other three documents:

- [`docs/belief_space_local_evaluation.md`](belief_space_local_evaluation.md):
  the caller's guide, in Python and C++. Start there to use the library.
- [`specs/replenished-belief-evaluation.md`](../specs/replenished-belief-evaluation.md):
  the design record, with every contract and invariant and the reasons for
  them. "The spec" in a header means this document.
- [`docs/replenished_belief_evaluation/algorithm.md`](replenished_belief_evaluation/algorithm.md):
  the theory.

## Map

### What a caller touches

| Header | Holds |
| --- | --- |
| `evaluate.hpp` | `evaluate()` itself, `EvaluateOptions`, `SamplingOptions`, the result and error types, the three early cuts |
| `types.hpp` | `Card`, `ObservationState`, `BeliefView`, `RankMap`, the probability aliases |
| `declarer_strategy.hpp` | π: `DeclarerStrategy` (`id`, `play`, optional `state_key`) |
| `defender_strategy.hpp` | δ: `DefenderStrategy`, `DefenderQuery`, `WeightedCard` |
| `layout_source.hpp` | `LayoutSource`, the abstract enumeration seam |
| `exhaustive_layout_source.hpp` | the shipped source: every consistent split, randomised by seed, narrowed by a play history |
| `play_record.hpp` | `PlayRecord`: the cards played before the root, and who led first |
| `validation.hpp` | `ValidationError` and the two callback-return checks |

### The solver seam

Separate build targets, so a caller supplying their own π and δ never links
the solver.

| Header | Holds |
| --- | --- |
| `double_dummy_defender.hpp` | a δ backed by `solve_board()` |
| `double_dummy_bound.hpp` | a `LayoutBound` backed by `solve_board()` |
| `spread.hpp` | `SpreadPolicy`, `canonical_best()`, and the pure, solver-free distribution over a solved position |
| `heuristic_defender.hpp` | `HeuristicDefender`, a second δ backed by `solve_board()`. It asks a caller-built `DefenderHeuristicChain`, and falls back to `spread()` |
| `brute_force_declarer.hpp` | `BruteForceDeclarer`, a π that searches ahead with an opponent model, and its options |

### Heuristic defenders

The chain that `HeuristicDefender` asks, and the shipped rules. These are
solver-free: each rule reads a solved candidate set and never calls
`solve_board()`. They share the `defender_heuristic` build target.

| Header | Holds |
| --- | --- |
| `defender_heuristic.hpp` | `DefenderHeuristic`, `DefenderHeuristicContext` |
| `defender_heuristic_chain.hpp` | `DefenderHeuristicChain`, the caller-assembled, order-matters sequence |
| `second_seat_low.hpp`, `high_in_third.hpp`, `third_seat_low.hpp`, `fourth_seat_low.hpp`, `ruff_small.hpp`, `discard_keep_winners.hpp` | the six concrete rules |
| `touching_group.hpp` | `true_lowest_rank()`, shared by every rule above that resolves a collapsed touching group |
| `suit_top_tricks.hpp` | the discard-suit estimator `discard_keep_winners` reads |
| `default_defender_heuristics.hpp` | `make_default_defender_heuristics()`, a convenience builder. Nothing treats its chain specially |

### Inside the recursion

| Header | Holds |
| --- | --- |
| `node.hpp` | `BeliefNode`, `make_root()` and its options, failures and outcomes |
| `belief_view.hpp` | the posterior view π reasons over |
| `expand.hpp` | one ply: `advance_state()`, declarer expansion, defender expansion |
| `replenishment.hpp` | the node-local top-up scan and the candidate replay behind it |
| `brute_force_cache.hpp` | the cache key and transposition table of `BruteForceDeclarer` |

### Primitives

| Header | Holds |
| --- | --- |
| `trick.hpp` | `seat_on_play()`, `legal_cards()`, `play()`, and the two rank-bit-convention conversions |
| `position.hpp` | a minimal position, for stating and testing the renumbering isomorphism |
| `renumber.hpp`, `rank_map.hpp`, `layout_key.hpp` | the renumbering bijection, the outstanding-pool map, a node-local layout identity |
| `defender_split.hpp` | the defender pool, `binomial_coefficient()`, `unrank_combination()`, and applying a split back onto a `Deal` |
| `constrained_decomposition.hpp` | the same pool, partitioned by what known voids force |
| `void_derivation.hpp` | the voids a play history establishes |
| `history_verification.hpp` | whether a play history belongs to a root, and which check caught it if not |
| `keyed_permutation.hpp` | a seeded bijection on `[0, n)`, the randomised enumeration order |
| `kahan.hpp` | compensated summation |

## Conventions

### 1. Caller input is reported; internal invariants assert

A callback's return, a `LayoutSource`, a play history and a root layout are
all caller input. A contract violation in any of them is reported in a
return value: `ValidationError`, `RootFailure`, `HistoryVerdict` or
`EvaluationError`. It is never thrown out of `evaluate()` and never
asserted. An internal invariant failure, such as mass conservation off by
more than the tolerance, asserts.

The Python binding turns each of these into an exception, because a Python
caller expects one. See the caller's guide.

### 2. One rejection cause per fix

When two failures need different fixes, they get different enum values,
even when they look the same from outside. For example, "your source has
nothing consistent in it" is not "your scan budget ran out". "Your history
does not fit this root" is not "this root has no layouts".

### 3. Out-of-domain input stays defined without asserts

A function given an argument outside its domain asserts. It *also*
returns a defined, harmless value when asserts are compiled out: 0 for a
rank or suit out of range, a safe substitute for a malformed card, a prompt
return instead of an endless loop. These guard against undefined
behaviour. They are not diagnostics: **a caller who relies on one gets a
wrong answer quietly.** So the Python binding validates its input first.

### 4. Ranks on the surface are absolute

`Card::rank` is always 2..14, never relative to a node's outstanding
pool. A strategy that works with relative ranks converts with
`RankMap::to_absolute` before returning a card. The packed bitmask from
`renumber()` is internal to key construction and appears in no callback.

Two bit conventions coexist underneath: `Deal::remainCards` sets bit *r* for
absolute rank *r*, while `Position::holding`, `legal_plays()` and
`trick_winner()` use bit *r*−2. `trick.hpp`, `rank_map.hpp` and
`layout_key.hpp` are the only places that cross between them.

### 5. `ObservationState::first` is the root's leader

Not the current trick's. Once play has moved on, the seat on play comes
from `seat_on_play()` on the current layout. The current trick's leader is
that layout's `first`. **Never use `state.first` for either:** it has not
changed since the root.

### 6. A node's outstanding pool is invariant; the defender seat is fixed

Every layout in one node shares trump, the trick in progress, declarer's and
dummy's exact holdings, and the same outstanding pool per suit. Layouts
differ only in how that pool splits between the two defenders.

**"Outstanding pool" names two different sets.** `RankMap::aggr` is every
card still in any of the four hands. `DefenderPool` (`defender_split.hpp`)
is the defenders' cards alone. Both are the same across a node.

Three things follow:

- one renumbering is valid for a whole node, not one layout;
- anything derived from the pool, such as `RankMap::aggr`,
  `tricks_remaining()` or whether the node is terminal, can be read from
  any one layout;
- one defender's holding fixes the other's. So wherever a layout is
  identified by a defender's cards, the seat is
  `(declarer + 1) % DDS_HANDS`. **It must never vary**, or the same layout
  gets different keys.

### 7. Identity is root-space

`BeliefNode::root_keys` holds each layout's key, computed at `make_root`
from the root-space candidate. It is carried unchanged and **never
recomputed at depth.** Two root layouts that differ only in cards since
played replay to the same `Deal`. A key computed at the node would make
them collide.

`layout_key()` itself is node-local and not a global layout identity.

### 8. Enumeration order is load-bearing, and the only seed is the source's

`DefenderPool::cards` is ordered by suit, then rank, both ascending.
`unrank_combination()` is colexicographic. **Do not change either:** that
would silently change which layout every index names.

The evaluator takes no seed. Sampling takes a *prefix* of the source's
order, so all randomness comes from the source. `ExhaustiveLayoutSource`
gets it from `keyed_permutation()`, with the seed fixed at construction.

### 9. Four things nothing here can validate

The coarseness of `state_key`, an injected trick-count bound, the
declaration that δ is double-dummy optimal for trick count, and a source
in random order. **Each is silently wrong, not an error.** The caller's
guide gives all four and the cost of each:
[four obligations the library cannot check](belief_space_local_evaluation.md#four-obligations-the-library-cannot-check).
