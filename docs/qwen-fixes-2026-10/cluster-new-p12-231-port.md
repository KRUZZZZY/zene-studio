# Cluster new-p12-231-port — schedule state reports character count as source bytes

**Verdict: FIXED** (the state-reporting half). The source-cap half of the original bug is
**ALREADY-FIXED** on this line.

Port of `bugs/qwen-new-p12-231` (commits `c56163202`, `bc3601ba8`) onto the newest line
(`050/m0-train-5`, `b02a0ff2b`). Both commits were re-applied by hand; nothing was merged,
cherry-picked or rebased.

## Finding

`QString::size()` counts UTF-16 code units. `source_bytes` is documented as UTF-8 bytes
(the unit the source cap uses), so a non-ASCII source reported too small a value.

## Evidence (file:line, current tree)

| Site | Before this fix | After this fix |
| --- | --- | --- |
| `src/core/ScriptClock.cpp:287` — `setSchedule` cap check | already `source.toUtf8().size()` (**ALREADY-FIXED**, no change) | unchanged |
| `src/core/ScriptClock.cpp:355` — `scheduleState()` `source_bytes` | `entry.source.size()` | `entry.source.toUtf8().size()` |
| `src/core/ScriptClock.cpp:463` — `stateJson()` `schedules[].source_bytes` | `entry.source.size()` | `entry.source.toUtf8().size()` |
| `src/core/ControlCommandsLivecode.cpp:198` — `livecode.schedule` reply `source_bytes` | `source.size()` | `source.toUtf8().size()` |

The finding's historical line 317 is the cap-side line in the older tree; on this line the
cap is already counted in UTF-8 bytes. The state-reporting sites above are the real defect.

## Regression tests

- `tests/src/core/ScriptClockTest.cpp` — new `ScriptClockTest::scheduleStateReportsUtf8Bytes`
  (declared line ~156, defined line ~393). The source `"-- é\nreturn 1"` is 13 UTF-16 units and
  14 UTF-8 bytes. It checks `scheduleState(id).source_bytes` and the `stateJson().schedules[]`
  entry for the same id. It removes its schedule before asserting, so a failure cannot leak
  state into later tests.
- `tests/control-livecode-commands.py` — `check_bar_edits_without_retrigger` gets a multi-byte
  comment (`é`) and a check that the `livecode.schedule` reply reports `len(source.encode("utf-8"))`.
  This covers site 4, which the C++ test cannot reach.

No new source files, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are
unchanged. No control command was added, so the `ControlCommandsSnapshot` canary is untouched.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-231-port.r1/logs/`.

Build environment note: the first build failed with `ccache: failed to create temporary file
under /run/user/1000/ccache-tmp` (EXIT=2, `build-1-red.log`). All later builds used
`CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen`, an environment variable only, with no config change.

| Step | Command (run from the worktree unless noted) | Result | Log |
| --- | --- | --- | --- |
| RED build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen cmake --build build --target ScriptClockTest -j2` | EXIT=0 | `build-3-red.log` |
| **RED test** | `cd build/tests && ctest -R '^ScriptClockTest$' --output-on-failure` | **EXIT=8**: `scheduleStateReportsUtf8Bytes` Actual 13, Expected 14; 19 passed, 1 failed | `test-2-red.log` |
| GREEN build | `CCACHE_TEMPDIR=… cmake --build build --target zene ScriptClockTest -j2` | EXIT=0 | `build-4-green.log` |
| **GREEN test** | `cd build/tests && ctest -R '^ScriptClockTest$' --output-on-failure` | **EXIT=0**, 100% passed | `test-3-green.log` |
| Proof: revert product fix | `git diff -- src/core/ScriptClock.cpp src/core/ControlCommandsLivecode.cpp > logs/fix.patch && git apply -R logs/fix.patch`, rebuild `zene ScriptClockTest` | apply-R EXIT=0; build EXIT=0 | `build-5-proof-reverted.log` |
| Proof: test on reverted tree | `ctest -R '^ScriptClockTest$' --output-on-failure` | **EXIT=8**: 13 vs 14; 19 passed, 1 failed | `test-4-proof-reverted.log` |
| Proof: re-apply fix | `git apply logs/fix.patch`, rebuild | apply EXIT=0; build EXIT=0 | `build-6-proof-restored.log` |
| Proof: test on fixed tree | `ctest -R '^ScriptClockTest$' -V` | **EXIT=0**: 20 passed, 0 failed | `test-5-proof-restored.log` |
| Python proof: revert line-198 fix only | `git diff -- src/core/ControlCommandsLivecode.cpp > logs/fix-livecode.patch && git apply -R …`, build `zene`, `ctest -R '^ControlLivecodeCommands$'` | build EXIT=0; **test EXIT=8**: `source_bytes=137 expected=138` | `build-8-py-red.log`, `test-8-py-red.log` |
| Python proof: re-apply | `git apply logs/fix-livecode.patch`, rebuild `zene`, ctest | apply EXIT=0; build EXIT=0; **test EXIT=0**, 100% passed | `build-9-py-green.log`, `test-9-py-green.log` |
| Affected family build | `cmake --build build --target ScriptBudgetActionTest -j2` | EXIT=0 | `build-7-family.log` |
| **Affected family test** | `cd build/tests && ctest -R '^(ScriptClockTest\|ScriptBudgetActionTest\|ControlLivecodeCommands)$' --output-on-failure` | **EXIT=0**, 3 of 3 executed and passed | `test-6-family.log` |
| Python check, verbose | `ctest -R '^ControlLivecodeCommands$' -V` | EXIT=0; `livecode.schedule reports UTF-8 source bytes ... ok` | `test-7-python-verbose.log` |

Family test count: 3 ctest entries executed (ScriptClockTest with 20 test functions;
ScriptBudgetActionTest; ControlLivecodeCommands). Nothing was run with zero tests.

### Ratchet gates

| Gate | Command | Exit | Log |
| --- | --- | --- | --- |
| Complexity | `bash tests/complexity-gate.sh --check` | **EXIT=1** | `gate-1-complexity.log` |
| File length | `bash tests/file-length-gate.sh --check` | **EXIT=1** | `gate-2-file-length.log` |

**Both gates fail on the base line too, and not because of this change.** I ran the same two
commands on a full `git archive HEAD src tests tools` export in `/tmp` (then removed it):
complexity EXIT=1 (`gate-5-base-complexity-full.log`) and file length EXIT=1
(`gate-6-base-file-length-full.log`). The `REGRESSION:` lines are identical between the base
and this tree (`diff` of the sorted sets was empty for both gates). The failures are:

- complexity: 6 functions over CCN 10 in files this change does not touch
  (`XyPadPairTest.cpp dotCentreAlong`, `DawProjectReadTracks.cpp parseNotes`,
  `ControlCommandsMixerTools.cpp adjust`, `ControlCommandsMixerStrip.cpp setStrip`,
  `ProjectRevisions.cpp rotateProjectRevision`, `ControlCommandsShortcuts.cpp importedOverrides`).
- file length: 13 files over 500 code lines that are not in `tests/file-length-baseline.tsv`
  (e.g. `src/core/TakeLane.cpp` 520, `src/core/SmfInterchangeReader.cpp` 504,
  `tests/src/gui/ArrangerTimelineTest.cpp` 689).

I did not fix these: they are unrelated to this bug, and fixing them means refactoring about
19 files, which is out of scope for one bug. I did not change any gate limit, baseline or
`tests/file-length-baseline.tsv`. Owner decision needed: a separate cluster for the gate debt.

Neither gate lists any function or file touched by this change (`ScriptClockTest.cpp`,
`ScriptClock.cpp`, `ControlCommandsLivecode.cpp`, `control-livecode-commands.py`).

## Not done

- Live-socket behaviour was checked only through the existing `ControlLivecodeCommands` test,
  which runs its own instance.
- The original commit's cap test `sourceByteCapUsesUtf8` was not ported. Its cap is already in
  UTF-8 on this line, and the existing `byteCapCountsBytes` (`ScriptClockTest.cpp`) covers it and
  passes, so it would be a duplicate.
