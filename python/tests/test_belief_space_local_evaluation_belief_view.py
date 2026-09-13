import unittest

from belief_space_local_evaluation import ExpiredBeliefViewError
from belief_space_local_evaluation._belief_space_local_evaluation import (
    _with_belief_view,
)

Spades, Hearts, Diamonds, Clubs = 0, 1, 2, 3
North, East, South, West = 0, 1, 2, 3
DDS_NOTRUMP = 4


def make_layout(spade_rank: int) -> dict:
    # Distinct layouts differ only in which spade East holds -- enough to
    # tell entries apart without needing a realistic full deal.
    return {
        "trump": DDS_NOTRUMP,
        "first": North,
        "remain_cards": [
            [0, 0, 0, 0],
            [1 << spade_rank, 0, 0, 0],
            [0, 0, 0, 0],
            [0, 0, 0, 0],
        ],
        "current_trick_suit": (0, 0, 0),
        "current_trick_rank": (0, 0, 0),
    }


class TestEntriesAndPosteriors(unittest.TestCase):
    def test_posteriors_are_normalised_and_match_hand_derived_values(self) -> None:
        layouts = [make_layout(2), make_layout(3), make_layout(4)]
        weights = [1.0, 2.0, 1.0]  # sums to 4 -> posteriors 0.25, 0.5, 0.25

        def callback(view):
            posteriors = [entry.posterior for entry in view.entries]
            self.assertEqual(len(posteriors), 3)
            self.assertAlmostEqual(sum(posteriors), 1.0, places=9)
            self.assertAlmostEqual(posteriors[0], 0.25, places=9)
            self.assertAlmostEqual(posteriors[1], 0.5, places=9)
            self.assertAlmostEqual(posteriors[2], 0.25, places=9)
            return None

        _with_belief_view(layouts, weights, False, callback)

    def test_entry_layouts_match_what_was_supplied(self) -> None:
        layouts = [make_layout(2), make_layout(5)]
        weights = [1.0, 1.0]

        def callback(view):
            seen = [entry.layout for entry in view.entries]
            self.assertEqual(seen, layouts)
            return None

        _with_belief_view(layouts, weights, False, callback)

    def test_unequal_weights_still_only_the_relative_size_matters(self) -> None:
        layouts = [make_layout(2), make_layout(3)]

        def callback(view):
            return [entry.posterior for entry in view.entries]

        a = _with_belief_view(layouts, [1.0, 1.0], False, callback)
        b = _with_belief_view(layouts, [10.0, 10.0], False, callback)
        self.assertEqual(a, b)


class TestIsSampleAndSpaceSize(unittest.TestCase):
    def test_space_size_is_entry_count_when_not_a_sample(self) -> None:
        layouts = [make_layout(r) for r in (2, 3, 4)]
        weights = [1.0, 1.0, 1.0]

        def callback(view):
            self.assertFalse(view.is_sample)
            self.assertEqual(view.space_size, 3)
            self.assertEqual(len(view.entries), 3)
            return None

        _with_belief_view(layouts, weights, False, callback)

    def test_space_size_is_zero_when_a_sample(self) -> None:
        layouts = [make_layout(r) for r in (2, 3, 4)]
        weights = [1.0, 1.0, 1.0]

        def callback(view):
            self.assertTrue(view.is_sample)
            self.assertEqual(view.space_size, 0)
            self.assertEqual(len(view.entries), 3)  # entries still report the true count
            return None

        _with_belief_view(layouts, weights, True, callback)


class TestExpiryAfterTheCallbackReturns(unittest.TestCase):
    def test_using_the_view_after_return_raises(self) -> None:
        layouts = [make_layout(2)]
        weights = [1.0]
        stashed = {}

        def callback(view):
            stashed["view"] = view
            return None

        _with_belief_view(layouts, weights, False, callback)

        view = stashed["view"]
        with self.assertRaises(ExpiredBeliefViewError):
            view.is_sample
        with self.assertRaises(ExpiredBeliefViewError):
            view.space_size
        with self.assertRaises(ExpiredBeliefViewError):
            view.entries

    def test_expiry_is_reported_even_when_the_callback_raises(self) -> None:
        layouts = [make_layout(2)]
        weights = [1.0]
        stashed = {}

        def callback(view):
            stashed["view"] = view
            raise RuntimeError("deliberate")

        with self.assertRaises(RuntimeError):
            _with_belief_view(layouts, weights, False, callback)

        with self.assertRaises(ExpiredBeliefViewError):
            stashed["view"].is_sample

    def test_a_stashed_entry_raises_after_return_but_an_already_read_layout_does_not(self) -> None:
        layouts = [make_layout(2), make_layout(3)]
        weights = [1.0, 1.0]
        stashed = {}

        def callback(view):
            entries = view.entries
            stashed["entry"] = entries[0]
            stashed["layout"] = entries[0].layout  # read while the view is live
            return None

        _with_belief_view(layouts, weights, False, callback)

        # The entry object itself carries the same flag as its view.
        with self.assertRaises(ExpiredBeliefViewError):
            stashed["entry"].layout
        with self.assertRaises(ExpiredBeliefViewError):
            stashed["entry"].posterior

        # But the dict already read out of it is a plain copy -- ordinary
        # data from here on, with no tie to the expired view at all.
        self.assertEqual(stashed["layout"], layouts[0])

    def test_an_entry_obtained_after_expiry_also_raises(self) -> None:
        layouts = [make_layout(2)]
        weights = [1.0]
        stashed = {}

        def callback(view):
            stashed["view"] = view
            return None

        _with_belief_view(layouts, weights, False, callback)

        with self.assertRaises(ExpiredBeliefViewError):
            stashed["view"].entries


if __name__ == "__main__":
    unittest.main()
