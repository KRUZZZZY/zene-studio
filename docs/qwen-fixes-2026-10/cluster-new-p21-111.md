Verdict: FALSE-POSITIVE

new-p21-111: `captureCreatedTrack` dereferences null track/container without a guard

## Finding

Luna's claim: `captureCreatedTrack(Track*, TrackContainer*)` at `src/core/ControlTrackCreation.cpp:113` calls `track->saveState(...)` with no null check, and the success path keeps raw `Track*`/`TrackContainer*` in `CreatedTrack` across the undo boundary.

Both parts fail on the code as it stands on `bugs/qwen-new-p21-111` (base `cf3618e61`). No product file is changed by this lane.

## Evidence 1: no caller can pass a null track

`captureCreatedTrack` is reached only through `recordCreatedTrack` (`ControlTrackCreation.cpp:108-111`) and `recordCreatedTracks` callers. Every call site either guards the pointer or dereferences the same track before recording it:

| Call site | Track dereferenced or guarded before capture |
| --- | --- |
| `src/core/ControlCommandsRecorder.cpp:106-112` | `track->setName` at :107; `Track::create(Type::Sample, ...)` at :106 |
| `src/core/ControlCommandsVideo.cpp:173-176` | `track->setName` at :174 |
| `src/core/ControlCommandsAudioAnalysis.cpp:98-118` | `track->setName` at :99, `track->createClip` at :100 |
| `src/core/ControlCommandsAudioSlice.cpp:256-259` | `if (track == nullptr) { return error; }` at :257 |
| `src/core/ControlCommandsBrowserMedia.cpp:321-323` | `if (track == nullptr) { return error; }` at :322 |
| `src/core/ControlCommandsClipImportFile.cpp:230-231, 294` | `if (track == nullptr) { return nullptr; }` at :231; `recordImport` records only when `created`, and `track` is non-null there (:318-319) |
| `src/gui/editors/TrackContainerView.cpp:627` | `copy != nullptr &&` |
| `src/gui/editors/PatternEditor.cpp:185-210` | `t` is dereferenced at :190 before capture at :209 (see Observation) |

The Instrument, Sample and Automation types passed to `Track::create(Type, ...)` at these sites never return null: `src/core/Track.cpp:483-500` returns null only from `default: break;` for types outside this list.

## Evidence 2: the raw-pointer claim is wrong for `Track*`, and the container pointer is safe

- `CreatedTrack` is a capture-time value. `recordCreatedTracks` (`ControlTrackCreation.cpp:122-131`) copies only `c.track->id()`, `c.saved` and `c.container` into `Recorded` (`:94-99`). No `Track*` is captured by the undo lambdas (`:133-151`). The id rule the header cites is already followed (`:125-127`, `trackWithId` at `:154-158`).
- The one raw pointer that survives the undo boundary is `TrackContainer* container`. Its only values are the Song and the PatternStore (`TrackContainerView.cpp:627`, `PatternEditor.cpp:160-163, 209`). Both are created once in `Engine::init` (`src/core/Engine.cpp:72` for `s_song`, `:74` for `s_patternStore`) and deleted only at shutdown (`Engine.cpp:110`, `:123`). They therefore outlive every undo step, so this pointer cannot dangle.

## What was not changed

No product code change. The finding's crash path (a null track reaching `captureCreatedTrack`) is not reachable from any caller in the tree. Adding a silent empty-return guard would hide a caller bug rather than fix one. The finding's pointer concern is not a defect for the same reason. The header comment already describes the capture-time contract (`include/ControlTrackCreation.h:49-52`).

## Observation (out of this lane, not fixed)

`src/gui/editors/PatternEditor.cpp:186-190`: `Track::create(element, model())` can return nullptr. `Track::create(const QDomElement&, ...)` returns nullptr when the `type` attribute is missing or out of range (`src/core/Track.cpp:555-566`). The next line, `t->getClips()` at :190, dereferences it with no check, so this crashes before `captureCreatedTrack` runs. By then `firstPatternTrack` (`PatternEditor.cpp:156-164, 185`) has already added the Song's pattern track with no undo step. Reachable only with a malformed `track_` drop payload; NOT reproduced by running it. Suggest a separate cluster.

## Tests

No regression test added. The reported crash is not reachable, so a test that passes null would exercise a contract no caller uses, and a test that passes both with and without a guard is not regression proof. The covering family named in the brief is `AafImportTest` (`ctest -R '^AafImportTest$'`); it was not built or run in this lane because nothing product-side changed (`build/tests/AafImportTest` does not exist in this worktree).

## Commands and EXIT codes

Run from the worktree root unless noted. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-111.r1/logs/`.

| # | Command | EXIT | Log |
| --- | --- | --- | --- |
| 1 | `git status --short` (before any work) | 0 (empty) | (none) |
| 2 | `python3 .../qwen/feature_check.py <worktree>` (stock script) | 1 | `feature-check-1.log` |
| 3 | `python3 .../qwen/submit_check.py <worktree> new-p21-111` (pre-commit) | 1 | `submit-check-1.log` |
| 4 | Stock `feature_check.py` blocked: it creates `<worktree>-feature-check` next to the worktree (`PermissionError`), outside the write-jail. The gate logic was run unchanged from a scratch copy in `$TMPDIR` with two lines redirected: `ROOT` pinned to the qwen folder and the log folder moved into the run folder. | 0 | `feature-check-2.log` (all 8 rows PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

Gate row 4 is the gate with the deviation. Its output is the required PASS/FAIL table, but it was produced by the redirected copy, not the unmodified script.

## Self-check

- **Commits on the branch:** the only commit is this report; it is the sole change in a FALSE-POSITIVE lane.
- **Working tree clean:** yes after the commit (see the final `git status`).
- **Report exists with Self-check:** yes.
- **Tests added or changed:** none, by design (see Tests). This item fails honestly.
- **Gates:** PASS in row 4 with the redirected copy. The stock script exits 1 only because of the write-jail path, not a gate failure.
- **ASK 1 (regression RED/GREEN):** not applicable; no product change, so there is nothing for a regression to prove.
- **ASK 2 (deterministic test):** not applicable; no test added.
- **ASK 3 (fix at the cause, every caller checked):** no fix. Every caller was checked (Evidence 1).
- **ASK 4 (no collateral damage):** nothing changed; no limit or check was added.
- **ASK 5 (FALSE-POSITIVE evidence):** `ControlTrackCreation.cpp:108-151`, the call-site table above, `Track.cpp:483-500`, and `Engine.cpp:72-74, 110, 123`.
