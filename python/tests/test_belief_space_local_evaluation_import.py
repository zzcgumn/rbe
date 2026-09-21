import unittest

from belief_space_local_evaluation import module_name


class TestBeliefSpaceLocalEvaluationImport(unittest.TestCase):
    def test_import_and_module_name(self) -> None:
        self.assertEqual(module_name(), "_belief_space_local_evaluation")


if __name__ == "__main__":
    unittest.main()
