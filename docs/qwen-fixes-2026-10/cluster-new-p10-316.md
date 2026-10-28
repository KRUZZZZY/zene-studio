ALREADY-FIXED

Cluster new-p10-316 ("Negative keys index before `KEY_ORDER`", `src/core/Piano.cpp:128`). The guard the finding asks for is on this branch: `isBlackKey()` rejects keys outside `[0, NumKeys)` before taking the remainder, and `isWhiteKey()` inherits the check. No product code changed in this lane. The fix landed in commit `34e33c4aa2` (cluster new-p9-339). This lane strengthens the existing regression test `PianoRollBenchmarkTest::invalidKeysAreSafe` with the negative-key cases the finding names.

## Evidence (file:line on this branch, HEAD `37eaf07557`)

- `include/Piano.h:80-83` `isValidKey(key)` is `key >= 0 && key < NumKeys`; `NumKeys = 128` (`include/Note.h:93`).
- `src/core/Piano.cpp:126-133` `isBlackKey()`: the guard is `if (!isValidKey(key)) { return false; }` at `:128`, before `key % KeysPerOctave` at `:130` and the `KEY_ORDER[keyCode]` read at `:132`. A negative key never reaches the array.
- `src/core/Piano.cpp:136-139` `isWhiteKey()`: `return isValidKey(key) && !isBlackKey(key);`.
- `include/Piano.h:53-56` `isKeyPressed()` is guarded too (same fix commit).
- `src/core/Piano.cpp:79`, `:100`, `:117` the setter and handlers are guarded by `isValidKey`.
- Caller check: `isBlackKey`/`isWhiteKey` callers in `src/gui/instrument/PianoView.cpp` (lines 100, 532, 543, 740, 771, 778, 863, 901, 923) pass keys through these helpers, so they now get `false` for out-of-range input instead of an out-of-bounds read.
- Sibling `NoteGrid::isBlackKey` (`src/gui/noteeditor/NoteGrid.cpp:292-296`) normalises with `((key % 12) + 12) % 12`, so it never indexes outside its 12-entry table. Not affected.
- `src/gui/editors/PianoRoll.cpp:3480-3533` indexes `prKeyOrder[key % KeysPerOctave]` directly. That is a separate path, not the Piano helpers. The paint loop's keys are clamped to `[0, NumKeys-1]` at `:3430` (`topKey`); I checked that by reading, not by a test, and did not change it.

## Regression test

`tests/src/gui/PianoRollBenchmarkTest.cpp:192-204` `invalidKeysAreSafe()` (added in `34e33c4aa2`). This lane's change (diff saved at the LOG FOLDER as `test-change.patch`):

- adds `#include <limits>`;
- the key list becomes `{-1, -KeysPerOctave - 1, std::numeric_limits<int>::min(), NumKeys}`, with a comment. Each of these has a negative remainder under `% KeysPerOctave`: `-1` and `-13` give `-1`, and `INT_MIN` gives `-8`.

The deterministic RED comes from the `NumKeys` assertion at the top of the test (`QVERIFY(!Piano::isBlackKey(NumKeys))`): `128 % 12 == 8` is a black key, so the test fails with no guard. The negative-key cases are not relied on for RED: without the guard, `KEY_ORDER[-1]` is an out-of-bounds read, so their outcome is undefined behaviour. They guard the fixed path against future refactors.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-316.r1/logs/`.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Build attempt 1 | `cmake --build build --target PianoRollBenchmarkTest -j4 > build-1.log 2>&1` | 2 | build-1.log (ccache temp-dir permission error, not a code error; see Environment) |
| Build attempt 2 | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target PianoRollBenchmarkTest -j4 > build-2.log 2>&1` | 0 | build-2.log |
| Baseline test (original test, guard present) | `cd build/tests && ctest -R '^PianoRollBenchmarkTest$' --output-on-failure > test-1.log 2>&1` | 0 | test-1.log: 1/1 passed |
| RED 1 (guard removed from `Piano.cpp:128`, first test version) | build: `... build-3-red.log`; test: `ctest -R '^PianoRollBenchmarkTest$' ... > test-2-red.log` | build 0, test **8** | test-2-red.log: `FAIL! ... '!Piano::isBlackKey(NumKeys)' returned FALSE`, `PianoRollBenchmarkTest.cpp(195)`, 5 passed 1 failed |
| GREEN 1 (guard restored) | build-4-green, `test-3-green.log` | 0, 0 | 1/1 passed |
| Family 1 | `ctest -R '^(NoteEditorTest\|PianoViewFocusTest\|PianoRollBenchmarkTest)$'` | 0 | test-4-family.log: 3/3 passed |
| RED final (guard removed; final test with negative keys) | build-6-red-final.log; `ctest ... > test-5-red-final.log` | build 0, test **8** | test-5-red-final.log: `FAIL! ... '!Piano::isBlackKey(NumKeys)' returned FALSE`, `PianoRollBenchmarkTest.cpp(196)`, 5 passed 1 failed |
| GREEN final (guard restored; final test) | build-7-green-final.log; `ctest -R '^PianoRollBenchmarkTest$' ... > test-6-green-final.log` | 0, 0 | 1/1 passed |
| Family final | `ctest -R '^(NoteEditorTest\|PianoViewFocusTest\|PianoRollBenchmarkTest)$'` | 0 | test-7-family-final.log: 3/3 passed (NoteEditorTest #397, PianoRollBenchmarkTest #534, PianoViewFocusTest #536) |

The RED/GREEN "product change" is the guard at `src/core/Piano.cpp:128`. The reversal was done by removing and re-adding that one line with `Edit`; `git diff -- src/core/Piano.cpp` is empty at the end (HEAD matches).

Gates. `feature_check.py` could not run: it writes its logs to `<worktree>-feature-check/`, next to the worktree, outside this lane's write area (`logs.mkdir` raised `PermissionError`; no log folder was created). I ran each gate command from its body directly in the worktree, with the output in the LOG FOLDER:

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 (train-level, 6 REGRESSION lines) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 (train-level, 23 REGRESSION lines) |

The two ratchets are red on the train on purpose. Under the `feature_check.py` rule they fail only on items in files this branch changes. The branch's changed file is `tests/src/gui/PianoRollBenchmarkTest.cpp`, and no REGRESSION line names it (`grep` of `gate-final-complexity.log` and `gate-final-file-length.log`). So both rows pass under that rule. Logs: `gate-final-*.log`.

Full table, from the same script with one change: its log folder points at this LOG FOLDER (`feature_check_logdir.py`, a copy of `feature_check.py` that differs only in lines 19 and 70; the baseline cache at `qwen/gate-baselines` is read unchanged). Command: `python3 -I feature_check_logdir.py <worktree> > feature-check-1.log 2>&1; echo EXIT=$?` gave `EXIT=0`, `RESULT: PASS`: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures all PASS (exit 0); complexity and file-length "no new item in your files" (base `37eaf0755`, 2 changed files). The official `feature_check.py` exits 1 here because it cannot create `<worktree>-feature-check/` outside this lane's write area; its traceback is in the first run's output and is not a gate result.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Yes. With the guard at `src/core/Piano.cpp:128` removed, `ctest -R '^PianoRollBenchmarkTest$'` exits 8 (`PianoRollBenchmarkTest.cpp(196)`, `!Piano::isBlackKey(NumKeys)` returned FALSE). With the guard restored it exits 0 (1/1 passed). Exit codes are in the table above.
- **The test is deterministic.** It uses literal key values and no timing, sleeps, threads or injected clock. The RED assertion reads `KEY_ORDER[8]` (`128 % 12`), which is in range, so the RED is defined behaviour. The negative keys are not relied on for RED (see Regression test).
- **The fix is at the cause and complete.** Every public helper that takes a key is guarded (`Piano.h:55`, `Piano.cpp:79/100/117/128/138`). The cause is the missing range check before the modulo, which `isValidKey` supplies. Callers are listed under Evidence. The sibling `NoteGrid::isBlackKey` is not affected. The `PianoRoll.cpp` `prKeyOrder` path is a separate function, checked by reading only.
- **No collateral damage.** No product code changed in this lane. Valid keys `[0, 128)` keep their behaviour (the guard was already there). The test change only adds negative keys to a list of rejected keys. Saved projects and key handling are unchanged.
- **Verdict ALREADY-FIXED, with code evidence:** `src/core/Piano.cpp:128` (guard), `:130-132` (modulo and read after it), `include/Piano.h:80-83` (`isValidKey`), and the fix commit `34e33c4aa2`.

## Environment notes

- `ccache` config (`/home/kruzzzzy/.cache/ccache-bunny/ccache.conf`) sets `temporary_dir` to `/home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`, which this sandbox cannot write. The build was run with `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp` (the `$TMPDIR` on disk). No config file was changed.
- `feature_check.py` could not create its log folder (see gates above).
- `-j4` at most was used throughout.
