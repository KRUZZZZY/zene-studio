Verdict: FIXED

# Cluster new-p10-62 - Scene tempo overrides accept invalid extremes

## Decision: REAL

`session.set_scene` wrote any number into a scene's tempo override. Nothing checked it against the engine's tempo range, so `tempo: 0` and `tempo: 1e30` were stored, saved to the project and reported back as a live override.

Evidence (line numbers are on the fixed tree):

- The handler wrote the value with no check: `applySceneOverrides` at [src/core/ControlCommandsSession.cpp:213](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-62/src/core/ControlCommandsSession.cpp:213) and [:214](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-62/src/core/ControlCommandsSession.cpp:214) (the pre-fix code, unchanged apart from the new check).
- The schema is unbounded: `{"tempo", numberProperty()}` at [src/core/ControlCommandsSession.cpp:419](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-62/src/core/ControlCommandsSession.cpp:419). `numberProperty()` carries no range (include/ControlVocabulary.h:62).
- The engine's contract is `MinTempo = 10`, `MaxTempo = 999` ([include/Song.h:78](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-62/include/Song.h:78), [:80](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-62/include/Song.h:80)). Its siblings already enforce it at the schema: `transport.set_tempo` ([src/core/ControlCommandsTransport.cpp:280](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-62/src/core/ControlCommandsTransport.cpp:280)), `clock` ([src/core/ControlCommandsClock.cpp:254](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-62/src/core/ControlCommandsClock.cpp:254)) and project info ([src/core/ControlCommandsProjectInfo.cpp:209](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-62/src/core/ControlCommandsProjectInfo.cpp:209)) all use `integerProperty(MinTempo, MaxTempo)`.
- The value persists: `Scene::saveState` writes `tempo` when the override is on ([src/core/SessionClip.cpp:242](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-62/src/core/SessionClip.cpp:242)-[:251](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-62/src/core/SessionClip.cpp:251)), and `Scene::restoreState` reads it back with no check ([src/core/SessionClip.cpp:266](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-62/src/core/SessionClip.cpp:266)-[:274](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-62/src/core/SessionClip.cpp:274)).

Scope of the timing harm, stated plainly: in this tree nothing applies a scene's tempo to the transport. `scene.tempo()` and `scene.tempoEnabled()` are read only by the state reply ([src/core/ControlCommandsSessionShared.h:341](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-62/src/core/ControlCommandsSessionShared.h:341)-[:342](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-62/src/core/ControlCommandsSessionShared.h:342)) and the grid's Edit Scene menu ([src/gui/SessionGridMenus.cpp:187](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-62/src/gui/SessionGridMenus.cpp:187)). So the "invalid timing" in the finding is latent, not observed. The defect is real anyway: the command accepted and saved a value outside the engine's contract, which is exactly the "override is stored" case the finding describes.

## Fix

Added `sceneTempoValid` ([src/core/ControlCommandsSession.cpp:381](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-62/src/core/ControlCommandsSession.cpp:381)). It accepts a supplied `tempo` only inside `[MinTempo, MaxTempo]`, and written as a positive range test it also refuses NaN. It returns `InvalidArgs` before anything is written. It is called beside the existing `sceneColorValid` check at [src/core/ControlCommandsSession.cpp:445](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-62/src/core/ControlCommandsSession.cpp:445), so it runs before `captureSession` and the mutation. The check runs whether or not `tempo_override` is supplied, because the value is stored either way.

The control schema and command description are unchanged, so `ControlCommandsSnapshot` and the canary are not affected. No new command was added.

## Regression test

`SessionCommandArgsTest::aSceneTempoOutsideTheEngineRangeIsRefused` ([tests/src/core/SessionCommandArgsTest.cpp:268](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-62/tests/src/core/SessionCommandArgsTest.cpp:268)). It sets up the grid, then checks:

1. Normal path: `MinTempo`, `MaxTempo` and `128.5` are accepted and read back.
2. Refusal: `0`, `9.5`, `1000`, `1e30` and `-120` are each refused as `InvalidArgs`.
3. Refusal with `tempo_override: false` in the same call is also `InvalidArgs`.
4. Failure leaves no write: a read-back afterwards still shows `128.5` with the override on.

Determinism: no timing, sleeps or threads. Commands run synchronously through `revtest::run` on fixed values.

Test matrix: the normal path, the two bounds, the out-of-range and override-off paths and the no-write check are covered. Audio thread: not touched, this is a control-thread handler. Undo: a refused command returns before `recordSessionEdit`, so it adds no undo step.

## Commands and exit codes

Build environment note: the sandbox denied ccache's default temp dir, which failed the first build (`build-red-1.log`, EXIT=2). Every build after that used `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p10-62`. This changes only where the compiler cache writes its temporary files, not the product.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Build, RED (first try, ccache denied) | `cmake --build build --target SessionCommandArgsTest -j4` | 2 | `build-red-1.log` |
| Build, RED | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p10-62 cmake --build build --target SessionCommandArgsTest -j4` | 0 | `build-red-2.log` |
| Test, RED | `cd build/tests && ctest -R SessionCommandArgsTest --output-on-failure` | 8 | `test-red-1.log` |
| Build, GREEN | `CCACHE_TEMPDIR=... cmake --build build --target zene SessionCommandArgsTest -j4` | 0 | `build-green-1.log` |
| Test, GREEN | `cd build/tests && ctest -R SessionCommandArgsTest --output-on-failure` | 0 | `test-green-1.log` |
| Test, GREEN, verbose | `cd build/tests && ctest -R SessionCommandArgsTest -V` | 0 | `test-green-3-verbose.log` |
| Proof: product diff saved | `git diff -- src/core/ControlCommandsSession.cpp > logs/fix.patch` | 0 | - |
| Proof: revert | `git apply -R logs/fix.patch` | 0 | - |
| Proof: rebuild reverted | `CCACHE_TEMPDIR=... cmake --build build --target zene SessionCommandArgsTest -j4` | 0 | `build-proof-revert-2.log` |
| Proof: test reverted | `cd build/tests && ctest -R SessionCommandArgsTest --output-on-failure` | 8 | `test-proof-revert-2.log` |
| Proof: reapply | `git apply logs/fix.patch` | 0 | - |
| Proof: rebuild fixed | `CCACHE_TEMPDIR=... cmake --build build --target zene SessionCommandArgsTest -j4` | 0 | `build-green-2.log` |
| Proof: test fixed | `cd build/tests && ctest -R SessionCommandArgsTest --output-on-failure` | 0 | `test-green-2.log` |
| Family build | `CCACHE_TEMPDIR=... cmake --build build --target ScenesUpconvertTest ProjectVersionTest SessionSceneFollowTest SessionModelTest -j4` | 0 | `build-family-1.log` |
| Family test | `cd build/tests && ctest -R "ScenesUpconvertTest\|ProjectVersionTest\|SessionSceneFollowTest\|SessionModelTest\|SessionCommandArgsTest"` | 0 | `test-family-2.log` |
| Broad family, not all built | `cd build/tests && ctest -R "Session\|Scene\|ProjectVersion"` | 8 | `test-family-1.log` |

RED result: `FAIL! : SessionCommandArgsTest::aSceneTempoOutsideTheEngineRangeIsRefused()`, `Actual (setTempo(refused).errorKind) : 0`, `Expected (ControlErrorKind::InvalidArgs): 3`, at `SessionCommandArgsTest.cpp(292)`. The first refused value, `0`, was accepted.

GREEN result: `Totals: 7 passed, 0 failed` (`test-green-3-verbose.log:30`); ctest `1/1 Passed`.

Affected-family test count: 5 of 5 built tests passed (`ScenesUpconvertTest`, `ProjectVersionTest`, `SessionSceneFollowTest`, `SessionModelTest`, `SessionCommandArgsTest`; `test-family-2.log`, `100% tests passed out of 5`).

Broad regex `Session|Scene|ProjectVersion` (`test-family-1.log`, run after the fix was reapplied): 35 tests, 4 passed, 1 failed, 30 Not Run.
- Passed: `SessionCommandArgsTest`, `ControlSessionLaunch`, `ControlSessionLifecycleTranscript`, `ControlSessionApiProof` (the last drives `session.set_scene` with `tempo` 128.5 over the socket).
- Not Run: 30 tests have no executable in this tree. I did not build them, so they are not claimed as passing.
- Failed: `ControlSessionTransitions` (`test-family-1.log:999`). The failing checks are audible-output checks ("the launched MIDI cell sounds", "the landed clip sounds in the arrangement"). The live server reports `missing_devices: tripleoscillator … the instrument plug-in module was not found or could not be created` (`test-family-1.log:1072`), and `build/plugins/TripleOscillator` has no built `.so`. The script never calls `set_scene` or touches tempo (`rg 'set_scene|tempo' tests/control-session-transitions.py` has no matches), so the fix cannot reach it. Cause by evidence: the module is missing from this targeted build. I did not re-run it on the unfixed base to prove it, so treat that as the likely cause, not a measured one.

## Gates

`feature_check.py` could not run in this sandbox. It writes its per-gate logs to `<worktree>-feature-check/`, a sibling of the worktree outside the write-jail (`gate-feature-check-1.log`: `PermissionError`, exit 1). I ran the same gate commands from `feature_check.py`'s own list, with logs in the LOG folder:

| Gate | Command | EXIT | Result |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | PASS |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | PASS |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | PASS |
| check-namespace | `tests/scripted/check-namespace` | 0 | PASS |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | PASS |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | PASS |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | red on the train, see below |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | red on the train, see below |

The two ratchets exit 1 because of items already red on the train: 6 `REGRESSION` lines in `gate-complexity.log` and 23 in `gate-file-length.log`, all in files this branch does not change (for example `src/wasm/WasmSandbox.cpp`, `src/core/TakeLane.cpp`, `plugins/Vst3Effect/Vst3Host.cpp`). I grepped both logs: no line names `src/core/ControlCommandsSession.cpp` or `tests/src/core/SessionCommandArgsTest.cpp`, the only two files this branch changes. Under the feature-check rule (only items new in changed files count), the ratchet rows pass.

Registration: both changed files are already listed in `tests/fork-sources.txt` (`ControlCommandsSession.cpp` at line 1590, `SessionCommandArgsTest.cpp` at line 2597) and in `tests/all-sources.txt`. No new file was added, so no registration edit was needed.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** RED: `test-red-1.log`, ctest EXIT=8, `aSceneTempoOutsideTheEngineRangeIsRefused` fails at line 292. Proof revert: `git apply -R` EXIT=0, build EXIT=0, `test-proof-revert-2.log` EXIT=8 with the same assertion. Reapply: `git apply` EXIT=0, build EXIT=0, `test-green-2.log` EXIT=0. The patch was exactly the product file diff (`logs/fix.patch`).
- **The test is deterministic.** It uses no clock, sleep, thread or race. Its inputs are fixed doubles (`MinTempo`, `MaxTempo`, `128.5`, `0`, `1e30`), and it calls the synchronous `revtest::run` command path. No seam is needed.
- **The fix is at the cause and complete.** The only writer of a scene tempo is `applySceneOverrides` (`ControlCommandsSession.cpp:213`), and its only caller is `applySceneEdits`, reached only from `session.set_scene`. I checked for other writers with `rg 'scene.setTempo|sceneModel.setTempo' src`: only line 213. The check runs before `captureSession` and before any mutation. Its siblings (`transport.set_tempo`, `clock`, project info) already use the same `MinTempo..MaxTempo` bounds; this fix brings `set_scene` into line.
- **No collateral damage.** Saved projects still load unchanged. `Scene::restoreState` (`SessionClip.cpp:266`) is untouched, and the load-path tests `ScenesUpconvertTest` and `ProjectVersionTest` both pass (`test-family-2.log`). The in-range callers, `SessionGridViewTest` (150), `control-engine-wiring.py` (150) and `session_api_proof_rows.py` (128.5), are all inside 10..999. One residual effect I did not change: the grid's Edit Scene dialog pre-fills the stored tempo only while the override is on (`SessionGridMenus.cpp:187`). A project already holding an out-of-range override, such as `0` saved before this fix, would have that value resubmitted by Edit Scene and refused with the new message. The user must correct the tempo or switch the override off. This is the intended refusal of an invalid value, but it is a visible change for such a project, so I am flagging it.
- **Verdict evidence.** REAL: the file:line evidence above is the contract (`Song.h:78-80`, sibling schemas) and the save path. The harm is latent because nothing consumes scene tempo yet, and the report says so.

## Files committed

- `src/core/ControlCommandsSession.cpp`
- `tests/src/core/SessionCommandArgsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-62.md`
