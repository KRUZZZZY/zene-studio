FIXED

# Cluster 5390: a rejected `<z:lanes>` section modified tracks before refusing

**Verdict:** REAL. `loadLanesSection()` claimed the lane section all-or-nothing but filled
each track's `TakeLaneModel` while it was still validating. A valid `<z:tracklanes>` group
followed by a group naming a track the file does not contain returned `false` with the
earlier groups already applied, and the walk then preserved the whole element verbatim as
unclaimed. The next save wrote the half-loaded models as the writer's own `<z:lanes>`
beside the preserved original — two sections, one of them invented from the file it had
just refused.

**Product change:** `src/core/Song.cpp` — `loadLanesSection()` resolves every group (name
and track id) into a locally collected list before the first model is written, then applies
them in the writer's order. One file, no API or header change, no caller change.

## Evidence

| # | Claim | Source | What it shows |
|---|---|---|---|
| E1 | The loader mutated while validating | `src/core/Song.cpp:2847` (`loadLanesSection`, RED state) | the group loop called `track->takeLanes().loadSettings( group )` and `synchronizeTakePlayback()` inside the same loop whose later iterations `return false` |
| E2 | A false answer preserves the section verbatim | `src/core/Song.cpp:2223` (`captureUnclaimed` in `Song::loadProject`'s walk), `src/core/Song.cpp:2606` (`reemitUnclaimed` in the save path) | unclaimed elements are captured as XML and re-appended to `<song>` on every save, so a half-load is written *beside* its own original |
| E3 | The writer emits the models as a second section | `src/core/Song.cpp:2402` (`writeLanesSection`) | a track whose model `hasPersistentState()` gets a `<z:tracklanes>` group in the writer's own `<z:lanes>` |
| E4 | The intended contract is all-or-nothing | `docs/COMPING.md:160-162`; `src/core/Song.cpp:2829-2841` (function comment) | "`<z:lanes>` is claimed … only in the exact shape this writer produces — anything else is preserved verbatim as unclaimed" |
| E5 | Reset-on-absence makes "untouched" observable | `src/core/Track.cpp:949` (`m_takeLanes.clear()` in `Track::loadTrack`) | after a load the model starts empty, so a lane present afterwards came from the refused section |
| E6 | Measured RED (before any product change) | `logs/test-2-red.log` | `FAIL! : LanesUpconvertTest::aRejectedLanesSectionAppliesNothing() … the refused section applied track A's group before meeting the unknown track: the model holds 2 lane(s), not 0` |
| E7 | Measured RED with the fix reversed by patch | `logs/test-6-red-reversed.log` | same assertion, same values, after `git apply -R logs/fix.patch` (EXIT=0) |
| E8 | Measured GREEN with the fix | `logs/test-3-green.log`, `logs/test-7-green-restored.log` | `LanesUpconvertTest … Passed`, `100% tests passed out of 1` |

## The fix

`src/core/Song.cpp:2847-2878`. Pass one walks the section's children and resolves each
`<z:tracklanes>` to its `Track` (`song.findTrackById`), collecting `{group, track}` pairs;
every failure (`v` not `"1"`, a foreign child, an unparsable or unknown track id) returns
`false` there, before any model is touched. Pass two (`src/core/Song.cpp:2871-2874`) applies
the collected pairs in document order with the same two calls as before
(`TakeLaneModel::loadSettings`, `synchronizeTakePlayback`). Nothing after pass one can
refuse: `TakeLaneModel::loadSettings` takes any element
(`src/core/TakeLanePersistence.cpp:146`).

Callers and siblings checked: `loadLanesSection` has exactly one caller
(`src/core/Song.cpp:2950`, the `z:lanes` branch of `restoreNamedSection`), unchanged. The
other two lane readers are element-scoped and cannot half-apply across tracks:
`Track::restoreState`'s `<takelanes>` payload (`src/core/Track.cpp:969`) and the journal
checkpoint, which restores through that same `Track::restoreState`. The sibling
claim-or-preserve section reader `SessionModel::restoreScenesSection`
(`src/core/SessionModel.cpp:551`) already validates into a candidate before committing, so
the convention the fix follows is the one the neighbouring section established.

## Test matrix

| Case | Input | Result | Evidence |
|---|---|---|---|
| Failure path (the bug) | saved two-track project, plus a planted `<z:tracklanes track="1000000">` after the valid groups | no track keeps a lane; the re-save carries exactly one `<z:lanes>` — the preserved original, its third group still naming track 1000000 | `logs/test-2-red.log` (RED), `logs/test-3-green.log` (GREEN) |
| Normal path | the same project without the planted group | both groups load: track A 2 lanes + 1 segment, track B 1 lane | same test, control block at the end; also `LanesUpconvertTest::lanesRoundTripThroughTheTopLevelSectionAndResavesIdentically` |
| Exhaustion | n/a | no limit, budget or count is involved: the fix adds no ceiling, so nothing new can be exhausted; a section with any number of groups is resolved into a vector sized by the file | `src/core/Song.cpp:2850-2868` |
| Reconfiguration | n/a | no setting, flag or platform gate in scope — one code path for every build | n/a |
| Audio thread | untouched | the changed code runs in `Song::loadProject` on the loading thread; it allocates a `QVector` of pairs, adds no atomic, lock, wait or log, and no per-period path reaches it | `src/core/Song.cpp:2847-2878`, `src/core/Track.cpp:949-951` (the existing `requestChangesGuard` around the track restore is unchanged) |
| Old projects | unchanged | a section in the exact writer shape with every track present takes the same two calls in the same order; anything else already took no model write | `LanesUpconvertTest` (all 5 assertions families) plus `ScenesUpconvertTest`, `TakeLaneCompTest`, `UnclaimedElementsTest` — `logs/test-5-siblings.log` |
| Supported platforms | unchanged | no Qt-version-specific API added; `QVector`, `QDomElement` handles and aggregate init are Qt 5 and Qt 6 | build log of the tree's GCC 16.2.1 / Qt 6.12.0 configuration |

## Commands and exit codes

Product files changed: `src/core/Song.cpp`. Test file changed:
`tests/src/core/LanesUpconvertTest.cpp` (new case `aRejectedLanesSectionAppliesNothing`,
line 259; claim 5 in the file header).

| Step | Command | Exit |
|---|---|---|
| RED build | `cmake --build build --target LanesUpconvertTest -j4` → `logs/build-1.log` | 0 |
| RED test | `cd build/tests && ctest -R LanesUpconvertTest --output-on-failure` → `logs/test-1-red.log` | 8 (fixture assertion, see note) |
| RED build 2 | `cmake --build build --target LanesUpconvertTest -j4` → `logs/build-2.log` | 0 |
| RED test 2 | `ctest -R LanesUpconvertTest --output-on-failure` → `logs/test-2-red.log` | 8 (the bug) |
| GREEN build | `cmake --build build --target zene LanesUpconvertTest -j4` → `logs/build-3-green.log` | 0 |
| GREEN test | `ctest -R LanesUpconvertTest --output-on-failure` → `logs/test-3-green.log` | 0 |
| Sibling build | `cmake --build build --target ScenesUpconvertTest TakeLaneCompTest UnclaimedElementsTest -j4` → `logs/build-4-siblings.log` | 0 |
| Sibling tests | `ctest -R "ScenesUpconvertTest\|TakeLaneCompTest\|UnclaimedElementsTest" --output-on-failure` → `logs/test-5-siblings.log` | 0 |
| Patch out | `git diff -- src/core/Song.cpp > logs/fix.patch` | 0 |
| Patch out | `git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/5390.r1/logs/fix.patch` | 0 |
| Reversed build | `cmake --build build --target LanesUpconvertTest -j4` → `logs/build-5-reversed.log` | 0 |
| Reversed test | `ctest -R LanesUpconvertTest --output-on-failure` → `logs/test-6-red-reversed.log` | 8 (fails without the fix) |
| Patch in | `git apply /home/kruzzzzy/.config/kads/zene/state/runs/5390.r1/logs/fix.patch` | 0 |
| Restored build | `cmake --build build --target zene LanesUpconvertTest -j4` → `logs/build-6-restored.log` | 0 |
| Restored test | `ctest -R LanesUpconvertTest --output-on-failure` → `logs/test-7-green-restored.log` | 0 |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` → `logs/gates-1.log` | 0 (`RESULT: PASS`, 8/8 rows PASS) |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> 5390` → `logs/submit-1.log` | see `## Self-check` |

Note on `logs/test-1-red.log`: that run failed on the fixture's own precondition
(`good.count("<z:tracklanes ")` was 3, not 2) because the test inherited the previous test's
lane-carrying track from the process-wide song. The fixture was made order-independent (it
loads a lane-free document first) and the run repeated; `logs/test-2-red.log` is the RED
evidence and fails on the defect itself.

## Self-check

Answers to every item the completion check reports as `?`, with the evidence for each.

- **Does the test fail without the fix?** Yes, twice, on the same assertion:
  `logs/test-2-red.log` (never-fixed tree) and `logs/test-6-red-reversed.log` (fix removed by
  `git apply -R`, EXIT=0). Both read
  `'rejectedA->takeLanes().laneCount() == 0' returned FALSE … the model holds 2 lane(s), not 0`.
- **Is the test deterministic?** Yes. It writes project files into a `QTemporaryDir`, drives
  `Song::loadProject`/`saveProjectFile` synchronously, and asserts values only. No sleep, no
  wall clock, no thread, no ordering assumption about other tests (it loads a one-track,
  lane-free document before building its fixtures).
- **Does the fix change behaviour outside the bug?** No: an in-shape section with every named
  track present takes the same two calls in the same order. `LanesUpconvertTest` (5 slots) and
  the three sibling tests in `logs/test-5-siblings.log` pass with the fix.
- **Registration.** No new product file, so `tests/fork-sources.txt` is untouched.
  `src/core/Song.cpp` is an inherited file and is registered in
  `tests/upstream-modifications.txt` (line 962 and following); the new reason line for this
  change sits beside them in the same commit. No header changed.
- **Control commands.** None added or touched — `ControlCommandsSnapshot` is not affected.
- **Undo.** No undo step is created or changed: the loader is not a command path.

## Other findings

- `cmake --build` in this worktree queues behind the shared build guard for several minutes
  (measured 5-30 min per invocation); every build above was run with a background process and
  a long wait, not with a short shell timeout.
- This build tree has only a subset of the test targets built (58 tests match
  `-R "Lanes|TakeLane|Comp|Scenes|Session"`, most report `Not Run` because their executables
  are absent from `build/tests`); the runnable, related family was built and run explicitly
  (row "Sibling tests" above).
