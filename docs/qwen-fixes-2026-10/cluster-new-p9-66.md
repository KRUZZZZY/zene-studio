FIXED

# cluster new-p9-66 - BUG 8: the default audio filename can be reused and overwrite an existing WAV

Branch `bugs/qwen-new-p9-66`, base `639e1d23e` (`050/m0-train-17`). Two commits: `c7f4cc01b` (behaviour-preserving
extraction of the naming search, so the regression can be reverted cleanly) and the fix commit that carries this report.

## Verdict and evidence

**FIXED.** The finding is real on the base tree.

Before the fix, the default-name loop (`src/core/ControlCommandsVideo.cpp:163` on the base) was
`for (int n = 2; QFileInfo::exists(candidate) && n < MaxAudioNames; ++n)`. Its condition runs after `n` is bumped, so
when the range runs out the loop exits with `candidate` set to the last name (`-99.wav`) **without checking that it
exists**, and `extractAudio` then decodes into it. With the same range, a free `-3.wav` was also never reached
(`-2.wav` came back even when it was taken). The caller (`extractAudio`, `ControlCommandsVideo.cpp:230-247`) had no
refusal path, so an existing WAV was written over.

Fix (now):

- `src/core/VideoAudioExtract.cpp:227-236` - `firstFreeAudioPath(dir, stem, maxNames)`: tries `<stem>.wav`, then
  `<stem>-2.wav` … `<stem>-<maxNames>.wav`, returns the first name no file takes, and **returns an empty string when
  every name is taken** (`if (n > maxNames) { return QString(); }`). `maxNames` is the existing limit of 100 - not raised,
  and not applied to the explicit `out` path.
- `src/core/ControlCommandsVideo.cpp:156-163` - `defaultAudioPath` delegates to it (the only caller).
- `src/core/ControlCommandsVideo.cpp:234-238` - `extractAudio` refuses with `ControlErrorKind::Refused` when the default
  name is empty. This check sits **before** the background branch and before the decode, so nothing is decoded,
  recorded or put on a track (no undo step is created).
- `include/VideoAudioExtract.h:62-66` - declaration (with `#include <QDir>`).

## Regression test

`tests/src/core/VideoAudioNameTest.cpp` (5 cases, temporary folder, no FFmpeg, no clock, no engine):

| case | what it pins | base code |
| --- | --- | --- |
| `noNameTakenGivesTheStem` | the stem is used when free | pass |
| `takenNamesAreSkipped` | `-2` taken → `-3` (range 100) | pass |
| `lastNameInRangeIsTried` | range 3, `-2` taken, `-3` free → `-3` | **FAIL** (returns `-2`) |
| `everyNameTakenGivesNoName` | range 3, all taken → empty | **FAIL** (returns existing `-2`) |
| `defaultRangeExhaustedGivesNoName` | range 100, all taken → empty | **FAIL** (returns existing `-99`) |

Registered in `tests/CMakeLists.txt` (LMMS_TESTS, after `VideoTestClipTest`) and `tests/fork-sources.txt` (sorted
entry before `VideoFfmpegTest.cpp`).

## Commands run (EXIT codes as seen)

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-66.r1/logs/`.

Environment notes (not product changes):
- A first build failed with `EXIT=2`: ccache's temp dir (outside the write-jail) was denied. Rebuilt with
  `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp` (`build-1-seam.log` EXIT=2, `build-2-seam.log` EXIT=0).
- `cmake -S . -B build` is refused by the lane guard (`configure-1.log`, EXIT=97). A targeted `zene` build made CMake
  regenerate the tree (`build-4-regen.log` EXIT=0), after which the new test target existed.
- `feature_check.py` writes its logs to `<projects>/zene-qwen-new-p9-66-feature-check`, outside the jail. Its first run
  died with `PermissionError` (`gates-1-feature-check.log`, EXIT=1). The gates were then run through
  `runs/new-p9-66.r1/gate_wrapper.py`, which calls the same `feature_check` functions and commands and redirects only
  the log folder (`gates-1-feature-check.table.log`, EXIT=0).

| step | command (cwd = worktree unless noted) | EXIT |
| --- | --- | --- |
| refactor build | `CCACHE_TEMPDIR=… cmake --build build --target zene -j4` | 0 |
| RED build | `cmake --build build --target zene VideoAudioNameTest` (after regen) | 0 |
| **RED run** (fix not applied) | `cd build/tests && ctest -R VideoAudioNameTest --output-on-failure` → `test-1-red.log` | **8** (3 failed, 4 passed) |
| GREEN build | `cmake --build build --target zene VideoAudioNameTest -j4` | 0 |
| **GREEN run** (fix applied) | `cd build/tests && ctest -R VideoAudioNameTest --output-on-failure` → `test-2-green.log` | **0** (100% of 1 test) |
| 4b save fix | `git diff -- src/core/VideoAudioExtract.cpp src/core/ControlCommandsVideo.cpp > fix.patch` | 0 |
| 4b revert | `git apply -R fix.patch`; rebuild `zene VideoAudioNameTest`; ctest → `test-3-proof-reverted.log` | 0; 0; **8** |
| 4b restore | `git apply fix.patch`; rebuild; ctest → `test-4-proof-restored.log` | 0; 0; **0** |
| family | build `VideoTrackTest VideoFfmpegTest VideoTestClipTest ArrangerOverlaysVideoLaneTest`; `ctest -R Video` → `test-5-family.log` | 0; **0** (100% of 5 tests) |
| gates | `python3 -I gate_wrapper.py <worktree>` → `gates-1-feature-check.table.log` | **0** (RESULT: PASS) |

Reverted-state assertions (`test-3-proof-reverted.log`):
- `lastNameInRangeIsTried`: `Compared values are not the same` - actual `clip-audio-2.wav`, expected `clip-audio-3.wav`
  (`VideoAudioNameTest.cpp:82`).
- `everyNameTakenGivesNoName`: `firstFreeAudioPath(...).isEmpty()` returned FALSE (`VideoAudioNameTest.cpp:94`).
- `defaultRangeExhaustedGivesNoName`: `...isEmpty()` returned FALSE (`VideoAudioNameTest.cpp:105`).

Gate table (`gates-1-feature-check.table.log`):

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

## Evidence table

- Affected-family tests executed: `ctest -R Video` = **5 of 5 passed** (`VideoAudioNameTest`, `VideoTrackTest`,
  `VideoFfmpegTest`, `VideoTestClipTest`, `ArrangerOverlaysVideoLaneTest`). `VideoTrackTest` includes the changed header,
  so it was rebuilt.
- RED: exit 8 (3 assertions fail). GREEN: exit 0. Reverted proof: exit 8. Restored proof: exit 0.
- Gates: exit 0 (through the wrapper; the direct run is exit 1 for the log-path reason above).
- Files committed (explicit paths): `src/core/ControlCommandsVideo.cpp`, `src/core/VideoAudioExtract.cpp`,
  `tests/src/core/VideoAudioNameTest.cpp`, `tests/CMakeLists.txt`, `tests/fork-sources.txt`,
  `docs/qwen-fixes-2026-10/cluster-new-p9-66.md`. (`include/VideoAudioExtract.h` and the refactor are in `c7f4cc01b`.)
- Not run: the full `ctest` tree and the golden/control socket Python programmes. The end-to-end `video.extract_audio`
  command needs FFmpeg and an attached video; it was verified by reading the caller, not by running the command.

## Self-check

1. **Each new test fails with the product change reverted, passes restored.** Yes: reverted, exit 8, three assertions
   (above); restored, exit 0.
2. **Acceptance criteria.** This is a fix report, not a feature record, so there is no acceptance table. The finding's
   two allowed behaviours are both covered: refuse when no unused name is left (`everyNameTakenGivesNoName`,
   `defaultRangeExhaustedGivesNoName`), and otherwise keep searching for a genuinely unused path (`takenNamesAreSkipped`,
   `lastNameInRangeIsTried`). The stem case is `noNameTakenGivesTheStem`.
3. **Reversibility / undo.** No command added or changed; `ControlCommandsSnapshot` untouched. The refusal happens before
   `landAudio`/`recordCreatedTrack`, so a refused call records no undo step. A successful call is unchanged (one step).
4. **Audio thread.** Nothing on the audio thread changed. The naming and the refusal run in the control command, before
   the decode (background or synchronous); `extractVideoAudio` and its writer are unchanged.
5. **Old projects and saved data.** No project format or saved field changed. Projects with an existing
   `<video>-audio.wav` now get `-2.wav` (as before, when that was free); a folder whose 100 default names are all taken now gets a
   refusal instead of an overwrite. The explicit `out` path is not affected by the limit or the refusal.
6. **Portability.** Only `QDir`, `QFileInfo`, `QString` and `QStringLiteral` are used, all Qt 5.15-compatible; no
   platform guard needed.
7. **Siblings.** `defaultAudioPath` has one caller (`extractAudio`). In `ControlCommandsVideo.cpp` the other `video.*`
   commands (`attach`, `detach`, `set_offset`, `get_state`) do not touch the naming and are unchanged. The background
   path takes its `wav` from `extractAudio`, so it gets the same refusal.
8. **Residual risk (not fixed here, outside the finding's trigger).** The default name is checked when the request
   arrives, but the WAV only appears when the decode commits (`src/core/VideoAudioExtract.cpp:60`, `QSaveFile`, and
   `:115` commit). Two `video.extract_audio {background:true}` requests sent before the first finishes both get
   `<video>-audio.wav`, and the second overwrites the first's output. Fixing that needs an in-flight reservation of
   names on the main thread, which is a lifecycle change beyond this bug; recommend a follow-up cluster. The explicit `out`
   path still replaces an existing file by design (`include/VideoAudioExtract.h`, "success replaces it").
9. **Submit check.** Run after the commit; the result is recorded in the next section.
