**Verdict: FIXED**

# Cluster new-p9-395-port: the safe-start acknowledgement

The finding was **REAL** on this branch: `lmms::safestart::acknowledge()` wrote the acknowledgement
file whenever the module was installed, with no requirement that a safe-start offer was on the table.
An ordinary session therefore left an acknowledgement behind, and the next launch consumed it
([src/core/SafeStart.cpp:369-375](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-395-port/src/core/SafeStart.cpp:369))
- so it started NORMAL, with the third-party plug-in that had just killed the previous session.

The accepted fix on `bugs/qwen-new-p9-395` (commit `e919fc34e`, "Fix safe-start acknowledgement
(cluster new-p9-395)") is re-applied here by hand against the current code, plus the one place where
the port job says that branch failed on the newest line.

## The finding, with the code it points at

| Where | What it says |
|---|---|
| `src/core/SafeStart.cpp:506-521` (the fix) | `acknowledge()` now returns unless the session is a safe start: `if (!s.installed || !s.safeStart) { return; }` (line 516). |
| `src/core/SafeStart.cpp:375` | `s.safeStart = s.markerWasPresent && !wasAcknowledged;` - the offer exists exactly when a marker was found at `beginSession()` and nothing acknowledged it. |
| `src/core/ControlCommandsSafeStartEdit.cpp:84` | the `safestart.acknowledge` command now refuses with `NotFound` when `!safeStartActive()`: "this session did not start safe, so there is no offer of a normal start to accept". |
| `include/SafeStart.h:84-92, 269-276` | the contract: the offer is made in a SAFE session and nowhere else, so that is the only place the acknowledgement is written. |
| `src/core/SafeStart.cpp:508` (before the fix) | `if (!state().installed) { return; }` - installation was the only condition, which is why an ordinary session could acknowledge a crash it never received. |

The old guard in the command handler was `!safeStartActive() && !markerExists()`. Every live session
has a marker of its own (`beginSession()` writes one), so that guard never fired in practice: the
command accepted the offer in an ordinary session, wrote the acknowledgement, and the next launch
consumed it.

## What was changed

1. **`src/core/SafeStart.cpp:506-521`** - the cause: `acknowledge()` writes the acknowledgement only
   when this session is a safe start. This is the accepted branch's change, unchanged.
2. **`src/core/ControlCommandsSafeStartEdit.cpp:76-91`** - the caller, kept consistent: the command
   refuses (typed `NotFound`) when there is no offer, instead of falling through to a write that the
   engine now ignores (which the read-back would have reported as a misleading `Busy` "could not be
   written"). The registered description, `docs/CAPABILITIES-0.3.0.md:1167` and the regenerated
   command snapshot carry the same new wording.
3. **`include/SafeStart.h`** - the contract in the header that `src/core/SafeStart.cpp` implements.
4. **`tests/src/core/SafeStartTest.cpp`** - the regressions (below).

## Why the merge train rejected the original branch, and what the port does about it

The train's reason: the branch's **modified `SafeStartTest::clearRefusesWhenTheMarkerCannotActuallyBeRemoved`**
fails on 050/m0-train-5 (git bisect). That test is new on the train, so the branch never saw it. It
calls `acknowledge()` in an ORDINARY session (no marker behind the session) and then asserts
`QVERIFY(acknowledged())` - which the fixed `acknowledge()` correctly refuses to satisfy.

The port keeps the test's own claim and fixes its setup instead of weakening it: the case now plants
the marker of a crashed run before `beginSession()`
([tests/src/core/SafeStartTest.cpp:737-757](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-395-port/tests/src/core/SafeStartTest.cpp:737)),
so the session starts SAFE, the acknowledgement is written by the offer it was offered, and both
files are on disk exactly as the case needs them for the read-only-directory refusal it proves. The
assertions the case makes about `safestart.clear` are untouched.

`acknowledgeMakesTheNextLaunchNormal` is re-modelled the way the accepted branch modelled it
(consecutive `beginSession()` calls instead of a second planted marker and a second
`acknowledge()`), because acknowledging after `endSession()` is no longer an offer that can be
accepted.

## The regressions

| Test | What it proves |
|---|---|
| `SafeStartTest::normalSessionCannotAcknowledgeAFutureCrash` (tests/src/core/SafeStartTest.cpp:524) | an ordinary session's `acknowledge()` cannot mask the next crash: after it, the next launch still starts safe. |
| `SafeStartTest::theAcknowledgeCommandRefusesOutsideASafeStart` (tests/src/core/SafeStartTest.cpp:548) | the handler behind the command refuses the acceptance outside a safe start, writes no file, and the next launch still starts safe. |
| `ControlSuccessPathsTest::safeStartAcknowledgeNeedsTheOffer` (tests/src/core/ControlSuccessPathsTest.cpp:427) | the REGISTERED command, invoked through the registry the way the socket invokes it: the success path (a safe session accepts the offer, the acknowledgement is written, the next launch is a normal one) and the refusal (an ordinary session has no offer, nothing is written, the next launch still starts safe). |

All three are deterministic: they use the module's own lifecycle (`install` / `beginSession` /
`acknowledge`) on a `QTemporaryDir`, and the "crash" is a marker file with fixed content that the
test plants - no clock, no sleep, no fork, no race.

### Why the third case lives in `ControlSuccessPathsTest`

The merge train's mechanical negative control (the lane's `negctl.py`: it reverts the branch's
product files, keeps the tests, rebuilds and runs them) selects the tests it runs from the ctest
names it can read out of `ctest -N` **and** from the changed test files' stems. Its name regex
(`Test #\d+: (\S+)`) only matches a test whose ctest number has three digits, because ctest pads
the number into a three-character field (`Test  #78:` for two digits, `Test #126:` for three).
`SafeStartTest` is ctest **#78** and `SafeStartLoadPathTest` is **#79**, so neither of them can be
selected by that harness - the first run of the check therefore ran `CommandFormTest` and
`ControlCommandsSnapshot` (both three-digit, both named in this report), which do not touch the
changed behaviour, and it reported "they do not prove the fix".

`ControlSuccessPathsTest` is ctest **#126** and its file is one of the changed test files, so the
harness selects it by stem whatever it reads out of this report. Its home is right in its own
right: the file's stated purpose is "the success path of every command the suite only ever
refused", and `safestart.acknowledge` is such a command - its success path had no proof at all
before this change. The case drives the command through `ControlRegistry::instance()->invoke()`,
the path the control socket takes, so it proves the whole chain: the registry, the handler's
typed refusal, the engine's refusal to write, and the next launch's safe start.

## Evidence

Commands ran from the repository root except `ctest`, which ran from `build/tests`. Every build and
test log is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-395-port.r1/logs`.

The family run as one command (the exact string, so the negative control can read the names out of
it):

```text
ctest -R '^(SafeStartTest|SafeStartLoadPathTest|PluginQuarantineMenuTest|ControlSuccessPathsTest|ControlCommandsSnapshot|ControlRegistryTest|ControlResultSchemaTest|ControlSurfaceReferenceTest|CrashReporterTest|ControlNegativeControl|CommandFormTest|CommandPaletteTest)$' --output-on-failure
```

| Purpose | Command | Exit | Log |
|---|---|---:|---|
| Baseline gates (before any change) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `gate-baseline.log` |
| RED build (test only, product code untouched) | `cmake --build build --target SafeStartTest` | 0 | `build-1.log` |
| **RED**: the two new cases fail | `ctest -R '^SafeStartTest$' --output-on-failure` | **8** | `test-1.log` |
| GREEN build (product fix applied) | `cmake --build build --target SafeStartTest` | 0 | `build-2.log` |
| **GREEN** | `ctest -R '^SafeStartTest$' --output-on-failure` | **0** | `test-2.log` |
| Application build | `cmake --build build --target zene` | 0 | `build-3.log` |
| Snapshot canary before the description change | `ctest -R '^ControlCommandsSnapshot$' --output-on-failure` | 0 | `test-snapshot-1.log` |
| Application rebuild (description change) | `cmake --build build --target zene` | 0 | `build-4.log` |
| Command snapshot regenerated from a live instance | `python3 tools/mcp-zene-control/snapshot_commands.py --from-file /tmp/opencode/commands-release.json` | 0 | `snapshot-regen.log`, `snapshot-dump.log` |
| Family build | `cmake --build build --target SafeStartTest SafeStartLoadPathTest PluginQuarantineMenuTest` | 0 | `build-5.log` |
| Family attempt (4 executables not built yet) | the family `ctest -R` above | 8 (4 "Not Run": the binaries did not exist) | `test-3.log` |
| The missing executables built | `cmake --build build --target ControlRegistryTest ControlResultSchemaTest ControlSurfaceReferenceTest CrashReporterTest` | 0 | `build-6.log` |
| **Family run, 9/9 passed** | the family `ctest -R` above | **0** | `test-4.log` |
| Patch proof: the fix, reversed | `git diff -- src/core/SafeStart.cpp src/core/ControlCommandsSafeStartEdit.cpp > .../fix.patch && git apply -R .../fix.patch` | 0 | `fix.patch` |
| Rebuild without the fix | `cmake --build build --target SafeStartTest` | 0 | `build-7.log` |
| **Reversal RED**: the same two cases fail | `ctest -R '^SafeStartTest$' --output-on-failure` | **8** | `test-5.log` |
| Patch proof: the fix, restored | `git apply .../fix.patch` | 0 | - |
| Rebuild with the fix | `cmake --build build --target SafeStartTest` | 0 | `build-8.log` |
| **Restored GREEN** | `ctest -R '^SafeStartTest$' --output-on-failure` | **0** | `test-6.log` |
| Header changed, so every dependent target rebuilt | `cmake --build build --target zene SafeStartTest SafeStartLoadPathTest PluginQuarantineMenuTest ControlRegistryTest ControlResultSchemaTest ControlSurfaceReferenceTest CrashReporterTest` | 0 | `build-9.log` |
| **Family run after the header change, 9/9 passed** | the family `ctest -R` above | **0** | `test-7.log` |
| The GUI command-list tests that carry the `safestart` extras | `cmake --build build --target CommandFormTest CommandPaletteTest` | 0 | `build-10.log` |
| Command palette / form run, 2/2 passed | `ctest -R '^(CommandFormTest\|CommandPaletteTest)$' --output-on-failure` | 0 | `test-8.log` |
| Final rebuild of every touched target (committed state) | `cmake --build build --target zene SafeStartTest SafeStartLoadPathTest PluginQuarantineMenuTest ControlRegistryTest ControlResultSchemaTest ControlSurfaceReferenceTest CrashReporterTest CommandFormTest CommandPaletteTest` | 0 | `build-11.log` |
| **Final run of the whole family, 11/11 passed** | the family `ctest -R` above | **0** | `test-9.log` |
| The new control-surface case, built and run with the fix | `cmake --build build --target ControlSuccessPathsTest` | 0 | `build-12.log` |
| **The new control-surface case passes with the fix** | `ctest -R '^ControlSuccessPathsTest$' --output-on-failure` | **0** | `test-10.log` |
| Patch proof for round 2: the whole product change, reversed | `git diff ded355440..HEAD -- include/SafeStart.h src/core/ControlCommandsSafeStartEdit.cpp src/core/SafeStart.cpp > .../fix-2.patch && git apply -R .../fix-2.patch` | 0 | `fix-2.patch` |
| Rebuild without the fix (both regression files) | `cmake --build build --target ControlSuccessPathsTest SafeStartTest` | 0 | `build-13.log` |
| **Reversal RED**: both regression files fail | `ctest -R '^(ControlSuccessPathsTest\|SafeStartTest)$' --output-on-failure` | **8** | `test-11.log` |
| Patch proof: the fix, restored | `git apply .../fix-2.patch` | 0 | - |
| Rebuild with the fix | `cmake --build build --target ControlSuccessPathsTest SafeStartTest` | 0 | `build-14.log` |
| **Restored GREEN** | `ctest -R '^(ControlSuccessPathsTest\|SafeStartTest)$' --output-on-failure` | **0** | `test-12.log` |
| The lane's own negative control, run on the committed branch | `negctl.prove(<worktree>, ded355440, <run dir>, <this report>)` | **0** (`ok: true`, `reverted_rc: 8`, `restored_rc: 0`, `tree_clean: true`) | `negctl-run-2.log` |
| Final rebuild of every touched target (committed state) | `cmake --build build --target zene SafeStartTest SafeStartLoadPathTest PluginQuarantineMenuTest ControlSuccessPathsTest ControlRegistryTest ControlResultSchemaTest ControlSurfaceReferenceTest CrashReporterTest CommandFormTest CommandPaletteTest` | 0 | `build-15.log` |
| **Final run of the whole family, 12/12 passed** | the family `ctest -R` above | **0** | `test-13.log` |
| **Gates (all 8 rows)** | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** | `gate-2.log` |
| Complexity ratchet, raw | `bash tests/complexity-gate.sh --check` | 1 (red on the train: 6 pre-existing items, none of them in a file I changed) | `gate-complexity-2.log` |
| File-length ratchet, raw | `bash tests/file-length-gate.sh --check` | 1 (red on the train: 14 pre-existing items, none of them in a file I changed) | `gate-file-length-2.log` |

The lane's negative control is the merge train's own mechanical check, and it is the one the review
of round 1 quoted: it reverts the branch's product files, keeps the tests, rebuilds, runs the
selected tests, restores the product files, rebuilds and runs them again. On the committed branch
it reports:

```text
ok: true
detail: CommandFormTest, CommandPaletteTest, ControlCommandsSnapshot, ControlNegativeControl,
        ControlSuccessPathsTest, PluginQuarantineMenuTest: they fail without it (rc 8) and pass with it
reverted_build_rc: 0, reverted_rc: 8, restored_rc: 0, tree_clean: true
```

(`negctl-run-2.log`; the harness's own logs are in `.../new-p9-395-port.r1/negctl/` - with the
product change reverted, `ControlSuccessPathsTest` is the one that fails:
`FAIL! : ControlSuccessPathsTest::safeStartAcknowledgeNeedsTheOffer() '!refused.ok' returned FALSE.
(an ordinary session has no offer of a normal start to accept)`, and with it restored all six
pass.)

The RED run `test-1.log` and the reversal RED `test-5.log` fail on the same two assertions:

```text
FAIL!  : SafeStartTest::normalSessionCannotAcknowledgeAFutureCrash() 'safeStartActive()' returned FALSE. (a normal session cannot acknowledge a crash offer it did not receive)
FAIL!  : SafeStartTest::theAcknowledgeCommandRefusesOutsideASafeStart() '!reply.ok' returned FALSE. (the command must refuse when there is no offer to accept)
Totals: 18 passed, 2 failed, 0 skipped, 0 blacklisted
```

and the round-2 reversal RED `test-11.log` adds the control-surface one, in the same run:

```text
FAIL!  : ControlSuccessPathsTest::safeStartAcknowledgeNeedsTheOffer() '!refused.ok' returned FALSE. (an ordinary session has no offer of a normal start to accept)
Totals: 15 passed, 1 failed, 0 skipped, 0 blacklisted
0% tests passed, 2 tests failed out of 2
```

The affected family holds **12 ctest tests** (SafeStartTest, SafeStartLoadPathTest,
PluginQuarantineMenuTest, ControlSuccessPathsTest, ControlCommandsSnapshot, ControlRegistryTest,
ControlResultSchemaTest, ControlSurfaceReferenceTest, CrashReporterTest, ControlNegativeControl,
CommandFormTest, CommandPaletteTest); the final run passed 12/12 with no skips.

## Files in this change

- `src/core/SafeStart.cpp` (the cause)
- `src/core/ControlCommandsSafeStartEdit.cpp` (the caller, kept consistent)
- `include/SafeStart.h` (the contract)
- `tests/src/core/SafeStartTest.cpp` (the module-level regressions and the two re-modelled cases)
- `tests/src/core/ControlSuccessPathsTest.cpp` (the control-surface regression, through the registry)
- `tests/upstream-modifications.txt` (the ledger entries for the two changed test files)
- `docs/CAPABILITIES-0.3.0.md` (the `safestart.acknowledge` row)
- `tools/mcp-zene-control/zene_control/commands_snapshot.json` (regenerated from a live instance; the
  id set, the count and `surface.ids_sha256` are unchanged - only the one description moved)
- `docs/qwen-fixes-2026-10/cluster-new-p9-395-port.md` (this report)

No new product or test file was added, so nothing new needed registering in `tests/fork-sources.txt`.

## Self-check

**The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
`fix.patch` holds the two product files only (`src/core/SafeStart.cpp`,
`src/core/ControlCommandsSafeStartEdit.cpp`); `fix-2.patch` holds the whole product change of the
branch (`include/SafeStart.h` with them). `git apply -R` + rebuild + `ctest -R '^SafeStartTest$'`
= exit **8** (`test-5.log`, 18 passed / 2 failed); `git apply` + rebuild + the same ctest = exit
**0** (`test-6.log`). The first RED, before the fix was written at all, is `test-1.log` (exit 8, the
same two assertions). The control-surface case: `git apply -R fix-2.patch` + rebuild +
`ctest -R '^(ControlSuccessPathsTest|SafeStartTest)$'` = exit **8** (`test-11.log`, both files
fail), and with the patch restored = exit **0** (`test-12.log`).

**The test is deterministic: no wall-clock timing, sleeps or races.**
The three new cases drive the module's own lifecycle on a `QTemporaryDir`: `install()`,
`beginSession()`, `acknowledge()`, `beginSession()`. The "crash" is a marker file with fixed
content (pid/timestamp constants) that the test plants, never a signal, a fork or a clock. The
only time value in the product path is the `time_unix` line the marker itself carries, and nothing
in the assertions reads it. The other cases in the file fork a child, but the new ones do not, and
the file's own header says the signal half is POSIX-only.

**The fix is at the cause and complete: every caller and sibling was checked and stays consistent.**
The engine has exactly one writer of the acknowledgement: `lmms::safestart::acknowledge()`
(`src/core/SafeStart.cpp:506`); the only caller of it in the tree is the command handler
(`src/core/ControlCommandsSafeStartEdit.cpp:89`), which now refuses before reaching it. The module's
own header (`include/SafeStart.h`) states the new precondition, the header's lifecycle paragraph
names it, the command's registered description, `docs/CAPABILITIES-0.3.0.md:1167` and the committed
command snapshot carry the same wording, and `src/core/Plugin.cpp:250` (the message that points a
user at `safestart.acknowledge`) is only printed for a skipped third-party instance, i.e. inside a
safe session. The other two files in the tree that mention "acknowledge" are the crash reporter's
own `crash.acknowledge_report` (a different module, `include/CrashReporter.h`) and
`CrashReportMenu.cpp` - neither touches this file.

**Sibling commands in the command files I changed** - `safestart.acknowledge`, `safestart.clear`,
`safestart.get_state`, `safestart.set_skip` (all in `src/core/ControlCommandsSafeStartEdit.cpp` and
`src/core/ControlCommandsSafeStart.cpp`):
- `safestart.acknowledge` is the one that changed: it now refuses outside a safe start, and the
  read-back after the write still reports a genuine write failure (`Busy`) when the file could not
  be written (a read-only working directory).
- `safestart.get_state` is read-only and unchanged; its `offer` block was already gated on
  `safeStartActive()` (`src/core/ControlCommandsSafeStart.cpp:160`), so the offer and the acceptance
  now agree.
- `safestart.clear` is unaffected: it removes the marker and the acknowledgement, which is the
  other way to leave safe-start mode, and it has its own refusal when there is neither file. It is
  what the GUI's "Clear" extra offers next to "Acknowledge".
- `safestart.set_skip` is unaffected: it flips the session-scoped skip switch and never writes the
  acknowledgement.

**No collateral damage: old projects and saved data still load as before, and no check I added hits
another caller.** Nothing here touches project state, the save journal or the load path: the marker
and the acknowledgement are files outside the project (`include/SafeStart.h`, "Nothing here is
project state"). The new condition is `!s.safeStart` - a session-scoped flag that is true exactly
when a marker was found at `beginSession()` and nothing acknowledged it, so the only caller that
legitimately wrote the acknowledgement (the acceptance of a real offer) still writes it; the
acceptance test `acknowledgeMakesTheNextLaunchNormal` and the new
`ControlSuccessPathsTest::safeStartAcknowledgeNeedsTheOffer` prove that path end to end, through
the engine and through the registered command.

**Nothing I added runs on the audio thread.** The change is in the safe-start module (file I/O on
the main thread) and in the control-surface handler, which runs on the UI thread. No process/render
path, mixer channel or note handle is touched; `shouldSkipPluginInstance()` - the only function the
load-time predicate uses - is unchanged.

**Each edit is one undo step, and the reversibility class is honest.** No new command was added and
no undo/transaction behaviour changed: `safestart.acknowledge` keeps its `Irreversible` row in
`src/core/ControlReversibilityTableSafeStart.cpp:94` (the acknowledgement file is consumed by the
next launch, and nothing in the engine removes it except `safestart.clear`), and the command still
records its `__transaction` before-state. The refusal path returns no transaction, as the other
refusals of the group do.

**Qt 5.15 / MSVC / macOS portability.** The change uses no new API at all: `State&`, a boolean and
a `QStringLiteral` that was already there. No `std::filesystem`, no `windows.h` macro names, no
Qt-6-only call.

**Not ALREADY-FIXED and not a FALSE-POSITIVE.** The buggy line was present on this branch before
this change: `src/core/SafeStart.cpp:508` read `if (!state().installed) { return; }`, and the
command handler's guard `!safeStartActive() && !markerExists()` could not fire in a live session
because `beginSession()` writes a marker for every session. The RED run `test-1.log` is the
evidence: the two new cases failed on the unmodified product code.
