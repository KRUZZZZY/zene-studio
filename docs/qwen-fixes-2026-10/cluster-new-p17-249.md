FALSE-POSITIVE

# Cluster new-p17-249 - consolidation does not render the sample track's fader and devices, by design

**Verdict:** FALSE-POSITIVE. The finding reads `src/core/ClipConsolidate.cpp:66` (line 66 in the
version it saw; `src/core/ClipConsolidate.cpp:88-99` here): `mixClip` renders each clip through its
own `SamplePlayHandle` into a local block and sums it, and does not pass that sum through the
sample track's `AudioBusHandle`, where the fader, pan and device chain live. That is the documented
contract, not a defect. Consolidation puts the replacement clip back on the SAME track
(`src/core/ControlCommandsConsolidate.cpp:191`, reached from `:231-242`), so the track's fader, pan
and devices apply - once - when the consolidated clip plays. `include/ClipConsolidate.h:40-50`
states it in those words: the render is the clips' own audio "and not the track's output: a bounce
carries the track's devices, fader and pan, and those would then apply twice". The command
description repeats it (`src/core/ControlCommandsConsolidate.cpp:269-272`), and so does the manual
(`data/manual/index.html:185-186`). The sibling feature that DOES render the track's own output is
bounce-in-place / freeze (`include/BounceInPlace.h:46-54`). Baking the fader into the consolidated
file - the finding's suggested fix - would make playback apply it twice.

**Product change:** none. The only change in this commit is a test that pins the contract, so a
future "fix" that bakes the fader into the file fails a test instead of double-applying it.

## Evidence (file:line, this tree)

| # | Claim | Where |
|---|---|---|
| E1 | What the finding points at: each clip is rendered through its own handle into a block and summed; no track bus is involved | `src/core/ClipConsolidate.cpp:88-99` (`handle.play(...)` at `:92`, sums at `:96-97`) |
| E2 | The contract: consolidation returns to the SAME track, so it is the clips' own audio, NOT the track's output; a bounce's devices/fader/pan would then apply twice | `include/ClipConsolidate.h:40-50` |
| E3 | The command says the same to agents: "the clips' own audio, not the track's output (the track's devices, fader and pan still apply once, on playback)" | `src/core/ControlCommandsConsolidate.cpp:269-272` |
| E4 | The replacement is created on the same track, after the render | `src/core/ControlCommandsConsolidate.cpp:191` (`track->createClip`), called at `:242` after the render at `:231-232` |
| E5 | At playback the clip's handle audio is summed by the track's bus, which applies the fader and pan and then the track's effect chain, before merging into its mixer channel | `include/AudioBusHandle.h:50-59` (bus contract), `src/core/AudioBusHandle.cpp:245` (volume/pan applied to the summed handles), `:199-215` (`applyVolumeAndPanning`), `:184-192` (the track's effect chain) |
| E6 | The sample track owns exactly such a bus, built on its volume, pan and mute models, and schedules its clips into it | `src/tracks/SampleTrack.cpp:61` (`m_audioBusHandle(tr("Sample track"), true, &m_volumeModel, &m_panningModel, &m_mutedModel)`), `:222` (`new SamplePlayHandle(st, scheduled.window)`), `:229` (`addPlayHandle`) |
| E7 | The sibling that renders the track's own contribution (devices, fader, pan, sends) is bounce-in-place / freeze | `include/BounceInPlace.h:46-54` |
| E8 | The manual documents Consolidate as the clips' source audio, not the track's output | `data/manual/index.html:184-186` |
| E9 | `range.consolidate` funnels every covered sample track into `clip.consolidate`, so it shares E2/E3 | `src/core/ControlCommandsRange.cpp:363-379` |
| E10 | The other caller of `ClipConsolidate::render` (`browser.save_clip` via `writeClip`) writes a clip's source audio to the library, where a track's fader was never part of the clip | `src/core/ControlCommandsLibraryClips.cpp:179-187` |

The finding's scenario ("a sample track has non-unity volume or an effect such as EQ") therefore
does change the file (the file is the clips' audio) but not what the track plays: the consolidated
clip goes through the same fader and effect chain the source clips went through. `include/ClipConsolidate.h:44-47`
is the intended contract the brief asks for; it predates this report (the header has one commit,
`817bdcfb64`, which introduced `clip.consolidate`).

## Regression test (contract pin)

New slot `theTracksFaderIsNotBakedInAndTheReplacementStaysOnTheTrack` in the existing, registered
fork-new test file `tests/src/core/ClipConsolidateTest.cpp` (no new file, no registration change;
the file is already in `tests/fork-sources.txt`). It sets the sample track's fader to 25 %, renders
a 0.5-level clip through `clip.consolidate`, and asserts:

- the consolidated file's level is still 0.5, not 0.125 - the fader was not baked in;
- the result names the same track and the only clip on that track is the replacement (`:226-228`),
  so the fader still applies to it on playback;
- the fader still reads 25 % after the command (`:235`), i.e. the command leaves the track chain
  where it was.

The test matrix:

| Path | What is covered | Result |
|---|---|---|
| Normal path | fader 25 %, clip level 0.5, no composite: file level 0.5, replacement on the same track, fader still 25 % | PASS (`test-2.log`, `test-9-reverted.log`) |
| Failure path (the finding's fix) | A temporary, uncommitted mutation bakes the track fader into the mix exactly as the finding asks (multiplies each frame by `volumeModel()->value() * 0.01f`) | FAIL, `TEST_EXIT=1`, `ClipConsolidateTest.cpp(231)` (`test-8-mutation.log`) |
| Exhaustion | Not touched: no allocation, limit or refusal is added by this cluster. The render's existing size limit has its own slots (`aRangeTooLargeToRenderIsRefusedBeforeItIsAllocated`, `theWholeSongRangeOfACommandIsRefusedAndChangesNothing`), still green in `test-5-clipconsolidate.log` | PASS |
| Reconfiguration | After consolidation the fader model is unchanged and the replacement clip is on the same track, so the playback chain still applies it once | PASS (assertions in the slot) |
| Audio thread | No product code changed. `mixClip` is the offline render behind the control command (control-command thread), not an audio-thread path; the slot runs it there | n/a |
| Supported platforms | No platform code, no absolute-path assumption; the slot uses the existing test target, `QTemporaryDir` and Qt Test only | n/a |

## Commands and EXIT codes

All commands ran in the worktree
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p17-249`; the log
folder is `LOG=/home/kruzzzzy/.config/kads/zene/state/runs/new-p17-249.r1/logs`.

```sh
cmake --build build --target ClipConsolidateTest -j4 > "$LOG/build-1.log" 2>&1; echo EXIT=$? >> "$LOG/build-1.log"
cd build/tests && ctest -R '^ClipConsolidateTest$' --output-on-failure > "$LOG/test-1.log" 2>&1; echo EXIT=$? >> "$LOG/test-1.log"
cd build/tests && ./ClipConsolidateTest theTracksFaderIsNotBakedInAndTheReplacementStaysOnTheTrack -v1 > "$LOG/test-2.log" 2>&1; echo EXIT=$? >> "$LOG/test-2.log"
git apply "$LOG/mutation.patch" > "$LOG/mutation-apply-2.log" 2>&1; echo APPLY_EXIT=$? | tee -a "$LOG/mutation-apply-2.log"
cmake --build build --target ClipConsolidateTest -j4 > "$LOG/build-5-mutation.log" 2>&1; echo BUILD_EXIT=$? >> "$LOG/build-5-mutation.log"
cd build/tests && ./ClipConsolidateTest theTracksFaderIsNotBakedInAndTheReplacementStaysOnTheTrack > "$LOG/test-8-mutation.log" 2>&1; echo TEST_EXIT=$? >> "$LOG/test-8-mutation.log"
git apply -R "$LOG/mutation.patch" > "$LOG/mutation-revert-2.log" 2>&1; echo APPLY_EXIT=$? | tee -a "$LOG/mutation-revert-2.log"
cmake --build build --target ClipConsolidateTest -j4 > "$LOG/build-6-reverted.log" 2>&1; echo BUILD_EXIT=$? >> "$LOG/build-6-reverted.log"
cd build/tests && ./ClipConsolidateTest theTracksFaderIsNotBakedInAndTheReplacementStaysOnTheTrack > "$LOG/test-9-reverted.log" 2>&1; echo TEST_EXIT=$? >> "$LOG/test-9-reverted.log"
cmake --build build --target RangeEditsTest LibraryBrowserTest -j4 > "$LOG/build-4-siblings.log" 2>&1; echo BUILD_EXIT=$? >> "$LOG/build-4-siblings.log"
cd build/tests && ctest -R '^ClipConsolidateTest$' --output-on-failure > "$LOG/test-5-clipconsolidate.log" 2>&1; echo TEST_EXIT=$? >> "$LOG/test-5-clipconsolidate.log"
cd build/tests && ctest -R '^RangeEditsTest$' --output-on-failure > "$LOG/test-6-range.log" 2>&1; echo TEST_EXIT=$? >> "$LOG/test-6-range.log"
cd build/tests && ctest -R '^LibraryBrowserTest$' --output-on-failure > "$LOG/test-7-library.log" 2>&1; echo TEST_EXIT=$? >> "$LOG/test-7-library.log"
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py "$PWD" > "$LOG/gates-1.log" 2>&1; echo EXIT=$? >> "$LOG/gates-1.log"
```

`mutation.patch` is `git diff -- src/core/ClipConsolidate.cpp` of the uncommitted mutation; it adds
`#include "SampleTrack.h"` and multiplies every mixed frame by the track's fader. The committed
tree carries no product change: `git diff --name-only -- src include plugins` prints nothing.

| Step | Log | EXIT |
|---|---|---|
| Build `ClipConsolidateTest` (with the new slot) | `build-1.log` | `EXIT=0` |
| Full `ClipConsolidateTest` | `test-1.log` | `EXIT=0` (1/1 passed) |
| The new slot alone, `-v1` | `test-2.log` | `EXIT=0` (`PASS`, `Totals: 3 passed`) |
| Apply the mutation | `mutation-apply-2.log` | `APPLY_EXIT=0` |
| Build with the mutation | `build-5-mutation.log` | `BUILD_EXIT=0` |
| New slot with the fader baked in | `test-8-mutation.log` | `TEST_EXIT=1` - `FAIL! ... 'std::fabs(levelAt(after[0], pad, length - pad) - 0.5) < 1e-4' returned FALSE`, `Loc: [../../tests/src/core/ClipConsolidateTest.cpp(231)]` |
| Reverse the mutation | `mutation-revert-2.log` | `APPLY_EXIT=0` |
| Rebuild without it | `build-6-reverted.log` | `BUILD_EXIT=0` |
| New slot on the reverted tree | `test-9-reverted.log` | `TEST_EXIT=0` (`Totals: 3 passed`) |
| Build sibling targets | `build-4-siblings.log` | `BUILD_EXIT=0` |
| `ClipConsolidateTest` family | `test-5-clipconsolidate.log` | `TEST_EXIT=0` (1/1 passed) |
| `RangeEditsTest` (`range.consolidate` path) | `test-6-range.log` | `TEST_EXIT=0` (1/1 passed) |
| `LibraryBrowserTest` (`browser.save_clip` path) | `test-7-library.log` | `TEST_EXIT=0` (1/1 passed) |
| Gates | `gates-1.log` | `EXIT=0`, `RESULT: PASS` (8 PASS rows, 0 FAIL) |

Gate table from `gates-1.log`:

```text
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Self-check

1. **"The regression test FAILS with your product change reverted and PASSES with it (exit codes)."**
   There is no product change: `git diff --name-only -- src include plugins` prints nothing, so the
   step-4b `git diff -- <product files>` has no content and there is nothing to reverse. The
   equivalent experiment was run with the finding's fix as an uncommitted mutation: with the track
   fader baked into `mixClip`, the new slot fails (`test-8-mutation.log`, `TEST_EXIT=1`,
   `ClipConsolidateTest.cpp(231)`); with the mutation reversed it passes (`test-9-reverted.log`,
   `TEST_EXIT=0`). A test with no fix that could not fail would be a wrong test; this one fails
   under the behaviour the finding asks for.
2. **"The test is deterministic: no wall-clock timing, sleeps or races."** No clock, sleep, thread
   or file-order dependence. The inputs are injected values (fader 25.0, clip level 0.5f), the
   output is read back through the same sample loader the app uses from a `QTemporaryDir` WAV, and
   the command is driven once through `ControlRegistry`. The assertions are exact for the silence
   and 1e-4 on the level.
3. **"The fix is at the cause and complete: every caller and sibling checked."** No fix was made.
   `ClipConsolidate::render` has two callers - `clip.consolidate`
   (`src/core/ControlCommandsConsolidate.cpp:231`) and `browser.save_clip` via `writeClip`
   (`src/core/ControlCommandsLibraryClips.cpp:183`) - and both go through the one `mixClip`; both
   families pass (`test-5-clipconsolidate.log`, `test-7-library.log`). Sibling entry point
   `range.consolidate` funnels to `clip.consolidate` (`src/core/ControlCommandsRange.cpp:363-379`)
   and its family passes (`test-6-range.log`). The track-output render is a different feature
   (`include/BounceInPlace.h:46-54`) and was not changed.
4. **"No collateral damage."** Product code, saved data and the project format are untouched; no
   limit or check was added, so no caller can newly exceed one. The test file is fork-new and
   already registered, so `tests/fork-sources.txt` / `tests/upstream-modifications.txt` need no
   change, and the whole `ClipConsolidateTest` binary passes (1/1).
5. **"FALSE-POSITIVE: the code (file:line) or the intended contract that shows it."** E2
   (`include/ClipConsolidate.h:40-50`) is the contract; E3 (`src/core/ControlCommandsConsolidate.cpp:269-272`)
   repeats it on the agent surface; E8 (`data/manual/index.html:184-186`) on the user surface; E4
   (`src/core/ControlCommandsConsolidate.cpp:191`, `:242`) shows the clip goes back on the same
   track; E5/E6 (`src/core/AudioBusHandle.cpp:184-215`, `src/tracks/SampleTrack.cpp:61`, `:222`,
   `:229`) show the fader, pan and devices apply there on playback; E7
   (`include/BounceInPlace.h:46-54`) is the sibling that renders the track's own output. The
   stated scenario (non-unity fader / EQ) changes the file, not what the track plays.

## Other findings (not fixed here)

- **Clip effects are not rendered by consolidation.** A clip's own effect chain is not run by
  `ClipConsolidate::mixClip` (it constructs `SamplePlayHandle handle(clip, window)` without
  `setClipEffects`, `src/core/ClipConsolidate.cpp:80`), and the consolidated clip has none. This is
  a documented known limit of the clip-effects feature (`docs/050-features/Q050-111.md:69`,
  `data/manual/index.html:185-186`), not the track-processing question this cluster asks.
- **`SamplePlayHandle::play` runs the clip's effect chain over the whole caller buffer**
  (`src/core/SamplePlayHandle.cpp:660`), while the clip's own edits and gates run over the frames
  after the start offset (`:652-658`). Not investigated and not changed; it is shared with the
  playback path.
