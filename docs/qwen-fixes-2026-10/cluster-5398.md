ALREADY-FIXED

# Cluster 5398: the async completion handler could dereference a deleted clip

## Verdict

ALREADY-FIXED on this branch by `9c784aa30d3ff0888654a9d7c8e7286b437feb21`
("fix(gui): snapshot stem split targets (cluster 5398)", 2026-10-08), which is an ancestor
of HEAD `21c2f3ee2e0d2554f60e9f0c1beeb2a25b37897c`
(`git merge-base --is-ancestor 9c784aa30d HEAD` → EXIT=0).

The finding's line numbers are exact for the pre-fix file, which I read from history:

- `git show 9c784aa30d^:include/StemSplitController.h` line 82 →
  `SampleClip* m_clip = nullptr;` — a raw pointer, no `QPointer`.
- `git show 9c784aa30d^:src/gui/StemSplitController.cpp` line 162 →
  `m_clip = clip;` — stored while the worker ran.
- same old file, line 209 →
  `m_clip->startPosition(), m_clip->name(), &error);` — dereferenced after the job finished.

That was a real use-after-free and could add the four stem tracks to whatever song was current.
The current code no longer has either defect.

## Code on this branch that shows it is fixed

- `include/StemSplitController.h:112-119` — `ClipSnapshot` holds `QPointer<Song> song`,
  `int trackId`, `TimePos position`, `QString name` and a `shared_ptr<const SampleBuffer> source`.
  The position and name are copied at submission, so nothing in the completion path needs the
  `SampleClip` object.
- `include/StemSplitController.h:162-165` — the members kept during a job are
  `QPointer<Song> m_song`, `int m_trackId`, `TimePos m_clipPosition`, `QString m_clipName`.
  There is no `SampleClip*` member anywhere in the header (`rg -n 'm_clip' include/StemSplitController.h`
  finds none).
- `src/gui/StemSplitController.cpp:261-275` — `captureClipSnapshot()` reads the track id,
  start position, name and buffer once, and refuses a clip whose track is not in the current song.
- `src/gui/StemSplitController.cpp:280-284` — `targetStillAvailable()` checks `song.data() ==
  Engine::getSong()` and that the track id still resolves; called before a job starts and again
  in `preparationEnded()` (`:380`).
- `src/gui/StemSplitController.cpp:443-446` — `startJob()` stores only the snapshot values.
- `src/gui/StemSplitController.cpp:513-528` — `completeJob()` validates the snapshotted
  `QPointer<Song>` against `Engine::getSong()` and the track id against `findTrackById()` before
  calling `stem.tracks_create`; the command receives `m_clipPosition` / `m_clipName`, never a clip
  pointer. A deleted clip or a closed/replaced project drops the result.
- `src/gui/StemSplitController.cpp:596-612` — `clearTarget()` / `resetJob()` clear the target
  on completion, failure and cancellation.

Later commit `ef85c1ac92` moved the source-WAV write to workers and kept this contract: the
worker holds the buffer by `shared_ptr` and reports through a `QPointer<StemSplitController>`
(`:343-353`), and `preparationEnded()` re-checks `targetStillAvailable()` (`:380`).

## Existing regression tests (already on the branch, added by the same fix commit)

`tests/src/core/StemSplitPipelineTest.cpp`:

- `controllerUsesClipSnapshotAfterSourceClipIsDeleted()` (line 598): submits a real job for an
  in-memory clip, deletes the clip and creates a replacement at another position, then asserts
  the four created stem tracks carry the **submitted** name and position. With the raw pointer
  this reads the replacement clip and fails.
- `controllerSkipsResultsWhenSourceTrackWasDeleted()` (line 650): deletes the whole source track
  while the job runs and asserts the song's track count is unchanged (the result is dropped).

## Red/green proof performed in this session (step 4b)

The tests need the external Python `onnxruntime` backend. It was missing on this box at first:
`test-1.log`/`test-2.log` show `SKIP ... external-process backend unavailable`, 0 passed,
1 skipped. I installed the module into a scratch directory and re-ran with `PYTHONPATH`; no
product or test file was changed for this:

- `python3 -m pip install --target .../logs/tmp/pydeps onnxruntime` → EXIT=0
  (`logs/pip-install-3.log`; onnxruntime 1.32.0).
- `PYTHONPATH=.../logs/tmp/pydeps ./StemSplitPipelineTest controllerUsesClipSnapshot... controllerSkips...`
  → EXIT=0, `QINFO ... backend: external-process (python onnxruntime)`, 4 passed
  (`logs/test-3.log`).

Then I reinstated the pre-fix behaviour in the two product files only (raw `SampleClip* m_clip`
member set at submission, dereferenced for position/name in `completeJob`, and the target
validity check removed), saved that diff as `logs/bug-reintroduced.patch`, rebuilt, and ran:

- `cmake --build build --target StemSplitPipelineTest -j4` → BUILD_EXIT=0 (`logs/build-2-red.log`)
- `... ./StemSplitPipelineTest controllerUsesClipSnapshot... controllerSkips...`
  → EXIT=1, `FAIL! ... controllerUsesClipSnapshotAfterSourceClipIsDeleted()
  'stemTrack->name().startsWith("Submitted source - ")` returned FALSE`,
  3 passed / 1 failed (`logs/test-4-red.log`).
- The clip-deletion test was re-run three times and failed every time: EXIT=1, EXIT=1, EXIT=1
  (`logs/test-4-red-run1.log` … `run3.log`).
- Note: in this experiment `controllerSkipsResultsWhenSourceTrackWasDeleted()` passed even with
  the raw pointer reinstated (the freed values made `stem.tracks_create` refuse); the
  deterministic determinant is the clip-deletion test, which failed 4/4 runs.

Restored the fixed code with `git apply -R logs/bug-reintroduced.patch` → APPLY_REVERSE_EXIT=0,
then rebuilt and ran:

- `cmake --build build --target StemSplitPipelineTest -j4` → BUILD_EXIT=0 (`logs/build-3-green.log`)
- Same two tests, three runs → EXIT=0, 4 passed each (`logs/test-5-green-run1.log` … `run3.log`).
- `git status --short` was empty after the restore.

## Family run and gates

- `ctest -R 'StemSplit' --output-on-failure` from `build/tests` → EXIT=8, because the two sibling
  executables had not been built ("Not Run") (`logs/test-6-family.log`).
- `cmake --build build --target StemSplitMenuTest StemSplitBackgroundTest -j4` → EXIT=0
  (`logs/build-4-siblings.log`).
- `ctest -R 'StemSplit' --output-on-failure` → EXIT=0, 3/3 passed: `StemSplitPipelineTest` 4.87 s,
  `StemSplitMenuTest` 1.25 s, `StemSplitBackgroundTest` 1.35 s (`logs/test-7-family.log`).
- `python3 .../feature_check.py <worktree>` → EXIT=0, `RESULT: PASS`; every gate PASS, including
  complexity and file-length (`no new item in your files`) (`logs/gates-1.log`).

## Test matrix

- Normal path: `testGuiSplitSharesTheRegisteredCommandAndIsOneUndoStep` (`:556`) creates the four
  tracks through the registered command as one undo step; passed in `test-7-family.log`.
- Failure path: source clip deleted but its track alive → snapshot name/position used
  (`:598`); source track deleted → result dropped (`:650`); both pass with the fix and the first
  fails with the raw pointer.
- Exhaustion: not applicable — the controller has no resource counter or bounded pool of its own;
  the temporary WAV is removed on every terminal path (`releaseTemp()`, `:617-624`).
- Reconfiguration: closing/replacing the project is the `song == nullptr || song !=
  Engine::getSong()` branch at `:517-518`; deleting the track is the `findTrackById()` branch on
  the same line. Both use the snapshot, so a stale target cannot receive the tracks.
- Audio thread: this class is GUI glue; nothing in it runs per render period. The worker only
  writes a WAV, and completions are queued to the GUI thread (`:343-353`). No realtime code was
  touched by this session.
- Platform: the tests are registered under `LMMS_HAVE_STEM_SPLIT`
  (`tests/CMakeLists.txt:1779-1793`, properties at `:8713-8719`) and use only portable Qt/C++.

## Self-check

- **Regression test fails with the product change reverted and passes with it (exit codes).**
  This session changed no product code; the fix is the earlier commit `9c784aa30d`. I reverted
  the fix's semantics locally for the duration of the proof: `logs/bug-reintroduced.patch`
  (raw `m_clip` + no target check) → build EXIT=0, test EXIT=1 (4/4 failed runs); after
  `git apply -R` → build EXIT=0, test EXIT=0 (3/3 runs, 4 passed). Logs: `build-2-red.log`,
  `test-4-red*.log`, `build-3-green.log`, `test-5-green-run*.log`.
- **The test is deterministic.** It drives the controller's real poll on a real offline job
  (`ExternalProcessStemSeparator` with the committed 458-byte stub model and
  `tools/stem_split_cli.py`), waits on the `QProgressDialog` it creates with
  `QTRY_VERIFY_WITH_TIMEOUT` (a condition wait, not a fixed sleep), and closes message boxes with
  a `QTimer`. The assertion itself is on the created tracks' names/positions. It failed on every
  red run and passed on every green run. The controller's `setStatusReaderForTest` seam is used
  by the neighbouring label test; this test does not need it.
- **Fix at the cause and complete.** The completion path uses only the snapshot; every sibling
  path was checked: submission (`captureClipSnapshot`, `splitClipToStems`), background
  preparation (`preparationEnded` re-checks `targetStillAvailable`), and the terminal paths
  (`completeJob`/`failJob`/`resetJob`) all clear or validate the same snapshot. No other caller
  of the controller keeps a `SampleClip*`; `addSplitToStemsMenu` already holds a
  `QPointer<SampleClip>` (`:121`) and checks it before calling `splitClipToStems`.
- **No collateral damage.** No code, save format, schema or limit changed. The controller's
  arguments to `stem.tracks_create` are byte-for-byte the same as the fixed implementation's
  snapshot values; old projects are untouched.
- **ALREADY-FIXED evidence.** `include/StemSplitController.h:112-119,162-165` and
  `src/gui/StemSplitController.cpp:261-284,443-446,513-528,596-612` above; the pre-fix
  `m_clip` lines are `9c784aa30d^` header:82 and cpp:162,209.
- **Sibling commands listed by submit_check** (`app.*`, `arrangement.*`, etc.): those files are
  inherited from the branch's merged train work; this session changed no command file and no
  command surface. The fix is a GUI object-lifetime change, so no registered command is affected.
- **Registration:** no product file was added or changed, so `tests/fork-sources.txt` and
  `tests/upstream-modifications.txt` need no entry. The fix commit `9c784aa30d` already added its
  own registration entry.
- **Realtime and undo:** nothing added on the audio thread; the command path is unchanged, so
  undo remains the single `stem.tracks_create` step.

## Other findings

- The box had no Python `onnxruntime`, so every test in `StemSplitPipelineTest` skips unless the
  module is provided (`logs/test-1.log`, `logs/test-2.log`). I used a scratch install at
  `logs/tmp/pydeps` via `PYTHONPATH` to run the proof; the repository is unchanged.
- `controllerSkipsResultsWhenSourceTrackWasDeleted()` is not a reliable red test for the raw
  pointer on its own (it passed with the pointer reinstated in this experiment, because the freed
  values were refused by `stem.tracks_create`). The clip-deletion test is the determinant.
