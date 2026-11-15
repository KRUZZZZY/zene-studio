# cluster ci-6422986f4db9676f — VideoClipsTest fails to compile on a build without FFmpeg

Verdict: **FIXED**

## Decision

**REAL — test-build portability defect, not a product runtime defect.** On the hosted `linux-arm64`
job the project configures without FFmpeg (the Ubuntu arm64 dependency list carries no
`libav*-dev`), yet always-built test sources reference `videotest::makeClip`, which existed only
inside `#ifdef ZENE_HAVE_FFMPEG`. The failure in the CI log is a compile error, so the runtime
`VideoDecoder::available()` skip in `needClips()` could never run.

Evidence on the pinned commit `cc29a63dde09cf28bb00498a2d2a5b124d4d614d`:

- `tests/src/core/VideoTestClip.h` (before this fix, `#ifdef ZENE_HAVE_FFMPEG` at line 50, now
  line 55) declared and defined `makeClip()` only when FFmpeg is present.
- `tests/src/core/VideoClipsTest.cpp:530` calls `videotest::makeClip()` unguarded.
- `tests/src/gui/VideoClipsGuiTest.cpp:337` also calls it unguarded — the next build failure the
  CI job would have hit after `VideoClipsTest` was repaired.
- `CMakeLists.txt:815-826` makes FFmpeg optional; the CI dependency list
  `.github/workflows/deps-ubuntu-24.04-gcc.txt` contains no FFmpeg packages, so
  `ZENE_HAVE_FFMPEG` is undefined in that job.
- The sibling call sites were already guarded and were checked:
  `tests/src/core/VideoTrackTest.cpp:301`, `tests/src/gui/ArrangerOverlaysVideoLaneTest.cpp:400`,
  `tests/src/core/VideoFfmpegTest.cpp:65`, and
  `tests/src/core/VideoTestClipTest.cpp:104,133,176`.

## Fix

`tests/src/core/VideoTestClip.h` now offers `makeClip()` in **every** configuration:

- The real FFmpeg implementation is unchanged behind
  `#if defined(ZENE_HAVE_FFMPEG) && !defined(ZENE_TEST_VIDEO_CLIP_WITHOUT_FFMPEG)` (line 55).
- The `#else` branch (line 242) defines an inline fallback that returns `false` and touches
  nothing (line 250). Every always-built video test therefore compiles, its existing
  `VideoDecoder::available()` check skips the case, and no assertion changes.
- `ZENE_TEST_VIDEO_CLIP_WITHOUT_FFMPEG` (line 52 comment) is a test-only seam so the regression
  below can run the no-FFmpeg branch on an FFmpeg-enabled machine. The build never defines it.
- `#include <QString>` moved to the always-available section because the fallback signature names
  `QString`; the fixture constants and `Palette` were already outside the conditional.

No product audio code, save/load path, undo path or control command is touched. The header is
included only by the seven test translation units listed by
`grep -rln "VideoTestClip.h" tests/src`; all of them were rebuilt.

## Deterministic regression

New `tests/src/core/VideoTestClipPortabilityTest.cpp` (registered in `tests/CMakeLists.txt`,
`tests/fork-sources.txt` and `tests/all-sources.txt`). It defines the seam and includes the
fixture, so it exercises the no-FFmpeg branch in the normal FFmpeg-enabled developer/CI tree:

| Path | Covered by |
|---|---|
| Normal no-FFmpeg branch | `makeClipIsDeclaredWithoutFfmpegAndRefuses`: helper exists, returns false, creates no file, constants/palette intact. |
| Both palette spellings / empty path / missing directory | `makeClipAcceptsBothPalettesAndAnEmptyPath`: default palette, `RedBlue`, `GreenYellow`, empty and unwritable paths all decline. |
| Compile path that CI actually hit | Both `VideoClipsTest.cpp` and `VideoClipsGuiTest.cpp` were compiled with `lmmsconfig.h`'s FFmpeg define removed; before the fix both failed with `'makeClip' is not a member of 'videotest'`, after it both exit 0. |
| Runtime FFmpeg path | Existing `VideoClipsTest`, `VideoClipsGuiTest`, `VideoTestClipTest`, `VideoTrackTest`, `VideoFfmpegTest`, `ArrangerOverlaysVideoLaneTest` suites. |

No wall-clock timing, sleeps or races: the test changes only which preprocessor branch the fixture
provides in its own translation unit.

## Evidence — exact commands and exit codes

The requested log folder
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-6422986f4db9676f.r1/logs` was **not
writable by this session**: `touch` as its owner returned `Permission denied` (exit 1), and even
`chmod u+w` plus a retry failed, while the worktree and `$TMPDIR` are writable. All logs were
therefore kept in
`/home/kruzzzzy/.cache/zene-tmp/zene-qwen-ci-6422986f4db9676f/ci-6422986f4db9676f.r1-logs`
(on disk, not `/tmp`), and the same commands are reproduced below with `LOGS` naming that folder.

| Purpose | Exact command | Exit |
|---|---|---:|
| Regenerate the Makefiles for the new target (configure itself is blocked by the lane wrapper) | `cmake --build build --target cmake_check_build_system > "$LOGS/configure-1.log" 2>&1` | 0 |
| Build the new regression on the unfixed tree | `cmake --build build --target VideoTestClipPortabilityTest -j12 > "$LOGS/build-1-red.log" 2>&1` | 0 |
| RED regression, unfixed tree | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^VideoTestClipPortabilityTest$' --output-on-failure > "$LOGS/test-1-red.log" 2>&1` | **8** |
| Exact CI compile failure, FFmpeg define removed from the generated config | `python3 … compile_commands.json` with `-I"$LOGS/noffmpeg-shadow"` and `-fsyntax-only` on `VideoClipsTest.cpp` and `VideoClipsGuiTest.cpp`, output in `"$LOGS/compile-noffmpeg-red.log"` | **1** (both files) |
| Build the affected FFmpeg-enabled binaries on the unfixed tree | `cmake --build build --target VideoClipsTest VideoClipsGuiTest -j12 > "$LOGS/build-2-pre-fix-ffmpeg.log" 2>&1` | 0 |
| Apply the fix (edit `VideoTestClip.h`) | — | — |
| Rebuild every target that includes the header | `cmake --build build --target VideoTestClipPortabilityTest VideoClipsTest VideoClipsGuiTest VideoTestClipTest VideoTrackTest VideoFfmpegTest ArrangerOverlaysVideoLaneTest -j12 > "$LOGS/build-3-green.log" 2>&1` | 0 |
| GREEN regression and unchanged video family | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^(VideoTestClipPortabilityTest\|VideoClipsTest\|VideoClipsGuiTest\|VideoTestClipTest\|VideoTrackTest\|VideoFfmpegTest\|ArrangerOverlaysVideoLaneTest)$' --output-on-failure > "$LOGS/test-2-green-family.log" 2>&1` | 0 (7/7) |
| Exact CI compile check with the fix, FFmpeg define removed | same shadow-config compile, output `"$LOGS/compile-noffmpeg-green.log"` | 0 (both files) |
| Save and reverse the product hunk | `git diff -- tests/src/core/VideoTestClip.h > "$LOGS/fix.patch"` then `git apply -R "$LOGS/fix.patch"` | 0 / 0 |
| Rebuild with the fix reversed | `cmake --build build --target VideoTestClipPortabilityTest -j12 > "$LOGS/build-4-red-reverted.log" 2>&1` | 0 |
| RED proof: regression must fail without the fix | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^VideoTestClipPortabilityTest$' --output-on-failure > "$LOGS/test-3-red-reverted.log" 2>&1` | **8** |
| Restore the fix | `git apply "$LOGS/fix.patch"` | 0 |
| Rebuild restored | `cmake --build build --target VideoTestClipPortabilityTest -j12 > "$LOGS/build-5-green-restored.log" 2>&1` | 0 |
| GREEN proof: regression passes restored | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^VideoTestClipPortabilityTest$' --output-on-failure > "$LOGS/test-4-green-restored.log" 2>&1` | **0** |
| Rebuild/re-run after replacing the POSIX-style missing path with `QTemporaryDir::filePath("missing/…")` | `cmake --build build --target VideoTestClipPortabilityTest -j12 > "$LOGS/build-7-portable-path.log" 2>&1`; `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^VideoTestClipPortabilityTest$' --output-on-failure > "$LOGS/test-5-portable-path.log" 2>&1` | 0 / 0 |
| Main application still builds | `cmake --build build --target zene -j12 > "$LOGS/build-6-zene.log" 2>&1` | 0 |
| Unchanged quality gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-ci-6422986f4db9676f > "$LOGS/gates-1.log" 2>&1` | 0 (`RESULT: PASS`, 8/8 gates) |

The RED run failed exactly at
`VideoTestClipPortabilityTest.cpp:55` — `'!videotest::makeClip(path)' returned FALSE` — because the
unfixed header took the real FFmpeg branch and made a clip. The GREEN runs passed 1/1 and the
unchanged family passed 7/7 (ctest exit 0).

## Files committed

- `tests/src/core/VideoTestClip.h` — makeClip declared in every configuration; false fallback.
- `tests/src/core/VideoTestClipPortabilityTest.cpp` — new deterministic regression.
- `tests/CMakeLists.txt` — registers it.
- `tests/fork-sources.txt`, `tests/all-sources.txt` — register the new test source.
- `docs/qwen-fixes-2026-10/cluster-ci-6422986f4db9676f.md` — this report.

## Self-check

- **Regression fails with the product change reverted, passes restored:** RED `ctest` exit 8
  (`test-3-red-reverted.log`), GREEN exit 0 (`test-4-green-restored.log`); exact commands above.
- **Deterministic, no wall-clock timing:** the test uses only the `ZENE_TEST_VIDEO_CLIP_WITHOUT_FFMPEG`
  preprocessing seam; no sleeps, timers or threads.
- **Cause complete, every caller/sibling checked:** `grep -rln "VideoTestClip.h" tests/src` names
  seven translation units, all rebuilt. All `makeClip` call sites were audited; the two unguarded
  ones (`VideoClipsTest.cpp:530`, `VideoClipsGuiTest.cpp:337`) are the reported bug and its sibling.
  The other four call sites already guard or run only under FFmpeg.
- **No collateral damage:** the real FFmpeg implementation is byte-for-byte unchanged; projects,
  saves, undo and commands are untouched. The fallback is only selected when FFmpeg is absent (or
  by the regression seam), exactly the configuration that previously did not compile. No limit or
  check was added that could affect other callers.
- **Audio thread / undo / commands:** no audio-path file, command file or undo code changed, so
  nothing new allocates, frees, locks, waits or logs on the audio thread, and no control command
  or canary changed.
- **Registration and gates:** new test registered in `tests/CMakeLists.txt` and both source
  manifests; `feature_check.py` exit 0 with all eight gates PASS (complexity and file-length:
  "no new item in your files").
