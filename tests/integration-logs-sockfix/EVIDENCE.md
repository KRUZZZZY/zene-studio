# Evidence: the `--control-socket <path>` data-loss fix

Every log here was produced with **unpiped exit codes** (`cmd > log 2>&1; echo EXIT=$?`), on the Release
configuration (`RelWithDebInfo`, `USE_WERROR=ON`, `WANT_QT6/VST3/CLAP=ON`), against
`build-rel/zene` in this worktree. Summary and reasoning: `docs/CONTROL-SOCKET-PATH-SAFETY.md`.

| File | What it proves |
|---|---|
| `cmake-configure.log` | the configure step, `CONFIGURE_EXIT=0` |
| `build-full.log` | the clean full build of the fixed tree, `BUILD_EXIT=0` |
| `build-after-probe.log` | the incremental build after the live-listener rule and the exit-ownership check were added, `BUILD_EXIT=0` |
| `build-final.log` | the last `make` on the verified tree, `BUILD_EXIT=0` |
| `repro-socket-path.sh` | the reproduction, re-runnable against any `zene` binary: `bash repro-socket-path.sh <zene> <label>` |
| `repro-before-fix.log` | THE DEFECT on the release configuration: exit 124 (still running after the bound), the 23-byte project file replaced by a `srw-------` 0-byte socket, nothing on stderr, `control socket listening on <path>` on stdout |
| `repro-after-fix.log` | the fix: exit 1, the file byte-identical (same sha256 `32f6b100…`), the typed `refused` refusal naming the path |
| `test-prefix-negative-control.log` | `tests/control-socket-path-safety.py` against the PRE-FIX binary: exit 1, 7 of 8 cases fail, each naming the defect; only "a free path still binds" passes |
| `test-postfix.log` | the same test against the fixed binary: exit 0, 8 of 8 cases PASS |
| `ctest.log` | `ctest --output-on-failure` from `build-rel/tests`: **87/87 passed, 0 failed, 134.18 s, exit 0** (the release tip's 86 plus `ControlSocketPathSafety`) |
| `gate9-fork-sources.log` | `bash tests/fork-sources-gate.sh` → exit 0 |
| `gate6-no-upstream-regression.log` | `bash tests/no-upstream-regression-gate.sh` → exit 0 (no undeclared divergence; no inherited file touched) |
| `check-namespace.log` | `python3 tests/scripted/check-namespace` → the same 4 known errors as before this change, exit 1 |

**How the pre-fix binary was built** (for the two `before` logs): the two product source files were taken
from the release tip with `git checkout ba24a9578 -- include/ControlServer.h src/core/ControlServer.cpp`,
then `make -C build-rel -j2` (exit 0), then the tree was restored with `git checkout HEAD -- <the same two
paths>` and rebuilt (`build-final.log`, exit 0). That intermediate build log was pruned during cleanup; the
two `before` logs above are its output.
