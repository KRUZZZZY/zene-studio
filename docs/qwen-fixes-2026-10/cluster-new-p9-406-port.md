# Cluster new-p9-406-port

Verdict: **FIXED** (port of the accepted `bugs/qwen-new-p9-406` fix onto `050/m0-train-5`). Both ratchet gates exit 1, but their failure sets are identical to the unmodified base tree (see Gates); they were red before this change.

## Finding

Comp playback silently drops spans after the fixed render capacity. `snapshotComp()` stops recording matching spans at `MaxCompSpans` and skips the rest, so a lane with more than 256 selected spans is silent after span 256.

Verified present on this branch before the fix:

| File | Evidence |
| --- | --- |
| `include/SamplePlayHandle.h` (pre-fix) | `static constexpr int MaxCompSpans = 256;` with no authoring-side bound |
| `src/core/SamplePlayHandle.cpp:320` | `if (segment.laneIndex != lane \|\| m_compSpanCount >= MaxCompSpans) { continue; }` drops spans silently |
| `include/TakeLane.h` (pre-fix) | No `MaxCompositeSpansPerLane`; `canSelect()` only checked the tick range and lane |
| RED run (below) | `canSelect(512, 513, 0)` returned true for the 257th span; `rebuild(0, 513)` accepted 257 base spans |

## Fix (ported by hand from `cb20115c8` + `c7147b85b`, not merged)

The fix follows the option the finding names first: enforce the limit when authoring a comp and refuse, never clamp. The render array stays fixed, and the model constant is now the single source of the bound.

- `include/TakeLane.h`: `MaxCompositeSpansPerLane = 256`; `canRemoveLane()` and `canRebuild()` predicates; `rebuild()` returns -1 when over budget.
- `src/core/TakeLane.cpp`: `canSelect()` judges the composite the selection would leave; `removeLane()` refuses an over-budget result (via `segmentsAfterRemovingLane`); `rebuild()` restores the original composite on refusal. Shared helpers `mergeAdjacentSegments` and `withinPlaybackSpanBudget`. `loadSettings()` is unchanged, so legacy over-budget composites still load (`c7147b85b` behaviour).
- `include/SamplePlayHandle.h`: `MaxCompSpans = TakeLaneModel::MaxCompositeSpansPerLane`. The audio path is untouched.
- `src/core/ControlCommandsComp.cpp`, `ControlCommandsCompEdits.cpp`, `ControlCommandsCompGroup.cpp`: refusals are checked before `addJournalCheckPoint()`, so a refused command leaves no undo step.

Differences from the accepted branch: the accepted fix copied `TakeLaneModel` for its preview (`TakeLaneModel preview = model`). That does not compile here because the model holds a `std::atomic<bool>`, so the preview is now a predicate (`canRemoveLane`, `canRebuild`) that reads without copying the model.

## Tests

- New RED regressions in `tests/src/tracks/TakeLaneCompPlaybackTest.cpp`: `refusesSelectionsBeyondThePlaybackSpanBudget` and `refusesRebuildAndLaneRemovalBeyondThePlaybackSpanBudget`.
- Preservation guard in `tests/src/core/TakeLaneTest.cpp`: `legacyOverBudgetCompositeSurvivesJournalAndFileRoundTrips`. It **passes before and after the fix**, so it is a guard that the refusal does not break loading legacy projects, not RED proof.

## Build and test evidence

Worktree: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-406-port`
Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-406-port.r1/logs`

Note: the first RED build (`build-1-red.log`) failed with EXIT=2 because ccache could not write under `/run/user/1000/ccache-tmp` (permission denied). Every later build sets `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9`. This is an environment fault, not a test result.

| Stage | Command (run from the worktree unless noted) | Exit | Result |
| --- | --- | ---: | --- |
| RED build (env fault) | `cmake --build build --target zene TakeLaneCompPlaybackTest TakeLaneTest -j2` (`build-1-red.log`) | 2 | ccache temp-dir permission; no code built |
| RED build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9 cmake --build build --target zene TakeLaneCompPlaybackTest TakeLaneTest -j2` (`build-2-red.log`) | 0 | built on the original production code |
| RED test | `ctest -R '^(TakeLaneCompPlaybackTest\|TakeLaneTest)$' --output-on-failure` from `build/tests` (`test-1-red.log`) | 8 | 2 failed, 9 passed. `refusesSelectionsBeyondThePlaybackSpanBudget`: `!comp.canSelect(512, 513, 0)` returned FALSE. `refusesRebuildAndLaneRemovalBeyondThePlaybackSpanBudget`: `Compared values are not the same` at `TakeLaneCompPlaybackTest.cpp:341` |
| GREEN build | `CCACHE_TEMPDIR=... cmake --build build --target zene TakeLaneCompPlaybackTest TakeLaneTest -j2` (`build-3-green.log`) | 0 | 450 translation units rebuilt |
| Dependent builds | one `cmake --build build --target <T>` per target, 24 targets listed in `dep-targets.txt` (`build-4-deps.log`) | 0 | all 24 targets exit 0 |
| GREEN test | `ctest -R '^(TakeLaneCompPlaybackTest\|TakeLaneTest)$' --output-on-failure` (`test-2-green.log`) | 0 | 2/2 passed |
| Family build | `cmake --build build --target TakeLanesPanelTest -j2` (`build-5-panel.log`) | 0 | was "Not Run" until built |
| Family test (final) | `ctest -R 'TakeLane\|FrozenTake\|ClipOffsetFade\|ClipFades\|SampleClipWindow\|RangeEdits\|Scenes?Upconvert\|LanesUpconvert\|TrackViewDeferred\|RecordingPass\|W3Engine' --output-on-failure` (`test-7-family-final.log`) | 0 | **16/16 passed** (`ctest -N` count 16) |
| Proof: reverse fix | `git diff -- <6 product files> > logs/fix.patch && git apply -R logs/fix.patch` (`proof-1.log`) | 0 | product fix removed; tests kept |
| Proof: RED build | `CCACHE_TEMPDIR=... cmake --build build --target zene TakeLaneCompPlaybackTest TakeLaneTest -j2` (`build-6-proof-red.log`) | 0 | |
| Proof: RED test | same ctest as RED (`test-5-proof-red.log`) | 8 | same two assertions fail as in `test-1-red.log` |
| Proof: reapply fix | `git apply logs/fix.patch` (`proof-2.log`) | 0 | |
| Proof: GREEN build | `CCACHE_TEMPDIR=... cmake --build build --target zene TakeLaneCompPlaybackTest TakeLaneTest -j2` (`build-7-proof-green.log`) | 0 | |
| Proof: GREEN test | same ctest as GREEN (`test-6-proof-green.log`) | 0 | 2/2 passed |

## Gates

| Gate | Command | Exit (fix) | Exit (base, fix reverted) | Result |
| --- | --- | ---: | ---: | --- |
| Complexity | `bash tests/complexity-gate.sh --check` (`gate-3-final-complexity.log`) | 1 | 1 (`gate-base-complexity.log`) | Red before and after. The 6 regressions are in files this change does not touch (`XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp`). None of the new functions is over CCN 10. The regression set is identical to base. |
| File length | `bash tests/file-length-gate.sh --check` (`gate-4-final-filelength.log`) | 1 | 1 (`gate-base-filelength.log`) | Red before and after. The 14 regressions are the same set as base, including `src/core/TakeLane.cpp`, which was already over 500 code lines on base (520). This change adds 55 code lines to that file (575). |

Both gates were run in `--check` mode, and neither baseline was changed or re-anchored. Base comparison: the full diff was reversed with `git apply -R`, both gates were run, and the diff was reapplied (`full-fix.patch`).

Fixing the file-length regression would need `TakeLane.cpp` split, and that split is larger than a port of this fix. It is left for the owner to decide.

## Register

- `tests/upstream-modifications.txt`: one line appended for `include/SamplePlayHandle.h` (inherited file, reason recorded).
- `include/TakeLane.h`, `src/core/TakeLane.cpp`, `src/core/ControlCommandsComp*.cpp`, `tests/src/core/TakeLaneTest.cpp` are already in `tests/fork-sources.txt`. No new files were created.
- `tests/src/tracks/TakeLaneCompPlaybackTest.cpp` is already in `tests/upstream-modifications.txt`.

## Files committed

- `include/SamplePlayHandle.h`
- `include/TakeLane.h`
- `src/core/TakeLane.cpp`
- `src/core/ControlCommandsComp.cpp`
- `src/core/ControlCommandsCompEdits.cpp`
- `src/core/ControlCommandsCompGroup.cpp`
- `tests/src/core/TakeLaneTest.cpp`
- `tests/src/tracks/TakeLaneCompPlaybackTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-406-port.md`

## Not done

- No control commands were added, so the `ControlCommandsSnapshot` canary is not moved.
- Tools: the `fff` tools were not used in this session; log checks used `rg` against the log folder.
