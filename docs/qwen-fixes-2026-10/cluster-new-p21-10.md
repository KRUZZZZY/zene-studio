FALSE-POSITIVE

# Cluster new-p21-10 — trackState "dereferences a possibly null track"

Finding: `src/core/ControlCommandsAutomation.cpp:111` (branch `bugs/qwen-new-p21-10`, base `050/m0-train-17`).

## Verdict

The finding says `trackState` calls `trackIdOf(nullptr)` unconditionally and dereferences a null track
when a lane's followed id no longer exists. The code does not do that. `trackIdOf` is null-safe, and the
null case already yields the empty string the finding asks for. No product change is needed.

## Evidence (file:line on this branch)

- `src/core/ControlCommandsAutomation.cpp:109-111` — `followedId` is taken from `lane->followsTrack()`;
  when it is `>= 0`, the value is `control::trackIdOf(Engine::getSong()->findTrackById(followedId))`,
  otherwise `QString()`.
- `src/core/TrackContainerFolders.cpp:52-62` — `TrackContainer::findTrackById` returns `nullptr` when no
  track has that id (`Track* found = nullptr; … return found;`). So a dangling followed id does reach
  `trackIdOf` as a null pointer.
- `include/ControlVocabulary.h:102` — `LMMS_EXPORT QString trackIdOf(const Track* track);`
- `src/core/ControlVocabulary.cpp:143-146` — `return track == nullptr ? QString() : trackId(track->id());`
  This is the guard the finding's "fix" describes, already present. A null track gives `""`, and
  `trackId` is never reached with a null pointer.
- `src/core/ControlCommandsAutomation.cpp:97` and `:133` use `trackIdOf` on live tracks; `:111` is the
  only place a possibly-dangling id is passed in. Its output for a dangling owner is `"follows": ""`,
  which is the behaviour the finding asks for.

The line number in the finding (111) matches the current tree.

Scope note: `ControlCommandsAutomation.cpp:126` (`Engine::getSong()->tracks()`) also dereferences
`Engine::getSong()` without a null check. It is a sibling but not what this finding names. I did not
check whether `getSong()` can be null inside a control command; it is out of scope for this cluster.

## Regression test

None added. The finding is not real, so no product fix exists for a test to prove. A test would pass
with and without a change, which proves nothing about this bug. I did not run the RED/GREEN sequence
(step 4b) because there is no product change to revert.

## Commands run and EXIT codes

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-10.r1/logs/`.

| Command | EXIT | Log |
| --- | --- | --- |
| `python3 …/qwen/feature_check.py <worktree>` | 1 | `gate-feature-check-1.log` — `PermissionError` creating `<worktree>-feature-check/` in `projects/zene-studio-program/`, outside the write-jail. No table was produced. |
| `bash tests/fork-sources-gate.sh` | 0 | `g-fork-sources.log` |
| `bash tests/all-sources-reproduce.sh` | 0 | `g-all-sources.log` |
| `bash tests/no-upstream-regression-gate.sh` | 0 | `g-no-upstream.log` |
| `tests/scripted/check-namespace` | 0 | `g-check-namespace.log` |
| `bash tests/no-tautology-gate.sh` | 0 | `g-no-tautology.log` |
| `python3 tests/doc-figures-gate.py` | 0 | `g-doc-figures.log` |
| `bash tests/complexity-gate.sh --check` | 1 | `g-complexity.log` — `FAIL: complexity ratchet regressed`. Listed items are in `ExternalProcessStemSeparator.cpp`, `DawProjectRead.cpp`, `DawProjectSession.cpp`, `DawProjectWrite.cpp`, `ScriptEngine.cpp`, `OnnxRuntimeStemSeparator.cpp`, `AudioPortsModel.cpp`, `ControlCommandsAutomationRamp.cpp`, and Python test scripts. None is in `ControlCommandsAutomation.cpp`. |
| `bash tests/file-length-gate.sh --check` | 1 | `g-file-length.log` — `FAIL: file-length ratchet regressed`. Listed items include `Vst3Host.cpp`, `TakeLane.cpp`, `WasmSandbox.cpp`. None is a file this lane touched. |
| `git status --short` (after the gates) | — | empty |

Gate interpretation: `feature_check.py` could not write its output directory, so the combined PASS/FAIL
table does not exist. The six exit-0 gates were run one by one. The two ratchets report regressions in
files this lane did not change. This lane changes no product file, so none of them is a new item in a
changed file. I did not compare the listed items against the base listing, because the cache that does
that lives outside the write-jail. Whether they are new relative to the base is therefore unverified.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p21-10.md` (this report only)

## Self-check

- **Regression test fails with the product change reverted and passes with it (exit codes):** not
  applicable. There is no product change. The verdict is FALSE-POSITIVE, and the code that rules out the
  null dereference is already on the branch (`src/core/ControlVocabulary.cpp:143-146`). Nothing was
  reverted and nothing was run for step 4b.
- **Test is deterministic (seam or injected values):** not applicable. No test was added. The evidence is
  control-flow reading of the code cited above, with no timing or concurrency involved.
- **Fix at the cause, every caller and sibling checked:** not applicable. No fix is made. Callers of
  `trackIdOf` are the existing ones at `ControlCommandsAutomation.cpp:97`, `:111`, `:133`, and the other
  files that already call it. All of them pass a possibly-null pointer through the same null-safe guard.
  The sibling `Engine::getSong()` dereference at `:126` is outside this finding and was not examined.
- **No collateral damage:** nothing changed, so no saved data, project load path or other caller is
  affected by this lane.
- **Verdict evidence (file:line):** `src/core/ControlVocabulary.cpp:143-146` (null guard),
  `include/ControlVocabulary.h:102` (declaration), `src/core/TrackContainerFolders.cpp:52-62` (returns
  `nullptr` on a missing id), `src/core/ControlCommandsAutomation.cpp:109-111` (call site).
- **Gates:** `feature_check.py` exits 1 only because it cannot create its output directory outside the
  write-jail (`logs/gate-feature-check-1.log`). Its six simple checks were rerun one by one and exit 0.
  The two ratchets exit 1 on items in files this lane did not change. The combined table is not
  produced, so the `gates` row of `submit_check` stays FAIL for that reason, not for a code defect.
