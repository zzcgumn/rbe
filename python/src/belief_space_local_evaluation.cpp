// The belief_space_local_evaluation extension module. Its own distinct
// import, not folded into dds3: this capability has its own vocabulary (a
// belief space, a layout source, replenishment) and a caller solving a
// board has no reason to import belief evaluation to do it.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <belief_evaluation/belief_view.hpp>
#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/exhaustive_layout_source.hpp>
#include <belief_evaluation/layout_source.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/types.hpp>
#include <belief_evaluation/validation.hpp>
#include <utility/constants.h>

#include "converters.hpp"

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
    py::object const cls = type_builtin(name, bases, py::dict());
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

// Validated at this boundary rather than left to the C++ side's own
// asserted-or-clamped defences: those exist as a last resort against
// undefined behaviour, not as a diagnostic, and a clamped card would
// silently change which suit or rank was played. A Python caller can build
// a malformed sequence far more easily than a C++ one can.
auto list_to_history(py::sequence const& cards) -> PlayTraceBin
{
    constexpr std::size_t deck_size = DDS_SUITS * 13;

    if (cards.size() > deck_size) {
        throw py::value_error(
            "history has " + std::to_string(cards.size()) +
            " cards (maximum " + std::to_string(deck_size) + ")");
    }

    PlayTraceBin result{};
    result.number = static_cast<int>(cards.size());
    for (std::size_t i = 0; i < cards.size(); ++i) {
        auto const& card = py::cast<be::Card const&>(cards[i]);
        if (card.suit < 0 || card.suit >= DDS_SUITS) {
            throw py::value_error(
                "history[" + std::to_string(i) + "].suit has invalid value " +
                std::to_string(card.suit) + " (expected range 0.." +
                std::to_string(DDS_SUITS - 1) + ")");
        }
        if (card.rank < 2 || card.rank > 14) {
            throw py::value_error(
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
        .def("__repr__", [](be::Card const& self) {
            return "Card(suit=" + std::to_string(self.suit) +
                ", rank=" + std::to_string(self.rank) + ")";
        });
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
        "declarer strategy's play() and state_key() at every call: identical\n"
        "across every layout of the current belief space.")
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
            "outstanding cards, not that defender's own actual holding.");
}

// BeliefView holds a std::span into caller-owned scratch and node.layouts,
// live only for the call it was built for -- see make_belief_view's own
// doxygen. A Python strategy is handed one at every declarer node; Python
// users store things ("self.last_view = view" is an entirely ordinary
// thing to write), and a naive non-owning binding would hand out an object
// reading freed memory the moment the callback returns, silently, with
// plausible-looking values.
//
// The fix is a validity flag the *binding* owns (nothing in library/src/
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
    expired_belief_view_error = py::exception<void>(module, "ExpiredBeliefViewError");

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
        "declarer currently knows it, handed to play() and state_key() at\n"
        "every call. Valid only for the duration of that one call --\n"
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
// eventual evaluate() binding runs on: the GIL is released for the
// duration of the C++ recursion (see probe_evaluate below) and acquired
// in every trampoline that calls back into Python -- these two, and the
// layout source's size()/at() (already GIL-acquiring). One
// acquire site per callback *kind*, here, rather than one per call site
// scattered through the recursion: a callback kind that forgets to
// acquire is a callback kind that corrupts the interpreter the first time
// two threads exercise it, and there must be exactly one place per kind
// that could get this wrong.
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
// a bound too high makes tier2_dead() fire when it should not and
// silently reports zero for a contract that makes. Absent by default,
// which leaves the cut disabled -- an empty std::function, matching
// LayoutBound's own doxygen.
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
        // Released for the whole recursion: every solver call inside it
        // (none yet -- the solver seam is a later task) runs without it,
        // and every trampoline above (pi's two callbacks, delta, bound,
        // and the layout source's size()/at()) reacquires it only for as
        // long as it runs.
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
// does. The full RootFailure/ValidationError hierarchy this eventually
// joins is designed separately, in one sitting; this is the narrower scope
// of a rejected history specifically, and none of it derives from that
// later hierarchy's base yet -- deliberately, since a history failure must
// never be catchable as the same thing as an ordinary evaluate() failure
// (see this module's own history_verdict()/constrained_space_status()
// ordering: the first non-Consistent verdict is the only one ever raised
// for a given
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

[[noreturn]] auto raise_constrained_space_empty(be::ConstrainedSpaceStatus status) -> void
{
    py::set_error(constrained_space_exceptions.at(status), constrained_space_status_message(status).c_str());
    throw py::error_already_set();
}

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
            [](be::ExhaustiveLayoutSource const& self, std::uint64_t index) {
                return dds3_python::deal_to_dict(self.at(index));
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
    // C++ code holds a LayoutSource& and calls through it -- which nothing
    // yet does, since evaluate()/make_root() are a later task. These two
    // probes are that C++-side consumer, standing in for it: they call
    // through a be::LayoutSource const& exactly as the real evaluator
    // eventually will, so a Python subclass is genuinely "consumed by
    // C++", not merely called from Python. Testing support only.
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
    register_observation_state_bindings(module);
    register_belief_view_bindings(module);
    register_strategy_error_bindings(module);
    register_root_failure_error_bindings(module);
    register_validation_error_bindings(module);
    register_history_error_bindings(module);
    register_layout_source_bindings(module);
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

    module.def("module_name", []() {
        return "_belief_space_local_evaluation";
    });
}
