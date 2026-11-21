# Cluster new-p9-7-port: envelope parsing allocation bound

**Verdict: FIXED**

The finding is real on this branch (`bugs/qwen-new-p9-7-port`, based on `050/m0-train-5`). `envelopeFromText` called `text.split(';', Qt::SkipEmptyParts)` before the `MaxGainPoints` check, so a long `gainenv` attribute allocated a `QStringList` entry for every segment even though only 32 points can be kept. The accepted fix from `60817eca4` was ported by hand (no merge, cherry-pick or rebase). The current code's `tickOk` handling was kept.

## Evidence (file:line on this branch)

- `src/core/ClipEdits.cpp:130-150`: `envelopeFromText`. The `split()` loop is replaced by an in-place walk over a `QStringView` that stops at `MaxGainPoints`. Empty and damaged parts are skipped as before (`colon > 0 && tickOk && std::isfinite(db)`).
- `src/core/ClipEdits.cpp:24`: `#include <QStringView>`.
- `tests/src/core/ClipEditsTest.cpp:225`: `parsingAnEnvelopeStopsBeforeAllocatingOverflowPoints()`. It parses a prebuilt 2,000,000-point attribute with `RLIMIT_AS` capped 32 MiB above current usage, then requires 32 points kept. Linux only; skips elsewhere.
- `tests/fork-sources.txt`: `tests/src/core/ClipEditsTest.cpp` registered (the file is fork-new and was missing from the list). `bash tests/fork-sources-gate.sh` passes.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-7-port.r1/logs/`.

ccache could not write its temp files under `/run/user/1000` (sandbox), so every build below sets `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-new-p9-7`. Nothing else differs.

| Step | Command (run from the worktree / `build/tests`) | Exit | Log |
|---|---|---:|---|
| Build test, pre-fix | `cmake --build build --target ClipEditsTest -j2 > .../build-2-red.log 2>&1` | 0 | build-2-red.log |
| **RED** (original parser) | `cd build/tests && ctest -R '^ClipEditsTest$' --output-on-failure > .../test-1-red.log 2>&1` | **8** | test-1-red.log: `FAIL! : ClipEditsTest::parsingAnEnvelopeStopsBeforeAllocatingOverflowPoints() 'parsed' returned FALSE. (the parser allocated temporary storage for points after MaxGainPoints)`; `0% tests passed, 1 tests failed out of 1` |
| Build with fix | `cmake --build build --target zene ClipEditsTest ClipGainEnvelopeTest ClipEditsMenuTest -j2 > .../build-3-green.log 2>&1` | 0 | build-3-green.log |
| **GREEN** (family, 3 tests) | `ctest -R '^(ClipEditsTest\|ClipGainEnvelopeTest\|ClipEditsMenuTest)$' --output-on-failure > .../test-2-green.log 2>&1` | **0** | test-2-green.log: `100% tests passed out of 3` |
| Proof: save patch | `git diff -- src/core/ClipEdits.cpp > .../fix.patch` | 0 | fix.patch |
| Proof: revert | `git apply -R .../fix.patch` | 0 | |
| Build, reverted | `cmake --build build --target zene ClipEditsTest -j2 > .../build-4-reverted.log 2>&1` | 0 | build-4-reverted.log |
| **Reverted: must FAIL** | `ctest -R '^ClipEditsTest$' --output-on-failure > .../test-3-reverted.log 2>&1` | **8** | test-3-reverted.log: same `FAIL!` line as RED |
| Proof: re-apply | `git apply .../fix.patch` | 0 | |
| Build, fixed | `cmake --build build --target zene ClipEditsTest -j2 > .../build-5-fixed.log 2>&1` | 0 | build-5-fixed.log |
| **Re-applied: must PASS** | `ctest -R '^ClipEditsTest$' --output-on-failure > .../test-4-fixed.log 2>&1` | **0** | test-4-fixed.log: `100% tests passed out of 1` |
| Relink Clip family (test binaries link the zene objects statically) | `cmake --build build --target <21 Clip test targets> -j2 > .../build-6-clipfamily.log 2>&1` | 0 | build-6-clipfamily.log (335 targets built, 0 errors) |
| **Affected family** (`-R Clip`) | `cd build/tests && ctest -R 'Clip' --output-on-failure > .../test-5-clipfamily.log 2>&1` | **0** | test-5-clipfamily.log: `100% tests passed out of 25` (`ctest -N -R Clip`: 25 tests, ctest-N-clip.log) |
| Fork-sources registration | `bash tests/fork-sources-gate.sh > .../gate-fork-sources.log 2>&1` | 0 | PASS |

Product code was rebuilt before each run. The ClipEdits family is 3 tests and the wider `Clip` family is 25; none were skipped.

## Ratchet gates

| Gate | Command | Exit on this branch | Exit on pristine HEAD |
|---|---|---:|---:|
| Complexity | `bash tests/complexity-gate.sh --check > .../gate-complexity.log 2>&1` | **1** | **1** |
| File length | `bash tests/file-length-gate.sh --check > .../gate-file-length.log 2>&1` | **1** | **1** |

Both gates fail, and they are **pre-existing**, not caused by this fix:

- The failing sets are identical to a pristine HEAD snapshot (`git archive HEAD` into `/tmp/qwen-head-gate`, same two commands, logs `gate-*-HEAD.log`). `diff` of the `REGRESSION`/`FAIL:` lines is empty for both gates.
- Complexity regressions (6): `XyPadPairTest.cpp dotCentreAlong`, `DawProjectReadTracks.cpp parseNotes`, `ControlCommandsMixerTools.cpp adjust`, `ControlCommandsMixerStrip.cpp setStrip`, `ProjectRevisions.cpp rotateProjectRevision`, `ControlCommandsShortcuts.cpp importedOverrides`.
- File-length regressions (14): `SmfInterchangeReader.cpp`, `TakeLane.cpp`, and 12 test files, none touched here.
- `envelopeFromText`, `ClipEdits.cpp` and `ClipEditsTest.cpp` (422 lines) are not in either list.

I did not fix the unrelated gate regressions: they are outside this bug, and clearing them would mean changing code in 20 files or re-anchoring a baseline, which I was told not to do. The branch owner should decide how to clear them.

## Files committed

- `src/core/ClipEdits.cpp`
- `tests/src/core/ClipEditsTest.cpp`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-7-port.md`

No control commands were added, so `ControlCommandsSnapshot` is unaffected. No header changed, so no dependents needed rebuilding.
