FIXED

# Cluster new-p9-546-port

Verdict: **FIXED** - a video-only AAF no longer crashes the reader; the audio
aggregate is checked before it is used.

## The bug in this tree

- `src/core/AafImportRead.cpp:315` (before the fix, the line the finding names):
  `walk.model->sampleRate = aafi->Audio->samplerate > 0 ...` - an unguarded
  dereference of `aafi->Audio`.
- `src/core/AafImportRead.cpp:317` and `:319`: `readMedia(walk, options)` and
  `AAFI_foreachAudioTrack(aafi, track)` - LibAAF's own macros
  (`AAFI_foreachAudioEssenceFile` and `AAFI_foreachAudioTrack` in
  `libaaf/AAFIface.h:1038` and `:992`) both start at `aafi->Audio->...`, so they
  crash on the same null pointer.
- `src/core/AafImportRead.cpp:329` (before the fix): the sibling path for video
  was already guarded - `if (aafi->Video != nullptr) { countVideo(walk); }` -
  which is why a composition with video but no audio was expected to work.

The fix (now at `src/core/AafImportRead.cpp:315-334`) takes the audio aggregate
into a local, uses the default sample rate when it is absent, and reads the
media and the audio tracks only when it is there. Markers and the video loss
report still run, so a video-only composition reports its video loss exactly as
before.

## The regression test

`tests/src/core/AafImportTest.cpp:372`
(`theVideoOnlyCompositionUsesTheDefaultSampleRate`) builds the LibAAF state of
a video-only composition - `AAF_Iface::Audio` left null (the
`AAF_Iface aafi{}` at `tests/src/core/AafImportTest.cpp:379`), one
`aafiVideoTrack` behind `aafi::Video` - and drives the reader over it. It
checks the default sample rate, an empty audio model (no tracks, no media) and
the video loss count (1 track, 0 clips).

Without the fix the test dies with SIGSEGV inside `readComposition` (the null
`aafi->Audio`), with the fix it passes. The whole `AafImportTest` binary is 17
QtTest cases with the fix in place
(`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/test-17-aaf-direct.log`:
`Totals: 17 passed, 0 failed, 0 skipped`).

## How the port avoids the collision that blocked the original merge

The fix accepted on `bugs/qwen-new-p9-546` (commit `53f3e95f1`) put a second
copy of the reader into the test file:

```cpp
#include "../../../src/core/AafImportRead.cpp"
```

`tests/src/core/AafImportTest.cpp:65` already includes that file once, for
cluster new-p8-259 (the `AafMaxWarnings` cap of `resolveEssence()`). A second
inclusion in the same translation unit defines the same helpers
(`Walk`, `readComposition`, `resolveEssence`, `text`, ...) twice, so the two
branches cannot both be applied.

This port therefore adds **no** second inclusion. The new test drives the copy
of the reader that the test file already includes, next to the new-p8-259 test
that introduced that inclusion. The product change itself is the same as the
accepted one.

## Verification evidence

Every build and test output was saved in the log folder
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs`.
Build, patch and gate commands were run from the worktree root
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-546-port`;
`ctest` commands were run from
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-546-port/build/tests`.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Baseline build (tree before the change) | `cmake --build build --target AafImportTest -j"$(nproc)" > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/build-1.log 2>&1; echo "EXIT=$?"` | 0 | Built `AafImportTest`. |
| Baseline test (tree before the change) | `ctest -R "^AafImportTest$" --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/test-1.log 2>&1; echo "EXIT=$?"` | 0 | `AafImportTest` (CTest #296) passed, 1/1. |
| Build the new regression on the unfixed product code | `cmake --build build --target AafImportTest -j"$(nproc)" > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/build-2.log 2>&1; echo "EXIT=$?"` | 0 | Built - the test compiles next to the new-p8-259 include, no second inclusion. |
| **RED** on the unfixed product code | `ctest -R "^AafImportTest$" --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/test-2.log 2>&1; echo "EXIT=$?"` | **8** | `AafImportTest` (CTest #296) crashed: `Received signal 11 (SIGSEGV), code SEGV_MAPERR, ... accessing address 0x000000000000000c` in `theVideoOnlyCompositionUsesTheDefaultSampleRate` (the null `aafi->Audio`). |
| Build with the fix | `cmake --build build --target zene AafImportTest -j"$(nproc)" > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/build-3.log 2>&1; echo "EXIT=$?"` | 0 | Rebuilt `zene` and the test target. |
| **GREEN** with the fix | `ctest -R "^AafImportTest$" --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/test-3.log 2>&1; echo "EXIT=$?"` | **0** | `AafImportTest` passed, 1/1. |
| Save the product diff | `git diff -- src/core/AafImportRead.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/fix.patch; echo "EXIT=$?"` | 0 | 39 lines, the product file only (the test is not in the patch). |
| Reverse the product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/fix.patch; echo "EXIT=$?"` | 0 | The reader is back to the unguarded dereference. |
| Rebuild without the fix | `cmake --build build --target zene AafImportTest -j"$(nproc)" > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/build-5.log 2>&1; echo "EXIT=$?"` | 0 | Rebuilt `zene` and the test target. |
| **RED** after reversing the fix | `ctest -R "^AafImportTest$" --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/test-6.log 2>&1; echo "EXIT=$?"` | **8** | The same SIGSEGV in `theVideoOnlyCompositionUsesTheDefaultSampleRate` - the test catches the bug. |
| Reapply the product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/fix.patch; echo "EXIT=$?"` | 0 | The guard is back. |
| Rebuild with the fix (after reapplying) | `cmake --build build --target zene AafImportTest AafImportMenuTest VideoTrackTest -j"$(nproc)" > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/build-6.log 2>&1; echo "EXIT=$?"` | 0 | Rebuilt `zene` and the three test targets. |
| **GREEN** after reapplying the fix | `ctest -R "^AafImportTest$" --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/test-8.log 2>&1; echo "EXIT=$?"` | **0** | `AafImportTest` passed, 1/1. |
| Forced rebuild of the reader and the tests | `touch src/core/AafImportRead.cpp tests/src/core/AafImportTest.cpp; cmake --build build --target zene AafImportTest AafImportMenuTest VideoTrackTest -j"$(nproc)" > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/build-9.log 2>&1; echo "EXIT=$?"` | 0 | `AafImportRead.cpp.o` rebuilt, `zene` and the three test targets relinked. |
| **GREEN** on the forced rebuild | `ctest -R "^AafImportTest$" --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/test-12.log 2>&1; echo "EXIT=$?"` | **0** | `AafImportTest` passed, 1/1. |
| Affected family (AAF + video) | `ctest -R "Aaf|VideoTrack" --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/test-14.log 2>&1; echo "EXIT=$?"` | **0** | 3/3 passed: `AafImportTest` (#296), `VideoTrackTest` (#297), `AafImportMenuTest` (#370). |
| Affected family, repeated on the final build | `ctest -R "Aaf|VideoTrack" --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/test-15.log 2>&1; echo "EXIT=$?"` | **0** | 3/3 passed. |
| The test binary on its own (case count) | `cd build/tests && QT_QPA_PLATFORM=offscreen ./AafImportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/test-17-aaf-direct.log 2>&1; echo "EXIT=$?"` | 0 | `Totals: 17 passed, 0 failed, 0 skipped, 0 blacklisted` - the new case `theVideoOnlyCompositionUsesTheDefaultSampleRate` is among them. |
| All gates (feature check) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-546-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/gate-1.log 2>&1; echo "EXIT=$?"` | 0 | `RESULT: PASS` - fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS (the two ratchets report "no new item in your files"). |
| All gates, on the committed tree | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-546-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/gate-4.log 2>&1; echo "EXIT=$?"` | 0 | `RESULT: PASS`, 3 changed files, the same eight PASS rows. |
| Rebuild for this report round (the product code is unchanged since the fix commit) | `cmake --build build --target zene AafImportTest AafImportMenuTest VideoTrackTest -j"$(nproc)" > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/build-12.log 2>&1; echo "EXIT=$?"` | 0 | Everything already built. |
| Targeted test for this report round | `ctest -R "^AafImportTest$" --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/test-18.log 2>&1; echo "EXIT=$?"` | 0 | `AafImportTest` passed, 1/1. |
| A/B: rebuild the PRISTINE reader (the product change reverted by hand, for the A/B only) | `git checkout HEAD~1 -- src/core/AafImportRead.cpp; cmake --build build --target zene AafImportMenuTest -j"$(nproc)" > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/build-13-pristine.log 2>&1; echo "EXIT=$?"` | 0 | The reader is back to the unguarded dereference (`src/core/AafImportRead.cpp:315`). |
| A/B: the GUI test on the PRISTINE tree, five runs | `cd build/tests && for i in 1 2 3 4 5; do QT_QPA_PLATFORM=offscreen timeout 60 ./AafImportMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/ab-pristine-$i.log 2>&1; echo "EXIT=$?"; done` | 124, 124, 124, 124, 124 | Five of five runs hung (124 = the 60 s timeout), with the product change reverted. |
| A/B: the GUI test with the fix, five runs | `cd build/tests && for i in 1 2 3 4 5; do QT_QPA_PLATFORM=offscreen timeout 60 ./AafImportMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/ab-fix-$i.log 2>&1; echo "EXIT=$?"; done` | 0, 124, 124, 124, 0 | Two of five runs hung with the fix in place, three passed. |
| Restore the committed reader after the A/B | `git checkout HEAD -- src/core/AafImportRead.cpp; echo "EXIT=$?"` | 0 | The working tree matches the commit again (`git status --porcelain` lists only the report). |
| Rebuild after the A/B | `cmake --build build --target zene AafImportTest AafImportMenuTest VideoTrackTest -j"$(nproc)" > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/build-14.log 2>&1; echo "EXIT=$?"` | 0 | `AafImportRead.cpp.o`, `zene` and the three test targets rebuilt. |
| Targeted test after the A/B | `ctest -R "^AafImportTest$" --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/test-22.log 2>&1; echo "EXIT=$?"` | 0 | `AafImportTest` passed, 1/1. |

### The direct ratchet gates: red on the train, nothing in this branch's files

Run on their own, the two ratchet gates exit 1. That is the state of the train,
not of this change: every `REGRESSION:` item is in a file this branch does not
touch, and `feature_check.py` compares the same gate output with the base
commit `ded355440` and reports "no new item in your files" for both rows.

`bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/gate-2.log 2>&1; echo "EXIT=$?"` - EXIT=1, six items:

```
REGRESSION: new function over target: dotCentreAlong@76-120@tests/src/gui/XyPadPairTest.cpp (CCN 16)
REGRESSION: new function over target: lmms::interchange::readdetail::parseNotes@94-151@src/core/DawProjectReadTracks.cpp (CCN 13)
REGRESSION: new function over target: lmms::adjust@215-249@src/core/ControlCommandsMixerTools.cpp (CCN 12)
REGRESSION: new function over target: lmms::setStrip@250-274@src/core/ControlCommandsMixerStrip.cpp (CCN 11)
REGRESSION: new function over target: lmms::control::rotateProjectRevision@151-222@src/core/ProjectRevisions.cpp (CCN 11)
REGRESSION: new function over target: lmms::importedOverrides@168-205@src/gui/ControlCommandsShortcuts.cpp (CCN 11)
```

`bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/gate-3.log 2>&1; echo "EXIT=$?"` - EXIT=1, fourteen items:

```
REGRESSION: new file over 500 lines: src/core/SmfInterchangeReader.cpp (504)
REGRESSION: new file over 500 lines: src/core/TakeLane.cpp (520)
REGRESSION: new file over 500 lines: tests/src/core/CollabTransportTest.cpp (541)
REGRESSION: new file over 500 lines: tests/src/core/ControlAutomationScriptTest.cpp (521)
REGRESSION: new file over 500 lines: tests/src/core/ControlDeviceCatalogueTest.cpp (567)
REGRESSION: new file over 500 lines: tests/src/core/ControlEditCommandsTest.cpp (511)
REGRESSION: new file over 500 lines: tests/src/core/CustomAccessTest.cpp (556)
REGRESSION: new file over 500 lines: tests/src/core/DeviceDspTest.cpp (752)
REGRESSION: new file over 500 lines: tests/src/core/HardwareIoTest.cpp (556)
REGRESSION: new file over 500 lines: tests/src/core/NotePlaybackTest.cpp (624)
REGRESSION: new file over 500 lines: tests/src/core/RackMacrosTest.cpp (535)
REGRESSION: new file over 500 lines: tests/src/core/RevisionTimelineTest.cpp (600)
REGRESSION: new file over 500 lines: tests/src/core/VideoTrackTest.cpp (589)
REGRESSION: new file over 500 lines: tests/src/gui/ArrangerTimelineTest.cpp (689)
```

Neither list names `src/core/AafImportRead.cpp` or
`tests/src/core/AafImportTest.cpp`; `readComposition` stays at CCN 7 (target
10) and the two files this branch touches are below the 500-line limit.

### The AafImportMenuTest timeout is a pre-existing flake, not this change

`AafImportMenuTest::aCancelledDialogRunsNothingAndAChosenFileIsImported()`
hangs in the offscreen GUI: the QTest watchdog fires after 300 s with
`QFATAL: Test function timed out`
(`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs/test-19.log`,
`.../logs/test-21.log`, `.../logs/test-9.log`), while other runs of the *same*
binary pass in about 3 s
(`.../logs/test-20-menu.log`, `.../logs/test-13-menu-1.log`,
`.../logs/test-13-menu-2.log`, `.../logs/test-11.log`,
`.../logs/test-10-menu-direct.log`).

The A/B settles the cause. With the product change reverted by hand and the
reader rebuilt
(`git checkout HEAD~1 -- src/core/AafImportRead.cpp`, then
`cmake --build build --target zene AafImportMenuTest`, EXIT=0), the same test
hung in **five of five** runs
(`.../logs/ab-pristine-1.log` ... `.../logs/ab-pristine-5.log`, exit 124 from
`timeout 60`). With the fix in place it hung in three of five runs and passed
in two (`.../logs/ab-fix-1.log` ... `.../logs/ab-fix-5.log`: 0, 124, 124, 124,
0). The hang is therefore pre-existing on this line and is not made worse by
the fix; it cannot be caused by it either, because the test never reaches the
reader - it cancels the dialog and imports a file that is not an AAF, which the
command refuses before any composition is walked
(`src/core/ControlCommandsAafImport.cpp:153` calls `readAaf()`, which returns
before `readComposition()` when the file does not load).

The family runs of record are `.../logs/test-14.log` and `.../logs/test-15.log`
(3/3 passed each, with the fix in place); in `.../logs/test-19.log` and
`.../logs/test-21.log` the two other tests of the family passed
(`AafImportTest`, `VideoTrackTest`) and only `AafImportMenuTest` hit this
pre-existing hang.

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with
  it (exit codes).** RED on the unfixed code:
  `ctest -R "^AafImportTest$" --output-on-failure` EXIT=8 (SIGSEGV in the new
  test, `logs/test-2.log`). RED again after reversing the product patch
  (`git apply -R .../logs/fix.patch`, EXIT=0): the same command EXIT=8
  (`logs/test-6.log`). GREEN with the patch: the same command EXIT=0
  (`logs/test-3.log`, `logs/test-8.log`, `logs/test-12.log`, `logs/test-18.log`,
  `logs/test-22.log`).
- **The test is deterministic: no wall-clock timing, sleeps or races (name the
  seam or injected values it uses).** The test injects the whole LibAAF state
  by hand - `AAF_Iface aafi{}` with `Audio == nullptr` (null-initialised, left
  untouched), `aafd` pointing at a zero-initialised `AAF_Data`, and one
  `aafiVideoTrack` behind `aafi::Video` - and calls the reader's
  `readComposition()` on it. No clock, no timer, no sleep, no file and no
  network: the outcome depends only on the injected pointers, so the same
  input always gives the same result.
- **The fix is at the cause and complete: every caller and sibling of what you
  changed was checked and stays consistent.** In the product tree (`src/`,
  `include/`, `plugins/`) `AAF_Iface` is used only in
  `src/core/AafImportRead.cpp` (lines 108, 312 and 342); the only other use in
  the whole repository is the regression test's own `AAF_Iface aafi{}` at
  `tests/src/core/AafImportTest.cpp:379`. `readComposition()` has exactly one
  caller, `readAaf()` (`src/core/AafImportRead.cpp:382`), which is the only
  public entry point (`include/AafImport.h:288`) and is used by
  `src/core/ControlCommandsAafImport.cpp:153`. All three places that touch the
  audio aggregate are now inside the one guard: the sample rate
  (`src/core/AafImportRead.cpp:318-320`), `readMedia()` (`:323`, whose
  `AAFI_foreachAudioEssenceFile` dereferences `aafi->Audio`) and the audio
  track loop (`:325`, whose `AAFI_foreachAudioTrack` does the same). The
  siblings that were already safe stay as they were: the markers
  (`AAFI_foreachMarker` walks `aafi->Markers`, null-safe) and the video
  (`if (aafi->Video != nullptr)`, `src/core/AafImportRead.cpp:336`).
- **No collateral damage: old projects and saved data still load as before, and
  any limit or check you added does not hit other callers that legitimately
  exceed it.** The change only decides what happens when the audio aggregate is
  absent. For every AAF that has audio - including the `PR_WAV_Internal.aaf`
  fixture the import tests read - `aafi->Audio` is not null, so the sample
  rate, the media list, the tracks and the edit rate are computed exactly as
  before; the guard adds no limit and no new field. Saved data is untouched:
  the reader produces the same `AafModel` for audio-bearing files, and a
  video-only file now yields an empty audio model instead of a crash. The
  import stays one undo step (the builder is not touched) and no command was
  added. The one test of the family that misbehaves on this line,
  `AafImportMenuTest`, was measured with and without the product change (five
  runs each): it hung in 5/5 runs with the change reverted and in 3/5 runs
  with it, and it never reaches the reader - so it is a pre-existing flake of
  that GUI test, not damage from this fix (see the A/B rows above).
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or
  the intended contract that shows it.** Not applicable - the verdict is
  FIXED. The bug was live in this tree: `src/core/AafImportRead.cpp:315`
  dereferenced `aafi->Audio` while `:329` checked `aafi->Video`.

Nothing here runs on the audio thread: the AAF reader is a file-import path
that runs on the GUI thread when a command is invoked, and the change adds no
allocation, no lock, no logging and no signal.

## Files committed

- `src/core/AafImportRead.cpp` - guard the audio aggregate in
  `readComposition()`.
- `tests/src/core/AafImportTest.cpp` - add the video-only regression test (no
  second inclusion of the reader, so it merges with cluster new-p8-259).
- `docs/qwen-fixes-2026-10/cluster-new-p9-546-port.md` - this verification
  report.

No new file is added to the product or to the test tree, so no scope manifest
changes: `src/core/AafImportRead.cpp` and `tests/src/core/AafImportTest.cpp`
are already registered in `tests/fork-sources.txt` (lines 1249 and 2191), and
`docs/` is not scanned by the fork-sources gate.
