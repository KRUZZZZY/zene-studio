ALREADY-FIXED

# Cluster ci-6422986f4db9676f-port — Port: CI regression: build / linux-arm64

The finding is only a title. Its parent cluster, `ci-6422986f4db9676f`, is recorded in
`docs/qwen-fixes-2026-10/cluster-ci-6422986f4db9676f.md`: the hosted `linux-arm64` job
configures without FFmpeg (Ubuntu's arm64 dependency list has no `libav*-dev`), while the
always-built video test sources referenced `videotest::makeClip`, which was declared only
inside `#ifdef ZENE_HAVE_FFMPEG`. That is a test-build compile error; no runtime
`VideoDecoder::available()` check can prevent it.

This port unit asks whether the accepted fix is on this branch (train 52). It is, with its
regression, so no product source changes here. What this round adds is the compile-level
red/green the reviewer asked for: the defect is reproduced at the real cause (a call site
that cannot see the helper), not at the local test seam, plus one additive assertion in the
regression, committed alone.

## Evidence on this branch (file:line)

- `tests/src/core/VideoTestClip.h:51` — the fixture is selected by
  `#if defined(ZENE_HAVE_FFMPEG) && !defined(ZENE_TEST_VIDEO_CLIP_WITHOUT_FFMPEG)`.
- `tests/src/core/VideoTestClip.h:238-249` — the `#else` branch declares
  `inline bool makeClip(const QString&, Palette = Palette::RedBlue) { return false; }`
  (declaration at `:243`) in every configuration, so every caller links and declines
  cleanly; each caller's own availability check turns that into a skip.
- `tests/src/core/VideoTestClip.h:249` — `#endif // ZENE_HAVE_FFMPEG`. Removing the
  `#else` block (lines 238-248) restores the pre-fix header, which is the RED setup below.
- `tests/src/core/VideoClipsTest.cpp:529` and `tests/src/gui/VideoClipsGuiTest.cpp:336` —
  the two previously unguarded call sites, now behind `#ifdef ZENE_HAVE_FFMPEG` with a
  `#else return false;`.
- `tests/src/core/VideoTestClipPortabilityTest.cpp:52-79` — the deterministic regression. It
  defines `ZENE_TEST_VIDEO_CLIP_WITHOUT_FFMPEG`, asserts the helper exists, refuses a real
  path without creating a file, keeps the fixture constants, and (added this round,
  `:71-78`) asserts `makeClip` returns `bool` by `static_assert` and is callable both with
  one argument and with an explicit `Palette`, both spellings returning `false`.
- `tests/CMakeLists.txt:1237` — the regression source is registered;
  `tests/fork-sources.txt:891` and `tests/all-sources.txt:3360` list it too.
- `tests/CMakeLists.txt:6566-6573` (`Train48Portability`) and `:9795-9805`
  (`NoFfmpegVideoClipsCompileCheck`, `VideoClipsNoFfmpegCheck`) — the CI-shaped compile
  probes for the same no-FFmpeg configuration, registered as ctest entries 1031 and 1032.
- The fix commit `d9a26d06ad` is an ancestor of HEAD (`git merge-base --is-ancestor`, EXIT 0,
  `018-state-and-base.log`), and the branch now also changes the regression
  (`git diff --stat 050/m0-train-33..HEAD` = 93 insertions: the original 82 plus 11 from the
  additive assertion).

Sibling call sites were walked on this tree: `tests/src/core/VideoTrackTest.cpp:301-302` and
`tests/src/core/VideoTrackTestParts1.h:233-236` guard or `QSKIP`;
`tests/src/core/VideoTestClipTest.cpp:35,110` and
`tests/src/core/VideoFfmpegTest.cpp:33,69` are guarded per translation unit; and
`tests/src/gui/ArrangerOverlaysVideoLaneTest.cpp:402-404` sits inside its file's
`#ifdef ZENE_HAVE_FFMPEG` slot.

## Commands and EXIT codes

`LOGS=/home/kruzzzzy/.config/kads/zene/state/runs/ci-6422986f4db9676f-port.r3/logs`.
Every command ran from the worktree unless the row says `build/tests`. The folder was first
verified writable with a one-line probe (`000-write-probe.log`, EXIT 0).

| # | Command | EXIT | Log |
| --- | --- | ---: | --- |
| 1 | `git merge-base --is-ancestor d9a26d06ad HEAD; git diff --stat 050/m0-train-33..HEAD -- tests/src/core/VideoTestClipPortabilityTest.cpp` | 0, 0 (+82 before this round's assertion, +93 after) | `018-state-and-base.log` |
| 2 | `cmake --build build --target VideoTestClipPortabilityTest VideoClipsTest VideoClipsGuiTest -j4` | 0 | `001-build-dependent-targets.log` |
| 3 | `python3 tests/no-ffmpeg-video-clips-compile-check.py --source-root $PWD --build-dir $PWD/build` (baseline) | 0, 4 PASS lines | `002-probe-baseline.log` |
| 4 | **RED setup**: `git checkout cc29a63dde -- tests/src/core/VideoClipsTest.cpp tests/src/gui/VideoClipsGuiTest.cpp tests/src/core/VideoTestClip.h` (pre-fix call sites, no `#else` fallback) | 0 | — |
| 5 | same probe as row 3 | **1** — `FAIL: tests/src/core/VideoClipsTest.cpp does not compile without FFmpeg: ...VideoClipsTest.cpp:530:33: error: ‘makeClip’ is not a member of ‘videotest’` | `003-probe-red-real-cause.log` |
| 6 | `git checkout HEAD -- <the same three paths>` (restore) | 0, `git status --porcelain` empty | — |
| 7 | `cd build/tests && ctest -N -R 'NoFfmpeg\|Portability\|VideoTestClip'` | 0 | `004-ctest-list.log` |
| 8 | same probe as row 3 (GREEN) | 0, 4 PASS lines | `005-probe-green.log` |
| 9 | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^(NoFfmpegVideoClipsCompileCheck\|VideoClipsNoFfmpegCheck)$' --output-on-failure` | 0 (2/2) | `006-ctest-noffmpeg-checks.log` |
| 10 | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^VideoTestClipPortabilityTest$' --output-on-failure` | 0 (1/1) | `007-ctest-portability.log` |
| 11 | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^(VideoClipsTest\|VideoClipsGuiTest\|VideoTrackTest)$' --output-on-failure` | 8 — `VideoTrackTest` **Not Run**: its binary was not built in this tree; the other two passed | `008-ctest-video-family.log` |
| 12 | **Additive assertion** edited into the regression, then `git add tests/src/core/VideoTestClipPortabilityTest.cpp && git commit -m "test(cluster ci-6422986f4db9676f-port): assert makeClip returns bool for both palette spellings"` | 0 (commit `1dd178c25b`) | — |
| 13 | `cmake --build build --target VideoTestClipPortabilityTest VideoTrackTest -j4` | 0 | `009-build-additive-assertion.log` |
| 14 | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^(VideoTestClipPortabilityTest\|VideoClipsTest\|VideoClipsGuiTest\|VideoTrackTest\|NoFfmpegVideoClipsCompileCheck\|VideoClipsNoFfmpegCheck)$' --output-on-failure` | 0 (6/6) | `010-ctest-family-after-additive.log` |
| 15 | `cmake --build build --target VideoTestClipPortabilityTest VideoClipsTest VideoClipsGuiTest -j4` at `1dd178c25b` | 0 | `011-build-at-final-commit.log` |
| 16 | RED at the final commit: rows 4 and 5 repeated | **1** — same `VideoClipsTest.cpp:530` error; restored afterwards, `git status --porcelain` empty | `012-probe-red-at-final-commit.log` |
| 17 | GREEN at the final commit: rows 8 and 14 repeated | 0 and 0 (6/6) | `013-probe-green-at-final-commit.log`, `014-ctest-green-at-final-commit.log` |
| 18 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 — 8/8 PASS, `RESULT: PASS` | `015-feature-check.log` |
| 19 | Fixture-level RED for the regression binary: rows 4 then `cmake --build build --target VideoTestClipPortabilityTest -j4` (0) then `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^VideoTestClipPortabilityTest$' --output-on-failure` | **8** — `FAIL! : VideoTestClipPortabilityTest::makeClipIsDeclaredWithoutFfmpegAndRefuses() '!videotest::makeClip(path)' returned FALSE. Loc: [../../tests/src/core/VideoTestClipPortabilityTest.cpp(57)]` | `019-red-build-reverted-fixture.log`, `020-red-regression-run.log` |
| 20 | rows 6, then the same build (0) and the same ctest at HEAD `70f6a4ae6c` | 0 (1/1) | `021-green-build-restored-fixture.log`, `022-green-regression-run.log` |
| 21 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> ci-6422986f4db9676f-port` | 0 — `RESULT: PASS - submit`, every mechanical row PASS | `023-submit-check.log` |

Two notes on readings that are not code failures. Row 11's EXIT 8 is a binary that was never
built in this configured tree (`Not Run`), cleared by building it in row 13. Row 5's message
is GCC 16's spelling (`‘makeClip’ is not a member of ‘videotest’`) of the clang text quoted in
the review note (`no member named makeClip in namespace videotest`); the compiler recorded in
`build/compile_commands.json` for this TU is `/usr/bin/c++` (GCC 16.2.1), and the failing line
is the call site, `VideoClipsTest.cpp:530`.

## Self-check

- **Regression fails without the fix and passes with it.** The fix is already on the branch, so
  it was reverted at its cause, not at the local seam: the pre-fix call sites and the header
  without the `#else` fallback (row 4) make the compile probe exit 1 at
  `VideoClipsTest.cpp:530` (row 5). Restored (row 6), the same probe exits 0 (row 8), and the
  regression binary passes (row 10). Repeated identically at the final commit (rows 16, 17).
- **Deterministic.** The test uses only the `ZENE_TEST_VIDEO_CLIP_WITHOUT_FFMPEG` preprocessing
  seam, `QTemporaryDir::filePath` and a compile-time `static_assert`; no clock, sleep, thread,
  network or file-system race. The probes compile a fixed shadow `lmmsconfig.h` from
  `compile_commands.json`.
- **Cause and completeness.** The fallback in `VideoTestClip.h:238-249` covers every
  configuration; every `videotest::makeClip` caller was walked (list above), and the two
  previously unguarded ones now carry the same `#ifdef` / `#else return false;` shape as
  `VideoTrackTest`.
- **No collateral damage.** The real FFmpeg implementation is untouched; the fallback is
  selected only when FFmpeg is absent or by the test seam. `VideoClipsTest`,
  `VideoClipsGuiTest`, `VideoTrackTest` and both no-FFmpeg compile checks pass on this
  FFmpeg-enabled tree (rows 14, 17). No save/load format, undo path, control command or
  audio-thread code is touched — the diff is one test file.
- **ALREADY-FIXED evidence.** `tests/src/core/VideoTestClip.h:243` (fallback declaration,
  the code reference in the proof manifest) with the seam at `:51`, and the regression at
  `tests/src/core/VideoTestClipPortabilityTest.cpp:52-79` registered at
  `tests/CMakeLists.txt:1237`; fix commit `d9a26d06ad` is an ancestor of HEAD (row 1).
- **Sibling commands (submit_check ASK: `app.*`, `arrangement.fit_tracks`,
  `arrangement.get_state`, …).** Those ids are listed because `FIX-BASE` is `050/m0-train-35`
  and `git merge-base HEAD 050/m0-train-35` is `fa4a5f2e7`; the diff from that base to HEAD
  therefore contains the train commits this branch inherited, including
  `src/core/ControlCommands*.cpp` and `include/ControlRegistry*.h`. This branch's own four
  commits touch only `tests/src/core/VideoTestClipPortabilityTest.cpp` and this report
  (`git show --name-only` for `1dd178c25b`, `70f6a4ae6c`, `cb9b4896e7`, `7569963a79`). No
  command, argument or reversibility class was added or changed here, so every listed sibling
  keeps its existing behaviour and the `ControlCommandsSnapshot` canary is unchanged.
- **Registration and gates.** The regression was already registered in `tests/CMakeLists.txt`,
  `tests/fork-sources.txt`, `tests/all-sources.txt` and `tests/upstream-modifications.txt`; no
  new file was added this round. `feature_check.py` reports 8/8 PASS (row 18). No new function
  and no file over 500 lines.
- **Platform.** This host is Linux x86_64 with FFmpeg enabled; the arm64 no-FFmpeg
  configuration is reproduced by the shadow-configuration compile probes. A hosted
  `linux-arm64` run of this branch was not available in this session, so the train's CI is the
  first end-to-end arm64 execution.

## Other findings

- The configured build tree had no `VideoTrackTest` binary, which made the first family run
  report `Not Run` (row 11). Building the target cleared it; not a product defect.
- GCC 16 and clang word the missing-member diagnostic differently; the review note quotes the
  clang form. Both are the same defect at the same call site.

## Proof manifest

`.../ci-6422986f4db9676f-port.r3/logs/proof.json` — unit `ci-6422986f4db9676f-port`, worktree
this tree, commit the final HEAD of this branch, claim: the branch already carries the
no-FFmpeg fixture fix and its deterministic regression, re-verified red/green this session.
Artifacts:

- `test` — `tests/src/core/VideoTestClipPortabilityTest.cpp`, command
  `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^VideoTestClipPortabilityTest$' --output-on-failure`,
  `green_exit` 0 (`022-green-regression-run.log`).
- `regression-test` — same test file, test `makeClipIsDeclaredWithoutFfmpegAndRefuses`,
  `red_exit` 8 (`020-red-regression-run.log`) with `green_exit` 0
  (`022-green-regression-run.log`), plus the compile-level red at the real call site,
  `VideoClipsTest.cpp:530`, exit 1 (`003-probe-red-real-cause.log`,
  `012-probe-red-at-final-commit.log`).
- `code-reference` — `tests/src/core/VideoTestClip.h:243`, the fallback declaration that keeps
  the helper visible in every configuration.

