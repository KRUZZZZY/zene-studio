# Cluster new-p9-328-port: OpulenZ reload deletes the emulator outside the mutex

**Verdict: FIXED** (port of the accepted fix from `bugs/qwen-new-p9-328`, re-applied by hand on `050/m0-train-5`).

Finding: "OpulenZ deletes the emulator before synchronizing a sample-rate reload" (`plugins/OpulenZ/OpulenZ.cpp:231-243` in the historical tree).

## Evidence

Status on the newest line before the fix (HEAD `b02a0ff2b`, worktree branch `bugs/qwen-new-p9-328-port`):

| Location | State before this fix |
| --- | --- |
| `plugins/OpulenZ/OpulenZ.cpp:225` | destructor deletes `theEmulator` with no lock (out of scope, see Residual risk) |
| `plugins/OpulenZ/OpulenZ.cpp:232-244` (`reloadEmulator`) | `delete theEmulator;` ran **before** `emulatorMutex.lock()`; the voice-state reset ran **after** `unlock()` |
| `plugins/OpulenZ/OpulenZ.cpp:299/380` (`handleMidiEvent`), `:397/408` (`playImpl`) | audio/MIDI paths use `theEmulator`, `voiceNote`, `voiceLRU` only under `emulatorMutex` |
| `plugins/OpulenZ/OpulenZ.cpp:494`, `:540` | `loadPatch` / `updatePatch` already take the mutex (landed by `0d7d9f224`, cluster new-p9-327) |

So the finding is **real** on the newest line: a sample-rate change deletes the emulator while `playImpl` or `handleMidiEvent` may hold a pointer to it, and the voice state is reset outside the lock.

## The fix

- `plugins/OpulenZ/OpulenZ.cpp:233-244`: `reloadEmulator()` now does `lock; delete; new CTemuopl; init; write(0x01,0x20); reset voiceNote/voiceLRU; updatePatch(); unlock` in one critical section.
- `plugins/OpulenZ/OpulenZ.cpp:93-94` and `plugins/OpulenZ/OpulenZ.h:144`: `emulatorMutex` is now `QRecursiveMutex` (a `QMutex` subclass in Qt 6, `/usr/include/qt6/QtCore/qmutex.h:342`).

**Deviation from the accepted fix (`381d6faca`).** That commit split `updatePatch()` into `updatePatch()` (locks) plus `updatePatchLocked()` (writes), so `reloadEmulator` could call the unlocked writer. The newest line's regression test `PluginPortsMigrationTest::opulenzUpdatePatchLocksEmulatorForWholeUpdate` (`tests/src/plugins/PluginPortsMigrationTest.cpp`, ~line 2537) asserts that `updatePatch()`'s own body holds the lock and contains the `theEmulator->write(0xBD,` and `loadPatchLocked(` writes. The split would break that test, and I may not weaken it. So `updatePatch()` is unchanged and the nested lock is allowed by making the mutex recursive. The critical section is the same: deletion, replacement, voice reset and patch write all happen under one lock.

## Regression test

`PluginPortsMigrationTest::opulenzReloadEmulatorReplacesEmulatorUnderMutex` (new, `tests/src/plugins/PluginPortsMigrationTest.cpp`). Like its neighbours it is a source-text check (it reads `plugins/OpulenZ/OpulenZ.cpp` and checks the statement order inside `reloadEmulator`'s body, plus that `emulatorMutex` is a `QRecursiveMutex`). It pins ordering, not runtime behaviour. The test is not a runtime proof of the race.

No new source file was added. `tests/upstream-modifications.txt` gained two lines (OpulenZ.cpp, OpulenZ.h).

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-328-port.r1/logs/`.

Build environment note: the first build failed with `ccache: error: failed to create temporary file for /run/user/1000/ccache-tmp/...: Permission denied` (`build-1.log`). Every later build used `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9-328`. The SWH decimator LADSPA plugin was missing (not built after that failure), so it was built with `cmake --build build --target decimator_1202` (`build-5-swh.log`, exit 0).

| Step | Command (run from the worktree unless noted) | Exit | Log |
| --- | --- | --- | --- |
| Build test target, first try | `cmake --build build --target PluginPortsMigrationTest -j2` | 2 (ccache) | `build-1.log` |
| Build test target | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9-328 cmake --build build --target PluginPortsMigrationTest -j2` | 0 | `build-2.log` |
| **RED** (test added, product unchanged) | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | **8** | `test-1-red.log` |
| RED detail | opulenz test: `FAIL! : ...opulenzReloadEmulatorReplacesEmulatorUnderMutex() ... returned FALSE` (`lock < deletion` ordering). Totals: 44 passed, 2 failed, 2 skipped (second failure is the decimator fixture, see below) | | |
| Apply fix | `CCACHE_TEMPDIR=... cmake --build build --target zene PluginPortsMigrationTest -j2` (after the lock-style change) | 0 | `build-4-green.log` |
| GREEN (full family, decimator present) | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | **0** | `test-6-green-clean.log` |
| GREEN (targeted) | `cd build/tests && QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest opulenzReloadEmulatorReplacesEmulatorUnderMutex opulenzUpdatePatchLocksEmulatorForWholeUpdate` | 0 | `test-7-targeted-green.log` (4 passed, 0 failed) |
| **Proof: revert product fix** | `git diff -- plugins/OpulenZ/OpulenZ.cpp plugins/OpulenZ/OpulenZ.h > logs/fix.patch && git apply -R logs/fix.patch` | 0 / 0 | |
| Test with fix reverted | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | **8** | `test-4-reverted-src.log` (opulenz test FAIL, 44 passed / 2 failed) |
| Re-apply fix | `git apply logs/fix.patch` then rebuild `zene PluginPortsMigrationTest` | 0 / 0 | `build-6-regreen.log` |
| Test with fix re-applied | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' ...` | **0** (after decimator build; before it, 8 with only the decimator fixture failing: `test-5-green.log`) | `test-6-green-clean.log` |

The revert run was not followed by a rebuild of `zene`. `PluginPortsMigrationTest` reads `OpulenZ.cpp` from disk at runtime, so the result depends on the source text, not on the binary. That is why the RED/GREEN proof is valid without the rebuild, but I'm stating it rather than hiding it.

Gates (final tree, with the fix):

| Command | Exit | Log |
| --- | --- | --- |
| `bash tests/complexity-gate.sh --check` | **1** | `gate-complexity-final.log` |
| `bash tests/file-length-gate.sh --check` | **1** | `gate-filelength-final.log` |

Gates with the product fix reverted (`git apply -R logs/fix.patch`):

| Command | Exit | Log |
| --- | --- | --- |
| `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity-reverted.log` |
| `bash tests/file-length-gate.sh --check` | 1 | `gate-filelength-reverted.log` |

The regression lines are identical with and without the fix (6 complexity, 14 file-length). None of them names `OpulenZ` or `PluginPortsMigration`. The failures were already on the base branch and come from other files (`XyPadPairTest`, `DawProjectReadTracks`, `ControlCommandsMixerTools`, `CollabTransportTest`, and others). I did not re-anchor or edit any baseline. **These two gates fail on the base line; the fix does not make them worse, but they are not green.**

## Known unrelated failure in this test family

`PluginPortsMigrationTest::nonRealtimeLadspaEffectsAreInvalid` fails with `SWH Decimator test fixture was not loaded` until `decimator_1202.so` exists. It was a build artifact missing because of the ccache failure. After building `decimator_1202`, it passes. It is not related to OpulenZ.

## Residual risk (not fixed here)

- `OpulenzInstrument::~OpulenzInstrument()` (`plugins/OpulenZ/OpulenZ.cpp:225`) still deletes `theEmulator` without taking `emulatorMutex`, before `removePlayHandlesOfTypes`. That is the same class of hazard for teardown and is outside this finding's `reloadEmulator` scope. It needs its own finding.
- The regression test is a source-text check, as its neighbours are. It does not exercise the race at runtime.
