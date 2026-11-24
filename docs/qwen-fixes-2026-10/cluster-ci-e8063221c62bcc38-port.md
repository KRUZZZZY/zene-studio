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
formerly-unguarded callers, so the defect cannot recur through either path. No product
source needs to change in this job.

This round adds the branch-local regression guard requested by the checkpoint:
`tests/VideoClipsNoFfmpegCheck.py` now compiles an unguarded no-FFmpeg probe for all three
`videotest::makeClip` spellings and proves the probe detects the defect with a negative
control that removes the fallback. The probe runs before every early-return path, so it is
exercised in both a configured-FFmpeg build and a configured-no-FFmpeg build.

## Code evidence

- `tests/src/core/VideoTestClip.h:229-234` - the real `makeClip` used with FFmpeg.
- `tests/src/core/VideoTestClip.h:238-247` - the `#else` branch declares the same inline
  `makeClip` signature and returns `false` when no encoder exists. This is the source fix.
- `tests/src/core/VideoClipsTest.cpp:529-538` - `needClips()` puts its `makeClip` calls
  inside `#ifdef ZENE_HAVE_FFMPEG` and returns `false` in the no-FFmpeg branch.
- `tests/src/gui/VideoClipsGuiTest.cpp:336-345` - the GUI sibling does the same.
- The other callers were already guarded or FFmpeg-only: `VideoTrackTest.cpp:302`,
  `VideoTrackTestParts1.h:234`, `VideoFfmpegTest.cpp:69,98`,
  `VideoTestClipTest.cpp:110`, and `ArrangerOverlaysVideoLaneTest.cpp:404`.
- `tests/src/core/VideoTestClipPortabilityTest.cpp:41-42` selects the no-FFmpeg branch with
  `ZENE_TEST_VIDEO_CLIP_WITHOUT_FFMPEG`; `:49-78` proves the helper is declared, returns
  `false`, leaves no file, and accepts both palette spellings.
- `tests/VideoClipsNoFfmpegCheck.py:36-52` adds the probe source and fallback matcher;
  `:287-352` forces a no-FFmpeg config, compiles the unguarded probe, and runs the
  negative control that removes the fallback and must fail.
- `tests/CMakeLists.txt:9800-9804` still registers `VideoClipsNoFfmpegCheck`, and
  `tests/fork-sources.txt:877` still lists `tests/VideoClipsNoFfmpegCheck.py`.

## Regression proof (RED / GREEN)

The product fix predates this branch, so RED is produced by temporarily swapping in the
pre-fix header (`git show 9feb23eb1b^:tests/src/core/VideoTestClip.h`). That pre-fix
header is the exact tree that failed the original CI job. GREEN is this branch's HEAD.
The direct runner exits `1` on RED and `0` on GREEN; ctest wraps the same runner.

`L=/home/kruzzzzy/.config/kads/zene/state/runs/ci-e8063221c62bcc38-port.r3/logs`

| Step | Command | EXIT | Evidence |
| --- | --- | --- | --- |
| GREEN baseline | `python3 tests/VideoClipsNoFfmpegCheck.py --source-root "$PWD" --build-dir "$PWD/build" --moc /usr/lib/qt6/moc` | `0` | `test-7-noffmpeg-direct-green-final.log`: probe PASS, negative control FAIL as expected, both TUs PASS |
| RED, pre-fix header | `git show 9feb23eb1b^:tests/src/core/VideoTestClip.h > tests/src/core/VideoTestClip.h` then the same runner | `1` | `test-8-noffmpeg-red-prefix-final.log`: `'makeClip' is not a member of 'videotest'` |
| Restore HEAD header | `git show HEAD:tests/src/core/VideoTestClip.h > tests/src/core/VideoTestClip.h` | `0` | `git status --porcelain` shows only `tests/VideoClipsNoFfmpegCheck.py` |
| GREEN, ctest | `cd build/tests && ctest -R '^VideoClipsNoFfmpegCheck$' --output-on-failure` | `0` | `test-9-ctest-noffmpeg-green-final.log`: 1/1 passed |
| GREEN, video family | `cd build/tests && ctest -R '^(VideoClipsTest\|VideoTestClipPortabilityTest\|VideoClipsGuiTest\|VideoClipsNoFfmpegCheck)$' --output-on-failure` | `0` | `test-10-ctest-video-family-green-final.log`: 4/4 passed |
| RED, ctest, pre-fix header | pre-fix header swap then `cd build/tests && ctest -R '^VideoClipsNoFfmpegCheck$' --output-on-failure` | `8` | `test-11-ctest-noffmpeg-red-prefix-final.log`: `0% tests passed, 1 tests failed` |

Proof artifact: `regression-test tests/VideoClipsNoFfmpegCheck.py` red_exit `1`
(`test-8-noffmpeg-red-prefix-final.log`), green_exit `0`
(`test-7-noffmpeg-direct-green-final.log`).

## Build and test commands

`L` is the log folder above.

| Command | EXIT | Evidence |
| --- | --- | --- |
| `cmake --build build --target zene VideoClipsTest VideoClipsGuiTest VideoTestClipPortabilityTest -j4` | `0` | `build-1-focused.log`: `Built target ...` for every target |
| `python3 tests/VideoClipsNoFfmpegCheck.py --source-root "$PWD" --build-dir "$PWD/build" --moc /usr/lib/qt6/moc` | `0` | `test-7-noffmpeg-direct-green-final.log` |
| `cd build/tests && ctest -R '^VideoClipsNoFfmpegCheck$' --output-on-failure` | `0` | `test-9-ctest-noffmpeg-green-final.log` |
| `cd build/tests && ctest -R '^(VideoClipsTest\|VideoTestClipPortabilityTest\|VideoClipsGuiTest\|VideoClipsNoFfmpegCheck)$' --output-on-failure` | `0` | `test-10-ctest-video-family-green-final.log` |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `0` | `gates-1.log`: all 8 rows PASS, `RESULT: PASS` |

## Test matrix

| Path | Configuration | Result |
| --- | --- | --- |
| Reported compile path | `ZENE_HAVE_FFMPEG` undefined, `VideoClipsTest.cpp` | PASS: `test-7` / `test-9` |
| Sibling compile path | `ZENE_HAVE_FFMPEG` undefined, `VideoClipsGuiTest.cpp` | PASS: `test-7` |
| Unguarded probe | forced no-FFmpeg `lmmsconfig.h`, all three `makeClip` spellings | PASS: `test-7` |
| Negative control | fallback removed from `VideoTestClip.h`, same probe | FAIL as required: `test-7` |
| Pre-fix header | `9feb23eb1b^`, no FFmpeg | FAIL with the original `makeClip` error: `test-8` / `test-11` |
| Reconfiguration path | forced shadow `lmmsconfig.h` with `#undef ZENE_HAVE_FFMPEG` | PASS: `test-7` |
| Audio thread | no product or audio code touched | no allocation, lock, wait or I/O added |
| Platforms | probe reuses the recorded compiler flags and spellings (`/Zs`, `/I`) | no new OS-only API |

## Self-check

- **Regression fails without the change and passes with it:** the extended
  `tests/VideoClipsNoFfmpegCheck.py` exits `1` against the pre-fix header
  (`test-8`) and `0` against HEAD (`test-7`). The negative control in the same runner
  removes the fallback and exits non-zero (`test-7`), so the probe itself detects the
  defect rather than passing vacuously.
- **Deterministic:** the probe compiles fixed source against an injected
  `lmmsconfig.h` that `#undef`s `ZENE_HAVE_FFMPEG`, and the negative control is a
  deterministic textual copy with the fallback removed. There is no clock, sleep, race or
  network dependency.
- **At the cause and complete:** both formerly-unguarded callers are guarded
  (`VideoClipsTest.cpp:529`, `VideoClipsGuiTest.cpp:336`), the fixture declares `makeClip`
  in both preprocessor branches (`VideoTestClip.h:238-247`), and every other caller listed
  above is already guarded or FFmpeg-only. The new probe covers all three call spellings.
- **No collateral damage:** the FFmpeg branch is unchanged; the fallback returns `false`
  only where the callers already skip. No project, saved-data, undo, command or realtime
  code is touched.
- **ALREADY-FIXED evidence:** `VideoTestClip.h:238-247` (fallback declaration), its
  introduction in ancestor commit `9feb23eb1b`, and the guarded callers at
  `VideoClipsTest.cpp:529-538` and `VideoClipsGuiTest.cpp:336-345`.
- **Sibling commands:** this job changes no command file. `tests/VideoClipsNoFfmpegCheck.py`
  is a test harness and does not register or alter a control command.
- **Registration:** `tests/CMakeLists.txt:9800-9804` still registers the check and
  `tests/fork-sources.txt:877` still lists the test file.

## Other findings

- `cmake --build build` without a target is refused by the lane guard, as in the previous
  round; named targets are the allowed form and were used above.
- `ctest -R '^Train48Portability$'` in this existing build tree still fails its unrelated
  SFZ mode because the ad-hoc compile cannot find `SfzPlayerTest.moc`. Its video mode is
  covered by the new probe here. I did not change it.
