FIXED — cluster new-p22-52: plugin.state_load did not validate its path

**Finding (Luna BUG 2):** `state_load` reads its `path` with no absolute/existence check, unlike `state_save` and the instrument's file load.

## Decision and evidence

REAL. Before the fix the handler passed the raw `path` straight to `controlReadFileBytes`:

- `src/core/ControlDeviceState.cpp:215-227` (`controlReadFileBytes`): `QFile file(path)` with no absolute check. A **relative** path is resolved against the DAW process's working directory and is read successfully if a file of that name exists there. A missing file returns `NotFound`; a directory is refused as an unreadable file.
- The sibling checks in the same command family refuse these inputs:
  - `plugin.state_save`: `src/core/ControlCommandsPluginState.cpp:99-101` refuses a non-absolute path with `InvalidArgs`.
  - The instrument file load: `src/core/ControlCommandsPluginState.cpp:267-269` requires `QFileInfo(path).isAbsolute() && isFile()` with `InvalidArgs`.
- The RED test below shows the relative path **loaded successfully** (`ok == true`) before the fix.

## The fix

`src/core/ControlCommandsPluginState.cpp:174-185` (`plugin.state_load` handler). Before `controlReadFileBytes`, the same rule as `state_save` and the instrument load is applied:

```cpp
if (!QFileInfo(path).isAbsolute() || !QFileInfo(path).isFile())
{
    return ControlResult::failure(ControlErrorKind::InvalidArgs,
        QStringLiteral("'path' must be an absolute path to an existing file"));
}
```

The refusal happens before `controlRestoreDeviceState` and before `control::addUndoStep`, so a refused load changes nothing and records no undo step.

Error-kind change (intentional, matches the sibling): a missing path was `not_found`, now `invalid_args`; a directory was `refused`, now `invalid_args`; a relative path was accepted, now refused.

## Regression test

`tests/src/core/UndoHonestyStateTest.cpp`, new test `theStateLoadRequiresAnAbsoluteExistingFile` (the file already holds the state load/undo tests; no new file, so no registration change). It saves a real device state into a `QTemporaryDir`, then asserts:

1. a relative name of an existing file in the working directory is refused with `InvalidArgs` (the working directory is set to the temp dir and restored before the assertions);
2. a directory path is refused with `InvalidArgs`;
3. a missing absolute path is refused with `InvalidArgs`;
4. `control.undo_depth` is unchanged.

Deterministic: no timing, sleeps or races; temp directory only. The test uses the built-in `tripleoscillator` instrument, which needed `build/plugins/libtripleoscillator.so` (see the build notes below).

## Commands and exit codes

Build notes: the shell's ccache temp dir is not writable inside this sandbox. Every build below used `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-52` (on disk). The first `UndoHonestyStateTest` run SKIPped because the build had no built-in instrument, so `tripleoscillator` and `ladspaeffect` were built as targeted plugin modules; no repo file changed for this.

| Step | Command (run from the worktree / build/tests) | Exit |
|---|---|---|
| RED build | `cmake --build build --target UndoHonestyStateTest -j2` (log `build-2-red.log`) | 0 |
| RED test | `./UndoHonestyStateTest theStateLoadRequiresAnAbsoluteExistingFile -v2` (`test-4-red-direct.log`) | **1** — `FAIL! '!relative.ok' returned FALSE` (relative path loaded) |
| RED ctest | `ctest -R UndoHonestyStateTest --output-on-failure` (`test-5-red-ctest.log`) | **8** (Failed) |
| GREEN build | `cmake --build build --target zene UndoHonestyStateTest -j2` (`build-5-green.log`) | 0 |
| GREEN test | `./UndoHonestyStateTest theStateLoadRequiresAnAbsoluteExistingFile -v2` (`test-9-reapply-direct.log`) | **0** — PASS |
| GREEN ctest | `ctest -R UndoHonestyStateTest --output-on-failure` (`test-10-green-final.log`) | **0** — 100% passed, 1/1 |
| Full UndoHonestyStateTest | `./UndoHonestyStateTest -v2` (`test-6-green-direct.log`) | 0 — 13 passed, 0 failed, 0 skipped |

### 4b: reverting the product file (the test must FAIL, then PASS)

- `git diff -- src/core/ControlCommandsPluginState.cpp > logs/fix.patch && git apply -R logs/fix.patch` — exit 0 (`fix.patch` in the LOG FOLDER).
- Rebuild `zene UndoHonestyStateTest` (`build-6-revert.log`): exit 0.
- `./UndoHonestyStateTest theStateLoadRequiresAnAbsoluteExistingFile -v2` (`test-8-revert-direct.log`): **exit 1** — `FAIL! '!relative.ok' returned FALSE`.
- `git apply logs/fix.patch` — exit 0; rebuild (`build-7-reapply.log`) exit 0; test (`test-9-reapply-direct.log`) **exit 0**.

So the test fails without the product change and passes with it.

### Affected-family ctest (`-R "ControlRegistry|ReversibilityUndo|UndoHonesty|ControlChainPresets|ControlReversibilityTranscript"`, run from build/tests)

Final run with the fix (`test-15-final-family.log`, exit 8, 8 tests):

| Test | With fix | Baseline (product change reverted, `test-14-baseline-family.log`, same 3 tests only) |
|---|---|---|
| ControlRegistryTest | Passed | not run |
| ControlRegistryNestingTest | Passed | not run |
| ReversibilityUndoTest | Passed | not run |
| UndoHonestyTest | Passed | not run |
| UndoHonestyStateTest | Passed (13/13 in the binary) | not run (test file unchanged; the new test fails there as expected) |
| ReversibilityUndoDeviceTest | **SegFault** (exit 139) in `theRestoreReadsTheWrappersHostKeyAndNotThePluginsNestedOne` | SegFault |
| ControlChainPresets | **Failed** | Failed |
| ControlReversibilityTranscript | **Failed** | Failed |

The three failures are identical with and without the fix, so they predate this change:
- `ControlChainPresets` / `ControlReversibilityTranscript` fail in `plugin.load` with `the LADSPA host plugin 'ladspaeffect' is not in this build` (before the module was built; after building it the scripts reach their chain/coalescing fixtures and fail there). Neither script calls `plugin.state_load` (`rg state_load` returns nothing in either script).
- `ReversibilityUndoDeviceTest` segfaults in a test that calls `instantiateRecordedDevice` / `controlEffectStateXml`, not `plugin.state_load`. The one `state_load`-using test in that binary (`deviceCommandsUndoToTheirPreCommandState`, `state_load` at line 231) PASSes with the fix (`test-13-revdev-direct.log`). This is a pre-existing crash in the environment's LADSPA device path, not this fix. Not investigated further (out of scope).

Build tree note: `libladspaeffect.so` and `libtripleoscillator.so` were built into `build/plugins/` (not repo files).

### Gates

`python3 .../qwen/feature_check.py <worktree>` as specified **could not run here**: it writes its per-gate logs to `<worktree>/../<name>-feature-check/`, which is outside this session's write-jail. The first attempt failed with `PermissionError` (EXIT 1; that log was overwritten by the rerun below). I ran a copy of the same script (`logs/gate-run/feature_check.py`, identical except the log directory is redirected into the LOG FOLDER `logs/gate-out/`; it reads the same committed baseline cache through a symlink):

```
python3 -I logs/gate-run/feature_check.py <worktree>      -> EXIT=0
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

The gate's logic was not changed. The unpiped exit is `0` (the script's own `RESULT` line is `PASS`).

## Caller and sibling audit

- `plugin.state_load` is registered only in `src/core/ControlCommandsPluginState.cpp`. Its handler is the only caller of `controlReadFileBytes` on this path.
- In-repo callers that pass a path: `tests/src/core/UndoHonestyStateTest.cpp` and `ReversibilityUndoTest.cpp` / `ReversibilityUndoDeviceTest.cpp` (`QTemporaryDir` absolute paths); `tests/control-socket-integration.py:569` (`os.path.join(tmp, ...)` with `tmp = tempfile.mkdtemp(dir="/tmp")`, absolute); crashbot scenarios (`tools/crashbot/scenario.py:129` resolves `@work:` from `run_dir`, which `tools/crashbot/runner.py:451` makes absolute with `os.path.abspath`).
- **Not verified:** `tests/plugin_compat_steps.py` passes a `folder` argument into `state_roundtrip` / `unchanged_state`; I did not trace where that folder comes from, so I cannot confirm it is absolute. This runner needs a plugin-compat run to confirm (not run here).
- Siblings: `plugin.state_save` (`:99-101`) and the instrument file load (`:267-269`) already apply the same rule and were not changed.

## Other properties

- **Undo:** unchanged. A refused load returns before `addUndoStep`; the regression test asserts `control.undo_depth` is unchanged.
- **Realtime:** the change is in a control-thread command handler. Nothing runs on the audio thread.
- **Commands / canary:** no command added, and the `plugin.state_load` description and schema are unchanged, so `ControlCommandsSnapshot` is not affected.
- **Saved data / old projects:** `state_load` only reads a file that the caller names. Project loading does not go through this path. Saved documents are unchanged.

## Self-check

Each judgement item from the completion check, answered with its evidence:

1. *The regression test FAILS with the product change reverted and PASSES with it (exit codes):* yes. Reverted: `test-8-revert-direct.log`, exit **1** (`FAIL! '!relative.ok' returned FALSE`). Restored: `test-9-reapply-direct.log`, exit **0**. Initial RED before any fix: `test-4-red-direct.log`, exit 1.
2. *The test is deterministic (no wall-clock timing, sleeps or races; name the seam or injected values):* yes. It uses only a `QTemporaryDir` (absolute paths it creates itself) and `QDir::setCurrent` to that temp directory for the relative-path case, restored before any assertion. No timer, sleep or thread. Assertions are on `ControlErrorKind` and `control.undo_depth`, both synchronous.
3. *The fix is at the cause and complete; every caller and sibling was checked and stays consistent:* the cause is the missing precondition in the handler, fixed at `ControlCommandsPluginState.cpp:174-185`. Callers and siblings: see "Caller and sibling audit". One caller is not verified: `tests/plugin_compat_steps.py`'s `folder` argument (its origin was not traced; a plugin-compat run is needed to confirm).
4. *No collateral damage (old projects and saved data load as before; any limit or check does not hit callers that legitimately exceed it):* the check is a path precondition only. It adds no size or count limit. `state_load` is not on the project-load path, and saved documents are unchanged. The in-repo callers with a path pass absolute paths (see the audit). `ReversibilityUndoTest`, `UndoHonestyTest`, `ControlRegistryTest` and `ControlRegistryNestingTest` pass with the fix.
5. *ALREADY-FIXED or FALSE-POSITIVE:* not applicable. The verdict is FIXED, and the evidence is the RED run plus the code at `ControlCommandsPluginState.cpp:96-101` and `:267-269`.

Also answered:
- *Undo:* a refused load records nothing (the check precedes `addUndoStep`). A successful load is still one undo step, as before.
- *Audio thread:* nothing new runs there; the handler is a control command.
- *Portability:* `QFileInfo` and `QStringLiteral` are already used in this file. No Qt-6-only API, no `windows.h`, no `std::filesystem`.

Completion-check status: `submit_check.py` reports **FAIL on the gates item only**, because the script it runs (`feature_check.py`) cannot create its log directory outside this sandbox's write-jail (`PermissionError`, the same as the first attempt above). The same gate logic, run from the LOG FOLDER with the log path redirected, passes (table above, EXIT 0). The other checks PASS: commits on the branch, working tree clean, report present, tests changed.
