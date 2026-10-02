"""Entry point for `bazelisk run //examples:restricted_choice_6nt`.

Separate from the example itself so that restricted_choice_6nt_belies_space.py
can be a py_library a test target can also depend on -- rather than a
py_test depending on a py_binary to reach the module.
"""

from restricted_choice_6nt_belies_space import main

if __name__ == "__main__":
    main()
