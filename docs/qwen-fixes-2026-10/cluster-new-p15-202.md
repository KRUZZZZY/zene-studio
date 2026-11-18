ALREADY-FIXED

# Cluster new-p15-202: a non-numeric sub-scene row is restored as row zero

Finding: `src/core/SessionModel.cpp:159` (BUG 3). The claim was that `toInt(&ok)` returns 0 for `scene="abc"` and the
range check ignores `ok`, so the row is accepted as scene 0. On this branch the check already includes `ok`.

## Evidence (file:line)

- `src/core/SessionModel.cpp:158-160` (current tree):
  `bool ok = false;` / `const int row = ...toInt( &ok );` / `if( !ok || static_cast<unsigned>( row ) >= static_cast<unsigned>( MaxScenes ) ) { return false; }`
  A non-numeric `scene` returns false before `scene` or `tracks` is written, so `*this` is unchanged.
- The track loop in the same function (`src/core/SessionModel.cpp:164-165`) already checks `!ok` too, as the finding says.
- The check was written with the feature: `git blame` puts lines 156-173 in `2ecec758f` (0.5.0 L02, WF-064, 2026-10-07).
  `git merge-base --is-ancestor 2ecec758f HEAD` = yes. `git show master:src/core/SessionModel.cpp | grep -c 'SubScene::restoreState'` = 0,
  so the buggy form never shipped on `master`.
- Callers: `SessionModel.cpp:104` (`restoreSubScenes`) keeps only rows whose `restoreState` returns true, so a rejected row is
  dropped. The project-file path is `src/core/SessionSubScenes.cpp:65` (`readRows`) and `:125-126` (`load`): one row that does not
  parse makes the whole `<z:subscenes>` section unclaimed, and it is kept verbatim (`m_preserved`).

Verdict reasoning: the finding's "fix" (add `!ok`) is already the code, so there is no product change. Because the bug is not
on the branch, RED was shown by a temporary mutation of `SessionModel.cpp:160` (below), not by running the test on unfixed code.

## Regression test (added)

`tests/src/core/SessionSubSceneTest.cpp:276` `aNonNumericSubSceneRowIsRefusedNotSceneZero()` (new, +56 lines, the only changed file):
- `SubScene::restoreState` on `scene="abc"` returns false and leaves `scene`/`name` unchanged (line 283).
- `tracks="0,x"` returns false.
- `scene="0"` still restores as row 0.
- Through a project file (`nonnumeric-base.mmp` edited to a `<z:subscenes v="1">` holding `scene="abc"` and a valid row):
  `song->sessionModel().subScenes()` is empty (line 323) and the re-saved file keeps the `scene="abc"` / `name="Bad"` row verbatim.
  This matches the section policy in `SessionSubScenes.cpp:125`, not a guessed row 0.
- Deterministic: no clock, sleep or race. Inputs are literal XML strings and a `QTemporaryDir` file.

No new file, so `tests/fork-sources.txt` / `tests/upstream-modifications.txt` need no change. The file is already listed in
`tests/CMakeLists.txt:790` and `tests/all-sources.txt:2764`.

## Commands run (all logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-202.r1/logs`)

| step | command (run from the worktree / build) | EXIT | log |
| --- | --- | --- | --- |
| build (first try) | `cmake --build build --target SessionSubSceneTest -j4` | 2 | build-1-green.log (ccache could not write `/run/user/1000/ccache-tmp`; not a code error) |
| build | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp cmake --build build --target SessionSubSceneTest -j4` | 0 | build-2-green.log |
| test (my first draft) | `ctest -R '^SessionSubSceneTest$'` from `build/tests` | 8 | test-1-green.log: my draft expected "Good" to load; the section is kept verbatim by design. Test corrected. |
| build (name clash) | same build | 2 | build-3-green.log: `kept` declared twice in the test. Renamed. |
| build | same build | 0 | build-4-green.log |
| **GREEN** (current tree, fix present) | `ctest -R '^SessionSubSceneTest$' --output-on-failure` from `build/tests` | **0** | test-3-green.log: 1/1 passed |
| mutation (RED) | Edit: `if( !ok || ...MaxScenes...)` → `if( ...MaxScenes...)` at SessionModel.cpp:160; `git diff -- src/core/SessionModel.cpp > logs/mutation.patch` | 0 | mutation.patch |
| build (mutated) | `CCACHE_TEMPDIR=… cmake --build build --target zene SessionSubSceneTest -j4` | 0 | build-5-red.log |
| **RED** (mutated) | `ctest -R '^SessionSubSceneTest$' --output-on-failure` | **8** | test-4-red.log: `FAIL! : aNonNumericSubSceneRowIsRefusedNotSceneZero() '!text.restoreState(...)' returned FALSE` at `SessionSubSceneTest.cpp(283)`; Totals 7 passed, 1 failed |
| restore | `git apply -R logs/mutation.patch` | 0 | `git diff --quiet -- src/core/SessionModel.cpp` → clean |
| build (restored) | same build | 0 | build-6-green-restored.log |
| **GREEN** (restored) | `ctest -R '^SessionSubSceneTest$'` | **0** | test-5-green-restored.log: 1/1 passed |

The file-level assertions were not run separately under the mutation: the unit assertion fails first in the same function.
Without the `!ok` check, the `scene="abc"` row would be claimed as scene 0 and the section would not be kept verbatim, so the
file-level assertions should fail too. I did not observe that.

### Affected family (after restore, zene and all five targets rebuilt)

| command | EXIT | result |
| --- | --- | --- |
| `cmake --build build --target SessionModelTest SessionGridRecordTest SessionAudioRecordTest SessionSubSceneGridTest -j4` | 0 | build-7-family.log |
| `ctest -R '^(SessionModelTest\|SessionGridRecordTest\|SessionAudioRecordTest\|SessionSubSceneGridTest\|SessionSubSceneTest)$'` | 0 | 5/5 passed (test-6-family.log) |
| `./SessionSubSceneTest` from `build/tests` (direct run, count) | 0 | `Totals: 8 passed, 0 failed` (test-7-subscene-direct.log) |

## Gates

- `python3 …/qwen/feature_check.py <worktree>` → EXIT 1, but the wrapper did not run its checks: it failed at
  `logs.mkdir` with `PermissionError` on `…/zene-qwen-new-p15-202-feature-check`, a folder beside the worktree that is outside my
  write-jail (gate-1-feature-check.log). I did not change the script.
- So I ran each of its commands by hand from the worktree, with the same arguments:

| gate | command | EXIT | log |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | gate-fork-sources.log |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | gate-all-sources-reproduce.log |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | gate-no-upstream-regression.log |
| check-namespace | `tests/scripted/check-namespace` | 0 | gate-check-namespace.log |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | gate-no-tautology.log |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | gate-doc-figures.log |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | gate-complexity.log |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | gate-file-length.log |

- The two ratchets exit 1 on the whole tree, as the brief says they are red on the train. The `REGRESSION:` lines name
  `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp` and
  `ProjectRevisions.cpp` (complexity), plus 15 file-length lines. None names `SessionSubSceneTest.cpp` or any file I changed
  (`grep -n SessionSubScene` on both logs: no match). So feature_check's per-item rule would not fail on this branch.
  I did not compute the base baseline (that cache is outside the write-jail), so the "new vs base" half is not verified.
- `git status --short` after the gates: only `tests/src/core/SessionSubSceneTest.cpp` and the new report. No gate rewrote a file.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** Here the fix is already on the
  branch, so "reverted" means the mutation above. RED: `ctest` EXIT 8, failing at `SessionSubSceneTest.cpp(283)`
  (test-4-red.log). GREEN: EXIT 0 with the fix (test-3-green.log), and EXIT 0 after the mutation was reverted (test-5-green-restored.log).
- **The test is deterministic: no wall-clock, sleep or race.** It uses literal XML, `SubScene::restoreState` directly, and the
  project file in a `QTemporaryDir`, with a fixed edited section. No timers or threads.
- **The fix is at the cause and complete: every caller and sibling checked.** No product change. Callers of `restoreState`
  (`SessionModel.cpp:104`, `SessionSubScenes.cpp:65`) both reject a non-numeric row; the track branch at `SessionModel.cpp:164-165`
  checks `ok` too. The only other `scene` reader, the clip restore at `SessionModel.cpp:120-126`, reads a clip's cell, not a sub-scene row, so it is outside this finding.
- **No collateral damage.** No product code changed, so behaviour outside the test is unchanged. The family tests pass (5/5, plus
  8/8 in the direct run).
- **ALREADY-FIXED: the code (file:line) that shows it.** `src/core/SessionModel.cpp:160` (`!ok ||`), from commit `2ecec758f`;
  not on `master`; the project-file behaviour is `src/core/SessionSubScenes.cpp:65,125`.

## Submit check

`python3 …/qwen/submit_check.py <worktree> new-p15-202` was run before the commit: FAIL on the items this report and the commit
address (nothing committed, report missing, Self-check missing, "tests added or changed"). The gates item still reports the
wrapper's PermissionError; the six gate commands above pass by hand, and the two ratchets have no item in my files. I did not
re-run the wrapper after the commit, because it cannot write its log folder.
