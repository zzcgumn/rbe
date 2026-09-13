// The belief_space_local_evaluation extension module. Its own distinct
// import, not folded into dds3: this capability has its own vocabulary (a
// belief space, a layout source, replenishment) and a caller solving a
// board has no reason to import belief evaluation to do it.
#include <pybind11/pybind11.h>

#include <map>
#include <optional>
#include <stdexcept>
#include <string>

#include <belief_evaluation/exhaustive_layout_source.hpp>
#include <belief_evaluation/layout_source.hpp>
#include <belief_evaluation/types.hpp>
#include <utility/constants.h>

#include "converters.hpp"

namespace py = pybind11;
namespace be = dds::belief_evaluation;

namespace
{

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
std::map<be::HistoryVerdict, py::exception<void>> history_rejected_exceptions;
std::map<be::ConstrainedSpaceStatus, py::exception<void>> constrained_space_exceptions;

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
    py::exception<void> history_base(module, "HistoryRejectedError");
    auto const add_history = [&](be::HistoryVerdict verdict, char const* name) {
        history_rejected_exceptions.emplace(verdict, py::exception<void>(module, name, history_base));
    };
    add_history(be::HistoryVerdict::InvalidInput, "InvalidHistoryInputError");
    add_history(be::HistoryVerdict::DuplicatedCard, "DuplicatedCardError");
    add_history(be::HistoryVerdict::CardPlayedAndHeld, "CardPlayedAndHeldError");
    add_history(be::HistoryVerdict::MissingCard, "MissingCardError");
    add_history(be::HistoryVerdict::TrickLengthMismatch, "TrickLengthMismatchError");
    add_history(be::HistoryVerdict::TrailingTrickMismatch, "TrailingTrickMismatchError");
    add_history(be::HistoryVerdict::LeaderMismatch, "LeaderMismatchError");
    add_history(be::HistoryVerdict::VoidContradiction, "VoidContradictionError");

    py::exception<void> space_base(module, "ConstrainedSpaceEmptyError");
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
}

}  // namespace

PYBIND11_MODULE(_belief_space_local_evaluation, module)
{
    module.doc() = "belief_space_local_evaluation Python extension";

    register_card_bindings(module);
    register_observation_state_bindings(module);
    register_history_error_bindings(module);
    register_layout_source_bindings(module);
    register_converter_probes(module);

    module.def("module_name", []() {
        return "_belief_space_local_evaluation";
    });
}
