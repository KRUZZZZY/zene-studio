FIXED

# Cluster new-p18-65 — NES detune changes raced with audio rendering

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

That read is the fork's existing audio-safe publication contract: `AutomatableModel::value()`
returns `publishedValue()` (`include/AutomatableModel.h:239`), which is an atomic acquire
load of a packed old/new pair published by the control thread
(`src/core/AutomatableModel.cpp:897-908`). No plain field crosses threads any more.

Changes, all in `plugins/Nes`:

- `Nes.h`: `m_freq1/2/3`, the `updateFreq1/2/3` slots and their `DirectConnection`
  connect block are gone; the per-note memory is renamed `m_lastFreq1/2/3` →
  `m_lastCrs1/2/3` (`plugins/Nes/Nes.h:186-188`) so it is obvious that the only writer
  is the audio thread.
- `Nes.cpp`: `updatePitch()` derives the three wavelengths from the model values and
  recomputes `exp2` only when a detune actually changed, so the render path gains one
  atomic load per channel per pitch tick and no transcendental in the steady state.
- `Nes.cpp`: the constructor body no longer wires or seeds the removed slots
  (`plugins/Nes/Nes.cpp:558-562`).

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
- `nesDetuneReadsTheModelOnTheRenderPath()` — cause. Reads `plugins/Nes/Nes.cpp` and
  `Nes.h` and asserts the mirror type is gone, that `updatePitch()` reads
  `m_ch1Crs/m_ch2Crs/m_ch3Crs` and no longer reads `m_parent->m_freq*`, and that no
  `SLOT( updateFreq` publishing slot survives.

Neither test uses a thread, a sleep or a clock: both are straight-line calls, and the
seam is the model's own signal state plus the plugin source.

`tests/CMakeLists.txt` gained focused `ctest` entries `NesDetunePublication`,
`NesDetuneRenderPath` and `NesCoarseDetune` (the latter is the pre-existing held-note
detune regression from new-p9-321, which previously had no entry of its own).

## Commands and exit codes (copied from the logs)

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p18-65.r1/logs`.

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

## Test matrix

- Normal path: `NesCoarseDetune` — a detune write with the signals free still retunes the
  held note (100 → 50). Green before and after.
- Failure/regression path: `NesDetunePublication` — a detune write that runs no
  control-side slot must still reach the renderer. Red before, green after.
- Reconfiguration path: `Restore/restoreState` is the project-load path and is what both
  NES tests use to write the detune; the new test additionally proves the value read by
  the renderer is the model's published value, so a project loaded with a non-zero detune
  renders at the saved detune without any slot firing.
- Exhaustion path: not applicable — no pool, limit or cache grows here; the removed
  members were three scalars.
- Audio-thread constraints: the render path gained three atomic loads
  (`AutomatableModel::publishedValue()`) and keeps `std::exp2` only on the change branch;
  no allocation, free, lock, wait, log or Qt call was added. Renaming `m_lastFreq*` to
  `m_lastCrs*` keeps those fields audio-thread-only.
- Platform: the module-host assertions skip on Windows via the file's existing
  `testHostCanLoadPluginModules()`/`PluginModuleHostSkipMessage` path (unchanged); the
  source-level assertions are platform-independent and still run there. No Qt-6-only API,
  no `windows.h` names, no `std::filesystem`.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.**
  Reverted: `git apply -R fix.patch` (exit 0), rebuild (exit 0), `ctest -R
  "NesDetunePublication|NesDetuneRenderPath"` → exit 8 with `before=100 after=100` and
  the `m_freq1` header failure (`test-red-9.log`). Restored: `git apply fix.patch` (exit
  0), rebuild (exit 0), same ctest → exit 0 (`test-green-11.log`). The first RED, before
  any product edit, is `test-red-4.log` (exit 8).
- **The test is deterministic.** No threads, sleeps or clocks. The seams are (a) the
  instrument's models holding their `dataChanged` signal during one detune write
  (`QObject::blockSignals`), and (b) autocorrelation over 8 rendered periods, which is
  integer arithmetic on a fixed sample buffer. The renderer is called directly through
  `Instrument::playNote()` with a fixed `framesPerPeriod` (44100/256).
- **Fix at the cause, complete.** The cause was the plain control-thread-written mirror;
  it is deleted, not merely ordered. All three channels were converted together; the
  three `DirectConnection` slots and their definitions were removed, the constructor no
  longer seeds them, and `rg -n "m_freq|updateFreq|m_lastFreq" plugins/Nes` is empty. No
  other file in the tree referenced them.
- **No collateral damage.** `crs1/2/3` are still the saved and loaded attributes, so
  project files round-trip unchanged; the existing `nesCoarseDetuneRetunesHeldNote`
  regression (the saved-settings path) still passes, as do 95 of the 99 other slots in
  the suite (the 4 failures reproduce without this change). No limit or check was added.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable, the verdict is FIXED; the
  code showing the race is cited above at `plugins/Nes/Nes.cpp:462,465-470,552,726` and
  `plugins/Nes/Nes.h:233-235` (pre-change `HEAD`).
- **Sibling commands in the command files the diff touched** (the check lists
  `app.about`, `arrangement.fit_tracks`, …): not applicable to this cluster. The check
  measures the branch against `FIX-BASE`, so it also sees the 431 files other clusters
  already landed on this branch. This cluster's own commit touches no command file:
  `git show --stat f404dd2349` lists exactly `docs/qwen-fixes-2026-10/cluster-new-p18-65.md`,
  `plugins/Nes/Nes.cpp`, `plugins/Nes/Nes.h`, `tests/CMakeLists.txt`,
  `tests/src/plugins/PluginPortsMigrationTest.cpp`, `tests/upstream-modifications.txt`.
  No agent command, no `ControlCommands*` file and no
  `tools/mcp-zene-control/zene_control/commands_snapshot.json` was changed, so the
  `ControlCommandsSnapshot` canary cannot move.

## Other findings

Unrelated, not fixed here — the whole-suite run `test-family-12.log` reports four
pre-existing environment failures that reproduce identically with this cluster's product
change reverted (`test-mallets-14-reverted.log`) and restored
(`test-mallets-16-fixed.log`):

- `PluginPortsMigrationTest::malletsVoiceLimitIsEnforcedPerInstrument` and
  `::malletsVoiceLimitSurvivesDeviceChange` — a Mallets note is silenced instead of taking
  a voice ("Mallets note 0 of instrument 0 was silenced instead of taking a prepared
  voice").
- `PluginPortsMigrationTest::slice8PluginsAreLiveAndExact` — `malletsstk` fails to render
  on this box.
- `PluginPortsMigrationTest::nonRealtimeLadspaEffectsAreInvalid` — the SWH Decimator LADSPA
  fixture is not installed here (`ls /usr/lib/ladspa` has only amp, delay, filter, noise,
  sine).
