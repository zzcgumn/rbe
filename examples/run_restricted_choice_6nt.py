"""Entry point for `bazelisk run //examples:restricted_choice_6nt`.

A separate file, so that the example can be a py_library that the test
imports.
"""

from restricted_choice_6nt_belief_space import main

if __name__ == "__main__":
    main()
