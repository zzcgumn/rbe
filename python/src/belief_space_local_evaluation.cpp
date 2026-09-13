// The belief_space_local_evaluation extension module. Its own distinct
// import, not folded into dds3: this capability has its own vocabulary (a
// belief space, a layout source, replenishment) and a caller solving a
// board has no reason to import belief evaluation to do it.
#include <pybind11/pybind11.h>

#include <string>

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
}

}  // namespace

PYBIND11_MODULE(_belief_space_local_evaluation, module)
{
    module.doc() = "belief_space_local_evaluation Python extension";

    register_card_bindings(module);
    register_observation_state_bindings(module);
    register_converter_probes(module);

    module.def("module_name", []() {
        return "_belief_space_local_evaluation";
    });
}
