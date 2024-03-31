# Contributing

Contributions are welcome, and they are greatly appreciated! Every little bit helps, and
credit will always be given.

## Types of Contributions

You can contribute in many ways:

### Report Bugs

Report bugs at <https://github.com/bpcreech/PyMiniRacer/issues>.

If you are reporting a bug, please include:

- Your operating system name and version.
- Any details about your local setup that might be helpful in troubleshooting.
- Detailed steps to reproduce the bug.

### Fix Bugs

Look through the GitHub issues for bugs. Anything tagged with "bug" is open to whoever
wants to implement it.

## Implement Features

Look through the GitHub issues for features. Anything tagged with "feature" is open to
whoever wants to implement it.

## Write Documentation

Python Mini Racer could always use more documentation, whether as part of the official
Python Mini Racer docs, in docstrings, or even on the web in blog posts, articles, and
such.

## Submit Feedback

The best way to send feedback is to file an issue at
<https://github.com/bpcreech/PyMiniRacer/issues>.

If you are proposing a feature:

- Explain in detail how it would work.
- Keep the scope as narrow as possible, to make it easier to implement.
- Remember that this is a volunteer-driven project, and that contributions are welcome
    :)

## Get Started!

Ready to contribute? Here's how to set up `PyMiniRacer` for local development.

!!! warning
    Building this package from source takes several GB of disk space and takes 1-2 hours.

1. Do a quick scan through [the architecture guide](ARCHITECTURE.md) before diving in.

1. Fork the `PyMiniRacer` repo on GitHub.

1. If you plan to change C++ code you should probably install at least `clang-format`
    and `clang-tidy` from [the latest stable LLVM](https://releases.llvm.org/). While
    the `PyMiniRacer` build uses its own compiler (!) on most systems, our pre-commit
    rules rely on the system `clang-format` and `clang-tidy`. If your versions of those
    utilities do not match the ones `PyMiniRacer` uses on GitHub Actions, you may see
    spurious pre-commit errors.

    If you're on a Debian-related Linux distribution using the LLVM project's standard
    apt packages, note that you will likely have to override `/usr/bin/clang-format`
    and `/usr/bin/clang-tidy` to point to the latest version, i.e.,
    `/usr/bin/clang-format-18` and `/usr/bin/clang-tidy-18`, respectively.

    You can always silence local pre-commit errors with the `-n` argument to
    `git commit`. We check `pre-commit`s on every pull request using GitHub Actions, so
    you can check for errors there instead.

1. Run some of the following:

    ```sh
        # Set up a virtualenv:
        $ python -m venv ~/my_venv
        $ . ~/my_venv/bin/activate
        $ pip install pre-commit hatch hatch-mkdocs

        # Set up a local clone of your fork:
        $ git clone git@github.com:your_name_here/PyMiniRacer.git
        $ cd PyMiniRacer/
        $ pre-commit install  # install our pre-commit hooks

        # Build and test stuff:
        $ hatch run docs:serve  # build the docs you're reading now!
        $ hatch build  # this may take 1-2 hours!
        $ hatch run test:run
    ```

    You can also play with your build in the Python REPL, as follows:

    ```sh
        $ hatch shell
        $ python
        >>> from py_mini_racer import MiniRacer
        >>> mr = MiniRacer()
        >>> mr.eval('6*7')
        42
        >>> exit()
        $ exit
    ```

    As a shortcut for iterative development, you can skip the `hatch build` and matrix
    tests, and do:

    ```sh
        $ python helpers/v8_build.py [--skip-fetch]  # will install a DLL into src/py_mini_racer
        $ PYTHONPATH=src pytest  # run the tests
        $ PYTHONPATH=src python  # play with the build in the REPL
        >>> from py_mini_racer import MiniRacer
        >>> ...
    ```

1. Create a branch for local development::

    ```sh
        $ git checkout -b name-of-your-bugfix-or-feature
    ```

    Now you can make your changes locally.

1. When you're done making changes, check that your changes pass the linter and the
    tests, including testing other Python versions:

    ```sh
       $ pre-commit run  # run formatters and linters
       $ hatch run docs:serve  # look at the docs if you changed them!
       $ hatch build  # this may take 1-2 hours!
       $ hatch run test:run
       $ hatch run test:run-coverage  # with coverage!
    ```

1. Commit your changes and push your branch to GitHub::

    ```sh
       $ git add .
       $ git commit -m "Your detailed description of your changes."
       $ git push origin name-of-your-bugfix-or-feature
    ```

1. (Optional) Run the GitHub Actions build workflow on your fork to ensure that all
    architectures work.

1. Submit a pull request through the GitHub website.

## Tests

If you want to run the tests, you need to build the package first:

```sh
    $ hatch build
```

Then run:

```sh
    $ hatch run test
```

Or for the full test matrix:

```sh
    $ hatch run test:run
    $ hatch run test:run-coverage  # with coverage!
```

## Pull Request Guidelines

Before you submit a pull request, check that it meets these guidelines:

1. The pull request should include tests.
1. If the pull request adds functionality, the docs should be updated. Put your new
    functionality into a function with a docstring, and add the feature to the list in
    README.md.
1. The pull request should work for the entire test matrix of Python versions
    (`hatch run tests:run`).

## Releasing `PyMiniRacer`

Releases for `PyMiniRacer` should be done by GitHub Actions on the official project
repository.

To release:

1. Merge all changes into `main` on the offical repository.

1. Update `HISTORY.md` with a summary of changes since the last release.

1. Pick the next revision number:

    ```sh
    $ git fetch --tags
    $ git tag -l
    # observe the next available tag
    ```

1. Update `src/py_mini_racer/__about__.py` with the new revision number.

1. Add and push a tag:

    ```sh
    NEXT_TAG=the next tag
    $ git tag "${NEXT_TAG}"
    $ git push origin "${NEXT_TAG}"
    ```

1. Observe the build process on GitHub Actions. It should build and push docs and upload
    wheels to PyPI automatically.

    !!! warning
        As of this writing, the `aarch64` Linux builds are slow because they're running on
        emulation. They time out on the first try (and second and third and...) after 6
        hours. If you "restart failed jobs", they will quickly catch up to where where they
        left off due to [`sccache`](https://github.com/mozilla/sccache). The jobs should
        *eventually* complete within the time limit. You can observe their slow progress
        using the Ninja build status (e.g., `[1645/2312] CXX obj/v8_foo_bar.o`).
