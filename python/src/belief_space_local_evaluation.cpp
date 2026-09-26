// The belief_space_local_evaluation extension module. Its own distinct
// import, not folded into dds3: this capability has its own vocabulary (a
// belief space, a layout source, replenishment) and a caller solving a
// board has no reason to import belief evaluation to do it.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <bit>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <api/dds_constants.hpp>
#include <belief_evaluation/belief_view.hpp>
#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/double_dummy_bound.hpp>
#include <belief_evaluation/double_dummy_defender.hpp>
#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/trick.hpp>
#include <belief_evaluation/exhaustive_layout_source.hpp>
#include <belief_evaluation/layout_source.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/spread.hpp>
#include <belief_evaluation/types.hpp>
#include <belief_evaluation/validation.hpp>
#include <dds3/converters.hpp>
#include <solver_context/solver_context.hpp>

#include "belief_converters.hpp"

namespace py = pybind11;
namespace be = dds::belief_evaluation;

namespace
{

// The one root every exception this module raises intentionally derives
// from -- a rejected history, a root construction failure, a callback
// contract violation. `except BeliefSpaceLocalEvaluationError` catches
// any of those without also catching a Python exception merely
// *propagating through* evaluate() from inside a callback: that keeps
// its own original type entirely (see evaluate()'s own comment on the
// two mechanisms), never becomes one of this hierarchy's members, and is
// not something this module's own exception design touches at all.
//
// Settled here, in one sitting, for all three of the mechanisms that
// converge on this module's error surface -- a history/constrained-space
// rejection (from a source's constructor), a RootFailure (from
// evaluate()'s own root construction), and a ValidationError (from
// evaluate() validating a callback's return) -- rather than accreting
// one exception at a time across unrelated tasks, which is how a
// hierarchy ends up with cousins that should have been siblings.
py::object belief_space_local_evaluation_error;

// Python's own `type(name, bases, namespace)` metaclass call.
// py::exception<> only supports a single base; two causes in this
// hierarchy need a second one (ValueError) alongside their own family's
// base, so existing `except ValueError` code keeps working alongside
// `except <the specific cause>`. The result is diamond-shaped (both
// bases eventually reach Exception), which Python's own C3
// linearisation resolves exactly as it would for a `class Foo(A, B):`
// statement written by hand.
auto make_exception_with_bases(py::module_& module, char const* name, py::tuple const& bases) -> py::object
{
    py::object const type_builtin = py::module_::import("builtins").attr("type");
    // py::exception<> sets __module__ to the extension itself
    // automatically; this hand-rolled path (needed at all only because
    // py::exception<> supports a single base) must do the same
    // explicitly, in the namespace dict passed to type() -- Python's own
    // type() does not infer it from the caller the way a `class` statement
    // does, and without this it defaults to whatever module the type()
    // call itself happens to be reached through (importlib's own bootstrap
    // machinery, observed directly), not this extension.
    py::dict ns;
    ns["__module__"] = module.attr("__name__");
    py::object const cls = type_builtin(name, bases, ns);
    module.attr(name) = cls;
    return cls;
}

auto register_root_exception_bindings(py::module_& module) -> void
{
    belief_space_local_evaluation_error = py::exception<void>(module, "BeliefSpaceLocalEvaluationError");
}

// The history representation used both ways: ObservationState::history comes
// out through history_to_list, and a caller-supplied history
// (ExhaustiveLayoutSource's constructor) goes in through list_to_history.
// Same representation both directions, so sequence -> PlayTraceBin ->
// sequence round-trips.

auto history_to_list(PlayTraceBin const& history) -> py::list
{
    py::list result;
    for (int i = 0; i < history.number; ++i) {
        result.append(be::Card{history.suit[i], history.rank[i]});
    }
    return result;
}

// Forward-declared: raises InvalidHistoryInputError (defined alongside
// history_rejected_exceptions, in register_history_error_bindings, well
// after this point in the file) with a caller-supplied detail message.
// list_to_history needs this rather than register_history_error_bindings'
// own generic raise_history_rejected(HistoryVerdict::InvalidInput): a
// malformed card names which index and which field, detail
// history_verdict_message's one fixed string for the whole verdict cannot
// carry.
[[noreturn]] auto raise_invalid_history_input(std::string const& detail) -> void;

// Validated at this boundary rather than left to the C++ side's own
// asserted-or-clamped defences: those exist as a last resort against
// undefined behaviour, not as a diagnostic, and a clamped card would
// silently change which suit or rank was played. A Python caller can build
// a malformed sequence far more easily than a C++ one can.
//
// Raises InvalidHistoryInputError, not a bare ValueError: this is exactly
// the shape of thing verify_history's own HistoryVerdict::InvalidInput
// names ("history is malformed... or some card's own suit/rank is out of
// range") when reached from ExhaustiveLayoutSource's constructor, and a
// caller catching that type around construction must not have a malformed
// *card* slip past it just because this earlier check caught it first
// instead of verify_history. InvalidHistoryInputError is itself a
// ValueError (see register_history_error_bindings), so this is a strict
// widening of what a caller who only wrote `except ValueError` already
// catches, not a narrowing.
auto list_to_history(py::sequence const& cards) -> PlayTraceBin
{
    constexpr std::size_t deck_size = DDS_SUITS * 13;

    if (cards.size() > deck_size) {
        raise_invalid_history_input(
            "history has " + std::to_string(cards.size()) +
            " cards (maximum " + std::to_string(deck_size) + ")");
    }

    PlayTraceBin result{};
    result.number = static_cast<int>(cards.size());
    for (std::size_t i = 0; i < cards.size(); ++i) {
        auto const& card = py::cast<be::Card const&>(cards[i]);
        if (card.suit < 0 || card.suit >= DDS_SUITS) {
            raise_invalid_history_input(
                "history[" + std::to_string(i) + "].suit has invalid value " +
                std::to_string(card.suit) + " (expected range 0.." +
                std::to_string(DDS_SUITS - 1) + ")");
        }
        if (card.rank < 2 || card.rank > 14) {
            raise_invalid_history_input(
                "history[" + std::to_string(i) + "].rank has invalid value " +
                std::to_string(card.rank) + " (expected range 2..14)");
        }
        result.suit[i] = card.suit;
        result.rank[i] = card.rank;
    }
    return result;
}

auto register_card_bindings(py::module_& module) -> void
{
    py::class_<be::Card>(
        module,
        "Card",
        "A single card. suit is 0=S, 1=H, 2=D, 3=C. rank is **absolute**,\n"
        "2..14 -- never relative to a node's outstanding-card pool. This is\n"
        "the type a declarer strategy's play() returns and a defender\n"
        "strategy's weighted cards carry; a strategy reasoning in relative\n"
        "ranks must convert to absolute before returning one.")
        .def(py::init<int, int>(), py::arg("suit"), py::arg("rank"))
        .def_readwrite("suit", &be::Card::suit)
        .def_readwrite("rank", &be::Card::rank)
        .def(
            "__eq__",
            [](be::Card const& self, be::Card const& other) {
                return self.suit == other.suit && self.rank == other.rank;
            })
        // Defining __eq__ alone makes a type unhashable -- a Python language
        // rule, not a pybind11 quirk -- so a Card could not be a dict key or a
        // set member, and a caller memoising by played card had to convert to
        // (suit, rank) first. Hashing that same tuple rather than combining the
        // fields by hand keeps "equal cards hash equal" true by construction
        // and stops the two drifting apart.
        //
        // Card is mutable, so this is a hashable mutable type: the hash follows
        // the fields, and mutating a Card already used as a key loses it. The
        // alternative -- an immutable Card -- would break the field assignment
        // this binding has always allowed.
        .def(
            "__hash__",
            [](be::Card const& self) {
                return py::hash(py::make_tuple(self.suit, self.rank));
            })
        .def("__repr__", [](be::Card const& self) {
            return "Card(suit=" + std::to_string(self.suit) +
                ", rank=" + std::to_string(self.rank) + ")";
        });
}

// A value type -- aggr plus two pure functions reading only it and static
// lookup tables, no ownership or per-call lifetime the way BeliefView has
// -- so bound the same way Card is: read-only, freely copyable, no
// validity flag needed. Read-only for the same reason ObservationState
// itself is: a strategy receives one, nothing constructs one from Python.
auto register_rank_map_bindings(py::module_& module) -> void
{
    py::class_<be::RankMap>(
        module,
        "RankMap",
        "The outstanding-card pool and the absolute/relative rank mapping\n"
        "over it, for one belief-evaluation node -- ObservationState.ranks.\n"
        "Layout-invariant across the node: every layout shares the same\n"
        "outstanding cards per suit, differing only in how the defenders'\n"
        "cards are split.")
        .def_property_readonly(
            "aggr",
            [](be::RankMap const& self) {
                return py::make_tuple(self.aggr[0], self.aggr[1], self.aggr[2], self.aggr[3]);
            },
            "Outstanding pool per suit, one unsigned int each -- the\n"
            "compacted convention (bit rank-2), not remain_cards' own\n"
            "(bit rank). Four-element tuple, indexed by suit.")
        .def(
            "to_relative", &be::RankMap::to_relative, py::arg("suit"), py::arg("rank"),
            "Absolute rank -> relative, 1 = highest, 0 if not outstanding\n"
            "(also 0, not an error, for a suit/rank out of range).")
        .def(
            "to_absolute", &be::RankMap::to_absolute, py::arg("suit"), py::arg("ordinal"),
            "Relative ordinal (1 = highest) -> absolute rank (also 0 for a\n"
            "suit/ordinal out of range). The conversion play() needs before\n"
            "returning a Card, if it reasoned in relative terms -- Card's\n"
            "own rank is always absolute.");
}

auto register_observation_state_bindings(py::module_& module) -> void
{
    // Read-only: a strategy receives one, nothing constructs one from
    // Python. Every field ObservationState defines that a declarer strategy
    // may condition on directly.
    py::class_<be::ObservationState>(
        module,
        "ObservationState",
        "The commonly-known part of a belief-evaluation node, handed to a\n"
        "declarer strategy's play() at every call (and, in the type's own\n"
        "shape, to state_key() too -- but state_key() is never actually\n"
        "called yet, there is no cache to key): identical across every\n"
        "layout of the current belief space.")
        .def_property_readonly(
            "trump", [](be::ObservationState const& self) { return self.trump; })
        .def_property_readonly(
            "first", [](be::ObservationState const& self) { return self.first; },
            "Seat on lead at the root.")
        .def_property_readonly(
            "history", [](be::ObservationState const& self) { return history_to_list(self.history); },
            "Every card played so far, in order, as a list of Card.")
        .def_property_readonly(
            "declarer", [](be::ObservationState const& self) { return self.declarer; },
            "Seat; dummy is (declarer + 2) % 4.")
        .def_property_readonly(
            "tricks_needed", [](be::ObservationState const& self) { return self.tricks_needed; })
        .def_property_readonly(
            "tricks_won_by_declarer",
            [](be::ObservationState const& self) { return self.tricks_won_by_declarer; })
        .def_property_readonly(
            "known_holdings",
            [](be::ObservationState const& self) { return dds3_python::deal_to_dict(self.known_holdings); },
            "A deal dict. declarer's and dummy's entries are exact; a\n"
            "defender's entry is the **union pool** of both defenders'\n"
            "outstanding cards, not that defender's own actual holding.")
        // --- derived, not stored: computed on access from known_holdings.
        //
        // Every one of these is something a strategy would otherwise work out
        // for itself on every call, and two of them are the derivations that
        // are wrong in the obvious way. Computed here rather than eagerly in
        // the binding because pi is called thousands of times for even a small
        // ending (3487 for a four-card one, measured) and most calls read one
        // or two of these, not all seven.
        .def_property_readonly(
            "seat_on_play",
            [](be::ObservationState const& self) { return be::seat_on_play(self.known_holdings); },
            "The seat (0..3) whose card this call is being asked for.\n\n"
            "A strategy is not told its seat any other way.")
        .def_property_readonly(
            "trick_leader",
            [](be::ObservationState const& self) { return self.known_holdings.first; },
            "The seat that led to the trick **in progress**.\n\n"
            "Not `first`, which is the seat on lead at the *root* and never\n"
            "moves for the whole evaluation. This one is reassigned to the\n"
            "winner every time a trick resolves. They agree at the root and\n"
            "diverge from the second trick on, so a strategy that reaches for\n"
            "`first` is right in testing and wrong in play.")
        .def_property_readonly(
            "current_trick",
            [](be::ObservationState const& self) {
                py::list cards;
                for (int i = 0; i < 3; ++i) {
                    // Rank 0 is the empty-slot sentinel. It is the *rank*
                    // array that says whether a slot is filled: suit 0 is
                    // spades, so a zeroed suit array is indistinguishable
                    // from a spade lead.
                    if (self.known_holdings.currentTrickRank[i] == 0) {
                        break;
                    }
                    cards.append(be::Card{self.known_holdings.currentTrickSuit[i],
                                          self.known_holdings.currentTrickRank[i]});
                }
                return cards;
            },
            "The cards already played to the trick in progress, in play\n"
            "order. **Empty means this seat is leading.**\n\n"
            "These are in nobody's known_holdings: a card leaves the hand\n"
            "that played it as it is played, and the trick in progress lives\n"
            "only here.")
        .def_property_readonly(
            "position_in_trick",
            [](be::ObservationState const& self) {
                int played = 0;
                while (played < 3 && self.known_holdings.currentTrickRank[played] != 0) {
                    ++played;
                }
                return played;
            },
            "0 when leading, 1..3 otherwise -- how many cards are already on\n"
            "the trick.")
        .def_property_readonly(
            "legal_cards",
            [](be::ObservationState const& self) {
                return be::enumerate_legal_cards(self.known_holdings,
                                                 be::seat_on_play(self.known_holdings));
            },
            "Every Card the seat on play may legally return, follow-suit rule\n"
            "already applied. Ordered suit ascending, then rank within a\n"
            "suit.\n\n"
            "A card from this list is legal in every layout of the node, not\n"
            "merely this one: the follow-suit rule reads only the seat's own\n"
            "holding, which is common knowledge for declarer and dummy.")
        .def_property_readonly(
            "can_follow_led_suit",
            [](be::ObservationState const& self) {
                if (self.known_holdings.currentTrickRank[0] == 0) {
                    return false;  // nothing led: there is no suit to follow
                }
                int const led = self.known_holdings.currentTrickSuit[0];
                int const seat = be::seat_on_play(self.known_holdings);
                return self.known_holdings.remainCards[seat][led] != 0;
            },
            "Whether the seat on play holds any card of the suit led.\n\n"
            "**False when leading**, there being no suit to follow -- so\n"
            "`if not state.can_follow_led_suit` reads as \"I am free to play\n"
            "anything\", which is true both when leading and when void.")
        .def_property_readonly(
            "is_declaring_side",
            [](be::ObservationState const& self) {
                int const seat = be::seat_on_play(self.known_holdings);
                int const dummy = (self.declarer + 2) % DDS_HANDS;
                return seat == self.declarer || seat == dummy;
            },
            "Whether the seat on play is declarer **or dummy**. A declarer\n"
            "strategy plays for both, so this is true at every play() call;\n"
            "it is meaningful for a strategy shared between the two sides.")
        .def_property_readonly(
            "ranks", [](be::ObservationState const& self) { return self.ranks; },
            "A RankMap -- the precomputed absolute/relative rank mapping\n"
            "over this node's outstanding pool, the same one a C++ strategy\n"
            "conditions on directly. A fresh copy per access, like every\n"
            "other property here; RankMap is a plain value with no\n"
            "lifetime of its own to protect.");
}

// BeliefView holds a std::span into caller-owned scratch and node.layouts,
// live only for the call it was built for -- see make_belief_view's own
// doxygen. A Python strategy is handed one at every declarer node; Python
// users store things ("self.last_view = view" is an entirely ordinary
// thing to write), and a naive non-owning binding would hand out an object
// reading freed memory the moment the callback returns, silently, with
// plausible-looking values.
//
// The fix is a validity flag the *binding* owns (nothing in src/
// changes for this): a shared_ptr<bool>, one per call, set false when the
// callback returns -- including when it raises, which is why invalidation
// is scope-bound (BeliefViewInvalidator's destructor) rather than placed on
// a success-only path. Every accessor checks it first.
//
// The half a naive implementation misses: invalidating the view object
// itself does not save `layout = view.entries[0].layout` stashed past the
// callback, if entries hand out something that itself references node
// memory. PyBeliefEntry carries the same shared flag as its view, so
// accessing a stashed *entry*'s properties after expiry raises too -- but
// `layout` itself, once read, is dict_to_deal's ordinary materialised
// copy: a Deal is small (a handful of ints and a 4x4 array), so copying it
// per access is affordable in exactly the way copying the whole belief set
// per node is not ("no copy of the belief set" is about that, not about a
// single Deal), and a dict already obtained while the view was live needs
// no flag of its own -- it is a plain value from then
// on, not a reference to anything.
// Set once, in register_belief_view_bindings, before anything can raise it.
py::object expired_belief_view_error;

[[noreturn]] auto raise_expired_belief_view() -> void
{
    py::set_error(
        expired_belief_view_error,
        "this BeliefView (or an entry obtained from it) has expired -- it is "
        "valid only for the duration of the callback it was handed to");
    throw py::error_already_set();
}

// One place for the "valid until the callback returns" pattern, used by
// every callback kind that hands a BeliefView to Python (the probe below,
// and both of pi's own callbacks) -- not scattered across each one, so a
// future callback added here cannot forget to invalidate on the way out.
// Scope-bound rather than a success-only clear: the destructor runs
// whether the callback returned normally or raised.
struct BeliefViewGuard
{
    std::shared_ptr<bool> valid = std::make_shared<bool>(true);

    ~BeliefViewGuard()
    {
        *valid = false;
    }
};

class PyBeliefEntry
{
public:
    PyBeliefEntry(be::BeliefEntry const* entry, std::shared_ptr<bool> valid)
        : entry_(entry), valid_(std::move(valid))
    {
    }

    auto layout() const -> py::dict
    {
        check_valid();
        return dds3_python::deal_to_dict(entry_->layout);
    }

    auto posterior() const -> be::Probability
    {
        check_valid();
        return entry_->posterior;
    }

private:
    auto check_valid() const -> void
    {
        if (! valid_ || ! *valid_) {
            raise_expired_belief_view();
        }
    }

    be::BeliefEntry const* entry_;
    std::shared_ptr<bool> valid_;
};

class PyBeliefView
{
public:
    PyBeliefView(be::BeliefView const* view, std::shared_ptr<bool> valid)
        : view_(view), valid_(std::move(valid))
    {
    }

    auto entries() const -> py::list
    {
        check_valid();
        py::list result;
        for (be::BeliefEntry const& entry : view_->entries) {
            result.append(PyBeliefEntry(&entry, valid_));
        }
        return result;
    }

    auto is_sample() const -> bool
    {
        check_valid();
        return view_->is_sample;
    }

    auto space_size() const -> std::size_t
    {
        check_valid();
        return view_->space_size;
    }

private:
    auto check_valid() const -> void
    {
        if (! valid_ || ! *valid_) {
            raise_expired_belief_view();
        }
    }

    be::BeliefView const* view_;
    std::shared_ptr<bool> valid_;
};

auto register_belief_view_bindings(py::module_& module) -> void
{
    expired_belief_view_error =
        py::exception<void>(module, "ExpiredBeliefViewError", belief_space_local_evaluation_error);

    py::class_<PyBeliefEntry>(
        module,
        "BeliefEntry",
        "One layout in a belief view, paired with its normalised posterior\n"
        "(the entries of one BeliefView sum to 1). Valid only while the\n"
        "BeliefView it came from is: accessing either property after the\n"
        "callback that received the view returns raises ExpiredBeliefViewError.")
        .def_property_readonly("layout", &PyBeliefEntry::layout, "A deal dict, freshly copied on every access.")
        .def_property_readonly("posterior", &PyBeliefEntry::posterior);

    py::class_<PyBeliefView>(
        module,
        "BeliefView",
        "What a declarer strategy reasons over: the belief space as\n"
        "declarer currently knows it, handed to play() at every call (and,\n"
        "in the type's own shape, to state_key() too -- but state_key() is\n"
        "never actually called yet, there is no cache to key). Valid only\n"
        "for the duration of that one call --\n"
        "accessing any property afterwards raises ExpiredBeliefViewError,\n"
        "and so does accessing an entry obtained from it (a *layout*\n"
        "already read from an entry is a plain copy and remains valid\n"
        "forever; the entry object itself is not). Storing this view, or\n"
        "anything obtained from it apart from an already-read layout,\n"
        "beyond the call it was handed to is always a mistake.")
        .def_property_readonly("entries", &PyBeliefView::entries)
        .def_property_readonly(
            "is_sample", &PyBeliefView::is_sample,
            "False only when this node holds the whole remaining belief\n"
            "space rather than a sample of it.")
        .def_property_readonly(
            "space_size", &PyBeliefView::space_size,
            "Layouts believed consistent, if known; 0 when is_sample is\n"
            "true -- the true count is genuinely unknown then, and\n"
            "reporting len(entries) instead would hand a strategy a false\n"
            "certainty. len(entries) is always the count actually being\n"
            "reasoned over, sampled or not.");
}

// Internal: stands in for the real caller (a later task's evaluate(),
// which hands a BeliefView to a Python declarer strategy at every node)
// so the guard above can be tested now. Builds a small BeliefNode from
// the given layouts/weights, calls callback with the resulting view, and
// invalidates it on the way out -- exactly the lifetime evaluate() will
// eventually give a real one, just constructed by hand here instead of by
// the search. Testing support only.
auto probe_with_belief_view(
    py::list const& layout_dicts, py::list const& weights, bool is_sample, py::function const& callback)
    -> py::object
{
    if (layout_dicts.size() != weights.size()) {
        throw py::value_error("layouts and weights must have the same length");
    }

    be::BeliefNode node;
    node.is_sample = is_sample;
    for (std::size_t i = 0; i < layout_dicts.size(); ++i) {
        node.layouts.push_back(dds3_python::dict_to_deal(py::cast<py::dict>(layout_dicts[i])));
        node.p.push_back(py::cast<be::Probability>(weights[i]));
        node.root_keys.push_back(0);
    }

    std::vector<be::BeliefEntry> scratch;
    be::BeliefView const view = be::make_belief_view(node, scratch);

    BeliefViewGuard guard;
    return callback(py::cast(PyBeliefView(&view, guard.valid)));
}

// pi and delta as Python callables, and the GIL discipline the whole
// evaluate() binding (below) runs on: the GIL is released for the
// duration of the C++ recursion and acquired in every trampoline that
// calls back into Python -- these two, and the layout source's
// size()/at() (already GIL-acquiring). One acquire site per callback
// *kind*, here, rather than one per call site scattered through the
// recursion: a callback kind that forgets to acquire is a callback kind
// that corrupts the interpreter the first time two threads exercise it,
// and there must be exactly one place per kind that could get this wrong.
//
// A Python pi/delta author can reach for `random.choice` without a second
// thought; DeclarerStrategy::play's own doxygen is emphatic that play
// must be a pure function of its arguments (state, view) alone, since the
// evaluator revisits sibling subtrees and a seeded strategy's card would
// then depend on how many decisions preceded it in traversal order rather
// than on the node itself -- silently wrong under any cut or future
// cache. `hash(seed, state) mod n` is the honest way to get variety
// without breaking purity.
auto make_declarer_strategy(
    be::StrategyId id, py::function const& play, std::optional<py::function> const& state_key)
    -> be::DeclarerStrategy
{
    be::DeclarerStrategy strategy;
    strategy.id = id;
    strategy.play = [play](be::ObservationState const& state, be::BeliefView const& view) -> be::Card {
        py::gil_scoped_acquire gil;
        BeliefViewGuard guard;
        py::object const result =
            play(py::cast(state, py::return_value_policy::copy), py::cast(PyBeliefView(&view, guard.valid)));
        return py::cast<be::Card>(result);
    };
    if (state_key.has_value()) {
        py::function const key_fn = *state_key;
        strategy.state_key = [key_fn](be::ObservationState const& state, be::BeliefView const& view) -> be::StateKey {
            py::gil_scoped_acquire gil;
            BeliefViewGuard guard;
            py::object const result = key_fn(
                py::cast(state, py::return_value_policy::copy), py::cast(PyBeliefView(&view, guard.valid)));
            return std::string(py::cast<py::bytes>(result));
        };
    }
    // Unset (state_key stays its default-constructed empty std::function)
    // when no Python callable was supplied -- DeclarerStrategy::state_key's
    // own doxygen: "unset disables reuse for this strategy entirely".
    return strategy;
}

// delta receives (layout: dict, seat: int, state: ObservationState) rather
// than a single bundled object: DefenderQuery holds Deal const& and
// ObservationState const&, references with no lifetime a Python object
// could safely carry past this call, and there is nothing here worth a
// dedicated bound type for -- three positional arguments, in the same
// flattened spirit the whole of this module's options surface takes.
//
// Every returned card must be held by seat in layout and legal there;
// probabilities strictly positive and summing to one within tolerance. A
// card the strategy will never play must be **omitted**, not given zero
// probability -- the evaluator treats "probability > 0" as the survival
// test for a layout, so a zero-probability entry keeps a layout alive
// with no mass, which is not the same thing as leaving it out. This is
// the single most likely way a Python delta is subtly wrong; validated
// server-side by validate_defender_distribution, not re-derived here.
auto make_defender_strategy(py::function const& delta) -> be::DefenderStrategy
{
    return [delta](be::DefenderQuery const& query) -> std::vector<be::WeightedCard> {
        py::gil_scoped_acquire gil;
        py::object const result = delta(
            dds3_python::deal_to_dict(query.layout),
            query.seat,
            py::cast(query.state, py::return_value_policy::copy));

        std::vector<be::WeightedCard> weighted;
        for (py::handle const item : py::cast<py::sequence>(result)) {
            py::sequence const pair = py::cast<py::sequence>(item);
            if (pair.size() != 2) {
                throw py::value_error(
                    "each defender distribution entry must be a (card, probability) pair");
            }
            weighted.push_back(be::WeightedCard{py::cast<be::Card>(pair[0]), py::cast<double>(pair[1])});
        }
        return weighted;
    };
}

auto register_strategy_error_bindings(py::module_& module) -> void
{
    py::enum_<be::ValidationError>(module, "ValidationError")
        .value("None_", be::ValidationError::None)
        .value("CardNotHeld", be::ValidationError::CardNotHeld)
        .value("CardIllegalForTrick", be::ValidationError::CardIllegalForTrick)
        .value("ProbabilityNonPositive", be::ValidationError::ProbabilityNonPositive)
        .value("ProbabilitiesDoNotSumToOne", be::ValidationError::ProbabilitiesDoNotSumToOne)
        .value("DistributionEmpty", be::ValidationError::DistributionEmpty);

    py::enum_<be::EvaluationCallback>(module, "EvaluationCallback")
        .value("RootConstruction", be::EvaluationCallback::RootConstruction)
        .value("DeclarerPlay", be::EvaluationCallback::DeclarerPlay)
        .value("DefenderStrategy", be::EvaluationCallback::DefenderStrategy);

    py::enum_<be::RootFailure>(module, "RootFailure")
        .value("None_", be::RootFailure::None)
        .value("SourceNotEnumerable", be::RootFailure::SourceNotEnumerable)
        .value("NoLayoutSurvived", be::RootFailure::NoLayoutSurvived)
        .value("ScanBudgetExhausted", be::RootFailure::ScanBudgetExhausted)
        .value("SampleSizeZero", be::RootFailure::SampleSizeZero);
}

// bound (EvaluateOptions::bound) is a Python callable taking a deal dict
// and returning a trick count, called under the same GIL discipline as
// every other callback. Carries an obligation nothing here can validate:
// a bound too *low* makes tier2_dead() fire when it should not and
// silently reports zero for a contract that makes; too high only loses
// pruning, never soundness. Absent by default, which leaves the cut
// disabled -- an empty std::function, matching LayoutBound's own
// doxygen.
auto make_layout_bound(py::object const& bound) -> be::LayoutBound
{
    if (bound.is_none()) {
        return {};
    }
    py::function const bound_fn = py::cast<py::function>(bound);
    return [bound_fn](Deal const& layout) -> int {
        py::gil_scoped_acquire gil;
        return py::cast<int>(bound_fn(dds3_python::deal_to_dict(layout)));
    };
}

std::map<be::RootFailure, py::object> root_failure_exceptions;

auto register_root_failure_error_bindings(py::module_& module) -> void
{
    py::exception<void> root_failure_base(
        module, "RootFailureError", belief_space_local_evaluation_error);

    root_failure_exceptions.emplace(
        be::RootFailure::SourceNotEnumerable,
        py::exception<void>(module, "SourceNotEnumerableError", root_failure_base));
    // NoLayoutSurvived vs ScanBudgetExhausted: the first means the whole
    // source was checked and rejected everything (fix your source); the
    // second means the budget ran out before a single consistent layout
    // was found, with the source not yet exhausted (raise the budget
    // instead). RootFailure's own doxygen says collapsing them "would
    // send them to debug the wrong thing" -- kept as two exception types
    // here for the same reason.
    root_failure_exceptions.emplace(
        be::RootFailure::NoLayoutSurvived,
        py::exception<void>(module, "NoLayoutSurvivedError", root_failure_base));
    root_failure_exceptions.emplace(
        be::RootFailure::ScanBudgetExhausted,
        py::exception<void>(module, "ScanBudgetExhaustedError", root_failure_base));

    // sample_size=0 is a directly out-of-range argument value -- the same
    // class of thing bindings.cpp's own "trump has invalid value 5"
    // raises as -- unlike the two above, which are about the *source's
    // content*, not a malformed literal, so this is the one RootFailure
    // that also derives from ValueError. Distinct from NoLayoutSurvived
    // on purpose (see RootFailure::SampleSizeZero's own doxygen): a
    // request for a sample of nothing is rejected before a single at()
    // call, regardless of what the source holds.
    py::object const sample_size_zero_error = make_exception_with_bases(
        module, "SampleSizeZeroError",
        py::make_tuple(root_failure_base, py::reinterpret_borrow<py::object>(PyExc_ValueError)));
    root_failure_exceptions.emplace(be::RootFailure::SampleSizeZero, sample_size_zero_error);
}

auto root_failure_message(be::RootFailure failure) -> std::string
{
    switch (failure) {
    case be::RootFailure::SourceNotEnumerable:
        return "source.size() returned None -- every LayoutSource must report a size";
    case be::RootFailure::NoLayoutSurvived:
        return "the whole source was scanned, but no candidate was consistent with root";
    case be::RootFailure::ScanBudgetExhausted:
        return "scan_budget ran out before a single consistent layout was found, "
               "with the source not yet exhausted -- raise scan_budget";
    case be::RootFailure::SampleSizeZero:
        return "sample_size=0 requests a sample of nothing, rejected before "
               "source is ever scanned";
    case be::RootFailure::None:
        break;
    }
    return "root construction failed";
}

[[noreturn]] auto raise_root_failure(be::RootFailure failure) -> void
{
    py::set_error(root_failure_exceptions.at(failure), root_failure_message(failure).c_str());
    throw py::error_already_set();
}

std::map<be::ValidationError, py::object> validation_error_exceptions;

auto register_validation_error_bindings(py::module_& module) -> void
{
    py::exception<void> callback_contract_error(
        module, "CallbackContractError", belief_space_local_evaluation_error);

    auto const add = [&](be::ValidationError cause, char const* name) {
        validation_error_exceptions.emplace(
            cause, py::exception<void>(module, name, callback_contract_error));
    };
    add(be::ValidationError::CardNotHeld, "CardNotHeldError");
    add(be::ValidationError::CardIllegalForTrick, "CardIllegalForTrickError");
    add(be::ValidationError::ProbabilityNonPositive, "ProbabilityNonPositiveError");
    add(be::ValidationError::ProbabilitiesDoNotSumToOne, "ProbabilitiesDoNotSumToOneError");
    add(be::ValidationError::DistributionEmpty, "DistributionEmptyError");
}

auto validation_error_message(be::ValidationError cause) -> std::string
{
    switch (cause) {
    case be::ValidationError::CardNotHeld:
        return "the card is not in the seat's remaining holding";
    case be::ValidationError::CardIllegalForTrick:
        return "the seat holds the led suit but the card is of another suit";
    case be::ValidationError::ProbabilityNonPositive:
        return "a returned probability is <= 0, NaN, or +-infinite";
    case be::ValidationError::ProbabilitiesDoNotSumToOne:
        return "the distribution's probabilities do not sum to 1 within tolerance";
    case be::ValidationError::DistributionEmpty:
        return "the distribution is empty -- a card the strategy will never "
               "play must be omitted, not given zero probability, but at "
               "least one card must remain";
    case be::ValidationError::None:
        break;
    }
    return "callback contract violated";
}

// Carries which callback, which seat, and the layout -- the context
// EvaluationError already holds -- as instance attributes rather than
// through a custom __init__: the exception type is called with just the
// message (inheriting Exception's own __init__, so args/str() work
// exactly as any other exception's do), then the three extra fields are
// set directly on that one instance before it is raised. The cause
// itself is not repeated as an attribute -- the exception's own type
// already names it, the same choice the history/root-failure families
// make.
[[noreturn]] auto raise_validation_error(
    be::EvaluationCallback callback, int seat, Deal const& layout, be::ValidationError cause) -> void
{
    py::object const exception_type = validation_error_exceptions.at(cause);
    py::object const instance = exception_type(validation_error_message(cause));
    instance.attr("callback") = py::cast(callback);
    instance.attr("seat") = seat;
    instance.attr("layout") = dds3_python::deal_to_dict(layout);
    py::set_error(exception_type, instance);
    throw py::error_already_set();
}

// The entry point: the first place a Python caller can evaluate anything.
// The six positional arguments are the call's actual subject, always
// supplied; every EvaluateOptions field crosses as a keyword-only
// argument (py::kw_only()) with its C++ default, so a caller cannot
// accidentally pass sample_size into bound's position, and the signature
// can grow later without breaking anyone. EvaluateOptions itself is not
// bound -- binding both shapes would give a caller two ways to say the
// same thing and guarantee they diverge.
//
// state_key is not an EvaluateOptions field (it belongs to
// DeclarerStrategy, alongside play), but pi crosses as a bare callable
// rather than a bundled object, so it is exposed as its own keyword-only
// argument here instead. bytes-returning and pure, like play; see
// make_declarer_strategy's own comment for both obligations.
//
// **This is the one place (with replenish_below below) the Python surface
// is deliberately stricter than the C++ one.** sample_size=0 raises
// SampleSizeZeroError rather than returning a result whose error names
// the same cause -- a later task designs the full exception hierarchy
// this joins; the routing here is only the one cause this task's own
// scope requires.
auto evaluate(
    py::dict const& root,
    int declarer,
    int tricks_needed,
    be::LayoutSource const& source,
    py::function const& pi,
    py::function const& delta,
    bool retain_root,
    bool collect_counters,
    py::object const& bound,
    bool delta_is_double_dummy_optimal,
    std::optional<std::uint64_t> const& sample_size,
    std::optional<std::uint64_t> const& scan_budget,
    std::optional<std::uint64_t> const& replenish_below,
    py::object const& state_key) -> py::dict
{
    // Checked here, unconditionally, before declarer ever reaches the C++
    // evaluator: make_root() indexes remainCards[declarer] (and, via
    // dummy = (declarer + 2) % DDS_HANDS, remainCards[dummy] too) with no
    // range check of its own -- src/ has no RootFailure cause for
    // this, since it is not a runtime condition on a valid root, it is a
    // malformed argument, the same class of thing ExhaustiveLayoutSource's
    // own constructor pre-checks declarer/opening_leader for before ever
    // reaching derive_voids.
    if (declarer < 0 || declarer >= DDS_HANDS) {
        throw py::value_error(
            "declarer has invalid value " + std::to_string(declarer) + " (expected range 0.." +
            std::to_string(DDS_HANDS - 1) + ")");
    }

    // already_made() is tricks_won_by_declarer >= tricks_needed, and
    // tricks_won_by_declarer starts at 0 -- a negative tricks_needed
    // therefore satisfies it before either strategy is ever called,
    // silently returning p_make=1.0 for a nonsensical request instead of
    // raising. 0 itself is legitimate (a real, if degenerate, already-made
    // case), so only strictly negative and above-the-most-tricks-in-a-deal
    // are rejected.
    constexpr int MaxTricksInADeal = 13;
    if (tricks_needed < 0 || tricks_needed > MaxTricksInADeal) {
        throw py::value_error(
            "tricks_needed has invalid value " + std::to_string(tricks_needed) +
            " (expected range 0.." + std::to_string(MaxTricksInADeal) + ")");
    }

    // replenish_below without sample_size is documented in C++ as "treated
    // as absent too" -- a silent no-op, safe for a C++ caller who can read
    // that on the field. A Python caller cannot, and is far more likely to
    // have made a mistake than to have meant it -- raise rather than
    // silently doing nothing. scan_budget without sample_size is
    // deliberately *not* checked here: it legitimately caps a scan that
    // would otherwise run to the source's end, on its own.
    if (replenish_below.has_value() && ! sample_size.has_value()) {
        throw py::value_error(
            "replenish_below requires sample_size -- without it there is no "
            "top-up target to replenish towards. In C++ this is documented as "
            "a silent no-op; a Python caller has no doxygen to read that on, "
            "so this is far more likely to be a mistake than an intention");
    }

    std::optional<py::function> const state_key_fn =
        state_key.is_none() ? std::nullopt : std::make_optional(py::cast<py::function>(state_key));

    Deal const root_deal = dds3_python::dict_to_deal(root);
    be::DeclarerStrategy const strategy = make_declarer_strategy(1, pi, state_key_fn);
    be::DefenderStrategy const delta_fn = make_defender_strategy(delta);

    be::EvaluateOptions options;
    options.retain_root = retain_root;
    options.collect_counters = collect_counters;
    options.bound = make_layout_bound(bound);
    options.delta_is_double_dummy_optimal = delta_is_double_dummy_optimal;
    options.sampling.sample_size = sample_size;
    options.sampling.scan_budget = scan_budget;
    options.sampling.replenish_below = replenish_below;

    be::EvaluationResult result;
    {
        // Released for the whole recursion: every trampoline above (pi's
        // two callbacks, delta, bound, and the layout source's
        // size()/at()) reacquires it only for as long as it runs. When
        // delta or bound is a DoubleDummyDefender/DoubleDummyBound, its
        // own __call__ nests a second, narrower release around just the
        // solve inside the acquire this trampoline already holds -- not a
        // second independent release of this one, so a solver call still
        // runs without the GIL, which is the whole point of releasing it
        // here in the first place.
        py::gil_scoped_release const release;
        result = be::evaluate(root_deal, declarer, tricks_needed, source, strategy, delta_fn, options);
    }

    // The evaluator itself never throws across a callback boundary -- a
    // callback's contract violation, or an unusable source, is *reported*
    // in EvaluationResult::error, precisely because a callback is user
    // input rather than an internal. Constructed into an exception here,
    // at this boundary, rather than propagated: an EvaluationError is
    // data this binding turns into a raise, never a C++ exception someone
    // threw. That is the opposite direction from a Python exception a
    // callback itself raises (see the trampolines above), which crosses
    // this same call unchanged -- own type, own message, own traceback --
    // and never becomes one of this module's own exception types.
    if (result.error.has_value()) {
        be::EvaluationError const& error = *result.error;
        if (error.callback == be::EvaluationCallback::RootConstruction) {
            raise_root_failure(error.root_failure);
        }
        raise_validation_error(error.callback, error.seat, error.layout, error.validation);
    }

    return dds3_python::evaluation_result_to_dict(result);
}

// A rejected play history raises from the constructor rather than
// mirroring history_verdict()/constrained_space_status()'s own C++-side
// "a constructor cannot report" accessor pattern. One exception type per
// cause, rather than one type with a cause attribute, so
// `except DuplicatedCardError` reads the way `except FileNotFoundError`
// does. Every cause here derives from BeliefSpaceLocalEvaluationError, the
// one root every exception this module raises intentionally shares (see
// register_root_exception_bindings) -- but deliberately *not* from
// RootFailureError or ValidationError (the sibling families
// register_root_failure_error_bindings/register_validation_error_bindings
// build below): a history failure must never be catchable as the same
// thing as an ordinary evaluate() failure (see this module's own
// history_verdict()/constrained_space_status() ordering: the first
// non-Consistent verdict is the only one ever raised for a given
// construction, never both).
std::map<be::HistoryVerdict, py::object> history_rejected_exceptions;
std::map<be::ConstrainedSpaceStatus, py::object> constrained_space_exceptions;

auto history_verdict_message(be::HistoryVerdict verdict) -> std::string
{
    switch (verdict) {
    case be::HistoryVerdict::InvalidInput:
        return "history is malformed: number, opening_leader, declarer, or "
               "some card's own suit/rank is out of range";
    case be::HistoryVerdict::DuplicatedCard:
        return "the same card appears twice in history";
    case be::HistoryVerdict::CardPlayedAndHeld:
        return "a card history says was played is still held by some hand in root";
    case be::HistoryVerdict::MissingCard:
        return "history and root together do not account for all 52 cards";
    case be::HistoryVerdict::TrickLengthMismatch:
        return "the number of cards history leaves trailing after its last "
               "complete trick does not match root's own trick in progress";
    case be::HistoryVerdict::TrailingTrickMismatch:
        return "the trailing cards' count matches root, but the cards "
               "themselves -- or their order -- do not";
    case be::HistoryVerdict::LeaderMismatch:
        return "opening_leader is wrong: replaying history's complete tricks "
               "from it does not land on root.first";
    case be::HistoryVerdict::VoidContradiction:
        return "history derives declarer or dummy void in a suit root shows "
               "that seat still holding";
    case be::HistoryVerdict::Consistent:
        break;
    }
    return "history is not consistent with root";
}

auto constrained_space_status_message(be::ConstrainedSpaceStatus status) -> std::string
{
    switch (status) {
    case be::ConstrainedSpaceStatus::ContradictoryVoid:
        return "history is self-contradictory: both defenders are void in a "
               "suit the pool still contains";
    case be::ConstrainedSpaceStatus::ForcedExceedsFixedSeatCount:
        return "more cards are forced to the fixed seat by history's voids "
               "than it holds at root";
    case be::ConstrainedSpaceStatus::InsufficientFreeCards:
        return "the fixed seat cannot reach its own hand size from what "
               "history's voids leave free";
    case be::ConstrainedSpaceStatus::Ok:
        break;
    }
    return "history leaves no legal split";
}

auto register_history_error_bindings(py::module_& module) -> void
{
    py::exception<void> history_base(module, "HistoryRejectedError", belief_space_local_evaluation_error);

    // InvalidInput alone is input-shaped -- a malformed PlayTraceBin (an
    // out-of-range declarer/opening_leader, or a malformed card), checkable
    // independent of which root it is checked against -- so it derives
    // from ValueError too, the same class of thing bindings.cpp's own
    // "trump has invalid value 5" already raises as. The other seven
    // causes are a *well-formed* history that simply does not fit this
    // particular root: a different kind of wrong, deliberately not
    // ValueError-derived, and a caller writing `except` around
    // construction must still be able to tell a rejected history apart
    // from NoLayoutSurvived (register_root_failure_error_bindings) -- both
    // derive from the one common root and neither from the other.
    py::object const invalid_history_input_error = make_exception_with_bases(
        module, "InvalidHistoryInputError",
        py::make_tuple(history_base, py::reinterpret_borrow<py::object>(PyExc_ValueError)));
    history_rejected_exceptions.emplace(be::HistoryVerdict::InvalidInput, invalid_history_input_error);

    auto const add_history = [&](be::HistoryVerdict verdict, char const* name) {
        history_rejected_exceptions.emplace(verdict, py::exception<void>(module, name, history_base));
    };
    add_history(be::HistoryVerdict::DuplicatedCard, "DuplicatedCardError");
    add_history(be::HistoryVerdict::CardPlayedAndHeld, "CardPlayedAndHeldError");
    add_history(be::HistoryVerdict::MissingCard, "MissingCardError");
    add_history(be::HistoryVerdict::TrickLengthMismatch, "TrickLengthMismatchError");
    add_history(be::HistoryVerdict::TrailingTrickMismatch, "TrailingTrickMismatchError");
    add_history(be::HistoryVerdict::LeaderMismatch, "LeaderMismatchError");
    add_history(be::HistoryVerdict::VoidContradiction, "VoidContradictionError");

    py::exception<void> space_base(module, "ConstrainedSpaceEmptyError", belief_space_local_evaluation_error);
    auto const add_space = [&](be::ConstrainedSpaceStatus status, char const* name) {
        constrained_space_exceptions.emplace(status, py::exception<void>(module, name, space_base));
    };
    add_space(be::ConstrainedSpaceStatus::ContradictoryVoid, "ContradictoryVoidError");
    add_space(be::ConstrainedSpaceStatus::ForcedExceedsFixedSeatCount, "ForcedExceedsFixedSeatCountError");
    add_space(be::ConstrainedSpaceStatus::InsufficientFreeCards, "InsufficientFreeCardsError");
}

[[noreturn]] auto raise_history_rejected(be::HistoryVerdict verdict) -> void
{
    py::set_error(history_rejected_exceptions.at(verdict), history_verdict_message(verdict).c_str());
    throw py::error_already_set();
}

// Defined here (forward-declared at list_to_history, well above this
// point) since it reads the same history_rejected_exceptions map
// raise_history_rejected does -- one InvalidHistoryInputError object,
// reached two ways: verify_history's own InvalidInput verdict, at a fixed
// message, or a malformed card caught earlier by list_to_history itself,
// at a message naming the specific index and field.
[[noreturn]] auto raise_invalid_history_input(std::string const& detail) -> void
{
    py::set_error(history_rejected_exceptions.at(be::HistoryVerdict::InvalidInput), detail.c_str());
    throw py::error_already_set();
}

[[noreturn]] auto raise_constrained_space_empty(be::ConstrainedSpaceStatus status) -> void
{
    py::set_error(constrained_space_exceptions.at(status), constrained_space_status_message(status).c_str());
    throw py::error_already_set();
}

// A LayoutSource holding one layout -- see the binding below for why it is
// worth shipping rather than left to each caller.
class SingleLayoutSource final : public be::LayoutSource
{
public:
    explicit SingleLayoutSource(Deal layout) : layout_(layout)
    {
    }

    auto size() const -> std::optional<std::uint64_t> override
    {
        return 1u;
    }

    auto at(std::uint64_t index) const -> Deal override
    {
        // The binding range-checks before reaching here; this is the C++-side
        // contract, for the evaluator calling through LayoutSource&.
        assert(index == 0);
        (void)index;
        return layout_;
    }

private:
    Deal layout_;
};

// The trampoline: LayoutSource is not a callable, it is an abstract class,
// so a Python subclass needs one. Every override acquires the GIL --
// PYBIND11_OVERRIDE's own mechanism does this already, which is exactly
// what lets this trampoline be called safely once evaluate() (a later
// task) releases the GIL for the whole call and Python callbacks each
// re-acquire it only for as long as they run.
//
// Written by hand rather than via PYBIND11_OVERRIDE_PURE: Deal is not a
// bound pybind11 type (it crosses as a dict, via converters, matching how
// ObservationState's own known_holdings already crosses), so the return
// value needs dict_to_deal, not a generic cast. A subclass that omits an
// override, returns something that
// is not dict-shaped, or returns a dict that is not a valid deal each fail
// with a distinct, comprehensible Python-side error rather than a crash --
// a missing override is RuntimeError, a non-dict return is pybind11's own
// TypeError, and a malformed dict is dict_to_deal's own ValueError.
class PyLayoutSource final : public be::LayoutSource
{
public:
    using be::LayoutSource::LayoutSource;

    auto size() const -> std::optional<std::uint64_t> override
    {
        py::gil_scoped_acquire gil;
        py::function const override_fn = py::get_override(this, "size");
        if (! override_fn) {
            throw std::runtime_error("LayoutSource subclass does not implement size()");
        }
        py::object const result = override_fn();
        if (result.is_none()) {
            return std::nullopt;
        }
        return py::cast<std::uint64_t>(result);
    }

    auto at(std::uint64_t index) const -> Deal override
    {
        py::gil_scoped_acquire gil;
        py::function const override_fn = py::get_override(this, "at");
        if (! override_fn) {
            throw std::runtime_error("LayoutSource subclass does not implement at()");
        }
        py::object const result = override_fn(index);
        return dds3_python::dict_to_deal(py::cast<py::dict>(result));
    }
};

// Mirrors defender_pool_decomposition's own count exactly (that function
// itself is not exposed to Python, and src/ has no public
// accessor for a pool's size in isolation) -- the total number of
// distinct cards either defender's remain_cards claims. dict_to_deal only
// validates each value's own bit shape, not that the two defender hands
// together stay within the 26-card domain binomial_coefficient (and so
// both size() and at()) is documented for; checked at this binding's own
// boundary, in ExhaustiveLayoutSource's py::init below -- *after*
// constructing the C++ object and confirming both history_verdict() and
// constrained_space_status() are the ordinary passing values, not before,
// so a root that is also independently rejected for one of those two
// reasons still reports that reason rather than this one (see the
// call site's own comment for why that order matters).
auto defender_pool_card_count(Deal const& root, int declarer) -> int
{
    int const fixed_seat = (declarer + 1) % DDS_HANDS;
    int const other_seat = (declarer + 3) % DDS_HANDS;
    int count = 0;
    for (int suit = 0; suit < DDS_SUITS; ++suit) {
        unsigned const suit_pool = root.remainCards[fixed_seat][suit] | root.remainCards[other_seat][suit];
        count += std::popcount(suit_pool);
    }
    return count;
}

auto register_layout_source_bindings(py::module_& module) -> void
{
    py::enum_<be::HistoryVerdict>(module, "HistoryVerdict")
        .value("Consistent", be::HistoryVerdict::Consistent)
        .value("InvalidInput", be::HistoryVerdict::InvalidInput)
        .value("DuplicatedCard", be::HistoryVerdict::DuplicatedCard)
        .value("CardPlayedAndHeld", be::HistoryVerdict::CardPlayedAndHeld)
        .value("MissingCard", be::HistoryVerdict::MissingCard)
        .value("TrickLengthMismatch", be::HistoryVerdict::TrickLengthMismatch)
        .value("TrailingTrickMismatch", be::HistoryVerdict::TrailingTrickMismatch)
        .value("LeaderMismatch", be::HistoryVerdict::LeaderMismatch)
        .value("VoidContradiction", be::HistoryVerdict::VoidContradiction);

    py::enum_<be::ConstrainedSpaceStatus>(module, "ConstrainedSpaceStatus")
        .value("Ok", be::ConstrainedSpaceStatus::Ok)
        .value("ContradictoryVoid", be::ConstrainedSpaceStatus::ContradictoryVoid)
        .value(
            "ForcedExceedsFixedSeatCount", be::ConstrainedSpaceStatus::ForcedExceedsFixedSeatCount)
        .value("InsufficientFreeCards", be::ConstrainedSpaceStatus::InsufficientFreeCards);

    py::class_<be::LayoutSource, PyLayoutSource>(
        module,
        "LayoutSource",
        "A dumb, ordered, restartable index space over candidate layouts.\n"
        "Subclass and override size() and at(i) to supply a belief space\n"
        "narrower than the root alone implies -- what the bidding ruled\n"
        "out, say. at(i) must return a deal dict (see the module's deal\n"
        "dict shape) and repeated calls with the same index must return the\n"
        "same layout.\n\n"
        "**A caller obligation this type cannot check or enforce**: sampling\n"
        "takes a *prefix* of this order rather than drawing from it at\n"
        "random, on the premise the order is already effectively random\n"
        "with respect to which layouts are consistent with any given root.\n"
        "A source that is sorted, or grouped by anything correlated with\n"
        "consistency, yields a systematically biased sample with no\n"
        "diagnostic anywhere. ExhaustiveLayoutSource is a correct\n"
        "implementation of this obligation; a Python subclass returning\n"
        "self._deals[i] from a list built in assembly order is the most\n"
        "likely way to get this wrong.")
        .def(py::init<>());

    // A source over exactly one layout. Defined here rather than in
    // src/belief_evaluation/ because its whole purpose is to save a *Python*
    // caller from writing a trampoline subclass; a C++ caller can write the
    // three lines directly, and the module's test support already does.
    //
    // It is also the one LayoutSource trivially exempt from the randomised
    // order obligation LayoutSource's own docstring describes, one element
    // having only one order -- so it doubles as a correct-by-construction
    // starting point, where the pattern that obligation warns against
    // (returning self._deals[i] from a list in assembly order) is the most
    // likely thing a caller writes instead.
    py::class_<SingleLayoutSource, be::LayoutSource>(
        module,
        "SingleLayoutSource",
        "A LayoutSource over exactly one layout: size() is 1 and at(0) is\n"
        "that layout.\n\n"
        "For evaluating one layout on its own -- which is how P_make over a\n"
        "belief space gets cross-checked against the mean of P_make over\n"
        "each layout alone, the check that is worth making about any strategy\n"
        "pair and that has already caught one library defect.\n\n"
        "Exempt from the randomised-order obligation in LayoutSource's own\n"
        "docstring, since one element has only one order. Sampling a\n"
        "one-layout space is a no-op rather than a biased draw.")
        .def(py::init([](py::dict const& layout) {
                 return SingleLayoutSource(dds3_python::dict_to_deal(layout));
             }),
             py::arg("layout"))
        .def("size", [](SingleLayoutSource const& self) { return self.size().value(); })
        .def(
            "at",
            [](SingleLayoutSource const& self, py::object const& index_obj) {
                // Same contract, and the same reasoning, as
                // ExhaustiveLayoutSource::at below: an out-of-range index is a
                // Python IndexError on every build, and the index is compared
                // as a Python int so a negative or oversized one never has to
                // be represented as uint64_t at all.
                py::object const index_int =
                    py::reinterpret_steal<py::object>(PyNumber_Index(index_obj.ptr()));
                if (! index_int) {
                    throw py::error_already_set();
                }
                if (index_int < py::int_(0) || index_int >= py::int_(1)) {
                    throw py::index_error(
                        "index " + std::string(py::repr(index_obj)) +
                        " is out of range for a source of size 1");
                }
                return dds3_python::deal_to_dict(self.at(0));
            },
            py::arg("index"));

    py::class_<be::ExhaustiveLayoutSource, be::LayoutSource>(
        module,
        "ExhaustiveLayoutSource",
        "Enumerates every layout consistent with root and, when history is\n"
        "supplied, with the voids that history establishes.\n\n"
        "history is optional: omitting it applies no void constraint at all,\n"
        "which is correct at trick one and **silently wrong at a mid-play\n"
        "root** -- there is no way for this type to tell 'no history' from\n"
        "'declarer chose not to supply one'. A non-empty history is checked\n"
        "against root: history_verdict() is checked first, and when it is\n"
        "not Consistent the constrained decomposition is never attempted --\n"
        "the raised exception (a HistoryRejectedError subclass) reports only\n"
        "that first failure, never a second one from constrained_space_status(),\n"
        "which reports the meaningless default (Ok) in that case. A\n"
        "well-formed history that nonetheless leaves no legal split raises a\n"
        "ConstrainedSpaceEmptyError subclass instead.")
        .def(
            py::init([](py::dict const& root,
                        int declarer,
                        std::uint64_t seed,
                        py::sequence const& history,
                        int opening_leader) {
                // ExhaustiveLayoutSource's own history_verdict() cannot be
                // trusted to catch an out-of-range declarer/opening_leader
                // here: verify_history (and so InvalidInput) only runs at
                // all when history is non-empty -- an empty history is, by
                // this type's own design, "not checked against root at
                // all". With an empty history, opening_leader reaches
                // derive_voids completely unvalidated, which asserts rather
                // than reports. Checked here instead, unconditionally, so
                // this binding never depends on whether the caller happened
                // to also supply a history.
                if (declarer < 0 || declarer >= DDS_HANDS || opening_leader < 0
                    || opening_leader >= DDS_HANDS) {
                    raise_history_rejected(be::HistoryVerdict::InvalidInput);
                }
                Deal const root_deal = dds3_python::dict_to_deal(root);
                PlayTraceBin const history_bin = list_to_history(history);
                be::ExhaustiveLayoutSource source(root_deal, declarer, seed, history_bin, opening_leader);
                be::HistoryVerdict const verdict = source.history_verdict();
                if (verdict != be::HistoryVerdict::Consistent) {
                    raise_history_rejected(verdict);
                }
                be::ConstrainedSpaceStatus const status = source.constrained_space_status();
                if (status != be::ConstrainedSpaceStatus::Ok) {
                    raise_constrained_space_empty(status);
                }
                // Only reachable, and only needed, once both checks above
                // have already passed: constrained_space_size (size())
                // returns 0 without calling binomial_coefficient at all
                // when status is not Ok, and a non-Ok status has already
                // raised above -- so free_cards (not itself exposed to
                // Python) is only ever what size()/at() actually read
                // from this point on. See defender_pool_card_count's own
                // comment for why pool_count, not free_cards.size()
                // directly, is what is checked: free_cards is always a
                // subset of the pool, so this bound is conservative
                // (never a false negative) even though a caller whose
                // voids happen to shrink free_cards back under 26 despite
                // a larger pool is rejected too -- a root that shape is
                // already not one a real 52-card deal could produce.
                constexpr int MaxOutstandingCards = 26;
                int const pool_count = defender_pool_card_count(root_deal, declarer);
                if (pool_count > MaxOutstandingCards) {
                    throw py::value_error(
                        "root has " + std::to_string(pool_count) +
                        " cards between the two defender hands (maximum " +
                        std::to_string(MaxOutstandingCards) + ")");
                }
                return source;
            }),
            py::arg("root"),
            py::arg("declarer"),
            py::arg("seed"),
            py::arg("history") = py::list(),
            py::arg("opening_leader") = 0)
        .def(
            "size",
            [](be::ExhaustiveLayoutSource const& self) -> py::object {
                auto const value = self.size();
                return value.has_value() ? py::cast(*value) : py::none();
            })
        .def(
            "at",
            [](be::ExhaustiveLayoutSource const& self, py::object const& index_obj) {
                // ExhaustiveLayoutSource::at()'s own precondition is an
                // assert(index < total) -- a last resort against
                // undefined behaviour once built -c opt (where it is
                // compiled out entirely), not a diagnostic. Checked here
                // instead, so an out-of-range index is a Python
                // IndexError on every build, not an aborted debug
                // process or silent undefined behaviour in release.
                //
                // index arrives as py::object, not std::uint64_t: a
                // negative Python int, or one too large for uint64_t, has
                // no valid uint64_t representation at all, so binding the
                // parameter as uint64_t directly would fail pybind11's
                // own argument conversion (TypeError/OverflowError,
                // depending on the value) before this function's own
                // range check ever ran -- the same "every out-of-range
                // index raises IndexError" contract this comment already
                // claims, silently broken for exactly the inputs most
                // likely to reach it by mistake. Compared as Python ints
                // throughout, so no C++ integer ever has to represent an
                // out-of-range value in the first place.
                //
                // PyNumber_Index, not py::cast<py::int_>: that cast goes
                // through PyNumber_Long (int()'s own conversion), which
                // truncates a float or parses a numeric string -- at(1.5)
                // would silently become at(1), a different, real layout,
                // rather than the caller's mistake it actually is.
                // PyNumber_Index is operator.index()'s own C-level
                // implementation: accepts only an int (or an __index__
                // implementer), rejects everything else with TypeError,
                // matching what real Python indexing ([1, 2, 3][1.5])
                // already does.
                py::object const index_int =
                    py::reinterpret_steal<py::object>(PyNumber_Index(index_obj.ptr()));
                if (! index_int) {
                    throw py::error_already_set();
                }
                std::optional<std::uint64_t> const total = self.size();
                if (index_int < py::int_(0) || ! total.has_value() || index_int >= py::int_(*total)) {
                    throw py::index_error(
                        "index " + std::string(py::repr(index_obj)) +
                        " is out of range for a source of size " + std::to_string(total.value_or(0)));
                }
                return dds3_python::deal_to_dict(self.at(py::cast<std::uint64_t>(index_int)));
            })
        .def(
            "history_verdict", &be::ExhaustiveLayoutSource::history_verdict,
            "Consistent, or when it is not, the specific cause -- see\n"
            "HistoryVerdict. On a successfully constructed source this is\n"
            "always Consistent: a non-Consistent verdict raises from the\n"
            "constructor rather than being left for this accessor to report.")
        .def(
            "constrained_space_status", &be::ExhaustiveLayoutSource::constrained_space_status,
            "Ok, or when it is not, the specific cause -- see\n"
            "ConstrainedSpaceStatus. Meaningless (reports the default, Ok)\n"
            "unless history_verdict() is Consistent; on a successfully\n"
            "constructed source this is always Ok, for the same reason\n"
            "history_verdict() is always Consistent there.");
}

// SolverContext crosses from dds3 into this module without this module
// ever registering py::class_<SolverContext> itself. pybind11 registers
// types per module; registering it a second time here would produce two
// distinct Python types that look identical and cannot be passed between
// modules -- the standard cross-module pitfall, and the first thing a
// caller who already has a context from solving will try. The fix is the
// standard cross-module pattern: dds3 keeps the registration (its own
// bindings.cpp), this module only *uses* the type, accepting it as a
// constructor argument and storing a reference exactly as the C++ types
// below already do. pybind11 shares its type registry between extensions
// built by the same toolchain through an interpreter-level capsule, which
// requires the registering module (dds3) to have been imported first --
// this package's own __init__.py does that (see its own comment) before
// this extension is ever imported, so the registration already exists by
// the time a constructor here is called.
//
// Both DoubleDummyDefender and DoubleDummyBound hold SolverContext& --
// a reference, not owned. A Python object outliving the context it was
// built from would dangle it: py::keep_alive<1, 2>() on both constructors
// ties the context's Python lifetime to the object holding it (argument
// index 1 is the object being constructed, 2 is ctx), the same way
// pybind11 documents for any object holding a reference to another.
class PyDoubleDummyDefender
{
public:
    PyDoubleDummyDefender(SolverContext& ctx, be::SpreadPolicy policy) : defender_(ctx, policy)
    {
    }

    // Usable directly as delta: same (layout, seat, state) shape
    // make_defender_strategy already gives a Python-authored one. seat
    // and state are accepted but unused -- DoubleDummyDefender's own
    // as_strategy() only ever reads query.layout, since solve_board
    // determines who is on play from the deal itself (its own trump/
    // first fields), not from a separate seat argument.
    auto call(py::dict const& layout, int seat, py::object const& state) -> py::list
    {
        (void)state;
        Deal const deal = dds3_python::dict_to_deal(layout);
        be::DefenderStrategy const strategy = defender_.as_strategy();

        std::vector<be::WeightedCard> weighted;
        {
            // Released for the solve itself -- the one piece of this
            // module that actually calls into the solver, so this is
            // where a caller's SolverContext gets to use whatever
            // internal parallelism it has. Already re-acquired by the
            // trampoline that called this __call__ in the first place
            // (make_defender_strategy's own gil_scoped_acquire), so this
            // is a release nested inside that acquire, not a second
            // independent one.
            py::gil_scoped_release const release;
            be::ObservationState const unused_state{};
            weighted = strategy(be::DefenderQuery{deal, seat, unused_state});
        }

        py::list result;
        for (be::WeightedCard const& card : weighted) {
            result.append(py::make_tuple(card.card, card.probability));
        }
        return result;
    }

private:
    be::DoubleDummyDefender defender_;
};

class PyDoubleDummyBound
{
public:
    PyDoubleDummyBound(SolverContext& ctx, int declarer) : bound_(ctx, declarer)
    {
    }

    // Usable directly as bound: same (deal dict) -> int shape
    // make_layout_bound already gives a Python-authored one.
    auto call(py::dict const& layout) -> int
    {
        Deal const deal = dds3_python::dict_to_deal(layout);
        be::LayoutBound const bound_fn = bound_.as_bound();
        py::gil_scoped_release const release;  // see PyDoubleDummyDefender::call
        return bound_fn(deal);
    }

private:
    be::DoubleDummyBound bound_;
};

auto register_solver_seam_bindings(py::module_& module) -> void
{
    py::enum_<be::SpreadPolicy>(
        module,
        "SpreadPolicy",
        "How DoubleDummyDefender spreads probability over a solved\n"
        "position's tied-for-best candidates. The two values differ in\n"
        "what justifies them, not merely in behaviour -- see each one's\n"
        "own docstring.")
        .value(
            "TouchingSequence", be::SpreadPolicy::TouchingSequence,
            "Uniform over the single canonical best card's own touching-card\n"
            "group. The cards in one touching-card group are literally\n"
            "interchangeable given the layout, so this is the canonical\n"
            "distribution over an equivalence class the underlying theory\n"
            "already licenses -- restricted choice, not a modelling guess.")
        .value(
            "AllOptimal", be::SpreadPolicy::AllOptimal,
            "Uniform over the union of every tied-for-best candidate's own\n"
            "touching-card group, across suits. These cards are equally\n"
            "*good* but not otherwise equivalent -- the resulting positions\n"
            "are not isomorphic, and the distribution's shape depends on how\n"
            "many suits happen to tie. Selecting this moves to the more\n"
            "advanced justification algorithm.md describes.");

    py::class_<PyDoubleDummyDefender>(
        module,
        "DoubleDummyDefender",
        "A defender strategy backed by the solver: solves a layout double\n"
        "dummy and spreads probability over the tied-for-best cards via\n"
        "policy. Usable directly as evaluate()'s own delta argument.\n\n"
        "**This is not best defence against a contract.** It maximises\n"
        "tricks (target = -1), not the contract threshold, and will\n"
        "sometimes concede the contract to hold the trick count down --\n"
        "the gap between maximising tricks and minimising P_make this\n"
        "whole capability exists to quantify, not a defect. It does\n"
        "satisfy delta_is_double_dummy_optimal regardless: that\n"
        "declaration is about trick count, which trick-maximising play\n"
        "delivers for both sides, not about matching a contract.\n\n"
        "ctx is not owned -- create, configure and outlive it yourself.\n"
        "**ctx is not thread-safe** (SolverContext's own contract: one\n"
        "context per thread) and __call__ releases the GIL around the\n"
        "actual solve, so two Python threads genuinely run concurrently if\n"
        "they share one -- construct one DoubleDummyDefender (and one\n"
        "SolverContext) per worker rather than sharing either across\n"
        "threads.")
        .def(
            py::init<SolverContext&, be::SpreadPolicy>(), py::arg("ctx"),
            py::arg("policy") = be::SpreadPolicy::TouchingSequence, py::keep_alive<1, 2>())
        .def("__call__", &PyDoubleDummyDefender::call, py::arg("layout"), py::arg("seat"), py::arg("state"));

    py::class_<PyDoubleDummyBound>(
        module,
        "DoubleDummyBound",
        "A LayoutBound backed by the solver: declarer's own double-dummy\n"
        "trick count from a given layout, regardless of who is actually on\n"
        "lead there. Usable directly as evaluate()'s own bound argument.\n\n"
        "**declarer is fixed for this object's whole lifetime -- not\n"
        "reusable across declarers.** Reusing one across two declarers\n"
        "produces a wrong bound silently, which then feeds tier2_dead(),\n"
        "whose whole soundness rests on the bound being right for the\n"
        "declarer actually being evaluated.\n\n"
        "The precondition this bound carries: R <= DD (the reason a cut\n"
        "may use it at all) holds only when the paired delta is\n"
        "double-dummy optimal for trick count -- pass\n"
        "delta_is_double_dummy_optimal=True to evaluate() and pair this\n"
        "with a DoubleDummyDefender, the intended sound configuration.\n\n"
        "ctx is not owned, and not thread-safe, the same as\n"
        "DoubleDummyDefender's own contract -- see its docstring.")
        .def(
            py::init([](SolverContext& ctx, int declarer) {
                // declarer is fixed for this object's whole lifetime (see
                // the docstring above), and as_bound() later indexes
                // remainCards[declarer_] through tricks_remaining with no
                // range check of its own -- checked here, once, at
                // construction, rather than left unchecked the way
                // storing it would otherwise leave it.
                if (declarer < 0 || declarer >= DDS_HANDS) {
                    throw py::value_error(
                        "declarer has invalid value " + std::to_string(declarer) +
                        " (expected range 0.." + std::to_string(DDS_HANDS - 1) + ")");
                }
                return PyDoubleDummyBound(ctx, declarer);
            }),
            py::arg("ctx"), py::arg("declarer"), py::keep_alive<1, 2>())
        .def("__call__", &PyDoubleDummyBound::call, py::arg("layout"));
}

// Internal: exercises the converters above end to end, ahead of the real
// callers (the layout source binds the inward history conversion; evaluate()
// constructs the ObservationState this same known_holdings/history logic
// serves). Not part of the public surface -- leading underscore, not
// re-exported from the package's own __init__.py.
auto register_converter_probes(py::module_& module) -> void
{
    module.def(
        "_deal_round_trip",
        [](py::dict const& deal) { return dds3_python::deal_to_dict(dds3_python::dict_to_deal(deal)); },
        "Round-trips a deal dict through the internal Deal representation.\n"
        "Raises ValueError if the dict is malformed. Testing support only.");
    module.def(
        "_history_round_trip",
        [](py::sequence const& history) { return history_to_list(list_to_history(history)); },
        "Round-trips a card sequence through the internal PlayTraceBin\n"
        "representation. Raises ValueError if malformed. Testing support only.");

    // Calling source.size()/.at(i) directly from Python never reaches
    // PyLayoutSource's C++ overrides at all: Python's own method
    // resolution finds a subclass's plain Python method first, regardless
    // of what the base class binds. The trampoline is only exercised when
    // C++ code holds a LayoutSource& and calls through it -- which
    // evaluate() (below) now does for real, through make_root(), for
    // whatever source a caller passes it. These two probes call through a
    // be::LayoutSource const& the same way, as an additional, narrower
    // direct test of the trampoline itself -- a Python subclass "consumed
    // by C++" in isolation, independent of evaluate()'s own much larger
    // surface. Testing support only.
    module.def(
        "_layout_source_size_from_cpp",
        [](be::LayoutSource const& source) -> py::object {
            auto const value = source.size();
            return value.has_value() ? py::cast(*value) : py::none();
        });
    module.def(
        "_layout_source_at_from_cpp",
        [](be::LayoutSource const& source, std::uint64_t index) {
            return dds3_python::deal_to_dict(source.at(index));
        });
    module.def(
        "_with_belief_view",
        &probe_with_belief_view,
        py::arg("layouts"),
        py::arg("weights"),
        py::arg("is_sample"),
        py::arg("callback"),
        "Builds a BeliefNode from layouts/weights, calls callback(view),\n"
        "and invalidates the view on the way out -- standing in for\n"
        "evaluate() so the validity guard can be tested against a fixture\n"
        "smaller than a real recursion. Testing support only.");
}

}  // namespace

PYBIND11_MODULE(_belief_space_local_evaluation, module)
{
    module.doc() = "belief_space_local_evaluation Python extension";

    // The exception hierarchy's one root first: everything else below
    // that raises derives from it.
    register_root_exception_bindings(module);

    register_card_bindings(module);
    register_rank_map_bindings(module);
    register_observation_state_bindings(module);
    register_belief_view_bindings(module);
    register_strategy_error_bindings(module);
    register_root_failure_error_bindings(module);
    register_validation_error_bindings(module);
    register_history_error_bindings(module);
    register_layout_source_bindings(module);
    register_solver_seam_bindings(module);
    register_converter_probes(module);

    module.def(
        "evaluate",
        &evaluate,
        py::arg("root"),
        py::arg("declarer"),
        py::arg("tricks_needed"),
        py::arg("source"),
        py::arg("pi"),
        py::arg("delta"),
        py::kw_only(),
        py::arg("retain_root") = false,
        py::arg("collect_counters") = false,
        py::arg("bound") = py::none(),
        py::arg("delta_is_double_dummy_optimal") = false,
        py::arg("sample_size") = std::nullopt,
        py::arg("scan_budget") = std::nullopt,
        py::arg("replenish_below") = std::nullopt,
        py::arg("state_key") = py::none(),
        "Evaluates P_make for pi against delta over the belief space "
        "source enumerates from root. See the module's own capability "
        "document for the option coupling this binding validates that "
        "the C++ type's own doxygen states but a Python caller cannot "
        "read on the field.");

    // The trick primitives, bound from trick.hpp rather than reimplemented.
    // These are the four the evaluator itself uses to walk a position
    // (evaluate.cpp, expand.cpp), exposed so a Python caller reaching a
    // mid-play root uses the *same* follow-suit and trick-winner rules the
    // evaluator will apply to the root they hand it. A caller's own copy that
    // disagrees produces a wrong root, and every number computed from it is
    // confidently about a different position, with nothing raised anywhere.
    //
    // All four take and return `deal` dicts, the same shape
    // ObservationState.known_holdings hands out, so they compose with what a
    // strategy is already given.
    module.def(
        "seat_on_play",
        [](py::dict const& deal) { return be::seat_on_play(dds3_python::dict_to_deal(deal)); },
        py::arg("deal"),
        "The seat (0..3) on play at deal: its `first` advanced by however\n"
        "many cards have been played to the trick in progress.\n\n"
        "Note this is deal['first'], the *current trick's* leader, advanced --\n"
        "not ObservationState.first, which is the root's leader and never\n"
        "moves. A strategy given an ObservationState should pass\n"
        "state.known_holdings here.");

    module.def(
        "legal_cards",
        [](py::dict const& deal, int seat) {
            return be::enumerate_legal_cards(dds3_python::dict_to_deal(deal), seat);
        },
        py::arg("deal"),
        py::arg("seat"),
        "Every Card seat may legally play at deal, honouring the suit led to\n"
        "the trick in progress when seat holds any card of it.\n\n"
        "Ordered suit ascending, then rank ascending within a suit -- the same\n"
        "order evaluate()'s root_children key uses, since that key is built by\n"
        "indexing into this list.\n\n"
        "Returns Cards, not the per-suit bitmasks the C++ legal_cards()\n"
        "returns: a strategy has to return a Card, so the bitmask form only\n"
        "ever gets expanded again by the caller.");

    module.def(
        "trick_complete_winner",
        [](py::dict const& deal, be::Card const& card) {
            return be::trick_complete_winner(dds3_python::dict_to_deal(deal), card);
        },
        py::arg("deal"),
        py::arg("card"),
        "The seat that wins the trick in progress once card is played as its\n"
        "fourth card. deal must already carry exactly three played cards in\n"
        "current_trick_suit / current_trick_rank.\n\n"
        "Highest trump if any were played, else highest card of the led suit:\n"
        "a discard never wins, however high, and a ruff beats any card of the\n"
        "suit led.");

    module.def(
        "play",
        [](py::dict const& deal, be::Card const& card) {
            return dds3_python::deal_to_dict(be::play(dds3_python::dict_to_deal(deal), card));
        },
        py::arg("deal"),
        py::arg("card"),
        "The deal after the seat on play plays card: removed from that seat's\n"
        "remain_cards, and either appended to the trick in progress or -- when\n"
        "card completes the trick -- the trick resolved, current_trick_*\n"
        "cleared and 'first' reassigned to the winner.\n\n"
        "Pure: the deal passed in is not modified, a new dict is returned.\n"
        "Carries no trick counter -- who won, and what that makes the running\n"
        "total, is the caller's business.");

    module.def("module_name", []() {
        return "_belief_space_local_evaluation";
    });
}
