FIXED

# Cluster new-p14-35: an unchanged seed or MTS master flag consumes an undo step

Branch `bugs/qwen-new-p14-35`, base `2da2ed356` (050/m0-train-8). Finding: BUG 13 (`ControlCommandsNoteRandom.cpp:275`, `ControlCommandsMts.cpp:320`), scoped by gpt-6-luna at `050/m0-train-7`.

## Verdict and root cause

REAL, confirmed on the branch before the fix. Both handlers called `control::addUndoStep` unconditionally, so a call that set the value the project already had still produced a journal step. The registry's no-op rule (`ControlRegistry.cpp:298-303`, `changedNothing()`) only treats a true-inverse call as unchanged when the journal produced no step and the result has no `__transaction`. With the unconditional step, the no-op counted as changed, and the next `control.undo` unwound the no-op instead of the real edit behind it.

## Evidence (file:line, after the fix)

- `src/core/ControlCommandsNoteRandom.cpp:275-299`: `randomSeedSet` computes `changed = requested != previous`. The write and the `control::addUndoStep` run only inside `if (changed)`. A no-op returns at line 299 with `changed: false` and no `__transaction`.
- `src/core/ControlCommandsMts.cpp:318-336`: `masterSet` still calls `setMtsMaster(enabled)` first, so refusal and the repeated-`true` publication are unchanged. Line 333 returns `fullState()` with no step when `enabled == wasEnabled`. The `addUndoStep` at line 336 is reached only on a real transition.
- Pre-fix: `ControlCommandsNoteRandom.cpp:275-285` (unconditional `setMidiSeed` and `addUndoStep`); `ControlCommandsMts.cpp:331-339` (unconditional `addUndoStep`).
- Precedent for the same fix: `src/core/ControlCommandsDevice.cpp:153` returns early for an unchanged value ("the device.mpe_set rule"), with test `ControlNoteScaleVerbsTest::theMpeSwitchTakesNoUndoStepForAValueItAlreadyHas`.
- Consumers of the removed `__transaction` on a no-op: `src/core/ControlRegistryRecords.cpp:99,133` read it only for held nested records. A no-op never gets `changed=true` (`changedNothing`), so it is never recorded, which is the path `mpe_set` already uses.

## Tests

- New: `ControlNoteScaleVerbsTest::theUnchangedSeedTakesNoUndoStep`, `tests/src/core/ControlNoteScaleVerbsTest.cpp:944-968`. It sets a new seed, sets the same seed again, checks `changed: false`, then checks that one `control.undo` restores the seed from before the real edit.
- Changed: `SessionTuningTest::masterSetReportsItsOwnMtsEspState`, `tests/src/core/SessionTuningTest.cpp:1345`. The old assertion expected `depthBefore + 1` for an idempotent disarm, which encoded the bug. It now expects `depthBefore`, as the brief asked.
- Added in the installed-library branch, `SessionTuningTest.cpp:1312-1321`: a repeated disarm followed by one `control.undo` must bring the master back. **This branch is not executed on this machine**: `/usr/local/lib/libMTS.so` is absent, so only the refusal branch runs.

No command was added and no id changed. `ControlCommandsSnapshot` and the canary figures are not affected. No new files, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are untouched.

## Commands and exit codes (all logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-35.r1/logs`)

| Step | Command (run from the worktree or `build/tests` as noted) | EXIT | Log |
|---|---|---|---|
| RED build, attempt 1 | `cmake --build build --target ControlNoteScaleVerbsTest SessionTuningTest -j4` | 2 | `build-red-1.log`: ccache could not write `/run/user/1000/ccache-tmp`, not a code error |
| RED build, attempt 2 | `env CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p14-35 cmake --build build --target ControlNoteScaleVerbsTest SessionTuningTest -j4` | 0 | `build-red-2.log` |
| **RED** (unfixed code) | `ctest -R 'ControlNoteScaleVerbsTest\|SessionTuningTest' --output-on-failure` (in `build/tests`) | **8** | `test-red-1.log`: `theUnchangedSeedTakesNoUndoStep` fails at `ControlNoteScaleVerbsTest.cpp:965`, actual 515151, expected 424242; `masterSetReportsItsOwnMtsEspState` fails at `SessionTuningTest.cpp:1345`, undo depth 1, expected 0 |
| Fix written, GREEN build | `env CCACHE_TEMPDIR=... cmake --build build --target zene ControlNoteScaleVerbsTest SessionTuningTest -j4` | 0 | `build-green-1.log` |
| **GREEN** (fixed code) | `ctest -R 'ControlNoteScaleVerbsTest\|SessionTuningTest' --output-on-failure` | **0** | `test-green-1.log`: 2/2 passed |
| Proof: save product patch | `git diff -- src/core/ControlCommandsNoteRandom.cpp src/core/ControlCommandsMts.cpp > logs/fix.patch` | 0 | |
| Proof: revert product fix | `git apply -R logs/fix.patch` | 0 | |
| Proof: rebuild reverted | `cmake --build build --target zene ControlNoteScaleVerbsTest SessionTuningTest -j4` | 0 | `build-proof-red.log` |
| Proof: RED again | `ctest -R ...` | **8** | `test-proof-red.log`: both fail at the same locations as above |
| Proof: reapply fix | `git apply logs/fix.patch` | 0 | |
| Proof: rebuild fixed | `cmake --build build --target zene ControlNoteScaleVerbsTest SessionTuningTest -j4` | 0 | `build-proof-green.log` |
| Proof: GREEN again | `ctest -R ...` | **0** | `test-proof-green.log`: 2/2 passed |
| Family build | `cmake --build build --target SessionTuningPublicationTest NoteRandomTest ReversibilityContractTest ReversibilityUndoTest ControlRegistryTest ControlRegistryNestingTest UndoHonestyTest -j4` | 0 | `build-family-1.log` |
| Family run (fix applied) | `ctest -R '^(SessionTuningPublicationTest\|NoteRandomTest\|ReversibilityContractTest\|ReversibilityUndoTest\|ControlRegistryTest\|ControlRegistryNestingTest\|UndoHonestyTest\|ControlNoteScaleVerbsTest\|SessionTuningTest)$'` | 8 | `test-family-1.log`: 8 of 9 pass; `UndoHonestyTest` fails |
| `UndoHonestyTest` on unfixed code | `git apply -R`, build `zene UndoHonestyTest`, `./UndoHonestyTest`, `git apply` | 1 | `test-baseline-undohonesty.log`: the same failure, so it is not from this change |
| Final rebuild and GREEN | `cmake --build build --target zene ControlNoteScaleVerbsTest SessionTuningTest -j4`; `ctest -R '^(ControlNoteScaleVerbsTest\|SessionTuningTest)$'` | 0 / 0 | `build-final-1.log`, `test-final-1.log` |
| Gates | `python3 feature_check.py <worktree>` | 1 | `gates-1.log`: `PermissionError` at `logs.mkdir`. The script writes to `<worktree>/../…-feature-check`, which is outside the write-jail. Not a gate failure. |
| Gates (same checks) | `python3 -I /tmp/fc_wrapper_new_p14_35/run_gates.py <worktree>` | 0 | `gates-2.log`; per-gate logs in `logs/gates/` |

Gate rows from `gates-2.log`: fork-sources 0, all-sources-reproduce 0, no-upstream-regression 0, check-namespace 0, no-tautology 0, doc-figures 0. The two ratchets exit 1 on the train by design, and the comparison found no new item in the files this branch changed: complexity (`exit 1; no new item in your files`) and file-length (`exit 1; no new item in your files`). The `randomize` CCN entry at `ControlCommandsNoteRandom.cpp` is pre-existing, not a regression. The wrapper runs the same functions and gate list as `feature_check.py` and differs only in log location.

Test counts (QTest `Totals`, run directly from `build/tests`): `ControlNoteScaleVerbsTest` 23 passed, 0 failed; `SessionTuningTest` 24 passed, 0 failed. The other family targets are counted per target only: `SessionTuningPublicationTest`, `NoteRandomTest`, `ReversibilityContractTest`, `ReversibilityUndoTest`, `ControlRegistryTest`, `ControlRegistryNestingTest` pass in ctest.

`UndoHonestyTest` fails in `armingSeveralTracksIsOneStep` (`UndoHonestyTest.cpp:414`): `the take file /home/kruzzzzy/Documents/Zene Studio/zene-take-trk10.wav could not be opened for writing`. Other cases in the same file log `plugin-scan-cache ... Permission denied` at `/home/kruzzzzy/Documents/Zene Studio`. It fails identically on the unfixed code (`test-baseline-undohonesty.log`: 12 passed, 1 failed, 4 skipped). This is the sandbox's write-jail, not this change.

## Self-check

- **Each new test fails with the product change reverted and passes with it.** Yes. RED: `ctest` EXIT 8 (`test-red-1.log`, `test-proof-red.log`). GREEN: EXIT 0 (`test-green-1.log`, `test-proof-green.log`).
- **Every acceptance criterion is proven by a test I ran.** The criteria are: unchanged seed takes no step; one undo reaches the real edit; `changed: false` is reported; the MTS no-op takes no step. The seed criteria are proven by `theUnchangedSeedTakesNoUndoStep` (EXIT 0 after the fix). The MTS criterion is proven by the refusal branch of `masterSetReportsItsOwnMtsEspState` (EXIT 0 after the fix). The MTS undo-reaches-real-change check runs only with libMTS installed and was **not executed here**.
- **Each edit is one undo step, and every changed command has an honest reversibility class.** A changed call still records exactly one step. An unchanged call records none. Both commands keep their `TrueInverse` rows in `ControlReversibilityTableNoteScale.cpp:75` and `ControlReversibilityTableMts.cpp:111`. Nothing was reclassified.
- **Nothing added runs on the audio thread.** These are UI-thread control handlers. The change adds one comparison and removes work on the no-op path.
- **Old projects still load and render as before; new data round-trips.** The save format, the `midiseed` header attribute and the MTS table are not touched. The result fields `seed`, `previous`, `changed` and `persisted` are unchanged for changed calls, and for no-ops only `__transaction` is omitted.
- **Qt 5.15 / MSVC / macOS portability.** Only `QJsonObject` and `QStringLiteral` already used in these files. No new API.
- **Determinism.** No timing, sleeps or races. The seed test uses fixed values 424242 and 515151, derived from the seed read back first.
- **Sibling check (each command the submit check lists, in `ControlCommandsMts.cpp` or the seed group):**
  - `mts.get_state`: read-only, no undo step, not affected.
  - `mts.master_set`: the fixed handler (`ControlCommandsMts.cpp:318-336`).
  - `note.random_seed_get` (same file family, `ControlCommandsNoteRandom.cpp`): read-only, not affected.
  - `mts.set_tuning`, `mts.set_note`, `mts.load_scale`, `mts.load_keymap`, `mts.reset`: **NOT changed and NOT verified here, and they are not clear of the bug.** Each one's success path goes through `tableMutation` (`ControlCommandsMts.cpp:139`, called at lines 196, 231, 271, 281, 300, 309), which calls `control::addUndoStep` unconditionally. So a call that writes the table's current state (for example `mts.reset` on an already-inactive table, or `mts.set_note` with the same frequency) probably takes an undo step too. The finding names only the seed and master-flag handlers, and a fix for the table verbs changes many outputs and undo counts, so I left them for a follow-up finding. I have not run a test to confirm the user-visible effect.
  - `clock.master_set` (`ControlCommandsClock*.cpp:111-113`) has the same unconditional `addUndoStep` shape. Not changed here: its no-op depends on the port subscription (`setMasterEnabled(enabled, port)`) as well as the flag. Also needs its own finding; not run.
- **Collateral and limits.** No limits or checks were added.
- **Verdict ALREADY-FIXED or FALSE-POSITIVE.** Not applicable: the verdict is FIXED.
- **Registration.** `src/core/ControlCommandsNoteRandom.cpp` and `src/core/ControlCommandsMts.cpp` are already in `tests/fork-sources.txt` (lines 1440 and 1430). Both test files already exist, so no `CMakeLists` change.
- **Gates.** Listed above. Both ratchets are red by design, and the comparison shows no new item in the changed files. The completion check's own row `gates (feature_check.py)` still reads FAIL, because `feature_check.py` exits 1 on `PermissionError` at its log directory, which lies outside the write-jail (`gates-1.log`). The gates themselves were run from the same script with logs redirected, and all six simple gates exit 0 (`gates-2.log`). The outer loop, which can write there, should re-run `feature_check.py` to confirm.
- **Completion check.** `submit_check-1.log` (`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-35.r1/logs`): commits, clean tree, report and test rows PASS; the gates row FAIL as above; the six judgement items are answered in this section.
