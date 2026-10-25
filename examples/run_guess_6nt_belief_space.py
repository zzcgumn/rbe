"""Entry point for `bazelisk run //examples:guess_6nt_belief_space`.

A separate file, so that the example can be a py_library that the tests
import.
"""

from guess_6nt_belief_space import main

if __name__ == "__main__":
    main()
