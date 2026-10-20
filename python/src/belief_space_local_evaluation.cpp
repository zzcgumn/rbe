// The belief_space_local_evaluation extension module. It is separate from
// dds3, so that a caller who solves one board need not import it.
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
#include <belief_evaluation/brute_force_declarer.hpp>
#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/default_defender_heuristics.hpp>
#include <belief_evaluation/defender_heuristic.hpp>
#include <belief_evaluation/defender_heuristic_chain.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/discard_keep_winners.hpp>
#include <belief_evaluation/double_dummy_bound.hpp>
#include <belief_evaluation/double_dummy_defender.hpp>
#include <belief_evaluation/fourth_seat_low.hpp>
#include <belief_evaluation/heuristic_defender.hpp>
#include <belief_evaluation/high_in_third.hpp>
#include <belief_evaluation/ruff_small.hpp>
#include <belief_evaluation/second_seat_low.hpp>
#include <belief_evaluation/third_seat_low.hpp>
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

// The base of every exception this module raises: a rejected history, a
// RootFailure, or a ValidationError. A Python exception raised inside a
// callback keeps its own type as it passes out of evaluate(), so this base
// does not catch it.
py::object belief_space_local_evaluation_error;

// Creates an exception type with several bases, through Python's
// `type(name, bases, namespace)`. py::exception<> allows only one base.
// Two causes also derive from ValueError, so that `except ValueError`
// still catches them. Python resolves the diamond as it would for
// `class Foo(A, B):`.
auto make_exception_with_bases(py::module_& module, char const* name, py::tuple const& bases) -> py::object
{
    py::object const type_builtin = py::module_::import("builtins").attr("type");
    // Set __module__ by hand. py::exception<> does this itself, but type()
    // does not: it would use the module that happens to call it
    // (importlib's bootstrap code, observed).
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

// A history as a list of Card, in both directions:
// ObservationState::history goes out through history_to_list, and the
// PlayRecord constructor takes one in through list_to_history. The two
// round-trip.

auto history_to_list(PlayTraceBin const& history) -> py::list
{
    py::list result;
    for (int i = 0; i < history.number; ++i) {
        result.append(be::Card{history.suit[i], history.rank[i]});
    }
    return result;
}

// Raises InvalidHistoryInputError with a detail message. Defined in
// register_history_error_bindings. list_to_history uses it instead of
// raise_history_rejected, because its message names the bad index and
// field.
[[noreturn]] auto raise_invalid_history_input(std::string const& detail) -> void;

// Validates each card here. The C++ side only asserts or clamps, as a last
// defence against undefined behaviour, and a clamped card would silently
// change the play.
//
// Raises InvalidHistoryInputError, the same type verify_history raises for
// HistoryVerdict::InvalidInput. So a caller who catches that type also
// catches a bad card found here. It is a ValueError too.
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
        "2..14, never relative to a node's outstanding cards.\n\n"
        "A declarer strategy returns a Card, and a defender strategy returns\n"
        "weighted Cards. A strategy that reasons in relative ranks must\n"
        "convert to absolute first.")
        .def(py::init<int, int>(), py::arg("suit"), py::arg("rank"))
        .def_readwrite("suit", &be::Card::suit)
        .def_readwrite("rank", &be::Card::rank)
        .def(
            "__eq__",
            [](be::Card const& self, be::Card const& other) {
                return self.suit == other.suit && self.rank == other.rank;
            })
        // Python makes a type with only __eq__ unhashable. Hash the
        // (suit, rank) tuple, so that equal cards always hash equal.
        //
        // Card is mutable. The hash follows the fields, so changing a Card
        // that is already a key loses it. Making Card immutable would break
        // field assignment.
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

// A plain value type, with no per-call lifetime like BeliefView. So it is
// bound read-only and copyable, with no validity flag. A strategy receives
// one; Python never constructs one.
auto register_rank_map_bindings(py::module_& module) -> void
{
    py::class_<be::RankMap>(
        module,
        "RankMap",
        "The outstanding cards at one node, and the mapping between absolute\n"
        "and relative ranks (ObservationState.ranks).\n\n"
        "It is the same in every layout of the node: the layouts differ only\n"
        "in how the defenders' cards are split.")
        .def_property_readonly(
            "aggr",
            [](be::RankMap const& self) {
                return py::make_tuple(self.aggr[0], self.aggr[1], self.aggr[2], self.aggr[3]);
            },
            "The outstanding cards per suit: a four-element tuple of unsigned\n"
            "ints, indexed by suit. Bit (rank - 2) is set for each card. Note:\n"
            "remain_cards uses bit rank instead.")
        .def(
            "to_relative", &be::RankMap::to_relative, py::arg("suit"), py::arg("rank"),
            "Absolute rank -> relative ordinal, 1 = highest. Returns 0 if the\n"
            "card is not outstanding, or if suit or rank is out of range.")
        .def(
            "to_absolute", &be::RankMap::to_absolute, py::arg("suit"), py::arg("ordinal"),
            "Relative ordinal (1 = highest) -> absolute rank. Returns 0 if\n"
            "suit or ordinal is out of range. Use it before returning a Card,\n"
            "because a Card rank is always absolute.");
}

auto register_observation_state_bindings(py::module_& module) -> void
{
    // Read-only: a strategy receives one; Python never constructs one.
    py::class_<be::ObservationState>(
        module,
        "ObservationState",
        "The public part of a belief-evaluation node. It is the same in every\n"
        "layout of the belief space. A declarer strategy's play() receives it\n"
        "on every call. state_key() takes it too, but nothing calls\n"
        "state_key() yet.")
        .def_property_readonly(
            "trump", [](be::ObservationState const& self) { return self.trump; })
        .def_property_readonly(
            "first", [](be::ObservationState const& self) { return self.first; },
            "Seat on lead at the root.")
        .def_property_readonly(
            "history", [](be::ObservationState const& self) { return history_to_list(self.history); },
            "Every card played since the root, in order, as a list of Card.")
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
            "A deal dict. Declarer's and dummy's entries are exact. Each\n"
            "defender's entry is the **union** of both defenders' cards, not\n"
            "that defender's actual hand.")
        // --- derived on access from known_holdings, not stored.
        //
        // Two of these are easy to derive wrongly by hand. They are lazy
        // because pi is called thousands of times even for a small ending
        // (3487 for a four-card one, measured), and most calls read one or
        // two of them.
        .def_property_readonly(
            "seat_on_play",
            [](be::ObservationState const& self) { return be::seat_on_play(self.known_holdings); },
            "The seat (0..3) this call is for. A strategy has no other way\n"
            "to learn its seat.")
        .def_property_readonly(
            "trick_leader",
            [](be::ObservationState const& self) { return self.known_holdings.first; },
            "The seat that led to the trick **in progress**.\n\n"
            "Do not use `first` for this. `first` is the root leader and never\n"
            "changes. The two agree at the root and differ from the second\n"
            "trick on.")
        .def_property_readonly(
            "current_trick",
            [](be::ObservationState const& self) {
                py::list cards;
                for (int i = 0; i < 3; ++i) {
                    // Rank 0 marks an empty slot. Test the rank, not the
                    // suit: suit 0 is spades.
                    if (self.known_holdings.currentTrickRank[i] == 0) {
                        break;
                    }
                    cards.append(be::Card{self.known_holdings.currentTrickSuit[i],
                                          self.known_holdings.currentTrickRank[i]});
                }
                return cards;
            },
            "The cards already played to the trick in progress, in order.\n"
            "**Empty means this seat is leading.**\n\n"
            "These cards are in no hand in known_holdings. A played card\n"
            "leaves the hand at once.")
        .def_property_readonly(
            "position_in_trick",
            [](be::ObservationState const& self) {
                int played = 0;
                while (played < 3 && self.known_holdings.currentTrickRank[played] != 0) {
                    ++played;
                }
                return played;
            },
            "How many cards are already on the trick: 0 when leading, else\n"
            "1..3.")
        .def_property_readonly(
            "legal_cards",
            [](be::ObservationState const& self) {
                return be::enumerate_legal_cards(self.known_holdings,
                                                 be::seat_on_play(self.known_holdings));
            },
            "Every Card the seat on play may legally play, with follow-suit\n"
            "applied. Sorted by suit, then rank, ascending.\n\n"
            "Each card is legal in every layout of the node. Follow-suit\n"
            "depends only on the seat's hand, and declarer's and dummy's\n"
            "hands are public.")
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
            "Whether the seat on play holds a card of the suit led.\n\n"
            "**False when leading.** So `not state.can_follow_led_suit` means\n"
            "\"free to play any card\": true when leading and when void.")
        .def_property_readonly(
            "is_declaring_side",
            [](be::ObservationState const& self) {
                int const seat = be::seat_on_play(self.known_holdings);
                int const dummy = (self.declarer + 2) % DDS_HANDS;
                return seat == self.declarer || seat == dummy;
            },
            "Whether the seat on play is declarer **or dummy**. Always true in\n"
            "play(). Useful for a strategy shared by both sides.")
        .def_property_readonly(
            "ranks", [](be::ObservationState const& self) { return self.ranks; },
            "A RankMap for this node: the mapping between absolute and\n"
            "relative ranks. Each access returns a fresh copy.")
        .def_property_readonly(
            "play_record",
            [](be::ObservationState const& self) -> py::object {
                // Only this binding's evaluate() and HeuristicDefender set
                // play_record_valid, because a Python caller may keep the
                // state after the call (see types.hpp). Empty means no guard
                // applies.
                if (self.play_record_valid && ! *self.play_record_valid) {
                    throw py::value_error(
                        "play_record is only valid for the duration of the call that "
                        "produced this state -- that call has since returned, and the "
                        "record it pointed to no longer exists");
                }
                if (self.play_record == nullptr) {
                    return py::none();
                }
                return py::cast(*self.play_record);
            },
            "Every card played before the root, or None if the caller passed\n"
            "no PlayRecord.\n\n"
            "Not the same as history. play_record is fixed for the whole\n"
            "evaluation. history starts at the root and grows during the\n"
            "search. If you keep `state` after the call, reading this raises\n"
            "ValueError.");
}

// A BeliefView points into memory that lives only for one call (see
// make_belief_view). Python code often stores objects
// (`self.last_view = view`). Without a guard, a stored view would read
// freed memory and return plausible values.
//
// The guard is a validity flag owned by this binding: one
// shared_ptr<bool> per call. BeliefViewGuard clears it when the callback
// returns or raises. Every accessor checks it first.
//
// PyBeliefEntry shares the flag of its view, so a stored entry expires
// too. A `layout` already read is a copied dict, not a reference, so it
// stays valid. A Deal is small, so copying one per access is cheap.
//
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

// Makes a BeliefView valid until the callback returns. Every callback that
// hands a view to Python uses it, so none can forget to invalidate. The
// destructor runs whether the callback returns or raises.
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

    /// Not exposed to Python. Gives C++ code the real be::BeliefView.
    /// BruteForceDeclarer.__call__ uses it to pass the view to
    /// DeclarerStrategy::play directly.
    auto raw() const -> be::BeliefView const&
    {
        check_valid();
        return *view_;
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
        "One layout in a belief view, with its normalised posterior. The\n"
        "posteriors of one BeliefView sum to 1.\n\n"
        "Valid only while its BeliefView is valid. After the callback\n"
        "returns, either property raises ExpiredBeliefViewError.")
        .def_property_readonly("layout", &PyBeliefEntry::layout, "A deal dict, freshly copied on every access.")
        .def_property_readonly("posterior", &PyBeliefEntry::posterior);

    py::class_<PyBeliefView>(
        module,
        "BeliefView",
        "The belief space as declarer knows it now. play() receives one on\n"
        "every call. state_key() takes one too, but nothing calls it yet.\n\n"
        "**Valid only during that call.** Afterwards, any property of the\n"
        "view or of an entry from it raises ExpiredBeliefViewError. A layout\n"
        "already read is a copy and stays valid. Do not store the view or\n"
        "its entries.")
        .def_property_readonly("entries", &PyBeliefView::entries)
        .def_property_readonly(
            "is_sample", &PyBeliefView::is_sample,
            "False only when this node holds the whole remaining belief\n"
            "space, not a sample.")
        .def_property_readonly(
            "space_size", &PyBeliefView::space_size,
            "The number of consistent layouts, or 0 when is_sample is true.\n"
            "In a sample the true count is unknown. len(entries) is always\n"
            "the number of layouts in this view.");
}

// Testing support only. Builds a BeliefNode from the given layouts and
// weights, calls callback with its view, and invalidates the view on the
// way out. This is the same lifetime evaluate() gives a view, so the guard
// can be tested without a search.
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

// pi and delta as Python callables. The GIL rule for evaluate(): release
// the GIL for the C++ recursion, and acquire it in every trampoline that
// calls Python. These two do, and so do the layout source's size() and
// at(). There is one acquire site per callback kind, so there is one place
// to get it right. A missing acquire corrupts the interpreter as soon as
// two threads run.
//
// Attaches play_record_valid to a copy of state (see types.hpp). The
// evaluate() binding creates one call_valid flag per call, shares it with
// every state copy it hands out, and clears it when the call returns by
// any path. So play_record raises on a copy kept after the call.
auto state_for_python(be::ObservationState const& state, std::shared_ptr<bool> const& call_valid)
    -> be::ObservationState
{
    be::ObservationState copy = state;
    copy.play_record_valid = call_valid;
    return copy;
}

// play must be a pure function of (state, view); see DeclarerStrategy::play.
// A Python author can easily use `random.choice`. Then the card depends on
// the traversal order, not the node, and the result is silently wrong. Use
// `hash(seed, state) mod n` for variety.
auto make_declarer_strategy(
    be::StrategyId id, py::function const& play, std::optional<py::function> const& state_key,
    std::shared_ptr<bool> const& call_valid) -> be::DeclarerStrategy
{
    be::DeclarerStrategy strategy;
    strategy.id = id;
    strategy.play = [play, call_valid](be::ObservationState const& state, be::BeliefView const& view) -> be::Card {
        py::gil_scoped_acquire gil;
        BeliefViewGuard guard;
        py::object const result = play(
            py::cast(state_for_python(state, call_valid), py::return_value_policy::copy),
            py::cast(PyBeliefView(&view, guard.valid)));
        return py::cast<be::Card>(result);
    };
    if (state_key.has_value()) {
        py::function const key_fn = *state_key;
        strategy.state_key =
            [key_fn, call_valid](be::ObservationState const& state, be::BeliefView const& view) -> be::StateKey {
            py::gil_scoped_acquire gil;
            BeliefViewGuard guard;
            py::object const result = key_fn(
                py::cast(state_for_python(state, call_valid), py::return_value_policy::copy),
                py::cast(PyBeliefView(&view, guard.valid)));
            return std::string(py::cast<py::bytes>(result));
        };
    }
    // With no Python callable, state_key stays empty, which disables reuse
    // (see DeclarerStrategy::state_key).
    return strategy;
}

// delta receives three arguments: (layout: dict, seat: int,
// state: ObservationState). DefenderQuery is not bound, because it holds
// references that a Python object must not keep after the call.
//
// Each returned card must be held by seat in layout, and legal. The
// probabilities must be positive and sum to one. **Omit a card the
// strategy never plays.** Do not give it probability 0: the evaluator
// keeps a layout alive if its card has probability > 0, so a zero entry
// is not the same as no entry. This is the most likely mistake in a
// Python delta. validate_defender_distribution checks the result.
auto make_defender_strategy(py::function const& delta, std::shared_ptr<bool> const& call_valid)
    -> be::DefenderStrategy
{
    return [delta, call_valid](be::DefenderQuery const& query) -> std::vector<be::WeightedCard> {
        py::gil_scoped_acquire gil;
        py::object const result = delta(
            dds3_python::deal_to_dict(query.layout),
            query.seat,
            py::cast(state_for_python(query.state, call_valid), py::return_value_policy::copy));

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

// bound (EvaluateOptions::bound) is a Python callable: deal dict in, trick
// count out. It follows the same GIL rule as the other callbacks.
// Nothing can check it. A bound that is too *low* makes tier2_dead() fire
// wrongly and reports zero for a contract that makes. A bound that is too
// high only loses pruning. None gives an empty LayoutBound, which disables
// the cut.
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
    // Two types, because the fixes differ:
    // - NoLayoutSurvived: the whole source was checked and nothing was
    //   consistent. Fix the source.
    // - ScanBudgetExhausted: the budget ran out before any consistent
    //   layout was found. Raise the budget.
    root_failure_exceptions.emplace(
        be::RootFailure::NoLayoutSurvived,
        py::exception<void>(module, "NoLayoutSurvivedError", root_failure_base));
    root_failure_exceptions.emplace(
        be::RootFailure::ScanBudgetExhausted,
        py::exception<void>(module, "ScanBudgetExhaustedError", root_failure_base));

    // sample_size=0 is a bad argument value, like "trump has invalid value
    // 5" in dds3. The causes above are about the source content. So this is
    // the only RootFailure that also derives from ValueError. It is
    // rejected before any at() call, whatever the source holds (see
    // RootFailure::SampleSizeZero).
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

// Adds callback, seat and layout from EvaluationError as instance
// attributes. The exception is constructed with the message only, so
// args and str() behave normally. The cause is not an attribute, because
// the exception type names it.
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

// The entry point. Six positional arguments are always supplied. Each
// EvaluateOptions field is a keyword-only argument with its C++ default.
// So a caller cannot pass an option in the wrong position, and new options
// break nobody. EvaluateOptions itself is not bound, so there is only one
// way to pass an option.
//
// state_key belongs to DeclarerStrategy in C++. pi is a bare callable here,
// so state_key is a separate keyword-only argument. It must return bytes
// and be pure, like play.
//
// Python is stricter than C++ here: an error result raises. For example,
// sample_size=0 raises SampleSizeZeroError. See also replenish_below below.
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
    py::object const& state_key,
    py::object const& play_record) -> py::dict
{
    // Check declarer before the C++ evaluator sees it. make_root() indexes
    // remainCards[declarer] and remainCards[dummy] with no range check, and
    // there is no RootFailure for a bad argument.
    if (declarer < 0 || declarer >= DDS_HANDS) {
        throw py::value_error(
            "declarer has invalid value " + std::to_string(declarer) + " (expected range 0.." +
            std::to_string(DDS_HANDS - 1) + ")");
    }

    // A negative tricks_needed would satisfy already_made() at once
    // (tricks_won_by_declarer starts at 0), and silently return p_make=1.0.
    // 0 is valid: the contract is already made.
    constexpr int MaxTricksInADeal = 13;
    if (tricks_needed < 0 || tricks_needed > MaxTricksInADeal) {
        throw py::value_error(
            "tricks_needed has invalid value " + std::to_string(tricks_needed) +
            " (expected range 0.." + std::to_string(MaxTricksInADeal) + ")");
    }

    // In C++, replenish_below without sample_size does nothing, as
    // documented. In Python the option is a flat keyword argument, so this
    // is most likely a mistake: raise. scan_budget without sample_size is
    // valid on its own, so it is not checked.
    if (replenish_below.has_value() && ! sample_size.has_value()) {
        throw py::value_error(
            "replenish_below requires sample_size -- without it there is no "
            "top-up target to replenish towards. In C++ this is documented as "
            "a silent no-op; a Python caller has no doxygen to read that on, "
            "so this is far more likely to be a mistake than an intention");
    }

    std::optional<py::function> const state_key_fn =
        state_key.is_none() ? std::nullopt : std::make_optional(py::cast<py::function>(state_key));

    // Shared by every state copy handed out during this call (see
    // state_for_python). Cleared when the call returns by any path, so
    // play_record on a kept copy raises instead of reading freed memory.
    auto const call_valid = std::make_shared<bool>(true);
    struct CallValidInvalidator
    {
        std::shared_ptr<bool> valid;
        ~CallValidInvalidator()
        {
            *valid = false;
        }
    } const invalidate_call_valid{call_valid};

    Deal const root_deal = dds3_python::dict_to_deal(root);
    be::DeclarerStrategy const strategy = make_declarer_strategy(1, pi, state_key_fn, call_valid);
    be::DefenderStrategy const delta_fn = make_defender_strategy(delta, call_valid);

    be::EvaluateOptions options;
    options.retain_root = retain_root;
    options.collect_counters = collect_counters;
    options.bound = make_layout_bound(bound);
    options.delta_is_double_dummy_optimal = delta_is_double_dummy_optimal;
    options.sampling.sample_size = sample_size;
    options.sampling.scan_budget = scan_budget;
    options.sampling.replenish_below = replenish_below;
    options.play_record =
        play_record.is_none() ? std::nullopt : std::make_optional(py::cast<be::PlayRecord>(play_record));

    be::EvaluationResult result;
    {
        // Release the GIL for the whole recursion. Each trampoline
        // reacquires it only while it runs. A DoubleDummyDefender or
        // DoubleDummyBound releases it again around its solve, so the
        // solver still runs without the GIL.
        py::gil_scoped_release const release;
        result = be::evaluate(root_deal, declarer, tricks_needed, source, strategy, delta_fn, options);
    }

    // The evaluator reports a callback contract violation, or an unusable
    // source, in EvaluationResult::error. It does not throw. Turn that
    // report into an exception here. A Python exception raised inside a
    // callback is different: it passes through unchanged, with its own
    // type, message and traceback.
    if (result.error.has_value()) {
        be::EvaluationError const& error = *result.error;
        if (error.callback == be::EvaluationCallback::RootConstruction) {
            raise_root_failure(error.root_failure);
        }
        raise_validation_error(error.callback, error.seat, error.layout, error.validation);
    }

    return dds3_python::evaluation_result_to_dict(result);
}

// A rejected play history raises from the constructor. In C++ the
// constructor cannot report, so it uses the history_verdict() and
// constrained_space_status() accessors instead.
//
// There is one exception type per cause, so `except DuplicatedCardError`
// reads like `except FileNotFoundError`. Each derives from
// BeliefSpaceLocalEvaluationError, but not from RootFailureError or the
// callback contract errors. So a history failure is never caught as an
// evaluate() failure. One construction raises at most one of them: the
// first verdict that is not Consistent.
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

    // Only InvalidInput derives from ValueError too. It is a malformed
    // argument (a bad declarer, opening_leader or card), wrong whatever the
    // root. The other seven causes are a well-formed history that does not
    // fit this root, so they are not ValueErrors.
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

// Declared before list_to_history. Raises the same InvalidHistoryInputError
// as the InvalidInput verdict, but with a message that names the bad index
// and field.
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

// A LayoutSource holding one layout. See the binding below.
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
        // The binding checks the range first. This assert is the C++
        // contract, for calls through LayoutSource&.
        assert(index == 0);
        (void)index;
        return layout_;
    }

private:
    Deal layout_;
};

// The trampoline that lets Python subclass the abstract LayoutSource. Each
// override acquires the GIL, because evaluate() releases it.
//
// Written by hand, not with PYBIND11_OVERRIDE_PURE: Deal is passed as a
// dict, so the return value needs dict_to_deal. Each mistake in a subclass
// gives a clear Python error, not a crash:
// - a missing override: RuntimeError
// - a return that is not a dict: TypeError
// - a dict that is not a valid deal: ValueError
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

// The number of distinct cards in the two defender hands. It matches the
// count in defender_pool_decomposition, which src/ does not expose.
//
// dict_to_deal checks only the bits of each value. binomial_coefficient,
// and so size() and at(), work only up to 26 cards. The
// ExhaustiveLayoutSource constructor below checks this limit after the
// history checks, so a root rejected for a history reason reports that
// reason.
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

// PlayRecord reuses list_to_history, history_to_list and
// raise_history_rejected, so it converts and raises exactly like
// ExhaustiveLayoutSource.
auto register_play_record_bindings(py::module_& module) -> void
{
    py::class_<be::PlayRecord>(
        module,
        "PlayRecord",
        "Every card played before an evaluation's root, and the opening\n"
        "leader. Public knowledge, and immutable.\n\n"
        "The constructor checks that each card is valid, that no card\n"
        "repeats, and that opening_leader is a seat. It raises the same\n"
        "exceptions as ExhaustiveLayoutSource. It does NOT check that\n"
        "opening_leader led the first trick: that needs a root, so\n"
        "ExhaustiveLayoutSource checks it.")
        .def(
            py::init([](py::sequence const& cards, int opening_leader) {
                PlayTraceBin const trace = list_to_history(cards);
                auto [record, verdict] = be::PlayRecord::create(trace, opening_leader);
                if (verdict != be::HistoryVerdict::Consistent) {
                    raise_history_rejected(verdict);
                }
                return *record;
            }),
            py::arg("cards"),
            py::arg("opening_leader"))
        .def_property_readonly(
            "cards", [](be::PlayRecord const& self) { return history_to_list(self.cards()); })
        .def_property_readonly("opening_leader", &be::PlayRecord::opening_leader);
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
        "An ordered, indexed list of candidate layouts.\n\n"
        "Subclass it and override size() and at(i) to supply a belief space\n"
        "narrower than the root gives, for example to apply the bidding.\n"
        "at(i) must return a deal dict, and the same layout for the same i.\n\n"
        "**The order must be random. Nothing can check this.** Sampling\n"
        "takes a prefix of the order. A sorted or grouped order gives a\n"
        "biased sample, with no warning. ExhaustiveLayoutSource meets this\n"
        "obligation. A common mistake is to return self._deals[i] from a\n"
        "list in the order it was built.")
        .def(py::init<>());

    // A source over exactly one layout. It lives here, not in src/, because
    // it saves a Python caller from writing a subclass. A C++ caller can
    // write it in three lines.
    py::class_<SingleLayoutSource, be::LayoutSource>(
        module,
        "SingleLayoutSource",
        "A LayoutSource over exactly one layout: size() is 1 and at(0) is\n"
        "that layout.\n\n"
        "Use it to evaluate one layout alone. For example, check that\n"
        "P_make over a belief space equals the mean of P_make over each\n"
        "layout. This check is worth making for any strategy pair.\n\n"
        "One layout has only one order, so the random-order rule of\n"
        "LayoutSource does not apply.")
        .def(py::init([](py::dict const& layout) {
                 return SingleLayoutSource(dds3_python::dict_to_deal(layout));
             }),
             py::arg("layout"))
        .def("size", [](SingleLayoutSource const& self) { return self.size().value(); })
        .def(
            "at",
            [](SingleLayoutSource const& self, py::object const& index_obj) {
                // As for ExhaustiveLayoutSource.at below: an out-of-range
                // index raises IndexError, and the index is compared as a
                // Python int.
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
        "Lists every layout consistent with root, and with the voids that\n"
        "record shows, if a record is given.\n\n"
        "record is optional. Without it, no voids apply. That is correct at\n"
        "trick one and **silently wrong at a mid-play root**.\n\n"
        "A record is checked against root, and the constructor raises:\n\n"
        "- a HistoryRejectedError subclass if the history does not fit root.\n"
        "  The void check is then skipped, so only this error is raised.\n"
        "- a ConstrainedSpaceEmptyError subclass if the history fits but\n"
        "  leaves no legal layout.")
        .def(
            py::init([](py::dict const& root, int declarer, std::uint64_t seed, py::object const& record) {
                // Check declarer here. history_verdict() checks it only
                // when a record is given.
                if (declarer < 0 || declarer >= DDS_HANDS) {
                    raise_history_rejected(be::HistoryVerdict::InvalidInput);
                }
                Deal const root_deal = dds3_python::dict_to_deal(root);
                std::optional<be::PlayRecord> const cpp_record =
                    record.is_none() ? std::nullopt : std::make_optional(py::cast<be::PlayRecord>(record));
                be::ExhaustiveLayoutSource source(root_deal, declarer, seed, cpp_record);
                be::HistoryVerdict const verdict = source.history_verdict();
                if (verdict != be::HistoryVerdict::Consistent) {
                    raise_history_rejected(verdict);
                }
                be::ConstrainedSpaceStatus const status = source.constrained_space_status();
                if (status != be::ConstrainedSpaceStatus::Ok) {
                    raise_constrained_space_empty(status);
                }
                // Needed only after both checks above pass: otherwise
                // size() never calls binomial_coefficient. The free cards
                // are a subset of the pool, so checking the pool is safe.
                // It also rejects a pool over 26 that voids would shrink,
                // but no real deal has such a pool.
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
            py::arg("record") = py::none())
        .def(
            "size",
            [](be::ExhaustiveLayoutSource const& self) -> py::object {
                auto const value = self.size();
                return value.has_value() ? py::cast(*value) : py::none();
            })
        .def(
            "at",
            [](be::ExhaustiveLayoutSource const& self, py::object const& index_obj) {
                // Check the range here. The C++ at() only asserts, and
                // -c opt compiles the assert out. So an out-of-range index
                // raises IndexError on every build.
                //
                // index is a py::object, not a uint64_t. A negative or
                // too-large int has no uint64_t form, so pybind11 would
                // raise TypeError or OverflowError before this check.
                // Compare as Python ints instead.
                //
                // Use PyNumber_Index (operator.index), not a cast to
                // py::int_. The cast truncates floats, so at(1.5) would
                // silently become at(1). PyNumber_Index raises TypeError,
                // as Python list indexing does.
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
            "Consistent, or the cause (see HistoryVerdict). On a constructed\n"
            "source this is always Consistent, because any other verdict\n"
            "raises in the constructor.")
        .def(
            "constrained_space_status", &be::ExhaustiveLayoutSource::constrained_space_status,
            "Ok, or the cause (see ConstrainedSpaceStatus). On a constructed\n"
            "source this is always Ok, because any other status raises in the\n"
            "constructor.");
}

// SolverContext comes from dds3. This module never registers
// py::class_<SolverContext> itself. pybind11 registers types per module, so
// a second registration would create a second, incompatible Python type.
// dds3 registers it, and this module only uses it. pybind11 shares
// registrations between extensions built with the same toolchain, if dds3
// is imported first. The package __init__.py does that.
//
// Converts a FutureTricks to the dict shape dds3.solve_board() returns:
// {'nodes', 'cards', 'suit', 'rank', 'equals', 'score'}. Only the first
// fut.cards entries are defined, so only those are copied.
auto future_tricks_to_dict(FutureTricks const& fut) -> py::dict
{
    py::dict result;
    result["nodes"] = fut.nodes;
    result["cards"] = fut.cards;
    py::list suit;
    py::list rank;
    py::list equals;
    py::list score;
    for (int i = 0; i < fut.cards; ++i) {
        suit.append(fut.suit[i]);
        rank.append(fut.rank[i]);
        equals.append(fut.equals[i]);
        score.append(fut.score[i]);
    }
    result["suit"] = suit;
    result["rank"] = rank;
    result["equals"] = equals;
    result["score"] = score;
    return result;
}

// DefenderHeuristicContext holds references that are valid only during one
// HeuristicDefender call, like BeliefView. Each accessor checks a per-call
// validity flag, so a stored context raises instead of reading freed
// memory.
class PyDefenderHeuristicContext
{
public:
    PyDefenderHeuristicContext(be::DefenderHeuristicContext const* ctx, std::shared_ptr<bool> valid)
        : ctx_(ctx), valid_(std::move(valid))
    {
    }

    auto layout() const -> py::dict
    {
        check_valid();
        return dds3_python::deal_to_dict(ctx_->layout);
    }

    auto fut() const -> py::dict
    {
        check_valid();
        return future_tricks_to_dict(ctx_->fut);
    }

    /// A copy of the state. Every field is safe to keep after the call.
    /// `play_record_valid` shares this context's flag, so `.play_record`
    /// on a kept copy raises ValueError, like the other accessors here.
    /// See `ObservationState::play_record_valid` (types.hpp).
    auto state() const -> be::ObservationState
    {
        check_valid();
        be::ObservationState copy = ctx_->state;
        copy.play_record_valid = valid_;
        return copy;
    }

    auto seat() const -> int
    {
        check_valid();
        return ctx_->seat;
    }

    auto position_in_trick() const -> int
    {
        check_valid();
        return ctx_->position_in_trick;
    }

    auto on_lead_to_trick() const -> int
    {
        check_valid();
        return ctx_->on_lead_to_trick;
    }

    auto was_on_lead() const -> bool
    {
        check_valid();
        return ctx_->was_on_lead;
    }

    auto can_follow_led_suit() const -> bool
    {
        check_valid();
        return ctx_->can_follow_led_suit;
    }

    auto defending_side() const -> bool
    {
        check_valid();
        return ctx_->defending_side;
    }

    auto trump() const -> int
    {
        check_valid();
        return ctx_->trump;
    }

private:
    auto check_valid() const -> void
    {
        if (! valid_ || ! *valid_) {
            throw py::value_error(
                "this context has expired -- it is valid only for the duration "
                "of the rule call it was handed to");
        }
    }

    be::DefenderHeuristicContext const* ctx_;
    std::shared_ptr<bool> valid_;
};

// Wraps a Python rule, (context, best_cards) -> Card | None, as a
// be::DefenderHeuristic. Like make_defender_strategy, it reacquires the
// GIL per call, because the caller runs with the GIL released.
auto wrap_python_heuristic(py::function rule) -> be::DefenderHeuristic
{
    return [rule = std::move(rule)](
               be::DefenderHeuristicContext const& ctx, std::vector<be::Card> const& best_cards)
        -> std::optional<be::Card>
    {
        py::gil_scoped_acquire const gil;
        auto valid = std::make_shared<bool>(true);
        struct Invalidator
        {
            std::shared_ptr<bool> valid;
            ~Invalidator()
            {
                *valid = false;
            }
        } invalidator{valid};

        py::object const result = rule(PyDefenderHeuristicContext(&ctx, valid), py::cast(best_cards));
        if (result.is_none()) {
            return std::nullopt;
        }
        return py::cast<be::Card>(result);
    };
}

auto register_defender_heuristic_bindings(py::module_& module) -> void
{
    // Opaque from Python. Only a DefenderHeuristicChain calls one, from C++.
    // The six factory functions below create them.
    py::class_<be::DefenderHeuristic>(
        module,
        "DefenderHeuristic",
        "One rule in a defender heuristic chain. Created by second_seat_low,\n"
        "high_in_third, third_seat_low, fourth_seat_low, ruff_small or\n"
        "discard_keep_winners. Pass it to DefenderHeuristicChain.add().\n\n"
        "add() also accepts a plain Python function,\n"
        "(context, best_cards) -> Card | None.");

    module.def(
        "second_seat_low", &be::second_seat_low, py::arg("randomize_touching_honors") = true,
        "A defender in second seat, following suit: play the lowest optimal\n"
        "card.\n\n"
        "If that card touches another optimal card of the suit, and\n"
        "randomize_touching_honors is True (the default), defer. The\n"
        "fallback spread of the chain then picks between them: restricted\n"
        "choice.");
    module.def(
        "high_in_third", &be::high_in_third,
        "A defender in third seat, after partner's lead: play the highest\n"
        "optimal card, if it beats the card of the second player. The\n"
        "second player is not always dummy (see the C++ doxygen).");
    module.def(
        "third_seat_low", &be::third_seat_low,
        "A defender in third seat, after partner's lead: play the lowest\n"
        "optimal card when high_in_third defers, because the second\n"
        "player's card beats every candidate.");
    module.def(
        "fourth_seat_low", &be::fourth_seat_low,
        "A defender in fourth seat, following suit: play the lowest optimal\n"
        "card.");
    module.def(
        "ruff_small", &be::ruff_small,
        "A defender void in the led suit, in a trump contract: ruff with the\n"
        "smallest optimal trump. Defer if declarer or dummy, still to play,\n"
        "could overruff.");
    module.def(
        "discard_keep_winners", &be::discard_keep_winners,
        "A defender discarding: play the lowest optimal card from the suit\n"
        "with the fewest remaining tricks for the defenders.");

    py::class_<PyDefenderHeuristicContext>(
        module,
        "DefenderHeuristicContext",
        "Everything a Python rule needs. **Valid only during one rule call**,\n"
        "like a BeliefView. Afterwards, any property raises ValueError.")
        .def_property_readonly("layout", &PyDefenderHeuristicContext::layout)
        .def_property_readonly("fut", &PyDefenderHeuristicContext::fut)
        .def_property_readonly("state", &PyDefenderHeuristicContext::state)
        .def_property_readonly("seat", &PyDefenderHeuristicContext::seat)
        .def_property_readonly("position_in_trick", &PyDefenderHeuristicContext::position_in_trick)
        .def_property_readonly("on_lead_to_trick", &PyDefenderHeuristicContext::on_lead_to_trick)
        .def_property_readonly("was_on_lead", &PyDefenderHeuristicContext::was_on_lead)
        .def_property_readonly("can_follow_led_suit", &PyDefenderHeuristicContext::can_follow_led_suit)
        .def_property_readonly("defending_side", &PyDefenderHeuristicContext::defending_side)
        .def_property_readonly("trump", &PyDefenderHeuristicContext::trump);

    py::class_<be::DefenderHeuristicChain>(
        module,
        "DefenderHeuristicChain",
        "An ordered list of defender rules, tried in the order added. The\n"
        "chain is exactly the add() calls you make. There are no other\n"
        "options.")
        .def(py::init<>())
        .def(
            "add",
            [](be::DefenderHeuristicChain& self, py::object const& heuristic) {
                if (py::isinstance<be::DefenderHeuristic>(heuristic)) {
                    self.add(py::cast<be::DefenderHeuristic>(heuristic));
                } else {
                    self.add(wrap_python_heuristic(py::cast<py::function>(heuristic)));
                }
            },
            py::arg("heuristic"),
            "Appends heuristic to the chain. It is a DefenderHeuristic from a\n"
            "factory function, or a Python function,\n"
            "(context, best_cards) -> Card | None.");

    module.def(
        "make_default_defender_heuristics", &be::make_default_defender_heuristics, py::arg("trump"),
        py::arg("randomize_touching_honors") = true,
        "Builds the shipped chain, for convenience. Equivalent to adding, in\n"
        "order: second_seat_low(randomize_touching_honors), high_in_third(),\n"
        "third_seat_low(), fourth_seat_low(), ruff_small() (only if trump is a\n"
        "suit), and discard_keep_winners().\n\n"
        "For another order, a subset, or your own rules, build a\n"
        "DefenderHeuristicChain directly.");
}

// DoubleDummyDefender and DoubleDummyBound hold a SolverContext& that they
// do not own. py::keep_alive<1, 2>() on both constructors keeps ctx
// (argument 2) alive as long as the new object (argument 1).
class PyDoubleDummyDefender
{
public:
    PyDoubleDummyDefender(SolverContext& ctx, be::SpreadPolicy policy) : defender_(ctx, policy)
    {
    }

    // Works directly as delta: (layout, seat, state). state is unused.
    // solve_board finds the seat on play from the deal itself.
    auto call(py::dict const& layout, int seat, py::object const& state) -> py::list
    {
        (void)state;
        Deal const deal = dds3_python::dict_to_deal(layout);
        be::DefenderStrategy const strategy = defender_.as_strategy();

        std::vector<be::WeightedCard> weighted;
        {
            // Release the GIL for the solve. This release is nested inside
            // the acquire in make_defender_strategy.
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

    // Works directly as bound: (deal dict) -> int.
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

// HeuristicDefender has the same SolverContext& contract as
// DoubleDummyDefender. Unlike it, the chain reads state.declarer
// (defending_side depends on it), and a Python rule may read the rest.
// So this accepts either a real ObservationState, used whole, or any object
// with a `.declarer` attribute, for a direct call.
auto observation_state_from(py::object const& state) -> be::ObservationState
{
    if (py::isinstance<be::ObservationState>(state)) {
        return py::cast<be::ObservationState>(state);
    }
    be::ObservationState state_value{};
    state_value.declarer = py::cast<int>(state.attr("declarer"));
    return state_value;
}

class PyHeuristicDefender
{
public:
    PyHeuristicDefender(SolverContext& ctx, be::DefenderHeuristicChain chain, be::SpreadPolicy fallback_policy)
        : defender_(ctx, std::move(chain), fallback_policy)
    {
    }

    auto call(py::dict const& layout, int seat, py::object const& state) -> py::list
    {
        Deal const deal = dds3_python::dict_to_deal(layout);
        be::ObservationState const state_value = observation_state_from(state);
        be::DefenderStrategy const strategy = defender_.as_strategy();

        std::vector<be::WeightedCard> weighted;
        {
            // Release the GIL for the solve. A Python rule reacquires it
            // (see wrap_python_heuristic).
            py::gil_scoped_release const release;
            weighted = strategy(be::DefenderQuery{deal, seat, state_value});
        }

        py::list result;
        for (be::WeightedCard const& card : weighted) {
            result.append(py::make_tuple(card.card, card.probability));
        }
        return result;
    }

private:
    be::HeuristicDefender defender_;
};

// The opponent_model wrapper needs no new play_record guard. Each state
// here is a copy of the state play() received (make_internal_root), and a
// copy keeps its play_record_valid. Through evaluate(), that is the outer
// call's flag (state_for_python), so the guard carries through. Do not
// reuse make_defender_strategy: it expects a call_valid of its own.
//
// The state must still be copied. query.state refers into a BeliefNode
// that the search keeps changing, so a Python opponent_model that keeps
// `state` would read reused memory. py::return_value_policy::copy gives
// Python its own copy, as in make_defender_strategy.
auto make_opponent_model(std::optional<py::function> const& opponent_model) -> be::DefenderStrategy
{
    if (! opponent_model.has_value()) {
        // Empty: BruteForceDeclarer then owns a DoubleDummyDefender over
        // the same ctx.
        return nullptr;
    }
    py::function const delta = *opponent_model;
    return [delta](be::DefenderQuery const& query) -> std::vector<be::WeightedCard> {
        py::gil_scoped_acquire gil;
        py::object const result = delta(
            dds3_python::deal_to_dict(query.layout), query.seat,
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

/// A BruteForceDeclarer as a Python callable, __call__(state, view). It
/// works directly as evaluate()'s pi. pybind11 converts state and view,
/// because both types are registered.
class PyBruteForceDeclarer
{
public:
    PyBruteForceDeclarer(
        SolverContext& ctx, be::DeclarerObjective objective,
        std::optional<py::function> const& opponent_model, std::optional<int> max_depth,
        std::optional<std::uint64_t> max_layouts)
        : declarer_(
              ctx, objective, make_opponent_model(opponent_model),
              be::BruteForceOptions{max_depth, max_layouts})
    {
    }

    auto call(be::ObservationState const& state, PyBeliefView const& view) -> be::Card
    {
        be::DeclarerStrategy const strategy = declarer_.as_strategy();
        // Call view.raw() before releasing the GIL. On an expired view it
        // raises through the Python C API, which needs the GIL.
        be::BeliefView const& raw_view = view.raw();
        try {
            py::gil_scoped_release const release;  // a Python opponent_model reacquires it
                                                    // (make_opponent_model)
            return strategy.play(state, raw_view);
        } catch (be::DeclarerStrategyContractViolation const& violation) {
            // The catch in expand_declarer_node (expand.cpp) works only in
            // pure C++. From Python, this __call__ is a pybind11 method, and
            // pybind11 would turn the exception into a plain RuntimeError,
            // losing validation, seat and offending_layout. So catch it
            // here and raise through raise_validation_error.
            //
            // Report DefenderStrategy, not DeclarerPlay, as
            // declarer_expand_error (evaluate.cpp) does: the internal
            // opponent model broke the contract. The GIL is held again
            // here, because `release` was destroyed during unwinding.
            raise_validation_error(
                be::EvaluationCallback::DefenderStrategy, violation.seat, violation.offending_layout,
                violation.validation);
        }
    }

private:
    be::BruteForceDeclarer declarer_;
};

auto register_solver_seam_bindings(py::module_& module) -> void
{
    py::enum_<be::SpreadPolicy>(
        module,
        "SpreadPolicy",
        "How DoubleDummyDefender spreads probability over the cards tied for\n"
        "best. The two values have different justifications; see each one.")
        .value(
            "TouchingSequence", be::SpreadPolicy::TouchingSequence,
            "Even over the touching sequence of the single best card. Those\n"
            "cards are interchangeable in the layout, so this is restricted\n"
            "choice, not a modelling guess.")
        .value(
            "AllOptimal", be::SpreadPolicy::AllOptimal,
            "Even over the touching sequences of every card tied for best,\n"
            "across suits. These cards are equally good but not equivalent:\n"
            "the positions differ, and the spread depends on how many suits\n"
            "tie. This needs the advanced justification in algorithm.md.");

    py::class_<PyDoubleDummyDefender>(
        module,
        "DoubleDummyDefender",
        "A defender strategy backed by the solver. It solves a layout double\n"
        "dummy and spreads probability over the cards tied for best,\n"
        "according to policy. Use it directly as evaluate()'s delta.\n\n"
        "**It is not best defence against a contract.** It maximises tricks\n"
        "(target = -1), and sometimes gives up the contract to do so. It\n"
        "still satisfies delta_is_double_dummy_optimal, which is about\n"
        "trick count.\n\n"
        "**ctx is not thread-safe.** Use one SolverContext per thread.\n"
        "__call__ releases the GIL during the solve, so two threads sharing\n"
        "ctx really do run at the same time. Create one DoubleDummyDefender\n"
        "and one SolverContext per thread. You own ctx: create it,\n"
        "configure it, and keep it alive.")
        .def(
            py::init<SolverContext&, be::SpreadPolicy>(), py::arg("ctx"),
            py::arg("policy") = be::SpreadPolicy::TouchingSequence, py::keep_alive<1, 2>())
        .def("__call__", &PyDoubleDummyDefender::call, py::arg("layout"), py::arg("seat"), py::arg("state"));

    py::class_<PyDoubleDummyBound>(
        module,
        "DoubleDummyBound",
        "A layout bound backed by the solver: declarer's double-dummy trick\n"
        "count from a layout, whoever is on lead. Use it directly as\n"
        "evaluate()'s bound.\n\n"
        "**declarer is fixed for the life of this object.** Do not reuse it\n"
        "for another declarer. The bound would be wrong, and the\n"
        "bound-gated cut would give a wrong answer.\n\n"
        "The cut is sound (R <= DD) only if delta is double-dummy optimal\n"
        "for trick count. Pair this with a DoubleDummyDefender, and pass\n"
        "delta_is_double_dummy_optimal=True to evaluate().\n\n"
        "The ctx rules of DoubleDummyDefender apply.")
        .def(
            py::init([](SolverContext& ctx, int declarer) {
                // Check declarer once here. as_bound() indexes
                // remainCards[declarer_] with no range check.
                if (declarer < 0 || declarer >= DDS_HANDS) {
                    throw py::value_error(
                        "declarer has invalid value " + std::to_string(declarer) +
                        " (expected range 0.." + std::to_string(DDS_HANDS - 1) + ")");
                }
                return PyDoubleDummyBound(ctx, declarer);
            }),
            py::arg("ctx"), py::arg("declarer"), py::keep_alive<1, 2>())
        .def("__call__", &PyDoubleDummyBound::call, py::arg("layout"));

    py::class_<PyHeuristicDefender>(
        module,
        "HeuristicDefender",
        "A defender strategy that applies a DefenderHeuristicChain. If every\n"
        "rule defers, it uses the spread of DoubleDummyDefender. It is a\n"
        "second delta, not a replacement for DoubleDummyDefender.\n\n"
        "**You build the chain.** This class never builds a default chain.\n\n"
        "The ctx rules of DoubleDummyDefender apply.")
        .def(
            py::init<SolverContext&, be::DefenderHeuristicChain, be::SpreadPolicy>(), py::arg("ctx"),
            py::arg("chain"), py::arg("fallback_policy") = be::SpreadPolicy::TouchingSequence,
            py::keep_alive<1, 2>())
        .def("__call__", &PyHeuristicDefender::call, py::arg("layout"), py::arg("seat"), py::arg("state"));

    py::enum_<be::DeclarerObjective>(
        module,
        "DeclarerObjective",
        "The quantity BruteForceDeclarer maximises. Fixed for the life of an\n"
        "instance, because all its searches share one cache. Mixing\n"
        "objectives would corrupt the cached values.")
        .value("MaximizeExpectedTricks", be::DeclarerObjective::MaximizeExpectedTricks)
        .value("MaximizeProbabilityToMake", be::DeclarerObjective::MaximizeProbabilityToMake);

    py::class_<PyBruteForceDeclarer>(
        module,
        "BruteForceDeclarer",
        "A declarer strategy that searches ahead over the belief space and\n"
        "keeps the best legal card for its DeclarerObjective. max_depth and\n"
        "max_layouts limit the search. Use it directly as evaluate()'s pi.\n\n"
        "**Match opponent_model to the paired delta.** With another delta,\n"
        "evaluate() still returns a well-defined p_make, but only for that\n"
        "pairing, not for double-dummy-optimal play.\n\n"
        "**An opponent_model you supply must answer from the position only.**\n"
        "It must not read the history or play_record of state, although an\n"
        "ordinary delta may. The cache key leaves them out, so that two\n"
        "play() calls on one line of play can share a value. Nothing checks\n"
        "this.\n\n"
        "The cache belongs to this instance:\n\n"
        "- With max_depth=None, it is kept between calls and is never wrong.\n"
        "  Hits between calls are rare: each call scales its root to sum to\n"
        "  1, and the search never rescales below that.\n"
        "- Within one call, transpositions still hit the cache.\n"
        "- With max_depth set, a cutoff depends on the depth of the current\n"
        "  call, so each call starts with an empty cache.\n"
        "- objective and opponent_model are fixed per instance, so one cache\n"
        "  never mixes them.\n\n"
        "With opponent_model=None, it owns a DoubleDummyDefender over ctx.\n"
        "The ctx rules of DoubleDummyDefender apply.")
        .def(
            py::init<
                SolverContext&, be::DeclarerObjective, std::optional<py::function> const&,
                std::optional<int>, std::optional<std::uint64_t>>(),
            py::arg("ctx"), py::arg("objective") = be::DeclarerObjective::MaximizeExpectedTricks,
            py::arg("opponent_model") = py::none(), py::arg("max_depth") = py::none(),
            py::arg("max_layouts") = py::none(), py::keep_alive<1, 2>())
        .def("__call__", &PyBruteForceDeclarer::call, py::arg("state"), py::arg("view"));
}

// Testing support: probes for the converters and the trampoline. Not public:
// the names start with an underscore, and __init__.py does not re-export them.
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

    // A Python call to source.size() finds the Python method first and
    // never reaches the trampoline. Only C++ code calling through a
    // LayoutSource& does, as evaluate() does. These two probes call through
    // a LayoutSource const&, to test the trampoline without evaluate().
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
        "Builds a BeliefNode from layouts and weights, calls callback(view),\n"
        "then invalidates the view. Tests the validity guard without\n"
        "evaluate(). Testing support only.");
}

}  // namespace

PYBIND11_MODULE(_belief_space_local_evaluation, module)
{
    module.doc() = "belief_space_local_evaluation Python extension";

    // The base exception first: every other exception derives from it.
    register_root_exception_bindings(module);

    register_card_bindings(module);
    register_rank_map_bindings(module);
    register_observation_state_bindings(module);
    register_belief_view_bindings(module);
    register_strategy_error_bindings(module);
    register_root_failure_error_bindings(module);
    register_validation_error_bindings(module);
    register_history_error_bindings(module);
    // After register_history_error_bindings, which fills the map that
    // raise_history_rejected reads.
    register_play_record_bindings(module);
    register_layout_source_bindings(module);
    // Before register_solver_seam_bindings: the HeuristicDefender
    // constructor takes a DefenderHeuristicChain, which must be registered.
    register_defender_heuristic_bindings(module);
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
        py::arg("play_record") = py::none(),
        "Computes P_make for pi against delta, over the belief space that\n"
        "source lists for root.\n\n"
        "Arguments:\n"
        "    root: the deal dict at the root.\n"
        "    declarer: declarer's seat, 0..3.\n"
        "    tricks_needed: tricks declarer still needs, 0..13.\n"
        "    source: a LayoutSource.\n"
        "    pi: the declarer strategy, pi(state, view) -> Card.\n"
        "    delta: the defender strategy,\n"
        "        delta(layout, seat, state) -> [(Card, probability), ...].\n\n"
        "Keyword-only options, with the defaults of EvaluateOptions in C++:\n"
        "retain_root, collect_counters, bound, delta_is_double_dummy_optimal,\n"
        "sample_size, scan_budget, replenish_below, play_record. Also\n"
        "state_key, which belongs to the declarer strategy in C++ (default\n"
        "None).\n\n"
        "Returns a dict. result['by_strategy'][1] holds 'p_make',\n"
        "'root_children' and 'root_is_declaring_side'.\n\n"
        "Raises:\n"
        "    ValueError: for a bad argument, or replenish_below without\n"
        "        sample_size.\n"
        "    RootFailureError subclasses: if no root can be built.\n"
        "    CallbackContractError subclasses: if pi or delta breaks its\n"
        "        contract.\n"
        "An exception raised inside pi, delta or the source passes through\n"
        "unchanged.\n\n"
        "See docs/belief_space_local_evaluation.md.");

    // The trick primitives from trick.hpp: the four functions the evaluator
    // uses to walk a position. A Python caller who reaches a mid-play root
    // with them applies the same rules as the evaluator. A caller's own
    // version that differs gives a wrong root, with no error.
    //
    // All four take and return deal dicts, the shape of
    // ObservationState.known_holdings.
    module.def(
        "seat_on_play",
        [](py::dict const& deal) { return be::seat_on_play(dds3_python::dict_to_deal(deal)); },
        py::arg("deal"),
        "The seat (0..3) on play at deal: deal['first'] advanced by the\n"
        "number of cards on the trick in progress.\n\n"
        "deal['first'] is the leader of the current trick. It is not\n"
        "ObservationState.first, the root leader. From a strategy, pass\n"
        "state.known_holdings.");

    module.def(
        "legal_cards",
        [](py::dict const& deal, int seat) {
            return be::enumerate_legal_cards(dds3_python::dict_to_deal(deal), seat);
        },
        py::arg("deal"),
        py::arg("seat"),
        "Every Card seat may legally play at deal, with follow-suit applied.\n\n"
        "Sorted by suit, then rank, ascending. evaluate() builds root_children\n"
        "by indexing into this list, so it uses the same order.\n\n"
        "Returns Cards, not the per-suit bitmasks of the C++ legal_cards(),\n"
        "because a strategy must return a Card.");

    module.def(
        "trick_complete_winner",
        [](py::dict const& deal, be::Card const& card) {
            return be::trick_complete_winner(dds3_python::dict_to_deal(deal), card);
        },
        py::arg("deal"),
        py::arg("card"),
        "The seat that wins the trick in progress when card is played fourth.\n"
        "deal must hold exactly three played cards in current_trick_suit and\n"
        "current_trick_rank.\n\n"
        "The highest trump wins. With no trump, the highest card of the led\n"
        "suit wins. A discard never wins.");

    module.def(
        "play",
        [](py::dict const& deal, be::Card const& card) {
            return dds3_python::deal_to_dict(be::play(dds3_python::dict_to_deal(deal), card));
        },
        py::arg("deal"),
        py::arg("card"),
        "Returns the deal after the seat on play plays card. The card leaves\n"
        "that seat's remain_cards and joins the trick in progress. If it\n"
        "completes the trick, the trick is cleared and 'first' becomes the\n"
        "winner.\n\n"
        "The deal passed in is not changed. There is no trick counter: use\n"
        "play_out to count tricks.");

    // evaluate() trusts tricks_needed, because the evaluator has no trick
    // counter (trick.hpp). An off-by-one does not fail: it evaluates a
    // different contract.
    //
    // play_out returns the root and the count together, so they cannot
    // disagree. It lives here, not in C++, because the risk is Python code
    // that replays a hand. A C++ caller builds a root directly.
    module.def(
        "play_out",
        [](py::dict const& deal, py::sequence const& history, int opening_leader,
           int declarer) {
            if (opening_leader < 0 || opening_leader >= DDS_HANDS) {
                throw py::value_error(
                    "opening_leader has invalid value " + std::to_string(opening_leader) +
                    " (expected range 0.." + std::to_string(DDS_HANDS - 1) + ")");
            }
            if (declarer < 0 || declarer >= DDS_HANDS) {
                throw py::value_error(
                    "declarer has invalid value " + std::to_string(declarer) +
                    " (expected range 0.." + std::to_string(DDS_HANDS - 1) + ")");
            }

            Deal current = dds3_python::dict_to_deal(deal);
            current.first = opening_leader;
            int const dummy = (declarer + 2) % DDS_HANDS;
            int won = 0;

            std::size_t index = 0;
            for (py::handle const item : history) {
                be::Card const card = py::cast<be::Card>(item);
                int const seat = be::seat_on_play(current);

                // be::play does not check legality: an illegal card gives a
                // nonsense position. A history is caller data, often a
                // transcribed hand, so check it here and name the index.
                std::array<unsigned, DDS_SUITS> const legal = be::legal_cards(current, seat);
                if (card.suit < 0 || card.suit >= DDS_SUITS || card.rank < 2 || card.rank > 14 ||
                    (legal[static_cast<std::size_t>(card.suit)] & (1u << card.rank)) == 0) {
                    throw py::value_error(
                        "history[" + std::to_string(index) + "] is not a legal play for seat " +
                        std::to_string(seat) + ": " + std::string(py::repr(py::cast(card))));
                }

                bool const completes_the_trick = current.currentTrickRank[2] != 0;
                current = be::play(current, card);
                // Only a completed trick has a winner, and be::play has set
                // `first` to it. An incomplete last trick counts for nobody.
                if (completes_the_trick && (current.first == declarer || current.first == dummy)) {
                    ++won;
                }
                ++index;
            }

            return py::make_tuple(dds3_python::deal_to_dict(current), won);
        },
        py::arg("deal"),
        py::arg("history"),
        py::arg("opening_leader"),
        py::arg("declarer"),
        "Replays `history` onto `deal` from `opening_leader`. Returns\n"
        "`(root, tricks_won_by_declarer)`.\n\n"
        "`root` is the position after the last card, ready for evaluate().\n"
        "Compute tricks_needed as `level + 6 - tricks_won_by_declarer`.\n"
        "Tricks won by **dummy** count for declarer.\n\n"
        "An incomplete last trick is replayed but counts for nobody. Its cards\n"
        "are on the trick in progress of `root`.\n\n"
        "Raises ValueError, naming the index, if a card is not legal when it\n"
        "is played. This catches a transcription mistake, or an\n"
        "`opening_leader` that does not match the history.");

    module.def("module_name", []() {
        return "_belief_space_local_evaluation";
    });
}
