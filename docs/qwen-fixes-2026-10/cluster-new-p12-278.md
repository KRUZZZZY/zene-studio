# Cluster new-p12-278

**Verdict: FIXED** (fix committed in `1b318355d`; this round re-verified it end to end and closed the
one gate the earlier attempt had left red).

## The bug

`Vibed::playNoteImpl` handed `graphModel::samples()` straight to `VibratingString`'s constructor.
`graphModel` is GUI-thread storage that every waveform action rewrites in place, so a note that
started while such a write was in flight copied that storage on the audio thread with no
synchronisation and could build its string from a half-applied waveform.

- Vulnerable read (was `plugins/Vibed/Vibed.cpp:221`, now the `addString` argument at
  [Vibed.cpp:236](../../plugins/Vibed/Vibed.cpp:236)): `m_graphModels[i]->samples()`.
- Racy writers, all GUI-thread: [Graph.cpp:516](../../src/gui/widgets/Graph.cpp:516) `setSampleAt`
  (a waveform drag), [Graph.cpp:524](../../src/gui/widgets/Graph.cpp:524) `setSamples` (a project
  load, `Vibed::loadSettings`), and the action buttons `setWaveToSine` … `setWaveToNoise`,
  [smooth](../../src/gui/widgets/Graph.cpp:614), [normalize](../../src/gui/widgets/Graph.cpp:665) —
  each rewrites `m_samples` in place and then emits `samplesChanged`.
- Fix, declaration: [Vibed.h:91](../../plugins/Vibed/Vibed.h:91) `Vibed::WaveformSnapshot`
  ([publish](../../plugins/Vibed/Vibed.h:95), [samples](../../plugins/Vibed/Vibed.h:107)),
  [Vibed.h:119](../../plugins/Vibed/Vibed.h:119) `publishWaveform`,
  [Vibed.h:134](../../plugins/Vibed/Vibed.h:134) `m_waveforms`.
- Fix, definition: [Vibed.cpp:130](../../plugins/Vibed/Vibed.cpp:130) one direct `samplesChanged`
  connection per string in the constructor (connected before the first `setWaveToSine()`, so the
  initial waveform is published too), [Vibed.cpp:136](../../plugins/Vibed/Vibed.cpp:136)
  `Vibed::publishWaveform`, [Vibed.cpp:236](../../plugins/Vibed/Vibed.cpp:236) the audio path now
  reads the published copy.

The handoff is three `float[128]` slots, one atomic index and the slot a reader may still be copying.
`publish()` fills the slot that is neither the published one nor the stale one and then flips the
index, so a reader always sees one waveform in full and the slot it is copying is not written again
until the publication after next. One writer (the GUI thread, where every `samplesChanged` is
emitted), no lock, no allocation and no logging on either side; the audio path's cost is unchanged
(one atomic load, then the same copy it already made). New functions measure CCN 8 (`publish`) and
CCN 4 (`samples`).

## Regression

[tests/src/plugins/VibedWaveformHandoffTest.cpp](../../tests/src/plugins/VibedWaveformHandoffTest.cpp)
compiles the Vibed plug-in sources into the test (the SlicerT plug-in tests' shape) and drives the
real audio entry point `Instrument::playNote` with fresh note handles. 4 cases plus
`initTestCase`/`cleanupTestCase` = **6 QtTest cases**, registered as **1 CTest test** — the only
Vibed test in this build tree (`ctest -N -R '^Vibed'` → `Test #4`).

1. `oneWaveformRendersTheSameNoteEveryTime` — the premise: an unedited waveform renders a
   bit-identical block, so a block can be read as a waveform (the test runs the device at twice the
   base sample rate so `VibratingString`'s oversample factor is 1).
2. `everyWaveformActionReachesTheNextNote` — behaviour preservation: `normalize`, `setWaveToSquare`,
   `smooth`, `setWaveToNoise` each still change the *next* note. Passes before and after the fix.
3. `anUnpublishedWaveformEditNeverReachesANewNote` — the handoff contract: a waveform written into
   the model's storage without being published must not reach a later note. **Fails without the fix.**
4. `aWaveformEditedWhileNotesStartIsNeverReadHalfApplied` — the reported race through the real GUI
   API: a second thread drives `fillWith`/`setSampleAt` in the inward-from-both-ends order a
   back-and-forth drag produces while notes keep starting; every block must equal one of the 130
   states that edit sequence passes through. **Fails without the fix.**

## Evidence

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-278.r4/logs` (`$L` below).
Build commands ran from the worktree root; CTest commands ran from `build/tests`.

| Step | Exact command | EXIT | Result |
|---|---|---:|---|
| Build the test, the plug-in module and the app (header change → both rebuilt) | `cmake --build build --target vibedstrings zene VibedWaveformHandoffTest > $L/build-1.log 2>&1` | 0 | Clean build of the committed fix. |
| GREEN on the committed fix | `ctest -R '^Vibed' --output-on-failure > $L/test-1.log 2>&1` | **0** | 1/1 CTest test passed. |
| Case list of the family | `./VibedWaveformHandoffTest > $L/cases-1.log 2>&1` | 0 | `Totals: 6 passed, 0 failed` (4 cases + setup/teardown). |
| Save and remove the product fix | `git show 1b318355d -- plugins/Vibed/Vibed.cpp plugins/Vibed/Vibed.h > $L/fix.patch && git apply -R $L/fix.patch` | 0 | Only the production change reversed; the test untouched. |
| Rebuild the test without the fix | `cmake --build build --target VibedWaveformHandoffTest > $L/build-2.log 2>&1` | 0 | Test rebuilt against the original `playNoteImpl`. |
| **RED without the fix** | `ctest -R '^Vibed' --output-on-failure > $L/test-2-red.log 2>&1` | **8** | 1/1 CTest test FAILED; failing assertions recorded verbatim: `FAIL!  : VibedWaveformHandoffTest::anUnpublishedWaveformEditNeverReachesANewNote() 'identical(handedOff, edited)' returned FALSE. (a note started after an in-place waveform edit rendered a waveform the GUI thread never handed off)` and `FAIL!  : VibedWaveformHandoffTest::aWaveformEditedWhileNotesStartIsNeverReadHalfApplied() Compared values are not the same / Actual (foreign.load()): 1 / Expected (0): 0` (test line 267; the first failure is test line 228). |
| Restore the fix and rebuild everything | `git apply $L/fix.patch && cmake --build build --target vibedstrings zene VibedWaveformHandoffTest > $L/build-3.log 2>&1` | 0 | Fix back in place. |
| GREEN after restore | `ctest -R '^Vibed' --output-on-failure > $L/test-3-green.log 2>&1` | **0** | 1/1 CTest test passed. |
| Rebuild after this round's edits, re-run | `cmake --build build --target VibedWaveformHandoffTest > $L/build-4.log 2>&1` then `ctest -R '^Vibed' --output-on-failure > $L/test-4-green.log 2>&1` | 0 / **0** | 1/1 CTest test passed. |
| Stability (10 consecutive runs of the binary) | `for i in $(seq 1 10); do ./VibedWaveformHandoffTest > /tmp/vw.$i.log 2>&1 \|\| fails=$((fails+1)); done > $L/repeat-1.log 2>&1` | 0 (`REPEAT_FAILS=0`) | 10 × `Totals: 6 passed, 0 failed`. |
| Complexity gate | `bash tests/complexity-gate.sh --check > $L/gate-complexity-final.log 2>&1` | **1** | Pre-existing, see below. New functions in this change: CCN 8 / 4. |
| File-length gate | `bash tests/file-length-gate.sh --check > $L/gate-filelength-final.log 2>&1` | **1** | Pre-existing, see below. |
| Upstream ledger gate | `bash tests/no-upstream-regression-gate.sh > $L/gate-upstream-final.log 2>&1` | 0 | All three changed inherited files declared with reasons. |
| Fork-sources gate (after re-deriving `tests/all-sources.txt`) | `bash tests/fork-sources-gate.sh > $L/gate-forksources-final.log 2>&1` | **0** | Was **1** in the earlier attempt: the new test was registered in `tests/fork-sources.txt` but missing from the whole-tree manifest `tests/all-sources.txt`, whose own header recipe gate 9 re-derives on every run. Fixed this round by running that recipe verbatim (`$L/all-sources-recipe.out`, one added path) and writing its output over the entry list; no hand edit. |

### Why the two ratchet gates are still red

Both gates fail on files this cluster never touched, and they failed the same way before it. The
final logs list 6 complexity and 14 file-length regressions; searching both logs for every path this
cluster changed (`$L/gate-attribution-final.log`) returns **0 hits** for all seven of them. The
regressing files were last touched by other clusters' merges on 2026-10-14/15
(`src/core/DawProjectReadTracks.cpp` ← `729fb9709`, `tests/src/gui/XyPadPairTest.cpp` ← `7a61ab54d`,
`src/core/TakeLane.cpp` ← `6fb450db1`, `tests/src/core/DeviceDspTest.cpp` ← `f91ea87b2`), and no
`Vibed` path appears in either log at all. Fixing them would mean refactoring 20 unrelated files
outside this cluster's contract; re-anchoring the baselines is forbidden, so they are reported
rather than silenced. No gate limit or baseline was touched.

## Files in this cluster's commit set

- `plugins/Vibed/Vibed.h` — `WaveformSnapshot`, `m_waveforms`, `publishWaveform()` declaration.
- `plugins/Vibed/Vibed.cpp` — publish on `samplesChanged`; audio path reads the published copy.
- `tests/src/plugins/VibedWaveformHandoffTest.cpp` — new regression (fork-NEW).
- `tests/CMakeLists.txt` — builds the test and compiles the Vibed sources into it.
- `tests/fork-sources.txt` — registers the new test.
- `tests/all-sources.txt` — whole-tree manifest re-derived from its own recipe (this round).
- `tests/upstream-modifications.txt` — declares the three changed inherited files with reasons, and
  the cluster comment now says "its two race cases fail before this change" (it said three).
- `docs/qwen-fixes-2026-10/cluster-new-p12-278.md` — this report.
