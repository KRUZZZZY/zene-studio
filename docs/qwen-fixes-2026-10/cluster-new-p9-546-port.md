# Cluster new-p9-546-port

Verdict: **FIXED**

`readComposition()` read the audio aggregate of a composition without checking
it, although the very same function already checks the video aggregate. A
video-only AAF has no audio aggregate at all - LibAAF leaves `AAF_Iface::Audio`
null - so the reader dereferenced a null pointer and the import crashed. The
audio half of the walk is now read only when there is an audio aggregate, and
the model keeps the default sample rate when there is none.

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
a video-only composition - `AAF_Iface::Audio` left null, one `aafiVideoTrack`
behind `aafi::Video` - and drives the reader over it. It checks the default
sample rate, an empty audio model (no tracks, no media) and the video loss
count (1 track, 0 clips).

Without the fix the test dies with SIGSEGV inside `readComposition` (the null
`aafi->Audio`), with the fix it passes.

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

CTest commands were run from `build/tests`; build, patch and gate commands
were run from the worktree root. Every build and test output was saved in the
requested log folder
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-546-port.r1/logs`.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Baseline build (tree before the change) | `cmake --build build --target AafImportTest -j$(nproc) > .../logs/build-1.log 2>&1; echo EXIT=$?` | 0 | Built. |
| Baseline test (tree before the change) | `ctest -R "^AafImportTest$" --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?` | 0 | 1/1 passed. |
| Build the new regression on the unfixed product code | `cmake --build build --target AafImportTest -j$(nproc) > .../logs/build-2.log 2>&1; echo EXIT=$?` | 0 | Built - the test compiles next to the new-p8-259 include, no second inclusion. |
| **RED** on the unfixed product code | `ctest -R "^AafImportTest$" --output-on-failure > .../logs/test-2.log 2>&1; echo EXIT=$?` | **8** | `AafImportTest` (test #296) crashed: `Received signal 11 (SIGSEGV) ... accessing address 0x000000000000000c` in `theVideoOnlyCompositionUsesTheDefaultSampleRate` (the null `aafi->Audio`). |
| Build with the fix | `cmake --build build --target zene AafImportTest -j$(nproc) > .../logs/build-3.log 2>&1; echo EXIT=$?` | 0 | Rebuilt `zene` and the test target. |
| **GREEN** with the fix | `ctest -R "^AafImportTest$" --output-on-failure > .../logs/test-3.log 2>&1; echo EXIT=$?` | **0** | 1/1 passed. |
| Save the product diff | `git diff -- src/core/AafImportRead.cpp > .../logs/fix.patch; echo EXIT=$?` | 0 | 39 lines, product file only (the test is not in the patch). |
| Reverse the product patch | `git apply -R .../logs/fix.patch; echo EXIT=$?` | 0 | The reader is back to the unguarded dereference. |
| Rebuild without the fix | `cmake --build build --target zene AafImportTest -j$(nproc) > .../logs/build-5.log 2>&1; echo EXIT=$?` | 0 | Rebuilt. |
| **RED** after reversing the fix | `ctest -R "^AafImportTest$" --output-on-failure > .../logs/test-6.log 2>&1; echo EXIT=$?` | **8** | Same SIGSEGV in `theVideoOnlyCompositionUsesTheDefaultSampleRate` - the test catches the bug. |
| Reapply the product patch | `git apply .../logs/fix.patch; echo EXIT=$?` | 0 | The guard is back. |
| Rebuild with the fix | `cmake --build build --target zene AafImportTest AafImportMenuTest VideoTrackTest -j$(nproc) > .../logs/build-9.log 2>&1; echo EXIT=$?` | 0 | Rebuilt `AafImportRead.cpp.o`, `zene` and the three test targets. |
| **GREEN** after reapplying the fix | `ctest -R "^AafImportTest$" --output-on-failure > .../logs/test-12.log 2>&1; echo EXIT=$?` | **0** | 1/1 passed. |
| Affected family (AAF + video) | `ctest -R "Aaf|VideoTrack" --output-on-failure > .../logs/test-14.log 2>&1; echo EXIT=$?` | 0 | 3/3 passed: `AafImportTest` (#296), `VideoTrackTest` (#297), `AafImportMenuTest` (#370). |
| All gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gate-1.log 2>&1; echo EXIT=$?` | 0 | `RESULT: PASS` - fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS. |
| Complexity ratchet (unpiped) | `bash tests/complexity-gate.sh --check > .../logs/gate-2.log 2>&1; echo EXIT=$?` | 1 | Red on the train: the `REGRESSION:` items are `tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsMixerStrip.cpp`, `src/core/ProjectRevisions.cpp`, `src/gui/ControlCommandsShortcuts.cpp` - none of them a file this branch touches. `readComposition` stays at CCN 7 (target 10). |
| File-length ratchet (unpiped) | `bash tests/file-length-gate.sh --check > .../logs/gate-3.log 2>&1; echo EXIT=$?` | 1 | Red on the train: `tests/src/core/VideoTrackTest.cpp` (589) and `tests/src/gui/ArrangerTimelineTest.cpp` (689) - neither is a file this branch touches. |

The two ratchets are red on the train on purpose; `feature_check.py` compares
them with the base commit and reports "no new item in your files" for both.

### The AafImportMenuTest timeout is not caused by this change

`AafImportMenuTest` hung twice (`.../logs/test-5.log`, `.../logs/test-9.log`,
`QFATAL: Test function timed out` after 300 s in
`aCancelledDialogRunsNothingAndAChosenFileIsImported`) and passed four times
(`.../logs/test-7.log`, `.../logs/test-10-menu-direct.log`,
`.../logs/test-11.log`, `.../logs/test-13-menu-1.log`,
`.../logs/test-13-menu-2.log`). The same binary failed in one run and passed
in the next, so the hang is a flake of that GUI test's file-dialog timer, not a
regression: the test never reaches the reader - it cancels the dialog and
imports a file that is not an AAF, which the command refuses before any
composition is walked. The final family run (3/3 passed) and the two
`test-13` runs are with the fix in place.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with
  it (exit codes).** RED on the unfixed code: `ctest -R "^AafImportTest$"`
  EXIT=8 (SIGSEGV in the new test, `logs/test-2.log`). RED again after
  reversing the product patch: EXIT=8 (`logs/test-6.log`). GREEN with the
  patch: EXIT=0 (`logs/test-3.log`, `logs/test-12.log`).
- **The test is deterministic: no wall-clock timing, sleeps or races (name the
  seam or injected values it uses).** The test injects the whole LibAAF state
  by hand - `AAF_Iface` with `Audio == nullptr`, `aafd` pointing at a
  zero-initialised `AAF_Data`, and one `aafiVideoTrack` behind `aafi::Video` -
  and calls the reader's `readComposition()` on it. No clock, no timer, no
  sleep, no file and no network: the outcome depends only on the injected
  pointers, so the same input always gives the same result.
- **The fix is at the cause and complete: every caller and sibling of what you
  changed was checked and stays consistent.** The reader is confined to
  `src/core/AafImportRead.cpp` (`AAF_Iface` appears nowhere else in the tree);
  `readComposition()` has exactly one caller, `readAaf()`
  (`src/core/AafImportRead.cpp:382`), which is the only public entry point
  (`include/AafImport.h:288`) and is used by
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
  added.
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
