# Cluster new-p12-278

**Verdict: FIXED** (round 6 — the round-5 rework: the mechanical proof control now sees and runs
the regression and it fails without the product change; the three-slot rotation is gone, replaced
by an immutable publication the reader can hold).

## The bug

`Vibed::playNoteImpl` handed `graphModel::samples()` straight to `VibratingString`'s constructor.
`graphModel` is GUI-thread storage that every waveform action rewrites in place, so a note that
started while such a write was in flight copied that storage on the audio thread with no
synchronisation and could build its string from a half-applied waveform.

- Vulnerable read (was `plugins/Vibed/Vibed.cpp:221`, now the `addString` argument at
  [Vibed.cpp:236](../../plugins/Vibed/Vibed.cpp:236)): `m_graphModels[i]->samples()`.
- Racy writers, all GUI-thread: [Graph.cpp:519](../../src/gui/widgets/Graph.cpp:519) `setSampleAt`
  (a waveform drag), [Graph.cpp:528](../../src/gui/widgets/Graph.cpp:528) `setSamples` (a project
  load, `Vibed::loadSettings`), and the action buttons `setWaveToSine` … `setWaveToNoise`,
  [smooth](../../src/gui/widgets/Graph.cpp:608), [normalize](../../src/gui/widgets/Graph.cpp:627) —
  each rewrites `m_samples` in place and then emits `samplesChanged`.
- Fix, declaration: [Vibed.h:109](../../plugins/Vibed/Vibed.h:109) `Vibed::WaveformSnapshot`
  ([samples](../../plugins/Vibed/Vibed.h:125), [publish](../../plugins/Vibed/Vibed.h:137),
  [takeFree](../../plugins/Vibed/Vibed.h:154), [reclaim](../../plugins/Vibed/Vibed.h:163)),
  [Vibed.h:191](../../plugins/Vibed/Vibed.h:191) `publishWaveform`,
  [Vibed.h:206](../../plugins/Vibed/Vibed.h:206) `m_waveforms`.
- Fix, definition: [Vibed.cpp:130](../../plugins/Vibed/Vibed.cpp:130) one direct `samplesChanged`
  connection per string in the constructor (connected before the first `setWaveToSine()`, so the
  initial waveform is published too), [Vibed.cpp:136](../../plugins/Vibed/Vibed.cpp:136)
  `Vibed::publishWaveform`, [Vibed.cpp:236](../../plugins/Vibed/Vibed.cpp:236) the audio path now
  reads the published copy.

## What round 4 got wrong, and what this round does

Round 4 published into one of three slots and kept a GUI-thread guess (`m_stale`) of the slot a
reader may still be copying. The guess is only right for two publications: on the third one the
writer reuses the reader's slot, so a reader paused across three publications can still copy a
waveform the GUI thread is rewriting. The QA review of round 4 asked for a hand-off where the
reader's active slot is visible to the writer.

This round replaces the rotation with an **immutable publication**:

- A publication fills a waveform nobody can see yet and hands it over with one atomic store
  ([Vibed.h:137](../../plugins/Vibed/Vibed.h:137)). **A published waveform is never written again**,
  so the audio thread reads one waveform in full however many publications pass while it copies.
- The reader announces the waveform it took ([Vibed.h:125](../../plugins/Vibed/Vibed.h:125)): the
  announced waveform is the one the GUI thread must not reuse, so the GUI thread keeps it out of
  the free list until the reader takes another one. That is the reader's active waveform, visible
  to the writer.
- While the reader is between taking a waveform and announcing it, the writer cannot tell which
  one it took, so it reuses none of them ([Vibed.h:163](../../plugins/Vibed/Vibed.h:163)). The
  reader's "I am reading" store and the writer's "is it reading" load are sequentially consistent,
  so they cannot both miss each other (the store-buffer litmus test is forbidden under SC
  ordering): either the writer sees the reader's flag, or the reader sees the writer's exchange.
- The writer recycles the waveforms it may write into ([Vibed.h:154](../../plugins/Vibed/Vibed.h:154)),
  so the steady state allocates nothing at all; a publication allocates only when the reader was
  busy and nothing could be recycled.

One writer (the GUI thread, where every `samplesChanged` is emitted), one reader (the audio
thread). The audio path costs one atomic load, two atomic stores and the same copy it already
made: **no allocation, no lock, no logging, no waiting on the audio thread**. New functions
measure CCN 1 (`samples`), 1 (`publish`), 2 (`takeFree`), 4 (`reclaim`), 1 (constructor),
3 (destructor).

## Regression

[tests/src/plugins/VibedWaveformHandoffTest.cpp](../../tests/src/plugins/VibedWaveformHandoffTest.cpp)
compiles the Vibed plug-in sources into the test (the SlicerT plug-in tests' shape) and drives the
real audio entry point `Instrument::playNote` with fresh note handles. **5 cases plus
`initTestCase`/`cleanupTestCase` = 7 QtTest cases**, registered as **1 CTest test** — the only
Vibed test in this build tree (`ctest -N -R 'Vibed'` → `Test #470: VibedWaveformHandoffTest`).

1. `oneWaveformRendersTheSameNoteEveryTime` — the premise: an unedited waveform renders a
   bit-identical block, so a block can be read as a waveform (the test runs the device at twice the
   base sample rate so `VibratingString`'s oversample factor is 1).
2. `everyWaveformActionReachesTheNextNote` — behaviour preservation: `normalize`, `setWaveToSquare`,
   `smooth`, `setWaveToNoise` each still change the *next* note. Passes before and after the fix.
3. `anUnpublishedWaveformEditNeverReachesANewNote` — the hand-off contract: a waveform written into
   the model's storage without being published must not reach a later note.
4. `aWaveformEditedWhileNotesStartIsNeverReadHalfApplied` — the reported race through the real GUI
   API: a second thread drives `fillWith`/`setSampleAt` in the inward-from-both-ends order a
   back-and-forth drag produces while notes keep starting; every block must equal one of the 130
   states that edit sequence passes through.
5. **`aHeldWaveformSurvivesThreePublications`** ([test:347](../../tests/src/plugins/VibedWaveformHandoffTest.cpp:347))
   — the deterministic regression: a reader takes a published waveform and holds it while the
   writer publishes three more, then the held waveform must be unchanged, and the last publication
   must have reached the next reader (the control that gives the check its power). No timing, no
   sleeps, no threads: one thread, four publications.

Case 3 and case 4 fail without the *original* fix (round 1, commit `1b318355d`); case 5 fails
without this round's fix.

### The feature trait around case 5

Case 5 drives the plug-in's hand-off, so it needs the product API that the fix introduces. The
mechanical proof control reverts the **whole** product change, and in that state the hand-off is
not there at all: the test file would not build, and the control would see a build error instead
of the failures the reverted plug-in produces. The case therefore goes through a feature trait
([test:87](../../tests/src/plugins/VibedWaveformHandoffTest.cpp:87) `HasWaveformHandOff`,
[test:128](../../tests/src/plugins/VibedWaveformHandoffTest.cpp:128) `runHeldWaveformCheck`):

- with the hand-off, the case is compiled and runs the deterministic check unchanged
  ([test:97](../../tests/src/plugins/VibedWaveformHandoffTest.cpp:97) `checkHeldWaveform`);
- without it, the case reports **"the plug-in has no immutable waveform hand-off: the audio thread
  reads the wave graph model the GUI thread rewrites"** — and case 3, which is a plain behavioural
  check, fails on its own as well.

So with the product change reverted the file still builds, and the control sees three real
failures — the behavioural one first.

## Evidence

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-278.r5/logs` (`$L` below).
Build commands ran from the worktree root; CTest commands ran from `build/tests`.

| Step | Exact command | EXIT | Result |
|---|---|---:|---|
| Save the product change (the fix commit) | `git show a6db57c8e -- plugins/Vibed/Vibed.cpp plugins/Vibed/Vibed.h > $L/fix.patch` | 0 | 176 lines, product files only. |
| Reverse only the product change | `git apply -R $L/fix.patch` | 0 | The test still compiles: the hand-off's *interface* did not change, only its implementation. |
| Rebuild without the fix | `cmake --build build --target VibedWaveformHandoffTest > $L/build-12-fine-red.log 2>&1` | 0 | Test rebuilt against the round-4 hand-off. |
| **RED without the fix (fine-grained proof)** | `ctest -R '^VibedWaveformHandoffTest$' --output-on-failure > $L/test-12-fine-red.log 2>&1` | **8** | 1/1 CTest test FAILED: `FAIL! : VibedWaveformHandoffTest::aHeldWaveformSurvivesThreePublications() 'std::memcmp(held, first.data(), sizeof(first)) == 0' returned FALSE. (a waveform the reader was holding was rewritten by a later publication: the reader would copy a half-written waveform)`, `Totals: 6 passed, 1 failed`. |
| Restore the fix | `git apply $L/fix.patch` | 0 | Fix back in place. |
| Rebuild the app, the plug-in module and the test (header change → all three) | `cmake --build build --target zene vibedstrings VibedWaveformHandoffTest > $L/build-13-fine-green.log 2>&1` | 0 | Clean build of the committed fix. |
| **GREEN after restore (fine-grained proof)** | `ctest -R '^Vibed' --output-on-failure > $L/test-13-fine-green.log 2>&1` | **0** | 1/1 CTest test passed (`Test #470`). |
| Case list of the family | `./VibedWaveformHandoffTest > $L/cases-1.log 2>&1` | 0 | `Totals: 7 passed, 0 failed` (5 cases + setup/teardown). |
| Stability (10 consecutive runs of the binary) | `for i in $(seq 1 10); do ./VibedWaveformHandoffTest > /tmp/vw.$i.log 2>&1 \|\| fails=$((fails+1)); done > $L/repeat-1.log 2>&1` | 0 (`REPEAT_FAILS=0`) | 10 × `Totals: 7 passed, 0 failed`. |
| Realtime-safety sweep (whole tree) | `ctest -R 'RtSafetySweep' --output-on-failure > $L/test-8-rtsafety.log 2>&1` | **0** | 1/1 passed; the Vibed plug-in is not in the declared scope and the change adds no allocation, lock or wait to the audio path. |
| **Mechanical proof control, whole product change reverted** | `negctl.prove(<worktree>, <base>, <run dir>, <report>)` (the loop's own control: reverts `git diff base..HEAD -- plugins/Vibed/Vibed.cpp plugins/Vibed/Vibed.h`, builds, runs the selected tests, restores) | **ok: True** | `RtSafetySweep, VibedWaveformHandoffTest: they fail without it (rc 8) and pass with it`; `reverted_build_rc: 0`, `reverted_rc: 8`, `restored_rc: 0`, `tree_clean: True`. Reverted run ($L/negctl-test-reverted.log): **three** failures — `anUnpublishedWaveformEditNeverReachesANewNote` ("a note started after an in-place waveform edit rendered a waveform the GUI thread never handed off"), `aWaveformEditedWhileNotesStartIsNeverReadHalfApplied` (foreign 1 vs 0) and `aHeldWaveformSurvivesThreePublications` ("the plug-in has no immutable waveform hand-off"), `Totals: 4 passed, 3 failed`. Restored run ($L/negctl-test-restored.log): 2/2 passed. |
| Gate table (all gates) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > $L/gate-feature-check-2.log 2>&1` | **0** | `RESULT: PASS` — fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity ("no new item in your files"), file-length ("no new item in your files"). |
| Complexity ratchet | `bash tests/complexity-gate.sh --check > $L/gate-complexity-2.log 2>&1` | **1** | Pre-existing, see below. New functions: CCN 1/1/2/4/1/3 in the plug-in, 1/2 in the test. |
| File-length ratchet | `bash tests/file-length-gate.sh --check > $L/gate-filelength-2.log 2>&1` | **1** | Pre-existing, see below. `plugins/Vibed/Vibed.h` is 273 lines, the test 362. |
| Upstream ledger gate | (part of `feature_check.py`, `no-upstream-regression`) | **0** | The changed inherited files are declared with reasons. |
| Fork-sources gate | (part of `feature_check.py`, `fork-sources`) | **0** | No new product file; the test file was already registered. |

### Why the two ratchet gates are still red

Both gates fail on files this cluster never touched, and they failed the same way before it (the
round-4 review recorded the same 6 complexity and 14 file-length regressions). Searching both logs
for `Vibed` returns **0 hits** in each. The regressing files were last touched by other clusters'
merges on 2026-10-14/15 (`src/core/DawProjectReadTracks.cpp`, `tests/src/gui/XyPadPairTest.cpp`,
`src/core/TakeLane.cpp`, `tests/src/core/DeviceDspTest.cpp`, …). Fixing them would mean refactoring
20 unrelated files outside this cluster's contract; re-anchoring the baselines is forbidden, so they
are reported rather than silenced. No gate limit or baseline was touched.

### What the round-5 proof check found, and what changed because of it

The control reverted the product change, rebuilt and ran the tests it had selected, and the only
test it selected was `RtSafetySweep` — which passes with or without the change. Two things were in
the way, and both are fixed:

1. **The control could not see the test.** It reads the ctest test *names* out of `ctest -N`, and
   ctest right-aligns the test number: with 595 tests the list reads `Test   #4:`, and the
   control's `Test #\d+:` pattern never matches a test numbered below 100. `VibedWaveformHandoffTest`
   was #4, so it was invisible to the control no matter what the report said. The test is
   registered **last** now ([tests/CMakeLists.txt:1361](../../tests/CMakeLists.txt:1361)) and is
   `Test #470` — the control finds it.
2. **The test did not build in the reverted state.** The control reverts the whole product change,
   which removes the hand-off the deterministic case drives. The case now goes through the feature
   trait described above, so the file builds either way and the control sees the reverted plug-in's
   real failures (three of them) instead of a compiler error.

## Files in this cluster's commit set

- `plugins/Vibed/Vibed.h` — the hand-off: an immutable published waveform, the waveform the reader
  announced, and the reader's reading flag; the free list and the retired list.
- `tests/src/plugins/VibedWaveformHandoffTest.cpp` — the deterministic held-waveform regression and
  the feature trait around it.
- `tests/CMakeLists.txt` — builds the test, compiles the Vibed sources into it, and registers it
  last so the mechanical control can see it.
- `tests/upstream-modifications.txt` — the ledger entries for the changed inherited files.
- `docs/qwen-fixes-2026-10/cluster-new-p12-278.md` — this report.

## Self-check

**1. Does the regression FAIL with the product change reverted and PASS with it?**
Yes — twice, at two different granularities, and both were run:

- *Fine-grained* (the fix commit's product diff reversed, the hand-off's interface kept):
  `git show a6db57c8e -- plugins/Vibed/Vibed.cpp plugins/Vibed/Vibed.h > $L/fix.patch` → **EXIT 0**;
  `git apply -R $L/fix.patch` → **EXIT 0**; `cmake --build build --target VibedWaveformHandoffTest`
  → **EXIT 0**; `ctest -R '^VibedWaveformHandoffTest$' --output-on-failure` → **EXIT 8**,
  `FAIL! : …aHeldWaveformSurvivesThreePublications() 'std::memcmp(held, first.data(),
  sizeof(first)) == 0' returned FALSE.` ($L/test-12-fine-red.log); `git apply $L/fix.patch` →
  **EXIT 0**; `cmake --build build --target zene vibedstrings VibedWaveformHandoffTest` →
  **EXIT 0**; `ctest -R '^Vibed' --output-on-failure` → **EXIT 0** ($L/test-13-fine-green.log).
- *Whole product change reverted* (what the loop's mechanical control does):
  `negctl.prove(...)` → **ok: True**, `reverted_build_rc: 0`, `reverted_rc: 8`, `restored_rc: 0`,
  `tree_clean: True`; the reverted run fails three cases, the first of them the behavioural
  "a note started after an in-place waveform edit rendered a waveform the GUI thread never handed
  off" ($L/negctl-test-reverted.log, $L/negctl-test-restored.log).

**2. Is the test deterministic — no wall-clock timing, no sleeps, no races?**
Yes. The new case `aHeldWaveformSurvivesThreePublications` runs on ONE thread with FOUR
publications and no timing at all: it publishes a waveform, takes it with `samples()` (the
hand-off's own reader entry point — the seam the QA asked for: the reader's active waveform is
what the writer must not touch), publishes three more waveforms, and compares the held waveform
with the one it published. The values are injected, not sampled: `flatWaveform(0.25f)`,
`flatWaveform(-0.5f)`, `flatWaveform(0.75f)`, `flatWaveform(-1.f)`. No clock, no sleep, no
timeout, no second thread. The older case 4 uses a second thread, but it is a race *probe* for
the reported symptom, not the regression that proves this fix: case 5 is the deterministic one, and
cases 3 and 5 are deterministic.

**3. Is the fix at the cause and complete — every caller and sibling checked?**
The cause was "the writer may rewrite the storage the reader is copying". The writer now never
writes into a published waveform, and never reuses one the reader may still hold. Every caller
and sibling was checked:

- `WaveformSnapshot` has exactly two callers: `Vibed::publishWaveform` (the writer,
  [Vibed.cpp:136](../../plugins/Vibed/Vibed.cpp:136)) and `Vibed::playNoteImpl` (the reader,
  [Vibed.cpp:236](../../plugins/Vibed/Vibed.cpp:236)). There is one instance per string
  (`std::array<WaveformSnapshot, 9>`, [Vibed.h:206](../../plugins/Vibed/Vibed.h:206)), each with
  its own reader state, and the audio thread reads them one after another — one reader per
  instance, which is what the hand-off's protocol assumes.
- The other path into the same storage is `graphModel::samples()` itself: the audio path no
  longer calls it at all; `saveSettings` ([Vibed.cpp:164](../../plugins/Vibed/Vibed.cpp:164)) and
  `loadSettings` still do, on the GUI thread.
- Every writer of the model ends in `samplesChanged`
  ([Graph.cpp:519](../../src/gui/widgets/Graph.cpp:519) … [Graph.cpp:738](../../src/gui/widgets/Graph.cpp:738)),
  and the plug-in's own connection ([Vibed.cpp:130](../../plugins/Vibed/Vibed.cpp:130)) turns each of
  those into one publication — including the constructor's first `setWaveToSine()` and a project
  load, so the published waveform never lags the model.
- No other code includes `plugins/Vibed/Vibed.h` (only `Vibed.cpp` and the regression test), so
  no sibling call site was left behind.

**4. No collateral damage — do old projects and saved data still load as before, and does any
added limit or check hit other callers?**
`saveSettings`/`loadSettings` and the saved format are untouched, so an old project loads exactly
as before; the waveform the plug-in publishes is still the model's waveform (case 2 proves every
waveform action — normalize, the wave buttons, smooth — still reaches the next note, and case 3
proves the hand-off still filters what was never handed off). No limit, capacity or check was
added that another caller could hit: the new bookkeeping is per string and per plug-in instance
(one published waveform, one retired list of at most two entries, one free list of at most two),
and the behaviour outside the bug is unchanged. The test's own change is confined to the test
file: the feature trait decides whether the deterministic case is compiled; it does not weaken the
assertion — without the hand-off the case fails instead of passing.

**5. If the verdict were ALREADY-FIXED or FALSE-POSITIVE — the code that shows it.**
Not applicable: the verdict is **FIXED**. The bug was reproduced on this branch before this round
(the mechanical control's reverted run fails three cases, **rc 8**) and is gone after it
(**rc 0**).

**Realtime, undo and commands.** The audio thread allocates nothing, frees nothing, locks nothing,
waits for nothing and logs nothing: it takes the published waveform with one atomic load and two
atomic stores and makes the copy it always made. The allocation is on the GUI thread, and only
when the reader was busy and nothing could be recycled. Undo is untouched — no command and no
undoable step was added — and no control command was added for this bug fix.
