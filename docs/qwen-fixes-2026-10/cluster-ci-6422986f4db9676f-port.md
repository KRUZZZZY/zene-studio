ALREADY-FIXED

# Cluster ci-6422986f4db9676f-port — Port: CI regression: build / linux-arm64

The finding is only a title. Its parent cluster, `ci-6422986f4db9676f`, is recorded in
`docs/qwen-fixes-2026-10/cluster-ci-6422986f4db9676f.md`: the hosted `linux-arm64` job
configures without FFmpeg (Ubuntu's arm64 dependency list has no `libav*-dev`), while the
always-built video test sources referenced `videotest::makeClip`, which was declared only
inside `#ifdef ZENE_HAVE_FFMPEG`. That is a test-build compile error; no runtime
`VideoDecoder::available()` skip can prevent it.

This port unit asks whether the accepted fix is on this branch (train 52). It is, with its
regression, so no product or test source needs to change. The work below is independent
verification of that state and a red/green proof of the regression's oracle.

## Evidence on this branch (file:line)

- `tests/src/core/VideoTestClip.h:51` — the real fixture is selected by
  `#if defined(ZENE_HAVE_FFMPEG) && !defined(ZENE_TEST_VIDEO_CLIP_WITHOUT_FFMPEG)`.
- `tests/src/core/VideoTestClip.h:238-247` — the `#else` branch declares the fallback
  `inline bool makeClip(const QString&, Palette = Palette::RedBlue) { return false; }` in
  every configuration. The helper is therefore always declared for callers; the no-FFmpeg
  branch touches nothing and returns false, so each caller's own availability check skips.
- `tests/src/core/VideoTestClipPortabilityTest.cpp:48-78` — the deterministic regression.
  It defines `ZENE_TEST_VIDEO_CLIP_WITHOUT_FFMPEG`, then asserts the helper exists, refuses
  a real path without creating a file, keeps the fixture constants, and accepts both palette
  spellings plus an empty path.
- `tests/CMakeLists.txt:1237`, `tests/fork-sources.txt:891`, `tests/all-sources.txt:3360`
  — the regression source is registered.
- `tests/CMakeLists.txt:6566-6573` (`Train48Portability`) and `:9795-9805`
  (`NoFfmpegVideoClipsCompileCheck`, `VideoClipsNoFfmpegCheck`) — the CI-shaped
  compile probes for the same no-FFmpeg configuration are registered as ctest entries.
- The fix commit `d9a26d06ad` is an ancestor of HEAD; it is the reconciliation that set the
  seam in `tests/src/core/VideoTestClip.h`, added
  `tests/src/core/VideoTestClipPortabilityTest.cpp`, and registered it.

The sibling call sites were checked on this tree: `tests/src/core/VideoClipsTest.cpp:529-532`
and `tests/src/gui/VideoClipsGuiTest.cpp:336-339` keep their clip creation behind
`#ifdef ZENE_HAVE_FFMPEG`; `tests/src/core/VideoTrackTest.cpp:301-302` and
`tests/src/core/VideoTrackTestParts1.h:233-236` do the same or `QSKIP`;
`tests/src/core/VideoTestClipTest.cpp:35,110` and `tests/src/core/VideoFfmpegTest.cpp:33,69`
are guarded per translation unit; and the video-lane call at
`tests/src/gui/ArrangerOverlaysVideoLaneTest.cpp:402-404` is inside its file's
`#ifdef ZENE_HAVE_FFMPEG` slot. The header fallback also makes any future unguarded caller
compile in the no-FFmpeg configuration.

## Commands and EXIT codes

`LOGS=/home/kruzzzzy/.config/kads/zene/state/runs/ci-6422986f4db9676f-port.r2/logs`.
Every command below was run from the worktree unless the row says otherwise.

| # | Command | EXIT | Log |
| --- | --- | ---: | --- |
| 1 | `cmake --build build --target VideoTestClipPortabilityTest -j4 > "$LOGS/build-1.log" 2>&1` | 0 | `build-1.log` |
| 2 | `cd build/tests && ctest -N > "$LOGS/ctest-list-1.log" 2>&1` | 0 | `ctest-list-1.log` |
| 3 | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^VideoTestClipPortabilityTest$' --output-on-failure > "$LOGS/test-1.log" 2>&1` | 0 (1/1 passed) | `test-1.log` |
| 4 | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^(Train48Portability\|NoFfmpegVideoClipsCompileCheck\|VideoClipsNoFfmpegCheck\|VideoTestClipPortabilityTest)$' --output-on-failure > "$LOGS/test-2.log" 2>&1` | 8 — the two compile probes needed generated `.moc` files from unbuilt test targets | `test-2.log` |
| 5 | `cmake --build build --target VideoClipsTest SfzPlayerTest -j4 > "$LOGS/build-2.log" 2>&1` | 0 | `build-2.log` |
| 6 | same ctest family, output `"$LOGS/test-3.log"` | 8 — only `VideoClipsGuiTest.moc` was still absent | `test-3.log` |
| 7 | `cmake --build build --target VideoClipsGuiTest -j4 > "$LOGS/build-3.log" 2>&1` | 0 | `build-3.log` |
| 8 | same ctest family, output `"$LOGS/test-4.log"` | 0 (4/4 passed) | `test-4.log` |
| 9 | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^(VideoClipsTest\|VideoClipsGuiTest)$' --output-on-failure > "$LOGS/test-7-video-family.log" 2>&1` | 0 (2/2 passed) | `test-7-video-family.log` |
| 10 | **RED setup**: locally removed the seam (`#if defined(ZENE_HAVE_FFMPEG)` only), then `git diff -- tests/src/core/VideoTestClip.h > "$LOGS/fix.patch"` | 0 | `fix.patch` |
| 11 | `cmake --build build --target VideoTestClipPortabilityTest -j4 > "$LOGS/build-4.log" 2>&1` | 0 | `build-4.log` |
| 12 | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^VideoTestClipPortabilityTest$' --output-on-failure > "$LOGS/test-5-red.log" 2>&1` | **8** — `FAIL! ... '!videotest::makeClip(path)' returned FALSE ... Loc: [../../tests/src/core/VideoTestClipPortabilityTest.cpp(55)]` | `test-5-red.log` |
| 13 | `git apply -R "$LOGS/fix.patch"` (restored the seam) | 0 | — |
| 14 | `cmake --build build --target VideoTestClipPortabilityTest -j4 > "$LOGS/build-5.log" 2>&1` | 0 | `build-5.log` |
| 15 | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^VideoTestClipPortabilityTest$' --output-on-failure > "$LOGS/test-6-green.log" 2>&1` | 0 (1/1 passed) | `test-6-green.log` |
| 16 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-6422986f4db9676f-port > "$LOGS/gates-1.log" 2>&1` | 0 (`RESULT: PASS`, 8/8) | `gates-1.log` |

Runs 4 and 6 are not code failures: the probes recompile entries from
`build/compile_commands.json`, and their source files `#include` generated `.moc` files that
only appear after the corresponding test target is built. Building those targets (rows 5 and
7) made the same probes pass unchanged (row 8).

## Self-check

- **Regression test RED/GREEN.** The port unit changes no product code, so the fix already on
  the branch was temporarily reverted at its cause in the fixture (`fix.patch`, row 10).
  With the seam gone the test takes the real FFmpeg branch: ctest EXIT=8 at
  `VideoTestClipPortabilityTest.cpp:55` (row 12). Restored and rebuilt: EXIT=0, 1/1 (rows
  13-15). The CI-shaped `NoFfmpegVideoClipsCompileCheck` and `Train48Portability` also pass
  (row 8).
- **Deterministic.** The test uses only the `ZENE_TEST_VIDEO_CLIP_WITHOUT_FFMPEG`
  preprocessing seam and `QTemporaryDir::filePath`; there is no clock, sleep, thread or
  network. `Train48Portability`'s video probe compiles a fixed shadow header.
- **Cause and completeness.** The fallback declaration covers every configuration at
  `tests/src/core/VideoTestClip.h:51,238-247`; every `videotest::makeClip` caller was walked
  (listed above). The two previously unguarded call sites are guarded, and the always-built
  callers can no longer fail to compile in a no-FFmpeg build. No limit or check was added.
- **No collateral damage.** The real FFmpeg implementation is unchanged; the fallback is
  selected only when FFmpeg is absent or by the test seam. `VideoClipsTest` and
  `VideoClipsGuiTest` pass on the FFmpeg-enabled tree (row 9). No save/load format, undo
  path, control command or audio-thread code is touched.
- **ALREADY-FIXED evidence.** `tests/src/core/VideoTestClip.h:51` and `:238-247`, with the
  regression at `tests/src/core/VideoTestClipPortabilityTest.cpp:48-78` registered by
  `tests/CMakeLists.txt:1237`; the reconciled fix commit `d9a26d06ad` is an ancestor of
  HEAD.
- **Sibling commands (submit_check ASK).** This unit changes no command file. Any
  `ControlCommands`/`ControlReversibilityTable` files and command ids listed by
  `submit_check.py` come from the train-wide diff this branch inherits against its base, not
  from this unit; no command was added or changed here, so the `ControlCommandsSnapshot`
  canary is unchanged.
- **Registration and gates.** The regression is already registered in `tests/CMakeLists.txt`,
  `tests/fork-sources.txt`, `tests/all-sources.txt` and `tests/upstream-modifications.txt`;
  `feature_check.py` reports 8/8 PASS, `RESULT: PASS` (row 16). No new file over 500 lines
  and no new function was added.
- **Platform.** This host is Linux x86_64 with FFmpeg enabled; the no-FFmpeg arm64
  configuration is reproduced by the header seam and the shadow-configuration compile
  probes. A hosted `linux-arm64` run of this branch was not available in this session; the
  train's CI will be the first end-to-end arm64 execution.

## Other findings

- None.
