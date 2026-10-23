"""Tests for `strategies.py`: the shape of the templates, and agreement with
the follow-suit rule of the evaluator.
"""

import unittest

import belief_space_local_evaluation as bsle
from belief_space_local_evaluation import legal_cards, seat_on_play


class TestTheTemplates(unittest.TestCase):
    """`empty_declarer` and `empty_defender` are templates to copy. Check
    that they have the signatures `evaluate()` calls, and that only the
    body is missing."""

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
    """Agreement with the rules of the evaluator.

    `evaluate()` rejects an illegal card. So running this module's choices
    at every node cross-checks the two follow-suit rules. A disagreement
    shows up as a ValidationError, not as a plausible number.
    """

    def test_every_card_this_module_calls_legal_is_accepted_by_evaluate(self) -> None:
        import guess_6nt_belief_space as example
        from strategies import lowest_eligible_defender

        sequence = example.guess_6nt()
        record = bsle.PlayRecord(sequence.history, sequence.opening_leader)
        source = bsle.ExhaustiveLayoutSource(
            sequence.current_deal, sequence.declarer, example.SEED, record=record)

        def pi(state, view):
            del view
            deal = state.known_holdings
            # The highest legal card, to test a different part of the legal
            # set from the example's pi.
            return max(legal_cards(deal, seat_on_play(deal)),
                       key=lambda c: (c.rank, -c.suit))

        result = bsle.evaluate(
            sequence.current_deal, sequence.declarer, sequence.tricks_needed,
            source, pi, lowest_eligible_defender)

        self.assertNotIn("error", result)


class TestPlayRecordChangesTheAnswer(unittest.TestCase):
    """`play_record` reaches pi and can change its answer.

    Two evaluate() calls use the same root, source and delta. Only
    `play_record` differs. `evaluate()` does not check a PlayRecord against
    the root, so one root serves both calls."""

    def test_the_first_card_returned_differs_with_and_without_a_pitched_heart(self) -> None:
        import guess_6nt_belief_space as example
        from bridge_notation import HEARTS
        from strategies import lowest_eligible_defender, remembers_whether_a_heart_was_already_pitched

        sequence = example.guess_6nt()
        layout_record = bsle.PlayRecord(sequence.history, sequence.opening_leader)
        source = bsle.ExhaustiveLayoutSource(
            sequence.current_deal, sequence.declarer, example.SEED, record=layout_record)

        def recording_pi(captured):
            def pi(state, view):
                captured.append(remembers_whether_a_heart_was_already_pitched(state, view))
                return captured[-1]
            return pi

        without_record = []
        result_without = bsle.evaluate(
            sequence.current_deal, sequence.declarer, sequence.tricks_needed,
            source, recording_pi(without_record), lowest_eligible_defender)

        with_record = []
        record = bsle.PlayRecord([bsle.Card(HEARTS, 8)], sequence.opening_leader)
        result_with = bsle.evaluate(
            sequence.current_deal, sequence.declarer, sequence.tricks_needed,
            source, recording_pi(with_record), lowest_eligible_defender,
            play_record=record)

        self.assertNotIn("error", result_without)
        self.assertNotIn("error", result_with)
        self.assertTrue(without_record)
        self.assertTrue(with_record)
        self.assertNotEqual(without_record[0], with_record[0])


if __name__ == "__main__":
    unittest.main()
