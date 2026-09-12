# Evidence — the 0.2.1-alpha version bump and the `check-namespace` fixes

`chore/version-0.2.1-alpha`, cut from `post-alpha/integration` tip `ba24a9578`. The narrative record is
`docs/VERSION-0.2.1-ALPHA.md`; this directory holds the raw runs it cites. **Every exit code here was captured
unpiped** (`cmd > log 2>&1; echo EXIT=$?`), and the logs are in the repo rather than `/tmp` on purpose — a
disk reclaim destroyed this project's evidence once before.

| log | what it is | exit |
|---|---|---|
| `00-configure-ci-flags.log` | `JOBS=2 bash tools/local-ci.sh --configure-only --build-dir build` — the linux-x86_64 job's CMAKE_OPTS, VST3 SDK `3.8.1_build_84` + CLAP `1.2.10` provisioned. It reports its own deviation: `-DWANT_QT6=ON`, because this box has no Qt5 development files and CI's runner has only Qt5 | 0 |
| `01a-reconfigure-no-debug-info.log` | the one deviation: the CI options kept, the build-type flags re-set to `-O2 -DNDEBUG` (**`-g` dropped**) so the tree fits in the 4.0 GB this box had free | 0 |
| `01-build-and-flags.log` | the same, plus the proof the flags took (the `flags.make` line, quoted) and the build's wall-clock times | 0 |
| `01b-build.log` | `cmake --build build -j2` — the whole tree under `-Werror` | 0 |
| `02-check-namespace-negative-control.log` | the four runs that close the four `check-namespace` errors **and prove the checker still fails a new file with no namespace**: clean tree → 0 errors; the probe named on the command line → 1 error; the probe made visible to the checker's real `git ls-files` scope (`git add -f -N`) → 1 error; probe removed → 0 errors and `git status` clean | 0 |
| `03-release-version-gate.log` | `bash tests/release-version-gate.sh` — the oracle: `RESULT: PASS — 0.2.1-alpha is the tree's, its release notes' and its download link's version`, with its tag point `[skip]`ped (no tag exists yet) | 0 |
| `04-test-release-version-gate.log` | `bash tests/test-release-version-gate.sh` — the gate's red/green harness, **8/8 controls as declared**: G0/G0b green, and the six injected defects (R1–R6) each exit 1 | 0 |
| `05-fork-sources-gate.log` | Gate 5 — 242 fork-NEW, 1036 inherited, 34 tooling, 0 stale | 0 |
| `06-no-upstream-regression-gate.log` | Gate 6 — 443 changed paths since the gate base, all in allowed classes (this change is docs, `tests/**`, `CMakeLists.txt`, `.github/**`, and one fork-NEW source) | 0 |
| `07-ctest.log` | `ctest` run **from `build/tests`** (workspace rule 7 — the top-level directory has no `CTestTestfile.cmake` and would report 0 tests) — **86/86 passed**, 124.5 s | 0 |
| `08-version-fallback-probe.log` | the version string's two paths: this untagged branch reports the **superseded** tag's string (`0.2.0-alpha.9+0de9f3b`), and the `CMakeLists.txt` fallback (`-DFORCE_VERSION=internal`, i.e. a source tarball or a checkout with no reachable tag) reports exactly **`0.2.1-alpha`** | 0 |

Not run here (they belong to the release verification, not to this job): Gate 2 coverage (needs an
instrumented `Debug` + `WANT_COVERAGE=ON` build and a tracefile), the whole-tree complexity / file-length
scopes, `tests/run-all-gates.sh`, and the seven-platform CI matrix.
