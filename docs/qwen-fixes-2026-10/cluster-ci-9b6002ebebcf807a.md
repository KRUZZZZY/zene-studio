FIXED

# cluster ci-9b6002ebebcf807a — macOS arm64: VideoClipsTest referenced an FFmpeg-only fixture helper unguarded

## Verdict and classification

**REAL — test portability defect (not product runtime, not infrastructure).**

The hosted `macos-arm64` job at the pinned train-33 commit `cc29a63dde09cf28bb00498a2d2a5b124d4d614d`
builds with `ZENE_WANT_FFMPEG=ON` but no FFmpeg development files, so `lmmsconfig.h` does **not** define
`ZENE_HAVE_FFMPEG`. `tests/src/core/VideoTestClip.h:50` opens the FFmpeg-only section and line 229 declares
`videotest::makeClip` inside it. `tests/src/core/VideoClipsTest.cpp:530` (pre-fix) called that helper with no
guard, so the translation unit did not compile in exactly the CI configuration:

```
tests/src/core/VideoClipsTest.cpp:530:19: error: no member named 'makeClip' in namespace 'videotest'
tests/src/core/VideoClipsTest.cpp:530:46: error: no member named 'makeClip' in namespace 'videotest'
```

The checkout under repair is the exact pinned base (HEAD `cc29a63dde`, branch
`bugs/qwen-ci-9b6002ebebcf807a`) and still had the unguarded call, so this reproduced rather than being
accepted as already fixed. The local lane build has FFmpeg (`build/lmmsconfig.h:42` defines
`ZENE_HAVE_FFMPEG`), which is why an ordinary native build does not expose the defect; the regression
probe below compiles the **real** translation unit with the FFmpeg define removed.

## Reproduction

Reproduced on the exact commit with `tests/no-ffmpeg-video-clips-compile-check.py`, which takes the real
`VideoClipsTest.cpp` compile line from `build/compile_commands.json` and prepends a shadow `lmmsconfig.h`
that has `#define ZENE_HAVE_FFMPEG` turned into `/* #undef ZENE_HAVE_FFMPEG */`. Its two controls prove the
shadow configuration is the no-FFmpeg one before it compiles the test:

```
PASS: the shadow configuration does not define ZENE_HAVE_FFMPEG
PASS: videotest::makeClip is not declared without ZENE_HAVE_FFMPEG
FAIL: tests/src/core/VideoClipsTest.cpp does not compile without FFmpeg:
  .../tests/src/core/VideoClipsTest.cpp:530:33: error: ‘makeClip’ is not a member of ‘videotest’
  .../tests/src/core/VideoClipsTest.cpp:530:60: error: ‘makeClip’ is not a member of ‘videotest’
```

That is the same source line and the same missing declaration as the hosted diagnostic. Exit **1**
(`test-1-red-probe.log`).

## Fix

`tests/src/core/VideoClipsTest.cpp:524-538`: `needClips()` now guards its fixture construction with
`#ifdef ZENE_HAVE_FFMPEG`, matching the already-established sibling pattern in
`tests/src/core/VideoTrackTest.cpp:300-304` (runtime `VideoDecoder::available()` check, then the helper call
inside the define). In a build without FFmpeg the function returns `false`, so every slot keeps its existing
`QSKIP("no FFmpeg in this build, or the test clips could not be made")` behaviour
(`VideoClipsTest.cpp:230,277,314,333,361,375,411,432,459,493`). No product code, save format, command, undo
or audio path changes; the fixture-dependent assertions are untouched.

All `videotest::makeClip` call sites were audited after the change:

* `tests/src/core/VideoTrackTest.cpp:302` — inside `#ifdef ZENE_HAVE_FFMPEG` (pre-existing).
* `tests/src/core/VideoTrackTestParts1.h:234` — inside `#ifdef ZENE_HAVE_FFMPEG` at line 233.
* `tests/src/core/VideoFfmpegTest.cpp:69,98` — slots are wrapped in `#ifdef ZENE_HAVE_FFMPEG`.
* `tests/src/core/VideoTestClipTest.cpp:110` — slot is wrapped in `#ifndef`/`#ifdef ZENE_HAVE_FFMPEG`.
* `tests/src/core/VideoClipsTest.cpp:531` — now guarded by this fix.

The only remaining unguarded `videotest::` uses in `VideoClipsTest.cpp` are `Width`, `Height`, `Fps` and
`Palette`, which are declared before the FFmpeg conditional (`VideoTestClip.h:36-48`) precisely so the
shape/palette constants are known in every build.

## Regression test

New `tests/no-ffmpeg-video-clips-compile-check.py`, registered as CTest
`NoFfmpegVideoClipsCompileCheck` (`tests/CMakeLists.txt:6237-6252`, guarded on `PYTHON3_EXECUTABLE` and
`CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang"`). It:

* reads this build's own `compile_commands.json` and recompiles the **actual** `tests/src/core/VideoClipsTest.cpp`
  with a shadow `lmmsconfig.h` copied from `build/lmmsconfig.h` minus `ZENE_HAVE_FFMPEG`;
* first proves the shadow really lacks FFmpeg, and that `videotest::makeClip` really is undeclared there
  (a check that cannot fail fails);
* is deterministic: it compiles a fixed header configuration and checks compiler exit status and the
  `makeClip` diagnostic; no clock, sleep, race or network.

Test matrix:

| Path | Evidence |
|---|---|
| no-FFmpeg compile (the failing CI configuration) | RED on pre-fix tree, `makeClip` at line 530; GREEN after fix (`test-1`, `test-2`) |
| FFmpeg build, normal fixture path | `VideoClipsTest` builds and all 10 clip slots run: 12 passed, 0 failed, 0 skipped (`test-7`) |
| FFmpeg build, sibling video tests | `VideoTrackTest`, `VideoFfmpegTest`, `VideoTestClipTest` all pass (`test-4`, 5/5 with the probe) |
| unavailable-FFmpeg runtime path | unchanged: `needClips()` returns `false` after `VideoDecoder::available()`, so the existing `QSKIP` lines still fire; compile path is exercised by the probe |
| boundary / invalid input / failure paths | untouched by this fix; `VideoClipsTest`'s empty-range, junk-attach, stale-frame and undo cases all still pass (`test-7`) |
| portability | no platform branch added; the guard is the same one the sibling `VideoTrackTest` uses, and the probe only registers on GCC/Clang-style command lines |

## Commands and exit codes

The instruction's canonical log folder
(`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-9b6002ebebcf807a.r1/logs`) returned `EACCES` even
for `touch` (owner/mode were correct; the harness denies `$HOME` outside the worktree), so the same
numbered files were written to the writable run-log store the harness exposes:
`/home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/ci-9b6002ebebcf807a.r1-logs/`.

| # | Command | Exit |
|---|---|---|
| 1 | `cmake --build build --target VideoClipsTest -j20` (baseline, FFmpeg on) | **0** |
| 2 | `python3 tests/no-ffmpeg-video-clips-compile-check.py --source-root . --build-dir build` (unfixed) | **1** (RED: `makeClip` at line 530) |
| 3 | `python3 tests/no-ffmpeg-video-clips-compile-check.py --source-root . --build-dir build` (fixed) | **0** (GREEN) |
| 4 | `cmake --build build --target VideoClipsTest -j20` (fixed) | **0** |
| 5 | `cmake --build build --target VideoTrackTest VideoTestClipTest VideoFfmpegTest -j20` | **0** |
| 6 | `cd build/tests && ctest -R "NoFfmpegVideoClipsCompileCheck\|VideoClipsTest\|VideoTrackTest\|VideoTestClipTest\|VideoFfmpegTest" --output-on-failure` | **0**, 5/5 passed |
| 7 | `git diff -- tests/src/core/VideoClipsTest.cpp > .../fix.patch` | **0** |
| 8 | `git apply -R .../fix.patch` (revert the fix) | **0** |
| 9 | `cmake --build build --target VideoClipsTest -j20` (reverted) | **0** |
| 10 | `cd build/tests && ctest -R NoFfmpegVideoClipsCompileCheck --output-on-failure` (reverted) | **8**, 0/1 — RED at line 530 |
| 11 | `git apply .../fix.patch` (restore) | **0** |
| 12 | `cmake --build build --target VideoClipsTest -j20` (restored) | **0** |
| 13 | `cd build/tests && ctest -R "NoFfmpegVideoClipsCompileCheck\|VideoClipsTest" --output-on-failure` (restored) | **0**, 2/2 passed |
| 14 | `QT_QPA_PLATFORM=offscreen ./build/tests/VideoClipsTest` | **0**, 12 passed, 0 failed, 0 skipped |
| 15 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0**, RESULT: PASS |

The reverse/restore proof uses `tests/src/core/VideoClipsTest.cpp` because the defect is in the test
translation unit itself; there is no product-file hunk to reverse. The probe compiles whatever that file
contains, so it is red on the reverted tree (row 10) and green on the fixed tree (rows 3 and 13).

Gate table (`gates-1.log`):

```
PASS  fork-sources
PASS  all-sources-reproduce
PASS  no-upstream-regression
PASS  check-namespace
PASS  no-tautology
PASS  doc-figures
PASS  complexity              no new item in your files
PASS  file-length             no new item in your files
RESULT: PASS
```

## No collateral damage

* Only the test's fixture-construction guard changed; no assertion, tolerance, timeout or gate was weakened.
* `VideoClipsTest`'s 10 clip/undo/save/playback/render slots still run and pass in the FFmpeg build with the
  same assertions (12 passed including init/cleanup).
* A no-FFmpeg build still compiles the test and reaches the pre-existing `QSKIP` path; nothing is silently
  treated as passing.
* No product code, project format, undo stack, command surface or audio-thread path is touched. No new
  control command was added.
* Sibling video tests were rebuilt and run unchanged (5/5 with the probe).

## Independent QA

An independent, read-only QA pass (separate agent, no worktree edits, no cmake configure/build) verified seven
checks against HEAD `3302fd79f7`; all PASS, `git status --porcelain` empty before and after:

1. **Base bug and HEAD fix:** `git rev-parse HEAD^` is `cc29a63dde`; the base file at line 530 has the
   unguarded `makeClip` call, while HEAD has `#ifdef ZENE_HAVE_FFMPEG` (527), the call (531), `#else`
   `return false;` (535-536) and `#endif` (537).
2. **Header guard:** `VideoTestClip.h:50` opens the FFmpeg conditional, `makeClip` is declared at 228, and
   the conditional closes at 237.
3. **All callers guarded:** the six `videotest::makeClip` sites in `tests/src/core` were inspected; every
   sibling site is inside a `ZENE_HAVE_FFMPEG` guard (some with a no-FFmpeg `QSKIP` branch), and the fixed
   site is inside the new guard. No `using videotest::`/`using namespace videotest` shortcut exists.
4. **Probe is real and deterministic:** the script compiles the tracked `tests/src/core/VideoClipsTest.cpp`
   (blob `15f447d9...` equals `HEAD:...`), not a fixture; the instrumented command ends in that absolute
   path with the shadow `-I` first. The shadow replaces exactly the one `ZENE_HAVE_FFMPEG` define. Two runs
   exited 0; imports contain no time/sleep/thread/random/network (the 300 s subprocess timeout is a hang
   guard only).
5. **Tests:** `ctest -R "NoFfmpegVideoClipsCompileCheck|VideoClipsTest"` exited 0, 2/2 passed; the direct
   binary reported 12 passed, 0 failed, 0 skipped.
6. **Diff scope:** exactly the six expected files, all 0 deletions, no assertion/gate/timeout weakened;
   `git show --check` clean.
7. **Clean tree:** empty `git status --porcelain` before and after the probes.

## Self-check

* **Decision with evidence:** FIXED. The pinned HEAD still had the unguarded call at
  `tests/src/core/VideoClipsTest.cpp:530`; `VideoTestClip.h:50,229` shows `makeClip` is declared only under
  `ZENE_HAVE_FFMPEG`; the probe reproduces the hosted diagnostic exactly.
* **Regression fails without the fix and passes with it:** yes — rows 2/3 and 8-13. Reverted: ctest EXIT 8
  (0/1, `makeClip` at line 530). Restored: ctest EXIT 0 (2/2), family EXIT 0 (5/5).
* **Deterministic:** yes. The probe compiles fixed source with a fixed shadow header; no timing, sleep,
  race or network.
* **Cause and completeness:** yes. The one unguarded caller is fixed, and every `videotest::makeClip` caller
  was audited; the other four are already inside `ZENE_HAVE_FFMPEG`.
* **No collateral damage:** yes, as summarised above; FFmpeg-supported behaviour and no-FFmpeg skip
  behaviour are both preserved.
* **Registration and gates:** `tests/fork-sources.txt` names the new probe and this report;
  `tests/upstream-modifications.txt` records the `tests/CMakeLists.txt` registration; the unchanged gate
  suite is PASS.
