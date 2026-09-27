"""Tests for `strategies.py`: the templates' shape, and agreement with the
evaluator's own follow-suit rule.

`play_sequence.py` and `bridge_notation.py` were promoted to the installed
package; their own tests moved with them to
`python/tests/test_belief_space_local_evaluation_play_sequence.py` and
`python/tests/test_bridge_notation.py`. What is left here is genuinely
example-specific: `strategies.py`'s copy-me templates, and a cross-check
between this package's own notion of "legal" and the evaluator's.
"""

import unittest

import belief_space_local_evaluation as bsle
from belief_space_local_evaluation import legal_cards, seat_on_play


class TestTheTemplates(unittest.TestCase):
    """empty_declarer / empty_defender are copy-me templates, and a template
    nobody has run is the kind that turns out not to work. These pin the two
    things a reader needs from them: the signature `evaluate()` calls with,
    and that the body is the only part left to fill in."""

    def test_they_take_the_arguments_evaluate_passes(self) -> None:
        import inspect

        from strategies import empty_declarer, empty_defender

        self.assertEqual(list(inspect.signature(empty_declarer).parameters),
                         ["state", "view"])
        self.assertEqual(list(inspect.signature(empty_defender).parameters),
                         ["layout", "seat", "state"])

    def test_they_raise_rather_than_returning_something_wrong(self) -> None:
        from strategies import empty_declarer, empty_defender

        with self.assertRaises(NotImplementedError):
            empty_declarer(None, None)
        with self.assertRaises(NotImplementedError):
            empty_defender(None, None, None)

    def test_the_real_strategies_match_the_template_signatures(self) -> None:
        # The templates are only useful if copying one gives a working shape.
        import inspect

        from strategies import (
            empty_declarer,
            empty_defender,
            lowest_eligible_declarer,
            lowest_eligible_defender,
        )

        self.assertEqual(list(inspect.signature(lowest_eligible_declarer).parameters),
                         list(inspect.signature(empty_declarer).parameters))
        self.assertEqual(list(inspect.signature(lowest_eligible_defender).parameters),
                         list(inspect.signature(empty_defender).parameters))


class TestAgainstTheLibrary(unittest.TestCase):
    """The reason this file exists: agreement with the evaluator's own rules.

    `evaluate()` rejects a card its own `legal_cards` considers illegal, so
    feeding it this module's choices at every node is a direct cross-check of
    the two follow-suit rules. A disagreement shows up as a ValidationError
    rather than as a plausible number.
    """

    def test_every_card_this_module_calls_legal_is_accepted_by_evaluate(self) -> None:
        import guess_6nt_belief_space as example
        from strategies import lowest_eligible_defender

        sequence = example.guess_6nt()
        source = bsle.ExhaustiveLayoutSource(
            sequence.current_deal, sequence.declarer, example.SEED,
            history=sequence.history, opening_leader=sequence.opening_leader)

        def pi(state, view):
            del view
            deal = state.known_holdings
            # Deliberately the *highest* legal card, so the choice exercises
            # a different part of the legal set than the example's own pi.
            return max(legal_cards(deal, seat_on_play(deal)),
                       key=lambda c: (c.rank, -c.suit))

        result = bsle.evaluate(
            sequence.current_deal, sequence.declarer, sequence.tricks_needed,
            source, pi, lowest_eligible_defender)

        self.assertNotIn("error", result)


if __name__ == "__main__":
    unittest.main()
