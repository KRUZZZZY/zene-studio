ALREADY-FIXED

# cluster ci-e8063221c62bcc38-port - Port: CI regression: build / linux-x86_64

ci-e8063221c62bcc38-port: ALREADY-FIXED on this branch

## Verdict

The parent cluster `ci-e8063221c62bcc38` is the no-FFmpeg test-build defect recorded in
`docs/qwen-fixes-2026-10/cluster-ci-e8063221c62bcc38.md` on the source branch. The hosted
`linux-x86_64` job builds without the optional FFmpeg development packages, so
`lmmsconfig.h` leaves `ZENE_HAVE_FFMPEG` undefined. `tests/src/core/VideoClipsTest.cpp`
called `videotest::makeClip` anyway, while the fixture declared that helper only inside
`#ifdef ZENE_HAVE_FFMPEG`, and the build stopped at:

```
tests/src/core/VideoClipsTest.cpp:530:33: error: 'makeClip' is not a member of 'videotest'
```

The source-branch fix is `cc1ba72f56` ("declare videotest::makeClip in no-FFmpeg test
builds"). On this train the same fix is already present in commit `9feb23eb1b`
("Fixed ... the no-FFmpeg fixture API ..."), and
`git merge-base --is-ancestor 9feb23eb1b HEAD` exits `0`. The train also guards both
formerly-unguarded callers, so the defect cannot recur through either path. No product or
test source needs to change in this job.

## Code evidence

- `tests/src/core/VideoTestClip.h:229-234` - the real `makeClip` used with FFmpeg.
- `tests/src/core/VideoTestClip.h:238-247` - the `#else` branch declares the same inline
  `makeClip` signature and returns `false` when no encoder exists. This is the source fix.
- `tests/src/core/VideoClipsTest.cpp:529-538` - `needClips()` now puts its `makeClip` calls
  inside `#ifdef ZENE_HAVE_FFMPEG` and returns `false` in the no-FFmpeg branch.
- `tests/src/gui/VideoClipsGuiTest.cpp:336-345` - the GUI sibling does the same.
- The other callers were already guarded or FFmpeg-only: `VideoTrackTest.cpp:302`,
  `VideoTrackTestParts1.h:234`, `VideoFfmpegTest.cpp:69,98`,
  `VideoTestClipTest.cpp:110`, and `ArrangerOverlaysVideoLaneTest.cpp:404`.
- `tests/src/core/VideoTestClipPortabilityTest.cpp:41-42` selects the no-FFmpeg branch with
  `ZENE_TEST_VIDEO_CLIP_WITHOUT_FFMPEG`; `:49-78` proves the helper is declared, returns
  `false`, leaves no file, and accepts both palette spellings.
- `tests/train-48-portability-check.py:107-123` is the committed compile regression: it
  writes a shadow `lmmsconfig.h` that `#undef`s `ZENE_HAVE_FFMPEG`, compiles a probe calling
  all three `makeClip` spellings, and runs the result. It is registered as
  `Train48Portability` in `tests/CMakeLists.txt:6566-6572`.

## Regression proof (RED / GREEN)

The product fix predates this branch, so RED is produced by temporarily swapping in the
pre-fix header (`git show 9feb23eb1b^:tests/src/core/VideoTestClip.h`). The pre-fix header
is the exact tree that failed the original CI job. The probe is the `mode_video` half of the
committed `Train48Portability` regression; the runner in the log folder only imports and
calls that function.

`L=/home/kruzzzzy/.config/kads/zene/state/runs/ci-e8063221c62bcc38-port.r2/logs`

| Step | Command | EXIT | Evidence |
| --- | --- | --- | --- |
| GREEN baseline | `python3 $L/run-train48-video.py "$PWD" "$PWD/build" 6` | `0` | `test-4-train48-video-green.log` (empty output) |
| Apply pre-fix header | `git show 9feb23eb1b^:tests/src/core/VideoTestClip.h > tests/src/core/VideoTestClip.h` | `0` | `git diff --stat`: 1 insertion, 13 deletions |
| RED | same runner after the pre-fix header | `1` | `test-5-train48-video-red.log`: `error: 'makeClip' is not a member of 'videotest'` |
| Restore header | `git show HEAD:tests/src/core/VideoTestClip.h > tests/src/core/VideoTestClip.h` | `0` | `git diff --stat` empty; `git status --porcelain` empty |
| GREEN restored | same runner after the restore | `0` | `test-6-train48-video-green-restored.log` (empty output) |

The RED log is the original CI diagnostic, reproduced deterministically from the recorded
source state. The GREEN run compiles and links all three call spellings with no encoder.

## Build and test commands

`L` is the log folder above.

| Command | EXIT | Evidence |
| --- | --- | --- |
| `cmake --build build --target zene -j4` | `0` | `build-1.log`: `Built target zene` |
| `cmake --build build --target VideoTestClipPortabilityTest VideoClipsTest VideoClipsGuiTest -j4` | `0` | `build-3-video-family.log` |
| `python3 tests/VideoClipsNoFfmpegCheck.py --source-root "$PWD" --build-dir "$PWD/build" --moc /usr/lib/qt6/moc` | `0` | `test-1-noffmpeg-check.log`: both callers compile as configured and with `ZENE_HAVE_FFMPEG` removed |
| `cd build/tests && ctest -R '^(VideoTestClipPortabilityTest\|VideoClipsNoFfmpegCheck\|VideoClipsTest\|VideoClipsGuiTest)$' --output-on-failure` | `0` | `test-2-green.log`: 4/4 passed (`VideoClipsTest`, `VideoTestClipPortabilityTest`, `VideoClipsGuiTest`, `VideoClipsNoFfmpegCheck`) |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `0` | `gates-1.log`: all 8 rows PASS, `RESULT: PASS` |

The full-build command is refused by the lane guard (`build-2-full.log`, EXIT `97`); named
targets are the allowed form and were used above.

## Test matrix

| Path | Configuration | Result |
| --- | --- | --- |
| Reported compile path | `ZENE_HAVE_FFMPEG` undefined, `VideoClipsTest.cpp` | PASS: `test-1-noffmpeg-check.log` |
| Sibling compile path | `ZENE_HAVE_FFMPEG` undefined, `VideoClipsGuiTest.cpp` | PASS: `test-1-noffmpeg-check.log` |
| Normal runtime path | FFmpeg build, video tests | PASS: 4/4 in `test-2-green.log` |
| Failure path | pre-fix header, no FFmpeg | FAIL with the original `makeClip` error (`test-5`) |
| Reconfiguration path | shadow `lmmsconfig.h` with the one `ZENE_HAVE_FFMPEG` define removed | probes prove the shadow takes effect (`test-1`) |
| Audio thread | no product or audio code touched | no allocation, lock, wait or I/O added |
| Platforms | the guard is macro-based; the compile probe uses the real recorded command line | Qt 5 and Qt 6 spelling comes from the recorded command; no OS-only API added |

## Self-check

- **Regression fails without the change and passes with it:** the committed
  `Train48Portability` video regression exits `1` against the pre-fix header with the exact
  `'makeClip' is not a member of 'videotest'` error, and exits `0` against this branch.
  This job changes no product code (`ALREADY-FIXED`), so there is no `fix.patch` of our own;
  the pre-fix header is the recorded negative control (`test-5` / `test-6`).
- **Deterministic:** the probe compiles a fixed source against a shadow `lmmsconfig.h` with
  `ZENE_HAVE_FFMPEG` undefined, then runs the resulting binary. There is no clock, sleep,
  race or network dependency.
- **At the cause and complete:** both formerly-unguarded callers are guarded
  (`VideoClipsTest.cpp:529`, `VideoClipsGuiTest.cpp:336`), the fixture declares `makeClip`
  in both preprocessor branches (`VideoTestClip.h:238-247`), and every other caller listed
  above is already guarded or FFmpeg-only. The same signature and default palette are used
  everywhere.
- **No collateral damage:** the FFmpeg branch is unchanged; the fallback returns `false`
  only where the callers already skip. No project, saved-data, undo, command or realtime
  code is touched.
- **ALREADY-FIXED evidence:** `VideoTestClip.h:238-247` (fallback declaration), its
  introduction in ancestor commit `9feb23eb1b`, and the guarded callers at
  `VideoClipsTest.cpp:529-538` and `VideoClipsGuiTest.cpp:336-345`.
- **Sibling commands:** this job changes no command file, so the command-family item is
  not applicable.

## Other findings

- `ctest -R '^Train48Portability$'` fails in this existing build tree in its unrelated SFZ
  mode because the ad-hoc compile cannot find `SfzPlayerTest.moc`
  (`test-3-train48-green.log`, EXIT `8`). The video mode is the part that covers this
  cluster and it passes (`test-4` / `test-6`). I did not change it.
- `cmake --build build` without a target is refused by the lane guard, as expected
  (`build-2-full.log`, EXIT `97`).
