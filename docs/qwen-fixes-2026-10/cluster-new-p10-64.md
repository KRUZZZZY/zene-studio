Verdict: FIXED

new-p10-64: Flatten accepts tracks that may not have renderable audio

## Evidence

The finding is real. `track.flatten` (and `render.queue_bounce` with `operation: flatten`, which shares the same
semantics) rendered and replaced any track that passed the bounce preflight. The preflight only checks that the track
has at least one clip, and an automation lane holds automation clips, so it passes:

- `src/core/BounceInPlace.cpp:184-200` (`validateRequest`): the only track-content refusal is
  `numOfClips() > 0` ("the track has no clips"). An automation track with an automation clip passes it.
- `src/core/BounceInPlace.cpp:59-69`: the renderer's own contract is that only types below `Track::Type::Automation`
  carry audio (`kFirstAutomationType`, `carriesAudio`). `include/Track.h:92-110`: `Automation`, `HiddenAutomation`
  and `Folder` come after the five audio types.
- `src/core/BounceInPlace.cpp:82-92` (`muteTrackElement`): the target is forced unmuted, and the comment says
  automation tracks are "LEFT ALONE" because they "carry no audio". So an automation target is rendered with no
  source of sound, and the result is silence. The flatten then removes the automation track and replaces it with a
  silent audio track; the undo restores it, but the commit is what loses the control data.
- Folders own no clips (`include/TrackFolder.h:125` overrides `createClip`; `src/core/Track.cpp:184` documents a track
  class that owns no clips), so they were already refused by the no-clips check; the new check refuses them by type
  first, so the reason is stated correctly.

Not verified: the silent output itself. I did not run the real renderer on an automation lane; the claim rests on the
renderer code above. The test below pins the refusal contract, not a measured silence.

## Fix

- `src/core/ControlBounceSupport.cpp:120-127` and `include/ControlBounceSupport.h:54`: `flattenRefusal(const Track*)`
  returns the refusal text for any track at or above `Track::Type::Automation` (the same test as `carriesAudio`), and
  an empty string otherwise. It is shared by both flatten entry points.
- `src/core/ControlCommandsTrackFlatten.cpp:82-92` (`resolveFlattenSource`) and `:97`: `track.flatten` refuses before
  any structural change or render. The resolver replaces the bare `resolveTrack` call so `flatten()` keeps its
  complexity within the gate.
- `src/core/ControlCommandsRenderBounce.cpp:424-430` (`captureOperationState`): the queued flatten refuses at submit,
  before a job exists, with the same text.

Freeze and `bounce.land` on an automation track are not changed. They are separate operations, and the task
scope was this finding. Their behaviour on automation tracks is unverified.

## Regression tests (fail without the fix, pass with it)

- `tests/src/core/ControlBounceRenderTest.cpp:401` `flattenRefusesATrackThatCarriesNoAudio`: an automation lane with a
  clip and a folder; both refused with `Refused` and a message containing "no audio"; track count unchanged; lane
  still in the song with its clip; no output file.
- `tests/src/core/ControlBounceRenderTest.cpp:423` `queuedFlattenRefusesATrackThatCarriesNoAudio`: the queued path
  refused at submit; track count unchanged.

The tests are deterministic: no timing, no sleeps, no race. They use the existing stand-in render child.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-64.r1/logs/`.

Build environment note: the first two builds failed with exit 2 because ccache could not write its temp file outside
the write-jail (`build-red-1.log`, `build-red-2.log`; `ccache: error: failed to create temporary file ... Permission
denied`). The third run used `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp` and every later build used the
same variable. No cache setting was changed.

| Step | Command (abridged) | EXIT | Log |
| --- | --- | --- | --- |
| RED build | `cmake --build build --target ControlBounceRenderTest ControlRenderJobTest -j4` (ccache temp in TMPDIR) | 0 | build-red-3.log |
| RED run | `cd build/tests && ctest -R "^(ControlBounceRenderTest\|ControlRenderJobTest)$" --output-on-failure` | 8 | test-red-1.log |
| RED evidence | `FAIL! ControlBounceRenderTest::flattenRefusesATrackThatCarriesNoAudio()`, Actual `flat.errorKind` 0, Expected Refused (5), `ControlBounceRenderTest.cpp:413` | | test-red-1.log |
| RED evidence | `FAIL! ControlRenderJobTest::queuedFlattenRefusesATrackThatCarriesNoAudio()`, Actual `accepted.errorKind` 0, Expected Refused (5) (this assertion was in ControlRenderJobTest; it has since moved to ControlBounceRenderTest, see the later runs) | | test-red-1.log |
| Proof, save | `git diff -- <4 product files> > fix.patch` | 0 | fix.patch |
| Proof, revert | `git apply -R fix.patch` | 0 | |
| Proof, rebuild | `cmake --build build --target zene ControlBounceRenderTest -j4` (fix reverted) | 0 | build-proof-revert-2.log |
| Proof, run (must fail) | `ctest -R "^ControlBounceRenderTest$"` (fix reverted) | 8 | test-proof-revert-2.log |
| Proof, evidence | `FAIL!` on `flattenRefusesATrackThatCarriesNoAudio` and on `queuedFlattenRefusesATrackThatCarriesNoAudio` | | test-proof-revert-2.log |
| Proof, re-apply | `git apply fix.patch` | 0 | |
| Proof, rebuild | `cmake --build build --target zene ControlBounceRenderTest -j4` (fix applied) | 0 | build-proof-fix-2.log |
| Proof, run (must pass) | `ctest -R "^ControlBounceRenderTest$"` (fix applied) | 0 | test-proof-fix-2.log |
| GREEN build | `cmake --build build --target zene BounceInPlaceTest ControlBounceSupportTest ControlBounceRenderTest ControlRenderJobTest FreezeRegionRepairTest ProjectToolsTest RackMacrosTest RenderSourceGuardTest -j4` | 0 | build-final-1.log |
| GREEN family | `ctest -R "^(BounceInPlaceTest\|ControlBounceSupportTest\|ControlBounceRenderTest\|ControlRenderJobTest\|FreezeRegionRepairTest\|ProjectToolsTest\|RackMacrosTest\|RenderSourceGuardTest)$"` | 0 | test-final-1.log |

Family test count: 8 test executables ran and passed (100%): BounceInPlaceTest, ControlBounceSupportTest,
ControlBounceRenderTest, ControlRenderJobTest, RenderSourceGuardTest, FreezeRegionRepairTest, RackMacrosTest,
ProjectToolsTest. ctest reports per executable here; it prints no per-case totals on a pass.

Gates:

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Gates, as the script is written | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | gates-1.log (PermissionError creating `<worktree>-feature-check` next to the worktree, outside the write-jail; no gate ran) |
| Gates, same code, logs in run folder | `python3 -I /home/kruzzzzy/.cache/zene-tmp/new-p10-64-gate-wrapper.py <worktree>` | 1 | gates-2.log (first run: complexity and file-length FAIL, see below) |
| Gates, after fixes | same wrapper | 0 | gates-3.log |

The wrapper loads `feature_check.py` unmodified and calls its own gate lists and `main()` logic, with the log directory
changed. It is not a change to the gate. Final table (gates-3.log):

```
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

The first wrapper run (gates-2.log) failed two ratchets, both in files this change touched. `lmms::flatten` reached
CCN 11 (`ControlCommandsTrackFlatten.cpp`). `ControlRenderJobTest.cpp` reached 508 code lines (gate count), over the
500 limit. Fixes: the refusal moved into `resolveFlattenSource` so `flatten()` keeps its complexity, and the queued-path
test moved from `ControlRenderJobTest.cpp` into `ControlBounceRenderTest.cpp`, which has headroom (about 457 code
lines by my count with the gate's rules; the gate itself passes). The upstream-modifications entry for `ControlCommandsTrackFlatten.cpp` now records this change.

## Files committed

- `src/core/ControlCommandsTrackFlatten.cpp`
- `src/core/ControlCommandsRenderBounce.cpp`
- `src/core/ControlBounceSupport.cpp`
- `include/ControlBounceSupport.h`
- `tests/src/core/ControlBounceRenderTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-64.md`

Not committed: `fix.patch` and the gate and build logs (in the run folder, outside the worktree).

## Self-check

Pre-submit check, run before this commit: `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p10-64`
(log `submit-1.log`, EXIT 1). Its rows and how each is answered:

- `commits on the branch` and `working tree clean`: FAIL before this commit; both are resolved by committing the seven
  files listed above. Re-run after committing (log in the run folder).
- `tests added or changed`: FAIL before this commit (the tests were uncommitted); resolved by the same commit.
- `report`: PASS.
- `gates (feature_check.py)`: FAIL in submit_check, but the gates themselves pass. submit_check runs
  `feature_check.py` directly, and that script creates its log folder next to the worktree
  (`<worktree>-feature-check`), which the write-jail refuses (`gates-1.log`, PermissionError, EXIT 1). Under the
  same gate code with the logs in the run folder (`python3 -I .../new-p10-64-gate-wrapper.py <worktree>`), all eight
  rows pass: `gates-4.log`, EXIT 0. This is an environment limit, not a gate result; the owner can run the gate
  directly outside the jail to confirm.

Judgement items:

- **Regression fails with the change reverted, passes with it.** Yes. Reverted: `git apply -R fix.patch`, rebuild,
  `ctest -R "^ControlBounceRenderTest$"` EXIT 8, both new cases FAIL (`test-proof-revert-2.log`). Re-applied,
  rebuilt, EXIT 0 (`test-proof-fix-2.log`). The earlier proof with the first patch version (before the
  `resolveFlattenSource` refactor and the test move) also gave EXIT 8 then the same pass; its logs are `test-proof-revert-1.log`
  and `test-proof-fix-1.log`.
- **Deterministic.** Yes. No timing, sleeps or races. The tests build their tracks in the song, and the refusal is a
  pure function of track type and clip count. The render child (`fakeRender`) is not reached on either path, so
  the stand-in render's timing is irrelevant.
- **Fix at the cause, every caller and sibling checked.** Both entry points that reach the flatten renderer are
  covered: `track.flatten` (`ControlCommandsTrackFlatten.cpp:82-97`) and `render.queue_bounce` with `flatten`
  (`ControlCommandsRenderBounce.cpp:424-430`, which runs at submit before a job exists). Both call the one predicate
  `flattenRefusal` (`ControlBounceSupport.cpp:120`), which is the renderer's own `carriesAudio` rule. The other
  callers of the bounce renderer are unchanged on purpose: `bounce.land` (`ControlCommandsBounceLand.cpp`),
  `freeze` (`ControlCommandsFreeze.cpp`) and `range.bounce` (`ControlCommandsRangeBounce.cpp`) still accept any track
  with a clip. Their behaviour on an automation track is unverified; it is outside this finding.
- **No collateral damage.** The predicate only refuses types at or above `Track::Type::Automation` (automation,
  hidden automation, folder) and only in the flatten operation. Instrument, pattern, sample, event and video tracks
  flatten as before: the existing `flattenReplacesTheTrackWithItsRenderAndUndoRestoresIt` and the queued flatten in
  `ControlRenderJobTest` still pass (`test-final-1.log`). Saved projects are not touched: nothing is serialised by
  this change, and no command or its description changed, so the `ControlCommandsSnapshot` canary is unaffected. The
  refusal is typed (`Refused`) and leaves the document and the queue as they were.
- **Realtime and undo.** Nothing on the audio thread changes: the check runs in the control-command path before the
  render. Undo is unchanged: a refused flatten records no undo step.
- **Registration.** No new files. `ControlBounceSupport.cpp` and `ControlCommandsRenderBounce.cpp` are already in
  `tests/fork-sources.txt`; `ControlCommandsTrackFlatten.cpp` is in `tests/upstream-modifications.txt`, and the entry
  now records this change. The two tests live in existing, already-registered test files.
