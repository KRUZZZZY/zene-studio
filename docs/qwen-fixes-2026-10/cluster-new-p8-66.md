FIXED

# cluster new-p8-66 — audio extraction could use the attached video as its output file

## What the finding said

`video.extract_audio` took an explicit `out` path without checking whether it names the
attached video's own file, so the extraction could replace the file it was reading.

## Verdict: REAL, fixed at the cause

- `src/core/ControlCommandsVideo.cpp:331-341` — `extractAudio()` reads `out`, resolves it with
  `QFileInfo(out).absoluteFilePath()` and hands it to `extractVideoAudio(video.path, wav, ...)`
  with no comparison against `video.path`. The background path (`:310`) does the same on the
  worker. Line 233 in the report is historical; the code is at 331-341 today.
- `src/core/VideoAudioExtract.cpp:256-278` — `extract()` opened the sink on that path
  (`sink.openFile(wav, ...)`, a `QSaveFile`) while `media.open(video, ...)` was reading the same
  file, and `sink.commit()` then renamed the finished WAV over it. The source is replaced with a
  WAV. The regression run below shows the damage: after an unfixed extraction the file no longer
  opens as a video (`video.attach` refuses it).
- `BounceInPlace::namesTheSameFile` (`include/BounceInPlace.h:168`,
  `src/core/BounceInPlace.cpp:299-326`) already resolves both names and compares device and inode,
  so a symlink, a hard link and a `./` spelling are one file. The base tree is
  `9913d36d5397025d1b7092097f50f6d50c652dc4` (the train snapshot), where that helper exists, so the
  fix calls it instead of inlining a second copy of the test.

## The fix

`src/core/VideoAudioExtract.cpp:256-262` — the first statement of `extract()`, before any buffer,
decoder or sink is opened:

```cpp
	if (BounceInPlace::namesTheSameFile(video, wav)) { return QObject::tr("'%1' would overwrite its own source").arg(wav); }
```

plus `#include "BounceInPlace.h"`. Both product callers reach the sink through `extract()`
(`src/core/ControlCommandsVideo.cpp:310` on the worker, `:341` on the calling thread) — verified
by grep: those two lines are the only product calls to `extractVideoAudio`. The direct call
returns `ControlErrorKind::Refused` with that message (`:341`), and the background call fails the
task with it (`landExtracted` → `TaskCenter::finish(..., TaskState::Failed, why)`), so nothing is
recorded and no track lands on either path. No second check was added in `extractAudio()`, and
`defaultAudioPath()` is untouched: the default name is `<stem>-audio.wav` next to the project, and
`firstFreeAudioPath` never hands out the clip's own name. The claim `firstFreeAudioPath` takes is
still released when the extraction returns (`:289`), refusal included.

Not on the audio thread: the check runs on the control thread or on the `QThreadPool` worker,
two `QFileInfo`s and two `stat` calls, no allocation on any audio path.

## Regression test

`tests/src/core/VideoTrackTest.cpp` (declarations, `taskStatus()` helper) and
`tests/src/core/VideoTrackTestParts2.h` (`refuseExtractionOverItsOwnSource()` + four slots):
`extractAudioRefusesItsOwnSource`, `extractAudioRefusesADotSegmentSpellingOfItsOwnSource`,
`extractAudioRefusesASymlinkToItsOwnSource`, `extractAudioRefusesAHardLinkToItsOwnSource`.

Every case copies the shared test clip first (`QFile::copy(m_clip, m_dir.filePath("alias-*.mkv"))`)
and uses the copy as both source and output, so an unfixed run damages only the copy. Each slot
asserts, sync and background: `ControlErrorKind::Refused` (sync) with a message containing
`its own source`, the task `Failed` with that status (background), the copy byte-identical
afterwards, the copy still readable as a video (`video.attach` succeeds), and no new track.
Determinism: no sleeps — the background case waits on `taskState(task)` with
`QTRY_COMPARE_WITH_TIMEOUT`, and the refusal happens before any decode, so the state is reached
at once. Windows: the symlink slot `QSKIP`s outside `Q_OS_UNIX` (Qt makes a shortcut there) and
the hard-link slot uses `::link` under `Q_OS_UNIX`.

## Commands and exit codes (all logs under `/home/kruzzzzy/.config/kads/zene/state/runs/new-p8-66.r7/logs`)

| # | command | exit |
|---|---------|------|
| 1 | `cmake --build build --target zene VideoTrackTest -j4` (`build-1.log`) — the fix alone, before any test was written | 0 |
| 2 | `cmake --build build --target zene VideoTrackTest -j4` (`build-2.log`) — with the new slots | 0 |
| 3 | `ctest -R VideoTrackTest --output-on-failure` from `build/tests` (`test-1-green.log`) | 0 |
| 4 | `./VideoTrackTest -v1` (`test-2-green-verbose.log`) — `Totals: 34 passed, 0 failed, 1 skipped` | 0 |
| 5 | `git diff c581750e91^ c581750e91 -- src/core/VideoAudioExtract.cpp > .../fix.patch` (`fix.patch`, 23 lines) | 0 |
| 6 | `git apply -R .../fix.patch` — product change reversed, tests kept | 0 |
| 7 | `cmake --build build --target zene VideoTrackTest -j4` (`build-3-red.log`) | 0 |
| 8 | `ctest -R VideoTrackTest --output-on-failure` (`test-3-red.log`) — RED: `Totals: 30 passed, 4 failed, 1 skipped`, `0% tests passed, 1 tests failed out of 1`, the four `extractAudioRefuses*` slots only | 8 |
| 9 | `git apply .../fix.patch` — fix restored | 0 |
| 10 | `cmake --build build --target zene VideoTrackTest -j4` (`build-4-green.log`) | 0 |
| 11 | `ctest -R VideoTrackTest --output-on-failure` (`test-4-green.log`) — `100% tests passed out of 1` | 0 |
| 12 | `cmake --build build --target VideoClipsTest VideoAudioNameTest VideoFfmpegTest -j4` (`build-5-neighbours.log`) and `--target VideoTestClipTest` (`build-6-clip.log`) — the neighbours of the extraction | 0, 0 |
| 13 | `ctest -R '^(VideoTrackTest\|VideoClipsTest\|VideoAudioNameTest\|VideoFfmpegTest\|VideoTestClipTest)$' --output-on-failure` (`test-6-family.log`) — `100% tests passed out of 5` | 0 |

The RED run's four failures are exactly the new slots, and three of them also show the data loss
the fix prevents:

```
FAIL!  : VideoTrackTest::extractAudioRefusesItsOwnSource() Compared values are not the same
FAIL!  : VideoTrackTest::extractAudioRefusesItsOwnSource() 'run(QStringLiteral("video.attach"), {{QStringLiteral("path"), copy}}).ok' returned FALSE. ()
FAIL!  : VideoTrackTest::extractAudioRefusesADotSegmentSpellingOfItsOwnSource() Compared values are not the same
   ... returned FALSE. ()
FAIL!  : VideoTrackTest::extractAudioRefusesASymlinkToItsOwnSource() ... returned FALSE. ()
FAIL!  : VideoTrackTest::extractAudioRefusesAHardLinkToItsOwnSource() Compared values are not the same
```

(`video.attach` failing on the alias copy afterwards is the "the source is no longer a readable
video" assertion: the unfixed extraction had replaced it with a WAV.)

## Gates

`python3 .../qwen/feature_check.py <worktree>` could not run in this session: it computes the base
listings by checking out the base commit in `qwen/gate-baselines/tree`, and that tree is on the
read-only `zene-bunny-fix` mount, while `git checkout` is refused by the lane guard:

```
bunny-guard: 'git -C /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/gate-baselines/tree checkout -q --detach 9913d36d5397025d1b7092097f50f6d50c652dc4' is not allowed in a Bunny lane
...
subprocess.CalledProcessError: Command '[...]' returned non-zero exit status 97.
feature_check.py EXIT=1 (`feature-check-1.log`)
```

`touch qwen/gate-baselines/probe-write` → `touch: ... Read-only file system`, and `mount` shows
`... on /home/kruzzzzy/.local/share/zene-bunny-fix type btrfs (ro,nosuid,nodev,noatime,...)`; only
`bunny/locks`, `bunny/build-pool.lock`, `bunny/make-jobserver.fifo` and `dsh/home/ccache-tmp-r6`
are mounted read-write. So I ran the same gates it runs, one by one
(`run-all-gates.sh`, summary in `final-gates-summary.log`):

| gate | log | exit |
|------|-----|------|
| fork-sources | `final-fork-sources-gate.sh.log` | 0 |
| all-sources-reproduce | `final-all-sources-reproduce.sh.log` | 0 |
| no-upstream-regression | `final-no-upstream-regression-gate.sh.log` | 0 |
| check-namespace | `final-check-namespace.log` | 0 |
| no-tautology | `final-no-tautology-gate.sh.log` | 0 |
| doc-figures | `final-doc-figures.log` | 0 |
| file-length | `final-file-length.log` | 0 |
| complexity | `final-complexity.log` | 2 — `lizard is not installed — run: pip install lizard`, no `REGRESSION` line at all |

No `REGRESSION` line names `src/core/VideoAudioExtract.cpp`, `tests/src/core/VideoTrackTest.cpp`
or `tests/src/core/VideoTrackTestParts2.h`, which is the condition `feature_check.py` tests for its
two ratchet rows (an item is only "new" when it is listed in a file the branch changed).

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it.**
   Reversed only the product hunk (`git apply -R fix.patch`, step 6, exit 0): `ctest -R VideoTrackTest`
   exit 8, `Totals: 30 passed, 4 failed` — the four new slots (`test-3-red.log`). Restored
   (`git apply fix.patch`, step 9, exit 0), rebuilt (step 10, exit 0): exit 0, 34 passed
   (`test-4-green.log`).
2. **The test is deterministic.** No sleeps, no wall-clock checks. The four slots use fixed alias
   paths; the background cases wait on the task state (`QTRY_COMPARE_WITH_TIMEOUT(taskState(task),
   TaskState::Failed, 20000)`), and the refusal precedes the first decode, so the state is reached
   without a race. A run without the fix is what makes the state race — that is the failing state.
3. **The fix is at the cause and complete.** `extractVideoAudio` has exactly two product callers
   (`ControlCommandsVideo.cpp:310` worker, `:341` direct), both inside `extract()`, so one check
   covers both; the direct caller turns the error into `ControlErrorKind::Refused`, the background
   one into a `Failed` task with the same text. `firstFreeAudioPath()` and `defaultAudioPath()` are
   unchanged, and the path claim is still released on every return (`VideoAudioExtract.cpp:289`).
   Every sibling of the command (`video.attach`, `video.clip_add`, `video.clip_remove`,
   `video.detach`, `video.get_state`, `video.set_offset`, `video.render`) is untouched; none of
   them writes a WAV over a source.
4. **No collateral damage.** The refusal only fires when the two names resolve to one file
   (same canonical path, or same device and inode). A path that does not exist yet — every normal
   extraction, including the default `<video>-audio.wav` — fails `stat`, so `namesTheSameFile`
   returns false and the extraction proceeds exactly as before. Project data, the journal and the
   saved-file format are not touched: this is a refusal before any file is written, so nothing new
   is persisted. The whole `VideoTrackTest` suite (34 slots, the extraction, project round-trip,
   window and lane cases) passes with the fix, and so does the video family that shares the code
   (`VideoClipsTest`, `VideoFfmpegTest`, `VideoTestClipTest`, `VideoAudioNameTest`: 5/5, exit 0 in
   `test-6-family.log`).
5. **ALREADY-FIXED / FALSE-POSITIVE:** not claimed; the verdict is FIXED.
6. **Undo:** no new control command and no new undo step. A refused extraction records nothing, so
   `control.undo` still removes exactly one landed track (the existing slot
   `extractAudioLandsTheSoundOnANewTrackInOneStep` still passes).
7. **Realtime:** the check runs on the control thread or the thread-pool worker, never on the audio
   thread; no allocation, lock, wait or signal is added to any audio-path function.
8. **Portability:** no Qt-6-only API; `::link` and the symlink case are under `Q_OS_UNIX`, the
   `_WIN32` branch inside `namesTheSameFile` is the one already in the tree.

## Other findings

- `qwen/feature_check.py` cannot complete in a lane session: its baseline cache
  (`qwen/gate-baselines`) is on a read-only mount and its `git checkout` of the base commit is
  refused by the lane guard. Every lane's `submit_check.py` "gates" row inherits the failure unless
  it runs outside the sandbox.
- The complexity gate cannot run here at all (`lizard is not installed`), so its row is only
  meaningful for the absence of `REGRESSION` lines.
