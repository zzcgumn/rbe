"""Entry point for `bazelisk run //examples:guess_6nt_belief_space`.

Separate from the example itself so that guess_6nt_belief_space.py can be a
py_library which the binary and both test targets all depend on -- rather
than a py_test depending on a py_binary to reach the module.
"""

from guess_6nt_belief_space import main

if __name__ == "__main__":
    main()
