FIXED

# Cluster new-p18-65 — NES detune changes raced with audio rendering

## Round 2 — the two returned regressions, both closed with their own red/green run

Round 1's detune fix was accepted; two regressions came back with it. Both are fixed,
and each has its own red and green run in this round's log folder
(`/home/kruzzzzy/.config/kads/zene/state/runs/new-p18-65.r2/logs`).

### R1 — an unmapped note could render from uninitialized wavelengths

`NesObject::updatePitch()` recomputes the three wavelengths only when the note frequency
or a channel detune *changed*, and round 1 initialised `m_lastCrs1/2/3` to `0.0f` — the
default model value. A note whose key is not mapped has `frequency()` exactly `0`
(`src/core/NotePlayHandle.cpp:711`), so its first update compared `0 == 0` on both sides,
computed nothing, and `renderOutput()` then read `m_wlen1/2/3` uninitialized.

Fixed at the gate: `NesInstrument::playNoteImpl()` renders silence and returns for a
zero-frequency note, before any oscillator state is constructed or read
(`plugins/Nes/Nes.cpp:570-574`):

```cpp
if (n->frequency() == 0.0f)
{
    zeroSampleFrames(out.data() + offset, frames);
    return;
}
```

`zeroSampleFrames()` is the existing `std::fill` helper (`include/SampleFrame.h:192-198`),
so nothing allocates, frees, locks, waits or logs on the audio thread, and it writes
exactly the range the render path writes (`out.data() + offset`, `frames` frames).
`NesObject` is constructed in one place only (`std::construct_at` inside `playNoteImpl`),
so this guard covers every path into oscillator state; a note that is unmapped for part
of its life simply renders silence for those periods.

The regression, inside `nesDetuneReachesRendererWithoutControlSignal()`, builds such a
note (microtuner enabled, its key outside the range, `QCOMPARE(frequency(), 0.0f)`), fills
both buffer channels with 1.0 first, renders one period and asserts both channels come
back exactly 0.0. Evidence in `test-10-red-guard-reverted.log` / `test-12-…`: with only
this guard reverted (`git apply -R guard.patch`, exit 0) and rebuilt (exit 0),
`ctest -R NesDetunePublication` is **exit 8** with `FAIL! … (an unmapped NES note must
render silence)`; guard restored (exit 0) and rebuilt (exit 0), the same ctest is **exit
0** and all three focused tests pass.

Why the wavelength members are not instead zero-initialised in the constructor: that would
make an unmapped note silent whatever the guard does, so this regression could no longer
fail — a test that passes without the fix is a wrong test. The guard is the fix; the
wavelength members stay the render path's own state, written by `updatePitch()` on every
note the guard admits.

### R2 — the source-level test could not find the plugin sources from another layout

`nesDetuneReadsTheModelOnTheRenderPath()` opened `../../plugins/Nes/Nes.cpp` relative to
`QCoreApplication::applicationDirPath()`, so a test binary run from any other layout (a
relocated binary, a temp directory) failed at `sourceFile.open(QIODevice::ReadOnly)`.

Fixed by resolving the sources at configure time instead of run time: the test target now
carries `NES_PLUGIN_SOURCE_DIR="${CMAKE_SOURCE_DIR}/plugins/Nes"`
(`tests/CMakeLists.txt:4788`, next to the existing `WATSYN_PLUGIN_SOURCE_DIR`) and the
test builds its path from it (`tests/src/plugins/PluginPortsMigrationTest.cpp:6269`).
`QStringLiteral(NES_PLUGIN_SOURCE_DIR)` also sidesteps `POSIX_ABS`-style absolute-path
assumptions in a test.

Evidence, with the binary copied to `$TMPDIR/nes-ext-layout/` and run there:
the old relative form fails — `test-5-external-layout-red.log`, **exit 1**,
`FAIL! … 'sourceFile.open(QIODevice::ReadOnly)' returned FALSE
(/home/kruzzzzy/.config/kads/zene/state/runs/new-p18-65.r2/tmp/nes-ext-layout/../../plugins/Nes/Nes.cpp)`;
the CMake-provided path passes from the same directory — `test-3-external-layout-green.log`
**exit 0** and `test-7-external-layout-green-again.log` **exit 0** (four passes, the source
test and the detune test). The relocated scratch directory was removed at the end.

## Verdict: REAL, fixed at the cause

The finding is real. On the pre-change tree the three channel coarse-detune factors
were plain `float` members of `NesInstrument`
(`plugins/Nes/Nes.h:233-235` at `HEAD`), written by three slots connected to the
knobs' `dataChanged` with `Qt::DirectConnection`:

```
plugins/Nes/Nes.cpp:552  connect( &m_ch1Crs, SIGNAL( dataChanged() ), this, SLOT( updateFreq1() ), Qt::DirectConnection );
plugins/Nes/Nes.cpp:726  m_freq1 = std::exp2(m_ch1Crs.value() / 12.0f);
```

and read by `NesObject::updatePitch()` on the audio thread — in the condition and in
the body, once per pitch-update interval (`wavelength(60)` ≈ 735 frames):

```
plugins/Nes/Nes.cpp:462  if( freq != m_lastNoteFreq || m_parent->m_freq1 != m_lastFreq1 || ...
plugins/Nes/Nes.cpp:465-470  m_wlen1 = wavelength( freq * m_parent->m_freq1 ); ... m_lastFreq1 = m_parent->m_freq1;
```

`updatePitch()` runs from `NesObject::renderOutput()` (`plugins/Nes/Nes.cpp:197`), i.e.
on the audio thread, while `updateFreq1()` runs on whichever thread changes the knob.
Nothing ordered those accesses: a `DirectConnection` slot body is not a
synchronisation edge for the render thread, so a knob turn during playback was a data
race on three plain floats read on the render path.

Note on history: the reads of `m_freq1..3` predate cluster new-p9-321 (they are in the
pre-migration source at `582d189a19^`); new-p9-321 widened them from "only when the note
frequency changes" to the condition of every pitch update, which makes the race fire on
every user detune change. The race is real on the current tree either way.

## The fix

The detune is no longer cached in a control-thread-written float at all. `updatePitch()`
reads the three models on the audio thread:

```
plugins/Nes/Nes.cpp:466  const float crs1 = m_parent->m_ch1Crs.value();
plugins/Nes/Nes.cpp:467  const float crs2 = m_parent->m_ch2Crs.value();
plugins/Nes/Nes.cpp:468  const float crs3 = m_parent->m_ch3Crs.value();
plugins/Nes/Nes.cpp:472  m_wlen1 = wavelength( freq * std::exp2( crs1 / 12.0f ) );
plugins/Nes/Nes.cpp:475  m_lastCrs1 = crs1;
```

For unconnected models, `AutomatableModel::value()` returns `publishedValue()`
(`include/AutomatableModel.h:239`), an atomic acquire load of the packed old/new pair
(`src/core/AutomatableModel.cpp:897-908`). Connected models follow the existing
controller read path. The three plain detune mirrors are gone.

Changes, all in `plugins/Nes`:

- `Nes.h`: `m_freq1/2/3`, the `updateFreq1/2/3` slots and their `DirectConnection`
  connect block are gone; the per-note memory is renamed `m_lastFreq1/2/3` →
  `m_lastCrs1/2/3` (`plugins/Nes/Nes.h:186-188`) so it is obvious that the only writer
  is the audio thread.
- `Nes.cpp`: `updatePitch()` derives the three wavelengths from the model values and
  recomputes `exp2` when the note frequency or a channel detune changes. The
  wavelengths are reused when those inputs hold steady.
- `Nes.cpp`: the constructor body no longer wires or seeds the removed slots
  (`plugins/Nes/Nes.cpp:558-562`).
- `Nes.cpp`: zero-frequency notes from unmapped keys render silence before
  oscillator state is constructed or read (`plugins/Nes/Nes.cpp:570-574`, round 2).
- `tests/CMakeLists.txt`: `NES_PLUGIN_SOURCE_DIR` is configured for the test target, so
  the source-level regression finds the plugin sources from any binary directory
  (round 2).

Siblings checked: `rg -n "m_freq|updateFreq|m_lastFreq" plugins/Nes` returns nothing, so
the three channels were the only users of the removed members, and the three channels are
all converted together (no channel is left on the old path). `NesObject` is the only
reader (`friend class NesObject`, `plugins/Nes/Nes.h`), the GUI view never touched the
factors, and no other plugin or test references them (`rg` over the tree outside
`plugins/Nes` is empty). Detune is not saved or loaded as a factor: the same `crs1/2/3`
models are serialised as before, so old projects load unchanged.

## Regression test

`tests/src/plugins/PluginPortsMigrationTest.cpp`, two new slots — the module host the
NES plugin needs already lives in that binary:

- `nesDetuneReachesRendererWithoutControlSignal()` — behaviour. Renders a held NES note
  (channel 1, vibrato/envelope/sweep off) for 8 periods, then writes coarse detune
  `0 → +12` with the instrument's models holding their change signal (a write that runs
  no control-side slot, which is also the shape of a render-thread automation tick:
  `AutomatableModel::setValueInternal()` defers the signal there), then measures the
  oscillator period of the next 8 periods by autocorrelation. Unfixed: `before=100
  after=100` (the mirror was never refreshed). Fixed: `before=100 after=50`.
  The same slot then renders an unmapped note (microtuner on, key outside the range,
  `frequency() == 0`) with both channels pre-filled with 1.0 and asserts both come back
  silent — the R1 regression above.
- `nesDetuneReadsTheModelOnTheRenderPath()` — cause. Reads `plugins/Nes/Nes.cpp` and
  `Nes.h` and asserts the mirror type is gone, that `updatePitch()` reads
  `m_ch1Crs/m_ch2Crs/m_ch3Crs` and no longer reads `m_parent->m_freq*`, and that no
  `SLOT( updateFreq` publishing slot survives. Its sources are located through
  `NES_PLUGIN_SOURCE_DIR`, so it does not depend on the build-directory layout.

Neither test uses a thread, a sleep or a clock: both are straight-line calls, and the
seams are the model's own signal state, the microtuner's key range and the plugin source.

`tests/CMakeLists.txt` gained focused `ctest` entries `NesDetunePublication`,
`NesDetuneRenderPath` and `NesCoarseDetune` (the latter is the pre-existing held-note
detune regression from new-p9-321, which previously had no entry of its own).

## Commands and exit codes, round 1 (copied from the logs)

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p18-65.r1/logs` (round 2 is
in `…/new-p18-65.r2/logs` and has its own table below).

| Step | Exact command | Exit | Log |
| --- | --- | --- | --- |
| Build test target, RED run | `cmake --build build --target PluginPortsMigrationTest -j4 > …/build-1.log 2>&1; echo EXIT=$?` | 0 | `build-1.log` |
| RED (test written, product unchanged), first shape | `ctest -R "NesDetunePublication" --output-on-failure > …/test-red-2.log 2>&1; echo EXIT=$?` (cwd `build/tests`) | 8 | `test-red-2.log` |
| Build after splitting the slot | `cmake --build build --target PluginPortsMigrationTest -j4 > …/build-3.log 2>&1; echo EXIT=$?` | 0 | `build-3.log` |
| RED, both new slots, product unchanged | `ctest -R "NesDetunePublication\|NesDetuneRenderPath\|NesCoarseDetune" --output-on-failure > …/test-red-4.log 2>&1; echo EXIT=$?` | 8 | `test-red-4.log` |
| Build plugin + test with the fix | `(cmake --build build --target nes PluginPortsMigrationTest -j4; echo BUILD_EXIT=$?) > …/build-5.log 2>&1` | 0 | `build-5.log` |
| GREEN after the fix | `ctest -R "NesDetunePublication\|NesDetuneRenderPath\|NesCoarseDetune" --output-on-failure > …/test-green-6.log 2>&1; echo EXIT=$?` | 0 | `test-green-6.log` |
| GREEN, measured periods visible | `./PluginPortsMigrationTest nesDetuneReachesRendererWithoutControlSignal > …/test-green-7.log 2>&1; echo EXIT=$?` (cwd `build/tests`) | 0 | `test-green-7.log` |
| Patch out the product change | `git diff -- plugins/Nes/Nes.cpp plugins/Nes/Nes.h > …/fix.patch; git apply -R …/fix.patch; echo APPLY_REVERSE_EXIT=$?` | 0 | `step4b-8.log` |
| Rebuild without the product change | `(cmake --build build --target nes PluginPortsMigrationTest -j4; echo BUILD_EXIT=$?) > …/build-8-red-after-revert.log 2>&1` | 0 | `build-8-red-after-revert.log` |
| RED with the product change reverted | `ctest -R "NesDetunePublication\|NesDetuneRenderPath" --output-on-failure > …/test-red-9.log 2>&1; echo EXIT=$?` | 8 | `test-red-9.log` |
| Patch back in | `git apply …/fix.patch; echo APPLY_FORWARD_EXIT=$?` | 0 | `step4b-8.log` |
| Rebuild with the fix restored | `(cmake --build build --target nes PluginPortsMigrationTest -j4; echo BUILD_EXIT=$?) > …/build-10-green-reapplied.log 2>&1` | 0 | `build-10-green-reapplied.log` |
| GREEN with the fix restored | `ctest -R "NesDetunePublication\|NesDetuneRenderPath\|NesCoarseDetune" --output-on-failure > …/test-green-11.log 2>&1; echo EXIT=$?` | 0 | `test-green-11.log` |
| Whole migration suite (family) | `ctest -R "PluginPortsMigrationTest" --output-on-failure > …/test-family-12.log 2>&1; echo CTEST_EXIT=$?` | 8 | `test-family-12.log` |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > …/feature-check-18.log 2>&1; echo EXIT=$?` | 0 | `feature-check-18.log` |
| Rebuild `zene` | `(cmake --build build --target zene -j4; echo BUILD_EXIT=$?) > …/build-19-zene.log 2>&1` | 0 | `build-19-zene.log` |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p18-65 > …/submit-check-20.log 2>&1; echo EXIT=$?` | 0 | `submit-check-20.log` |

What the logs say:

- `test-red-4.log`: `FAIL! : … nesDetuneReachesRendererWithoutControlSignal() 'std::abs(afterPeriod * 2 - beforePeriod) <= 3' returned FALSE` and `QINFO … NES detune without a control signal: before=100 after=100`; `FAIL! : … nesDetuneReadsTheModelOnTheRenderPath() '!header.contains(…) m_freq1' returned FALSE`; `NesCoarseDetune Passed`. Total: exit 8.
- `test-red-9.log` (product change reverted again, after the fix had once passed): same two failures, `before=100 after=100`, exit 8.
- `test-green-6.log` / `test-green-11.log`: `NesDetunePublication Passed`, `NesDetuneRenderPath Passed`, `NesCoarseDetune Passed`, `100% tests passed out of 3`, exit 0.
- `test-green-7.log`: `QINFO … NES detune without a control signal: before=100 after=50`, `Totals: 3 passed, 0 failed`.
- `feature-check-18.log`: `PASS fork-sources`, `PASS all-sources-reproduce`, `PASS no-upstream-regression`, `PASS check-namespace`, `PASS no-tautology`, `PASS doc-figures`, `PASS complexity`, `PASS file-length`, `RESULT: PASS`, exit 0.
- `submit-check-20.log`: `PASS commits on the branch`, `PASS working tree clean`, `PASS report docs/qwen-fixes-2026-10/cluster-new-p18-65.md`, `PASS tests added or changed`, `PASS new test files registered in tests/CMakeLists.txt`, `PASS gates (feature_check.py)`, `RESULT: PASS - submit`, exit 0.
- `build-19-zene.log`: `[100%] Built target zene`, `BUILD_EXIT=0`.

The family run `test-family-12.log` is exit 8 with `Totals: 95 passed, 4 failed, 2
skipped`; the four failures are `malletsVoiceLimitIsEnforcedPerInstrument`,
`malletsVoiceLimitSurvivesDeviceChange`, `nonRealtimeLadspaEffectsAreInvalid` (SWH
Decimator fixture not installed on this box) and `slice8PluginsAreLiveAndExact`
(`malletsstk`). They are not caused by this change: the same four slots, with the same
messages, fail with the product change reverted (`test-mallets-14-reverted.log`, exit 4)
and fail again with it restored (`test-mallets-16-fixed.log`, exit 4). See Other findings.

## Commands and exit codes, round 2 (copied from the logs)

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p18-65.r2/logs`. Every row
below was run in this round, on top of `2a580e032c`.

| Step | Exact command | Exit | Log |
| --- | --- | --- | --- |
| Build plugin + test | `(cmake --build build --target nes PluginPortsMigrationTest -j4; echo BUILD_EXIT=$?) > …/build-1.log 2>&1` | 0 | `build-1.log` |
| Green baseline, three focused tests | `cd build/tests && ctest -R "NesDetunePublication\|NesDetuneRenderPath\|NesCoarseDetune" --output-on-failure > …/test-2-baseline.log 2>&1; echo EXIT=$?` | 0 | `test-2-baseline.log` |
| Source test from a relocated binary (CMake-provided path) | `cp build/tests/PluginPortsMigrationTest $TMPDIR/nes-ext-layout/ && cd $TMPDIR/nes-ext-layout && ./PluginPortsMigrationTest nesDetuneReadsTheModelOnTheRenderPath > …/test-3-external-layout-green.log 2>&1; echo EXIT=$?` | 0 | `test-3-external-layout-green.log` |
| Rebuild with the old applicationDirPath-relative source path | `(cmake --build build --target PluginPortsMigrationTest -j4; echo BUILD_EXIT=$?) > …/build-4-red-layout.log 2>&1` | 0 | `build-4-red-layout.log` |
| RED for R2, relocated binary | `cd $TMPDIR/nes-ext-layout && ./PluginPortsMigrationTest nesDetuneReadsTheModelOnTheRenderPath > …/test-5-external-layout-red.log 2>&1; echo EXIT=$?` | 1 | `test-5-external-layout-red.log` |
| Restore the CMake-provided path and rebuild | `(cmake --build build --target PluginPortsMigrationTest -j4; echo BUILD_EXIT=$?) > …/build-6-green-layout.log 2>&1` | 0 | `build-6-green-layout.log` |
| GREEN for R2, relocated binary, two slots | `cd $TMPDIR/nes-ext-layout && ./PluginPortsMigrationTest nesDetuneReadsTheModelOnTheRenderPath nesDetuneReachesRendererWithoutControlSignal > …/test-7-external-layout-green-again.log 2>&1; echo EXIT=$?` | 0 | `test-7-external-layout-green-again.log` |
| Revert only the unmapped-note guard | `git show 2a580e032c -- plugins/Nes/Nes.cpp > …/guard.patch && git apply -R …/guard.patch; echo GUARD_REVERSE_EXIT=$?` | 0 | `step-8-guard-revert.log` |
| Rebuild without the guard | `(cmake --build build --target nes PluginPortsMigrationTest -j4; echo BUILD_EXIT=$?) > …/build-9-red-guard-reverted.log 2>&1` | 0 | `build-9-red-guard-reverted.log` |
| RED for R1 | `cd build/tests && ctest -R "NesDetunePublication" --output-on-failure > …/test-10-red-guard-reverted.log 2>&1; echo EXIT=$?` | 8 | `test-10-red-guard-reverted.log` |
| Restore the guard and rebuild | `git apply …/guard.patch; echo GUARD_FORWARD_EXIT=$?` then `(cmake --build build --target nes PluginPortsMigrationTest -j4; echo BUILD_EXIT=$?) > …/build-11-green-guard-restored.log 2>&1` | 0 / 0 | `step-8-guard-revert.log`, `build-11-green-guard-restored.log` |
| GREEN for R1, three focused tests | `cd build/tests && ctest -R "NesDetunePublication\|NesDetuneRenderPath\|NesCoarseDetune" --output-on-failure > …/test-12-green-guard-restored.log 2>&1; echo EXIT=$?` | 0 | `test-12-green-guard-restored.log` |
| Whole product diff (detune fix + guard) out | `git diff $(git rev-parse f404dd2349^)..HEAD -- plugins/Nes/Nes.cpp plugins/Nes/Nes.h > …/fix.patch; git apply -R …/fix.patch; echo APPLY_REVERSE_EXIT=$?` | 0 | `step-13-fix-patch.log` |
| Rebuild with the product code reverted | `(cmake --build build --target nes PluginPortsMigrationTest -j4; echo BUILD_EXIT=$?) > …/build-14-red-product-reverted.log 2>&1` | 0 | `build-14-red-product-reverted.log` |
| RED, both detune regressions | `cd build/tests && ctest -R "NesDetunePublication\|NesDetuneRenderPath" --output-on-failure > …/test-15-red-product-reverted.log 2>&1; echo EXIT=$?` | 8 | `test-15-red-product-reverted.log` |
| Restore the product diff and rebuild | `git apply …/fix.patch; echo APPLY_FORWARD_EXIT=$?` then `(cmake --build build --target nes PluginPortsMigrationTest -j4; echo BUILD_EXIT=$?) > …/build-16-green-product-restored.log 2>&1` | 0 / 0 | `step-13-fix-patch.log`, `build-16-green-product-restored.log` |
| GREEN, three focused tests | `cd build/tests && ctest -R "NesDetunePublication\|NesDetuneRenderPath\|NesCoarseDetune" --output-on-failure > …/test-17-green-product-restored.log 2>&1; echo EXIT=$?` | 0 | `test-17-green-product-restored.log` |
| Whole migration suite (family) | `cd build/tests && ctest -R "PluginPortsMigrationTest" --output-on-failure > …/test-18-family.log 2>&1; echo CTEST_EXIT=$?` | 8 | `test-18-family.log` |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > …/feature-check-19.log 2>&1; echo EXIT=$?` | 0 | `feature-check-19.log` |

What the round-2 logs say:

- `test-2-baseline.log`, `test-12-…`, `test-17-…`: `NesDetunePublication Passed`,
  `NesDetuneRenderPath Passed`, `NesCoarseDetune Passed`, `100% tests passed out of 3`,
  exit 0.
- `test-10-red-guard-reverted.log`: exit 8, `FAIL! :
  PluginPortsMigrationTest::nesDetuneReachesRendererWithoutControlSignal() 'std::all_of(…)'
  returned FALSE. (an unmapped NES note must render silence)`.
- `test-15-red-product-reverted.log`: exit 8, `QINFO … before=100 after=100` plus
  `FAIL! … std::abs(afterPeriod * 2 - beforePeriod) <= 3 …` and `FAIL! …
  '!header.contains(…) m_freq1' returned FALSE`.
- `test-3-…` / `test-7-external-layout-*.log`: relocated binary, exit 0 with
  `Totals: 3 passed` / `Totals: 4 passed`.
- `test-5-external-layout-red.log`: relocated binary with the old relative path, exit 1,
  `FAIL! … 'sourceFile.open(QIODevice::ReadOnly)' returned FALSE
  (…/new-p18-65.r2/tmp/nes-ext-layout/../../plugins/Nes/Nes.cpp)`.
- `test-18-family.log`: `Totals: 95 passed, 4 failed, 2 skipped`, exit 8 — byte-for-byte
  the same four failures and messages as round 1's `test-family-12.log`, so the guard
  added no new collateral.
- `feature-check-19.log`: all eight gates `PASS`, `RESULT: PASS`, exit 0.

## Test matrix

- Normal path: `NesCoarseDetune` — a detune write with the signals free still retunes the
  held note (100 → 50). Green before and after.
- Failure/regression path: `NesDetunePublication` — a detune write that runs no
  control-side slot must still reach the renderer. Red before, green after.
- Reconfiguration path: `Restore/restoreState` is the project-load path and is what both
  NES tests use to write the detune. The new test restores a non-zero detune while
  the note is sounding and the instrument's model signals are blocked.
- Exhaustion path: not applicable — no pool, limit or cache grows here; the removed
  members were three scalars.
- Unmapped / degenerate-input path (round 2): `NesDetunePublication` also renders a
  zero-frequency note and requires silence on both channels; red without the guard
  (`test-10-red-guard-reverted.log`), green with it (`test-12-…`).
- Audio-thread constraints: the render path uses the existing model reads and calls
  `std::exp2` on the pitch-change branch. The per-note `m_lastCrs*` fields are
  audio-thread-owned. The zero-frequency guard calls `zeroSampleFrames()`
  (`std::fill`) and returns: no allocation, free, lock, wait, log or Qt call.
- Platform: the module-host assertions skip on Windows via the file's existing
  `testHostCanLoadPluginModules()`/`PluginModuleHostSkipMessage` path (unchanged); the
  source-level assertions are platform-independent and still run there, and their paths
  are now configured by CMake rather than guessed from the executable directory. No
  Qt-6-only API, no `windows.h` names, no `std::filesystem`.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.**
  Round 1: `git apply -R fix.patch` (exit 0), rebuild (exit 0), `ctest -R
  "NesDetunePublication|NesDetuneRenderPath"` → exit 8 with `before=100 after=100` and
  the `m_freq1` header failure (`test-red-9.log`); restored (exit 0), rebuild (exit 0),
  same ctest → exit 0 (`test-green-11.log`); the first RED, before any product edit, is
  `test-red-4.log` (exit 8). Round 2 repeats it in its own folder:
  `test-15-red-product-reverted.log` → exit 8, `test-17-green-product-restored.log` →
  exit 0. Round 2 additionally proves each returned regression on its own: the
  unmapped-note guard reverted alone gives exit 8 with `an unmapped NES note must render
  silence` (`test-10-red-guard-reverted.log`) and exit 0 restored (`test-12-…`), and the
  source-path fix gives exit 1 from a relocated binary with the old relative path
  (`test-5-external-layout-red.log`) and exit 0 with the CMake-provided path (`test-3-…`,
  `test-7-…`).
- **The test is deterministic.** No threads, sleeps or clocks. The seams are (a) the
  instrument's models holding their `dataChanged` signal during one detune write
  (`QObject::blockSignals`), (b) autocorrelation over 8 rendered periods, using
  floating-point arithmetic on a fixed sample buffer, and (c) the microtuner's key range,
  which makes `NotePlayHandle::frequency()` exactly 0 — the test asserts that with
  `QCOMPARE(unmappedNote.frequency(), 0.0f)` before rendering it. The renderer is called
  directly through `Instrument::playNote()` with a fixed `framesPerPeriod` (44100/256).
- **Fix at the cause, complete.** The cause was the plain control-thread-written mirror;
  it is deleted, not merely ordered. All three channels were converted together; the
  three `DirectConnection` slots and their definitions were removed, the constructor no
  longer seeds them, and `rg -n "m_freq|updateFreq|m_lastFreq" plugins/Nes` is empty. No
  other file in the tree referenced them. The round-2 guard sits on the only path into
  oscillator state (`NesObject` is constructed only by `std::construct_at` inside
  `playNoteImpl`, `plugins/Nes/Nes.cpp:582`), so a zero frequency can neither
  construct nor be read by the render path.
- **No collateral damage.** `crs1/2/3` are still the saved and loaded attributes, so
  project files round-trip unchanged; the existing `nesCoarseDetuneRetunesHeldNote`
  regression (the saved-settings path) still passes, as do 95 of the 99 other slots in
  the suite (the 4 failures are identical, with identical messages, with and without this
  change: `test-family-12.log` round 1 and `test-18-family.log` round 2). No limit or
  check was added; the zero-frequency guard only changes notes that produce no pitch.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable, the verdict is FIXED; the
  code showing the race is cited above at `plugins/Nes/Nes.cpp:462,465-470,552,726` and
  `plugins/Nes/Nes.h:233-235` (pre-change `HEAD`).
- **Sibling commands in the command files the diff touched** (the check lists
  `app.about`, `arrangement.fit_tracks`, …): not applicable to this cluster. The check
  measures the branch against `FIX-BASE`, so it also sees the files other clusters
  already landed on this branch. This cluster's own commits touch no command file:
  `git show --stat f404dd2349` and `git show --stat 2a580e032c` list only
  `docs/qwen-fixes-2026-10/cluster-new-p18-65.md`, `plugins/Nes/Nes.cpp`,
  `plugins/Nes/Nes.h` (round 1), `tests/CMakeLists.txt`,
  `tests/src/plugins/PluginPortsMigrationTest.cpp` and `tests/upstream-modifications.txt`,
  plus this round's report and ledger edits. No agent command, no `ControlCommands*` file
  and no `tools/mcp-zene-control/zene_control/commands_snapshot.json` was changed, so the
  `ControlCommandsSnapshot` canary cannot move.

## Other findings

Unrelated, not fixed here — the whole-suite run reports four pre-existing environment
failures, identical in round 1 (`test-family-12.log`) and round 2 (`test-18-family.log`),
and identical again with this cluster's product change reverted
(`test-mallets-14-reverted.log`) and restored (`test-mallets-16-fixed.log`):

- `PluginPortsMigrationTest::malletsVoiceLimitIsEnforcedPerInstrument` and
  `::malletsVoiceLimitSurvivesDeviceChange` — a Mallets note is silenced instead of taking
  a voice ("Mallets note 0 of instrument 0 was silenced instead of taking a prepared
  voice").
- `PluginPortsMigrationTest::slice8PluginsAreLiveAndExact` — `malletsstk` fails to render
  on this box.
- `PluginPortsMigrationTest::nonRealtimeLadspaEffectsAreInvalid` — the SWH Decimator LADSPA
  fixture is not installed here (`ls /usr/lib/ladspa` has only amp, delay, filter, noise,
  sine).
