# The `belief_evaluation` module: map and conventions

Where each header sits on the path from a root position to `P_make`, and
the nine rules that hold across all of them. Read this once and the header
doxygen stops needing to restate it.

The other three documents, and what each is for:

- [`docs/belief_space_local_evaluation.md`](belief_space_local_evaluation.md)
  — the caller's guide, in both languages. Start there if you are using the
  library rather than changing it.
- [`specs/replenished-belief-evaluation.md`](../specs/replenished-belief-evaluation.md)
  — the design record: every capability-wide contract and invariant, and the
  reasoning behind each. Where a header says "see the spec", this is what it
  means.
- [`docs/replenished_belief_evaluation/algorithm.md`](replenished_belief_evaluation/algorithm.md)
  — the theory the whole capability implements.

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
| `validation.hpp` | `ValidationError` and the two callback-return checks |

### The solver seam

Separate build targets, so a caller supplying their own π and δ never links
the solver.

| Header | Holds |
| --- | --- |
| `double_dummy_defender.hpp` | a δ backed by `solve_board()` |
| `double_dummy_bound.hpp` | a `LayoutBound` backed by `solve_board()` |
| `spread.hpp` | `SpreadPolicy` and the pure, solver-free distribution over a solved position |

### Inside the recursion

| Header | Holds |
| --- | --- |
| `node.hpp` | `BeliefNode`, `make_root()` and its options, failures and outcomes |
| `belief_view.hpp` | the posterior view π reasons over |
| `expand.hpp` | one ply: `advance_state()`, declarer expansion, defender expansion |
| `replenishment.hpp` | the node-local top-up scan and the candidate replay behind it |

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
all caller input. A contract violation in any of them is reported through a
return value — `ValidationError`, `RootFailure`, `HistoryVerdict`,
`EvaluationError` — never thrown and never asserted. An internal invariant
failure (mass conservation off by more than tolerance) is the other
category, and asserts.

The Python surface converts each of these back into an exception at its own
boundary, because raising is what a Python caller expects; see the caller's
guide.

### 2. One rejection cause per fix

Where two failures would send a caller to change different things, they get
different enum values, even when the observable state is the same. Nothing
in this module collapses "your source has nothing consistent in it" into
"your scan budget ran out", or "your history does not fit this root" into
"this root has no layouts": each pair reads identically from the outside and
has a different repair.

### 3. Out-of-domain input stays defined without asserts

A function given an argument outside its domain asserts, *and* returns a
defined, harmless value in a build where the assert is compiled out — 0 for
a rank or suit out of range, a substituted safe value for a malformed card,
a prompt return rather than an unbounded loop. These are last-resort
defences against undefined behaviour, not a diagnostic: a caller relying on
one is getting a wrong answer quietly. The Python binding validates at its
own boundary instead, for exactly that reason.

### 4. Ranks on the surface are absolute

`Card::rank` is 2..14, always, never relative to a node's outstanding pool.
A strategy reasoning in relative terms converts with one
`RankMap::to_absolute` before returning a card. The low-packed bitmask
`renumber()` produces is internal to key construction and appears in no
callback signature.

Two bit conventions coexist underneath: `Deal::remainCards` sets bit *r* for
absolute rank *r*, while `Position::holding`, `legal_plays()` and
`trick_winner()` use bit *r*−2. `trick.hpp`, `rank_map.hpp` and
`layout_key.hpp` are the only places that cross between them.

### 5. `ObservationState::first` is the root's leader

Not the current trick's. Once play has moved on, the seat on play comes from
`seat_on_play()` against the layout as it stands, and the current trick's
own leader comes from that layout's `first` — never from `state.first`,
which has not moved since the root.

### 6. A node's outstanding pool is invariant; the defender seat is fixed

Every layout in one node shares trump, the trick in progress, declarer's and
dummy's exact holdings, and the same outstanding pool per suit. Layouts
differ only in how that pool splits between the two defenders.

**"Outstanding pool" names two different sets in this codebase**, and the
difference matters when reading either. `RankMap::aggr` is every card still in
any of the four hands; `DefenderPool` (`defender_split.hpp`) is the defenders'
cards alone. Both are invariant across a node. `aggr` also uses bit `r - 2`
where `Deal::remainCards` uses bit `r`. Three things
follow, relied on throughout:

- one renumbering is valid for a whole node, not one layout;
- anything derived from the pool — `RankMap::aggr`, `tricks_remaining()`,
  whether the node is terminal — can be read from a single representative
  layout;
- one defender's holding determines the other's by complement, so wherever a
  layout is identified by a defender's cards the seat is fixed at
  `(declarer + 1) % DDS_HANDS`. It must never vary between one computation
  and another, or the same layout hashes differently in each.

### 7. Identity is root-space

`BeliefNode::root_keys` holds each layout's key computed at `make_root` from
the root-space candidate, and carried unchanged from then on — never
recomputed at depth. Two root-space layouts differing only in cards that
have since been played replay forward to bit-identical `Deal`s, so a key
derived at a node would collide where the candidates do not.

`layout_key()` itself is node-local and not a global layout identity.

### 8. Enumeration order is load-bearing, and the only seed is the source's

`DefenderPool::cards` is canonically ordered (suits ascending, then ranks
ascending) and `unrank_combination()` is colexicographic. Both are fixed
choices, not defaults: a different order numbers the same subsets
differently, so changing either silently changes which layout every existing
index names.

The evaluator takes no seed anywhere. Sampling is a *prefix* of a source's
own order, so whatever randomness a sampled run has comes from the source —
`ExhaustiveLayoutSource` supplies it through `keyed_permutation()`, fixed at
construction.

### 9. Four things nothing here can validate

`state_key`'s coarseness, an injected trick-count bound, the declaration
that δ is double-dummy optimal for trick count, and a source presenting a
randomised order. Each is silently wrong rather than an error. The caller's
guide states all four and what each one costs when it is wrong:
[the four obligations nothing here can validate](belief_space_local_evaluation.md#the-four-obligations-nothing-here-can-validate).
