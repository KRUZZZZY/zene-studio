FIXED

# cluster ci-e8063221c62bcc38 - linux-x86_64 build fails compiling VideoClipsTest without FFmpeg

## Verdict and classification

FIXED. The failure is a **test build defect**, not a product defect and not an infrastructure
defect: FFmpeg is explicitly optional (`CMakeLists.txt:129`, `CMakeLists.txt:816-825`,
`include/VideoDecoder.h:53-54`), the hosted `linux-x86_64` dependency list has no
`libavformat-dev`/`libavcodec-dev`/... (`.github/workflows/deps-ubuntu-24.04-gcc.txt`), and a
no-FFmpeg build must therefore compile every test target. Two test sources called the
FFmpeg-only fixture helper `videotest::makeClip` unconditionally while
`tests/src/core/VideoTestClip.h:50` declares it only under `ZENE_HAVE_FFMPEG`.

The exact CI diagnostic was reproduced at the cited line against the pinned checkout
(`HEAD = cc29a63dde09cf28bb00498a2d2a5b124d4d614d`):

```
tests/src/core/VideoClipsTest.cpp:530:33: error: 'makeClip' is not a member of 'videotest'
tests/src/core/VideoClipsTest.cpp:530:60: error: 'makeClip' is not a member of 'videotest'
```

The same guard mismatch also existed in the sibling caller `tests/src/gui/VideoClipsGuiTest.cpp:337`,
which the reported build would have reached after VideoClipsTest. Both are fixed by the same change.

## Cause and fix

`tests/src/core/VideoTestClip.h` declared `makeClip` only inside `#ifdef ZENE_HAVE_FFMPEG`
(declaration at line 228, guard opened at line 50), while the two callers above use it
unconditionally inside their `needClips()` helpers (the runtime `VideoDecoder::available()` guard
does not help the compiler). Other callers already guard it:
`tests/src/core/VideoTrackTest.cpp:301-303`, `tests/src/core/VideoTrackTestParts1.h:234`,
`tests/src/core/VideoTestClipTest.cpp:104-110`, `tests/src/core/VideoFfmpegTest.cpp:69,98`, and
`tests/src/gui/ArrangerOverlaysVideoLaneTest.cpp:404`.

The fix makes the fixture's contract uniform: `VideoTestClip.h:237-254` now has an
`#else // ZENE_HAVE_FFMPEG` branch declaring

```cpp
inline bool makeClip(const QString&, Palette = Palette::RedBlue) { return false; }
```

so every caller compiles in both configurations. In a no-FFmpeg build `makeClip` returns false,
which is exactly what the callers already handle (they then skip); in an FFmpeg build the original
implementation is unchanged. No product source, no command, no save/load or undo behaviour changes.
The header is already registered in `tests/fork-sources.txt:3002`, so no registration change was
needed.

## Evidence table

All logs are in `$TMPDIR/logs-ci-e8063221c62bcc38/` (the requested
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-e8063221c62bcc38.r1/logs` directory is
write-protected by this session's write jail: `touch`/`mkdir` there fail with EACCES even though it
is owned by the session user and mode 755; `$TMPDIR` is on disk and writable).

| Step | Exact command | Exit | Evidence |
|---|---|---|---|
| RED, reported caller, no-FFmpeg | `python3 "$TMPDIR/compile-noffmpeg.py" "$SHADOW" tests/src/core/VideoClipsTest.cpp` (shadow `lmmsconfig.h` = `build/lmmsconfig.h` with `#define ZENE_HAVE_FFMPEG` removed; exact compile command from `build/compile_commands.json`) | `1` | `red-2-noffmpeg-compile.log`: line 530 errors |
| RED, sibling caller, no-FFmpeg | same script with `tests/src/gui/VideoClipsGuiTest.cpp` | `1` | `red-3-noffmpeg-gui-compile.log`: line 337 errors |
| GREEN, both callers, no-FFmpeg | same script with both sources | `0` | `green-1-noffmpeg-compile.log`: `EXIT=0` for both |
| Step 4b, save patch | `git diff -- tests/src/core/VideoTestClip.h > "$LOGS/fix.patch"` | `0` | 17 added lines |
| Step 4b, reverse | `git apply -R "$LOGS/fix.patch"` | `0` | |
| Step 4b, RED reverted | no-FFmpeg compile of `VideoClipsTest.cpp` | `1` | `red-4-noffmpeg-reverted.log`: line 530 errors |
| Step 4b, restore | `git apply "$LOGS/fix.patch"` | `0` | |
| Step 4b, GREEN restored | no-FFmpeg compile of `VideoClipsTest.cpp` | `0` | `green-2-noffmpeg-restored.log` |
| Build affected family | `cmake --build build --target VideoClipsTest VideoFfmpegTest VideoTestClipTest VideoTrackTest ArrangerOverlaysVideoLaneTest VideoClipsGuiTest` | `0` | `build-4-video-family-after-reapply.log` |
| Run affected family | `cd build/tests && ctest -R "^(VideoTrackTest\|VideoClipsTest\|VideoFfmpegTest\|VideoTestClipTest\|ArrangerOverlaysVideoLaneTest\|VideoClipsGuiTest)$" --output-on-failure` | `0` | `test-2-video-family-final.log`: **6/6 passed**, 0 failed |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `0` | `gates-1.log`: all 8 rows PASS |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> ci-e8063221c62bcc38` | `0` | after this report's commit; see below |

The FFmpeg-enabled build was rebuilt before every run, so the final GREEN run is not a stale binary.
The ctest run executed 6 tests (zero tests would be an error).

## Test matrix

| Path | Configuration | Result |
|---|---|---|
| Reported compile path | `ZENE_HAVE_FFMPEG` undefined | RED (exit 1) before fix, GREEN (exit 0) after |
| Sibling compile path | `ZENE_HAVE_FFMPEG` undefined | RED (exit 1) before fix, GREEN (exit 0) after |
| Normal runtime path | FFmpeg build, real `ffmpeg`/libavcodec fixture | `VideoClipsTest`, `VideoClipsGuiTest`, `VideoTrackTest`, `VideoFfmpegTest`, `VideoTestClipTest`, `ArrangerOverlaysVideoLaneTest` all pass (6/6) |
| Failure/reconfiguration | no-FFmpeg compile with the real test sources | compiles; `makeClip` returns false and callers skip (no assertions weakened) |
| Audio thread | no product/audio code touched | no allocation, lock, wait or I/O added |
| Platforms | guard is macro-based, not OS-based | Qt5/Qt6, Linux/macOS/Windows use the same `ZENE_HAVE_FFMPEG` contract; no platform-only API added |

## Files committed

- `tests/src/core/VideoTestClip.h` (adds the no-FFmpeg `makeClip` declaration)
- `docs/qwen-fixes-2026-10/cluster-ci-e8063221c62bcc38.md` (this report)

## Self-check

- **Regression FAILS with the product change reverted and PASSES with it (exit codes):** the
  no-FFmpeg compile is the regression. Reverted, `red-4-noffmpeg-reverted.log` exits `1` with
  `VideoClipsTest.cpp:530: error: 'makeClip' is not a member of 'videotest'`; restored,
  `green-2-noffmpeg-restored.log` exits `0`. Before the fix, both real callers failed:
  `red-2` (VideoClipsTest, line 530) and `red-3` (VideoClipsGuiTest, line 337), both exit `1`;
  after the fix both compile with exit `0` (`green-1`).
- **Deterministic:** the check injects the configuration seam directly - a shadow
  `lmmsconfig.h` identical to the configured one except that the single
  `#define ZENE_HAVE_FFMPEG` line is removed - and replays the exact compiler command recorded in
  `build/compile_commands.json`. There is no wall-clock timing, sleep, race or network dependency.
- **Fix at the cause and complete:** the helper is now declared in both preprocessor branches of
  `VideoTestClip.h`; every include site of that header was enumerated
  (`VideoClipsTest.cpp`, `VideoFfmpegTest.cpp`, `VideoTestClipTest.cpp`, `VideoTrackTest.cpp`,
  `ArrangerOverlaysVideoLaneTest.cpp`, `VideoClipsGuiTest.cpp`) and all six targets were rebuilt
  and run. The previously guarded callers keep their guards and remain consistent.
- **No collateral damage:** the change is test-only. The FFmpeg branch is byte-for-byte unchanged,
  so an FFmpeg build behaves exactly as before; the stub returns false only where the product's
  own `VideoDecoder::available()` is false and the tests already skip. No saved project, undo
  record, command or realtime path is touched, and no limit/check was added.
- **ALREADY-FIXED / FALSE-POSITIVE:** not claimed. The pinned checkout fails as reported; the
  evidence is the reproduced compiler diagnostic above.
