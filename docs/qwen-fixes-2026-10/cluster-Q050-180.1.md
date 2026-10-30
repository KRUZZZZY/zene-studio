IMPLEMENTED

# Q050-180.1 — Several positioned video clips with correct lane and render behaviour (consolidation completion)

Branch `feat/050-Q050-180.1-oc`, cut from `050/m0-train-18` (`7641e9493`); integrated onto `050/m0-train-20` (`c6ee4fc7bc`) in round 3, and onto `050/m0-train-22` (`4ee5321ffc`) in round 4 (next section). Commits: `deb2171400` (feature + tests),
`20c16c9933` (live snapshot, canary, A16 histogram), `7fed22e3ec` (manual, feature record), and this report. The full
record, with file:line pointers, is `docs/050-features/Q050-180.1.md`.

Starting point: Q050-139's unmerged implementation (`d66c0ffcef` + `ea9ed92301` on `feat/050-Q050-114-oc`) applied
cleanly (the video files had not changed since its parent). On top of it this unit fixed `video.attach`'s undo (it
restored only the first clip), the video window's stale picture on a clip change and its acceptance of a late frame for
a clip no longer at the play position, the lane's tag overflow / per-clip aspect / drag-also-seeks, added
`video.render_frames` (range render), the lane's context menu, View > Add Video Clip..., and the MCP transcript.

Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-180.1.r1/logs/` (round 0) and `.../Q050-180.1.r2/logs/` (`r2/logs/`, round 1), one numbered file per build/test run. All test runs:
`unset LMMS_PLUGIN_DIR; QT_QPA_PLATFORM=offscreen`, from `build/tests`.

## Integration onto `050/m0-train-22` (round 4)

The accepted patch (round 3's `a48d0ffc7a`, base `c6ee4fc7bc`) was three-way applied by the driver onto
`050/m0-train-22` (`4ee5321ffc`) on branch `integration/Q050-180.1-4ee5321ffc`. Every product and test file applied
cleanly. The staged diff of each of the 16 code/test files has the same changed-line count as the accepted patch. The
only exception is `tests/control-video-clips-mcp.py` (below). Four bookkeeping files conflicted:

| file | conflict | resolution |
| --- | --- | --- |
| `README.md` | canary 878/77 (train) vs 874/76 | **881 ids / 77 groups** (train's 878 + `video.clip_add`, `video.clip_remove`, `video.render_frames`); `python3 tests/doc-figures-gate.py` PASS, exit 0 |
| `docs/RELEASE-NOTES-v0.3.0-alpha.md` | A16 block | train's entries kept, one Q050-180.1 line added, `measured: rows=890 true_inverse=436 snapshot=42 irreversible=25 not_mutating=387` (train 887/434/386 + 2 `true_inverse` + 1 `not_mutating`); `ReversibilityContractTest` passes on it |
| `tests/upstream-modifications.txt` | both sides appended rows | every train row kept, plus the patch's `MainWindow.cpp` row |
| `tools/mcp-zene-control/zene_control/commands_snapshot.json` | both regenerated | regenerated live with `snapshot_live.py`: 881 commands / 77 groups, exit 0 (`r4/logs/snapshot-8.log`) |

`tests/all-sources.txt` applied without a conflict. `bash tests/all-sources-reproduce.sh` prints `REPRODUCES`, exit 0.

**The two failures the driver reported:**

- **`ControlVideoClipsMcp`**: the cause is the interpreter, reproduced here (`r4/logs/test-4.log`, exit 8). The shared
  `mcp_stdio_session.server_python()` returns the first interpreter that can `import mcp`. Here that is the system
  Python 3.14 with `mcp` 1.29.1. The bridge then dies in `Server(on_list_tools=...)` (`TypeError`). The train's own
  `ControlMcpGroupCoverage` fails identically on this tree (`r4/logs/test-5.log`, exit 8). Round 3 needed a manual
  `ZENE_CONTROL_PYTHON` override. This round fixes the patch's own test so it needs no override: `bridge_python()` in
  `tests/control-video-clips-mcp.py` goes through the same candidates (`ZENE_CONTROL_PYTHON`, `sys.executable`, the
  Hermes venv), keeps the first whose `mcp.server.lowlevel.Server.__init__` takes `on_list_tools`, and otherwise SKIPs
  (77) like a missing `mcp`. No assertion changed and the shared harness is untouched. Without an override it now
  runs every check and passes (`r4/logs/test-7.log`, exit 0, 17 `ok` + `PASS`), using the Hermes venv (`mcp` 2.0.0).
- **`DeviceDspTest`**: not reproduced. It passes on the integrated tree once its plug-in modules (`insertrecorder`,
  `frequencyshifter`, `console`, its `add_dependencies`) are built (`r4/logs/build-3.log` exit 0; `test-4.log`,
  `test-12.log`, `test-18-restored.log` all Passed). The patch touches no DSP, mixer or plug-in file. The likely cause
  is plug-in modules that were not rebuilt in the driver's tree, but that is not verified.

**Revert proof on the integrated tree** (repeated this round): every `include/` and `src/` hunk reversed
(`git diff HEAD -- include src | git apply -R`), tests kept, `zene` and both test executables rebuilt, exit 0
(`r4/logs/build-13-negctl.log`). `./VideoClipsTest` **exit 10** (10 of 12 fail), `./VideoClipsGuiTest` **exit 3**,
`tests/control-video-clips-mcp.py build/zene` **exit 1** (`unknown tool 'zene_video_clip_add'`)
(`r4/logs/test-14/15/16-negctl-*.log`). Restored with `git checkout -- include src` (worktree equal to the index),
rebuilt, exit 0 (`r4/logs/build-17-restored.log`).

**Focused run on the integrated tip** (`r4/logs/test-18-restored.log`, exit 0, 11/11):
`ctest -R '^(Video[A-Za-z]*Test|ControlVideoClipsMcp|ControlCommandsSnapshot|ReversibilityContractTest|DeviceDspTest|ArrangerOverlaysVideoLaneTest)$' --output-on-failure`.
That covers `ReversibilityContractTest`, `DeviceDspTest`, `VideoTrackTest`, `VideoClipsTest`, `VideoFfmpegTest`,
`VideoTestClipTest`, `VideoAudioNameTest`, `ArrangerOverlaysVideoLaneTest`, `VideoClipsGuiTest`,
`ControlCommandsSnapshot` and `ControlVideoClipsMcp`. The environment is the brief's, plus
`CCACHE_TEMPDIR=/tmp/ccache-q180` because the default ccache temp directory is outside this lane's write-jail. The
new test executables are only in `build/tests`'s Makefile, so they were built with `cmake --build build/tests --target ...`.

## Integration onto `050/m0-train-20` (round 3)

The accepted patch (`ddb7cb8d4`, base `050/m0-train-18` `7641e9493`) was three-way applied onto `050/m0-train-20`
(`c6ee4fc7bc`). Every code file applied cleanly; there are **no semantic changes** from the accepted patch. Five
bookkeeping files conflicted and were resolved as follows:

| file | conflict | resolution |
| --- | --- | --- |
| `tests/upstream-modifications.txt` | both sides appended rows | kept every train row and the patch's `MainWindow.cpp` row |
| `tests/all-sources.txt` | train added `VideoAudioNameTest.cpp` beside the patch's `VideoClipsTest.cpp` | re-derived with the header's recipe; its verify command prints `REPRODUCES` |
| `tools/mcp-zene-control/zene_control/commands_snapshot.json` | both regenerated | regenerated live with `snapshot_live.py` on the merged `zene`: **874 ids / 76 groups** (871 on the train + `video.clip_add`, `video.clip_remove`, `video.render_frames`), exit 0 (`snapshot-3.log`) |
| `README.md` | canary 871 vs 870 | 874 (`tests/doc-figures-gate.py` PASS, exit 0, `gate-10.log`) |
| `docs/RELEASE-NOTES-v0.3.0-alpha.md` | A16 block | train 880/434/379 + 2 `true_inverse` + 1 `not_mutating` = `rows=883 true_inverse=436 snapshot=42 irreversible=25 not_mutating=380`, plus one Q050-180.1 line; `ReversibilityContractTest` passes |

**Why the patch was reported as failing `ControlVideoClipsMcp` on the train:** the cause is the environment, not
this patch or the merge. `mcp_stdio_session.server_python()` takes `sys.executable` (system Python 3.14, `mcp` 1.29.1)
before the Hermes venv (`mcp` 2.0.0), and the bridge calls the 2.x constructor (`Server(on_list_tools=...)`). The
train's own `ControlMcpGroupCoverage` fails identically on the merged tree (`test-5.log`, exit 8,
`TypeError: Server.__init__() got an unexpected keyword argument 'on_list_tools'`). This is the same interpreter note
recorded in the earlier rounds. With the harness's own `ZENE_CONTROL_PYTHON` override set to the Hermes venv, both pass
(`test-6.log`, exit 0, 2/2). `ControlVideoClipsMcp -V` shows all 17 checks `ok` and `PASS` (`test-7.log`, exit 0), so
it ran and did not skip. No test and no harness was changed. A lasting fix is to point the box's runner at an
interpreter with `mcp` 2.x, or to pin `mcp>=2` for the bridge. That is shared tooling outside this unit's files.

Integration evidence (logs in `~/.local/share/zene-bunny-fix/qwen/runs/Q050-180.1.r3/logs/`, environment as below plus
`CCACHE_TEMPDIR=/tmp/ccache-q180`, because the default ccache temp directory is outside this lane's write-jail; `build-1.log` failed on that):

- `cmake --build build --target zene VideoClipsTest VideoClipsGuiTest ReversibilityContractTest -- -j4`: exit 0 (`build-2.log`).
- `ctest -R "^(VideoClipsTest|VideoClipsGuiTest|ControlVideoClipsMcp|ControlCommandsSnapshot|ReversibilityContractTest)$"`
  without the override: exit 8. Four pass; only `ControlVideoClipsMcp` fails, on the interpreter issue above (`test-4.log`).
- With `ZENE_CONTROL_PYTHON=/home/kruzzzzy/.hermes/hermes-agent/venv/bin/python3`: `ControlVideoClipsMcp` and
  `ControlMcpGroupCoverage` exit 0 (`test-6.log`, `test-7.log`).
- Neighbours built (exit 0, `build-8.log`) and run: `VideoTrackTest VideoFfmpegTest VideoTestClipTest VideoAudioNameTest
  ArrangerOverlaysVideoLaneTest`, exit 0, 5/5 (`test-9.log`). The train's new `VideoAudioNameTest` covers the
  `video.extract_audio` default name. The train changed it in `ControlCommandsVideo.cpp` at the same time as this patch,
  and it still passes.
- The revert proofs (each new test fails without the product change) are from the accepted rounds below. The
  integration changed no product or test code, so they were not repeated.

## Review round 1 (REWORK) - what changed

Luna found that the video window identified a frame's source by clip id only; `video.attach` can give the new file
the id of the clip it replaced, so a late frame of the old file could be shown for the new one. Fixed in `108240140a`:
the requested, shown and active frame are keyed by clip id AND file (`src/gui/VideoWindow.cpp:59` `sourceKey`, used by
`noteActiveSource` `:131`, `stale` `:161`, `take` `:168`); a change of either takes the picture down at once and drops
the late answer. The reviewer's regression is kept, extended to the already-shown case:
`VideoClipsTest::aReplacementWithTheSameClipIdDoesNotShowTheOldFrame`.

- Fix reverted (only the `VideoWindow.cpp`/`.h` hunks), rebuilt exit 0 (`r2/logs/build-03-negctl.log`):
  `./VideoClipsTest` **exit 1**, only that case fails at `VideoClipsTest.cpp:479` ("a late frame of the replaced file
  was shown") (`r2/logs/test-04-negctl.log`).
- Restored, rebuilt exit 0 (`r2/logs/build-05-restored.log`); the 12 focused tests **exit 0, 12/12**
  (`r2/logs/test-06.log`, MCP under `ZENE_CONTROL_PYTHON` as below); gates 8/8 PASS (`r2/logs/feature-check-07.log`).

## Acceptance

| # | criterion | test(s) | command | exit |
| --- | --- | --- | --- | --- |
| 1 | Two files at distinct positions/offsets; save and reopen; files, metadata, positions, identities independently addressable | `VideoClipsTest::twoClipsKeepTheirIdsOffsetsAndMediaAcrossSaveAndReopen`; `ControlVideoClipsMcp` (save/new/open readback field for field) | `ctest -R "^(VideoClipsTest\|VideoClipsGuiTest\|ControlVideoClipsMcp\|VideoTrackTest\|ArrangerOverlaysVideoLaneTest)$" --output-on-failure` (`test-40-restored.log`) | 0 |
| 2 | Each clip drawn over its own span; clicking either span seeks; the viewer follows the clip at the playhead without replacing the other | `VideoClipsGuiTest::eachSpanIsDrawnAndAClickSeeksTheWindowToThatClip` (real clicks, real video window); `VideoClipsTest::aJumpIntoTheOtherClipNeverShowsTheFirstClipsFrame`; `VideoClipsTest::aLateFrameOfTheOtherClipIsDropped`; `VideoClipsTest::aReplacementWithTheSameClipIdDoesNotShowTheOldFrame` (round 1) | same run; round 1: `r2/logs/test-06.log` | 0 |
| 3 | Play and render a range spanning both clips; correct frames, project-tempo timing; no stale frame before/after/in a gap | `VideoClipsTest::playbackShowsEachClipAndNothingBeforeAfterOrInTheGap` (real playback via `renderNextPeriod`); `VideoClipsTest::aRangeRenderShowsEachClipAtProjectTempoAndBlackElsewhere` (90 bpm, 250 frames checked against an independent tick→seconds formula and pixel colours); `VideoClipsGuiTest::theContextMenuRendersTheLoopRange`; `ControlVideoClipsMcp` render step | same run | 0 |
| 4 | Creation, independent edits, save, readback through registered commands and MCP; same add/edit through the video lane UI | `ControlVideoClipsMcp` (`tests/control-video-clips-mcp.py`, real MCP stdio session to `tools/mcp-zene-control/server.py` on a live `zene`); `VideoClipsGuiTest::theMenusAndTheLaneAddAndMoveClips` (View-menu add, lane double-click add, real drag, context-menu move, save/reopen) | same run (rounds 0-3 with `ZENE_CONTROL_PYTHON`); round 4: `r4/logs/test-18-restored.log` with no override | 0 |
| 5 | One undo/redo step per mutating clip operation; legacy single-`<video>` loads as one clip; empty-project reset preserved | `VideoClipsTest::everyClipEditIsOneUndoStep` (clip_add, set_offset, clip_remove, attach, detach); `VideoClipsTest::attachReplacesTheTrackAndItsUndoPutsEveryClipBack`; `VideoClipsTest::anOldSingleVideoProjectLoadsAsOneClip`; `VideoClipsTest::anEmptyProjectResetsTheVideoTrack`; drag and context-menu undo in `VideoClipsGuiTest::theMenusAndTheLaneAddAndMoveClips` | same run | 0 |

Neighbours on the same binaries (`test-30.log`, exit 0, 12/12): `ControlRegistryTest`, `ReversibilityContractTest`,
`TaskCenterTest`, `AafImportTest`, `VideoTrackTest`, `VideoClipsTest`, `VideoFfmpegTest`, `VideoTestClipTest`,
`ArrangerOverlaysVideoLaneTest`, `VideoClipsGuiTest`, `ControlCommandsSnapshot`, `ControlVideoClipsMcp`.

**MCP interpreter note (environment, not this change):** the system `python3` here has `mcp` 1.29 while the bridge
needs the 2.x API (`Server(on_list_tools=...)`); the existing `ControlMcpGroupCoverage` fails the same way on this box
at the base (`test-16.log`, exit 8, `TypeError: ... 'on_list_tools'`). `ControlVideoClipsMcp` passes when the bridge
runs under the Hermes venv (`mcp` 2.0.0) through the harness's own `ZENE_CONTROL_PYTHON` override. Since round 4
the test itself selects such an interpreter (`bridge_python()`, see the round-4 section).

## Self-check

**Round 4 (integration onto `050/m0-train-22`, commit `4ed1f762ff`; logs `runs/Q050-180.1.r4/logs/`):**

- `submit_check.py` after the commit (`submit-check-22.log`, exit 1): every row PASS except "gates (feature_check.py)".
  That row fails because the script makes its log folder next to the worktree, outside this lane's write-jail
  (`PermissionError`, `feature-check-19.log`, exit 1). A copy in `/tmp` that changes only `ROOT` (pinned to the
  original directory) and the log folder gives **8/8 PASS, exit 0** at the tip (`feature-check-23.log`; before the
  commit, `feature-check-20.log`): fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace,
  no-tautology, doc-figures, complexity (no new item), file-length (no new item). `diff` shows those two lines as the
  only changes.
- **Each new test fails with the product change reverted and passes restored.** Repeated on this integrated tree.
  Reverted: `VideoClipsTest` exit 10, `VideoClipsGuiTest` exit 3, `control-video-clips-mcp.py` exit 1
  (`test-14/15/16-negctl-*.log`, build `build-13-negctl.log` exit 0). Restored: 11/11, exit 0 (`test-18-restored.log`).
- **Every acceptance criterion was proven by a test run this round:** rows 1-5 of the Acceptance table. All their
  tests (`VideoClipsTest`, `VideoClipsGuiTest`, `ControlVideoClipsMcp`, `VideoTrackTest`,
  `ArrangerOverlaysVideoLaneTest`) are in `test-18-restored.log`, exit 0. `ControlVideoClipsMcp` ran all 17 checks
  with no override (`test-7.log`, exit 0).
- **One undo step per edit; reversibility classes.** Product code is unchanged from the accepted patch, so the
  round-0 answer below still holds. `ReversibilityContractTest` passes on the re-taken block (`rows=890
  true_inverse=436 snapshot=42 irreversible=25 not_mutating=387`).
- **Realtime.** No product code changed in this integration; the round-0 design reading below applies. The only new
  code is in the Python test. TSan does not run on this box.
- **Old projects; round trip.** Unchanged from the accepted patch. `VideoTrackTest` (legacy single-video round trip)
  and `VideoClipsTest::anOldSingleVideoProjectLoadsAsOneClip` / `anEmptyProjectResetsTheVideoTrack` pass
  (`test-18-restored.log`).
- **Portability.** No C++ changed in this integration. The Python edit uses `str | None` under the file's existing
  `from __future__ import annotations` and runs only on the host.
- **Not run:** the full `ctest` suite and `run-all-gates.sh` (targeted tests only, per the brief).
  `ControlMcpGroupCoverage` (shared, not in this patch) still fails on this box for the interpreter reason
  (`test-5.log`, exit 8). I did not change it.

**Earlier rounds:**

**Round 3 (integration onto `050/m0-train-20`, commit `ecbd179ece`):** `submit_check.py` (`r3/logs/submit-check-11.log`):
every row PASS except "gates (feature_check.py)". That row fails because the script creates its log folder beside the
worktree, outside this lane's write-jail (`PermissionError`, `r3/logs/feature-check-12.log`, exit 1). I ran a copy in
`/tmp` that changes only `ROOT` (pinned to the original directory) and the log folder; `diff` shows nothing else.
It gives **8/8 PASS, exit 0** (`r3/logs/feature-check-13.log`): fork-sources, all-sources-reproduce,
no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length. The "?" answers below
still hold: the integration changed no product or test code, only the five bookkeeping files listed in the
integration section. Every acceptance test passed again on the merged tree (`r3/logs/test-4.log`, `test-6.log`,
`test-7.log`, `test-9.log`). The A16 figures are now `rows=883 true_inverse=436 snapshot=42 irreversible=25
not_mutating=380`.

`submit_check.py` (`submit-check-41.log`, before this report existed): every row PASS except the report (this file) and
"gates (feature_check.py)". The gates row fails only because `feature_check.py` creates its log folder beside the
worktree (`.../zene-feat-q050-180.1-feature-check`), outside this lane's write-jail: `PermissionError` (`feature-check-42-original.log`, exit 1).
The same script with only its log folder (and `ROOT`) pointed at `/tmp` — `diff` shows nothing else differs — gives
**8/8 PASS, exit 0** at the tip (`feature-check-43.log`): fork-sources, all-sources-reproduce, no-upstream-regression,
check-namespace, no-tautology, doc-figures, complexity (no new item), file-length (no new item).

- **Each new test FAILS with the product change reverted, then PASSES restored.**
  - Every `include/` and `src/` hunk reversed to `7641e9493` (`git diff 7641e9493 HEAD -- include src | git apply -R`),
    tests kept: build `cmake --build build --target zene VideoClipsTest VideoClipsGuiTest` exit 0 (`build-33-negctl.log`);
    `./VideoClipsTest` **exit 9** (all 9 cases fail), `./VideoClipsGuiTest` **exit 3** (all 3 fail),
    `tests/control-video-clips-mcp.py build/zene` **exit 1** (`test-34/35/36-negctl-*.log`).
  - Only Q050-139's code (this unit's fixes reversed): build exit 0 (`build-37-negctl139.log`); `./VideoClipsTest`
    **exit 4**: `attachReplacesTheTrackAndItsUndoPutsEveryClipBack` (1 clip after undo, expected 2),
    `aJumpIntoTheOtherClipNeverShowsTheFirstClipsFrame` (red shown, expected black), `aLateFrameOfTheOtherClipIsDropped`
    (late first-clip frame shown), `aRangeRenderShowsEachClipAtProjectTempoAndBlackElsewhere` (no command) (`test-38-negctl139.log`).
  - Restored with `git checkout HEAD -- include src` (tree clean), rebuilt exit 0 (`build-39-restored.log`), the five
    video suites **exit 0** (`test-40-restored.log`).
  - Round 1: the source-key fix alone reversed: `./VideoClipsTest` **exit 1** (only the reused-id regression fails);
    restored: **exit 0** (see the round-1 section).
- **Every acceptance criterion proven by a test I ran:** see the table; each row's tests ran in `test-40-restored.log`
  (exit 0) and `test-30.log` (exit 0).
- **One undo step per edit; honest reversibility classes.** All clip edits go through `recordClips`
  (`src/core/ControlCommandsVideo.cpp:144`): one `addUndoStep` over the whole list. Proven per command by
  `everyClipEditIsOneUndoStep` (one undo restores exactly the prior list, one redo the next), by the MCP undo/redo step,
  and for the GUI drag and context-menu move in `theMenusAndTheLaneAddAndMoveClips`. A click no longer records a move:
  press/drag is decided on release. Classes (`src/core/ControlReversibilityTableVideo.cpp`): `video.clip_add`,
  `video.clip_remove`, `video.attach`, `video.detach`, `video.set_offset` `true_inverse` (the recorded inverse puts the
  previous list back, ids included); `video.render_frames` and `video.get_state` `not_mutating` (render writes PNG
  output files only, like `render.render`). `ReversibilityContractTest` exit 0 after re-taking the published A16 block
  (`rows=879 true_inverse=434 snapshot=42 irreversible=25 not_mutating=378`).
- **Realtime.** No audio-thread function was changed. The checker's audio-path hint matches the tests' use of
  `AudioEngine::renderNextPeriod()` / `Song::playSong()` to drive real playback; the product diff touches only
  interface-thread code: `VideoReference` (commands, load/save, lane paint, window timer), the `video.*` handlers
  (dispatched on the interface thread), `VideoWindow::follow/noteActiveClip/take` (timer and queued signal on the
  interface thread), `VideoLane` (widget events), `video.render_frames` (its own `VideoDecoder` in the handler). Decoding
  stays on `VideoFramePump`'s worker, unchanged. No audio-thread allocation, lock, wait, I/O or signal was added. TSan is
  not available on this box; this is a design reading.
- **Old projects; round trip.** A project without `<video>` loads none and saves none (`shouldPersist()` is false with
  no clips) — byte-for-byte as before (`anEmptyProjectResetsTheVideoTrack` checks the saved file has no `<video>`). An
  old single-`<video>` project (no `<clip>` children) loads as one clip at its offset and the id-less legacy
  `video.set_offset` still moves it (`anOldSingleVideoProjectLoadsAsOneClip`); `VideoTrackTest` (the 0.5.0 single-video
  round trip) passes unchanged. New data: `<clip id ...>` children round-trip every field (criterion 1 tests, and the
  MCP save/new/open step). The first clip stays mirrored in `<video>`'s attributes so an older build opens the project
  with that clip. No audio render path changed.
- **Portability.** No Qt-6-only API: `QMenu::addAction(text, receiver, functor/member)` exists in Qt 5.15; the tests use
  `QList<Colour>` rather than `QSet` (an enum class has no `qHash` before Qt 6). No `near`/`far`/`small`/`interface`/
  `min`/`max` identifiers, no `std::filesystem`, no `windows.h` (scan of the added lines: no hit). The test clip maker's
  new `Palette` parameter defaults to the old red/blue clip, so `VideoTestClipTest`/`VideoTrackTest` call sites are
  unchanged (both pass).

## Registration and figures

- New files: `src/core/ControlCommandsVideoRender.cpp`, `tests/src/core/VideoClipsTest.cpp`,
  `tests/src/gui/VideoClipsGuiTest.cpp`, `tests/control-video-clips-mcp.py` in `tests/fork-sources.txt`;
  `tests/all-sources.txt` re-derived (the header's verify command prints `REPRODUCES`).
- `src/gui/MainWindow.cpp` (inherited) gains one declared View-menu item: reason row in `tests/upstream-modifications.txt`.
- Tests registered in `tests/CMakeLists.txt` (`VideoClipsTest`, `VideoClipsGuiTest` in `LMMS_TESTS` with offscreen
  environments; `ControlVideoClipsMcp` as an `add_test`, skip code 77).
- Snapshot regenerated from a live instance with `snapshot_live.py` (870 ids / 76 groups, `wasm.*` out, exit 0,
  `snapshot-18.log`); README canary updated; `tests/doc-figures-gate.py` PASS (`docfig-20.log`).

## Not done / limits

- One video track only; no compositing, transitions, encoded video export or source trim (out of scope by the unit).
- `video.render_frames` is synchronous (max 10000 frames) and blocks the interface while it runs.
- A drag has no live preview; the clip moves on release.
- The full `ctest` suite and `run-all-gates.sh` were not run (the brief limits this lane to targeted tests).
