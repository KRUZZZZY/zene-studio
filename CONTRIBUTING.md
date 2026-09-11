# Contributing to Zene Studio

Zene Studio is a free, open-source DAW derived from [LMMS](https://github.com/LMMS/lmms). It is
licensed **GPL-2.0-or-later** ([LICENSE.txt](LICENSE.txt)) — contributions come in under the same
licence, and every upstream LMMS notice is retained. Zene Studio is not affiliated with, or
endorsed by, the LMMS project.

## Before you start

- Build and test locally first. The CI matrix is slow and metered, and a push that fails one job
  costs a whole matrix run.
- Read [docs/KNOWN-LIMITATIONS.md](docs/KNOWN-LIMITATIONS.md) before reporting a gap — a lot of
  the alpha's rough edges are documented there and do not need an issue.
- Bugs and feature requests go through the templates in
  [`.github/ISSUE_TEMPLATE/`](.github/ISSUE_TEMPLATE/): `bug_report.yml`, `feature_request.yml`
  and `alpha-feedback.yml` for the alpha build.
- Security problems are **not** issues — see [SECURITY.md](SECURITY.md).

## Build and test

The supported one-command path is the local CI script. It runs the `linux-x86_64` job's
configuration, measures every exit code unpiped, and states which of the other jobs it cannot
reproduce:

```sh
tools/local-ci.sh                  # configure + build + ctest
tools/local-ci.sh --configure-only # cheapest: flags and dependencies only
tools/local-ci.sh --no-build       # ctest against an existing build
tools/local-ci.sh --jobs N         # compile parallelism (default 4)
tools/local-ci.sh --build-dir DIR  # build directory (default: build-ci)
```

It is documented in [docs/LOCAL-CI.md](docs/LOCAL-CI.md). Its CMake flags are the `linux-x86_64`
job's `CMAKE_OPTS` from [`.github/workflows/build.yml`](.github/workflows/build.yml), byte for
byte.

Driving CMake yourself works too:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DWANT_QT6=ON
cmake --build build -j4
cd build/tests && QT_QPA_PLATFORM=offscreen ctest --output-on-failure
```

**Run `ctest` from `<build>/tests`, never from the top-level build directory.** The top level has
no `CTestTestfile.cmake` and reports `0 tests`. Treat 0 tests as an error, never as a pass.

## The gate suite

`tests/` is the executable quality contract; [tests/QA-GATES.md](tests/QA-GATES.md) is the
contract text. Run everything at once:

```sh
bash tests/run-all-gates.sh                 # gates 1, 3, 4, 5, 6, 7, 8
bash tests/run-all-gates.sh --no-mutation   # skip the ~3-minute mutation sweep
bash tests/run-all-gates.sh --with-coverage # add the slow coverage build
```

Every gate is also a script you can run on its own:

| Gate | Command |
|---|---|
| 1 — unit tests | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest --output-on-failure` |
| 2 — coverage ratchet | `tests/coverage-gate.sh build-coverage/coverage/coverage-fork.info --check` |
| 3 — no tautological tests | `tests/no-tautology-gate.sh` |
| 4 — per-method complexity | `tests/complexity-gate.sh --check` |
| 5 — mutation score | `tests/mutation-gate.sh` |
| 6 — no undeclared divergence from upstream | `tests/no-upstream-regression-gate.sh` |
| 7 — per-file length | `tests/file-length-gate.sh --check` |
| 8 — token duplication | `tests/duplication-gate.sh` |

A missing baseline, an unparsable tracefile or an absent tool is a failure (exit 2), never a
pass. Baselines only move deliberately, with a recorded reason. [docs/CONVENTIONS.md](docs/CONVENTIONS.md)
lists what is enforced and — just as importantly — what is not: `.clang-format` and `.clang-tidy`
are committed but no gate runs them.

## Change size

Small, verified fixes go straight to `main`. **A change over 600 lines goes through a pull
request.** Split large work into sub-commits below that size where the design allows it; oversized
diffs are where this project's changes have historically stalled.

## Licensing rules

These are hard requirements, not preferences:

- **Never vendor Steinberg's VST2 headers.** Vestige, the built-in VST2 host, is the only VST2
  path this project has or will have.
- **Third-party dependencies must be GPL-2.0-or-later-compatible** — MIT/BSD for the vendored
  libraries, and no licence that adds a restriction conflicting with the GPL. The per-plugin
  notices under `plugins/*/LICENSE-NOTICE.md` are the pattern to follow.
- **Retain upstream LMMS notices** when you touch inherited code. Divergences from upstream must
  be declared where Gate 6 expects them.
