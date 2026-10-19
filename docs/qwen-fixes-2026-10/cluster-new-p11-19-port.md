# Cluster new-p11-19-port: crash report acknowledgement marker (port onto 050/m0-train-5)

**Verdict: FIXED**

## What the port is, and why the accepted fix could not be merged

The accepted fix on `bugs/qwen-new-p11-19` (commit `5a7e17007`, "fix(crash): verify
acknowledgement marker (cluster new-p11-19)") is re-applied here **by hand** against the
newest line - no merge, no cherry-pick, no rebase. It was written against a base
(`6d1a1143d`) whose `crash.acknowledge_report` had no "already acknowledged" pre-check.
The newest line has one (`src/core/ControlCommandsCrash.cpp:294` in the unmodified file;
`:303` after this change), and that pre-check is
what made the accepted branch's own test fail here: with a DIRECTORY at the
`offered`-marker path the pre-check fired first and answered `Refused` ("ALREADY
acknowledged"), while the accepted test asserted `Busy`.

That is the same conflation the finding names, one layer earlier: the pre-check asks
"does a path exist at the marker name?" where the question is "was the marker written?".
The reporter writes the sentinel as a FILE (`writeSmallFile()` opens it for writing,
`src/core/CrashReporter.cpp:170-184`), so a path that merely EXISTS is not an
acknowledgement. The port therefore applies the accepted fix's rule - *the marker is a
valid file, and the report is no longer pending* - to BOTH places that decide the state
(the pre-check and the success), and keeps the read side of the same state consistent.

## File:line evidence

| File | Evidence |
|---|---|
| `src/core/ControlCommandsCrash.cpp:303` | The "ALREADY acknowledged" refusal now fires only when the marker path is a FILE (`QFileInfo(...).isFile()`). A directory at that path was never written and cannot be written, so it is not an acknowledgement and the refusal would be a claim about a state that never happened. |
| `src/core/ControlCommandsCrash.cpp:319-330` | The success is READ BACK, as the accepted fix requires: the marker must be a FILE the reporter wrote **and** `hasPendingReport()` must read back false. A path that merely exists is not enough, so the command can no longer report `acknowledged: true` for a marker it did not write. The reply reuses the same `pending` readback value. |
| `src/core/ControlCommandsCrash.cpp:230-234` | The sibling read: `crash.list_reports`' `offered` field now uses the same rule as the write (`isFile()`), so the read and the write agree on what "offered" means. `pending` is untouched - it stays the module's own predicate (`hasPendingReport()`), which the reply's `note` names. |
| `tests/src/core/CrashReporterTest.cpp:519-567` | `acknowledge_refuses_directory_at_offered_marker`: builds a real report, puts a real DIRECTORY at the `offered`-marker path, calls the registered `crash.acknowledge_report` command and asserts a typed `busy` refusal, that the report file survives, that the blocking directory is untouched, and that `crash.list_reports` does not report the report as `offered`. |
| `tests/src/core/CrashReporterTest.cpp:107-115` | The `cleanup()` slot: the engine is torn down again after that one case, so the cases around it (which must not get an engine) are unaffected. |
| `tests/upstream-modifications.txt:2409` | Records the regression added to the inherited `CrashReporterTest` source. |

The fault state is reached through the real path: the report is written where the
reporter writes it (`pendingReportPath()`), the directory is created with
`QDir::mkdir()` at the marker path the reporter uses
(`crashreporter::kOfferedMarkerName`), and the command is invoked through
`ControlRegistry::invoke("crash.acknowledge_report")` - the same seam a client uses.

## Evidence table

Every command below was run in this worktree. CTest ran from `build/tests`; all output
is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-19-port.r1/logs`.

| Proof | Exact command | Exit | Result / log |
|---|---|---:|---|
| Baseline, train 5 before any change | `cd build/tests && ctest -R '^CrashReporterTest$' --output-on-failure` | 0 | 1/1 passed. `test-1.log`. |
| RED, test added, product code untouched | `cd build/tests && ctest -R '^CrashReporterTest$' --output-on-failure` | 8 | `FAIL! ... Compared values are not the same: Actual (result.errorKind) 5, Expected (Busy) 4` at `tests/src/core/CrashReporterTest.cpp(554)`; 13 passed, 1 failed. `test-2.log`. |
| GREEN, fix applied | `cd build/tests && ctest -R '^CrashReporterTest$' --output-on-failure` | 0 | 1/1 passed. `test-3.log`. |
| Reversal RED, product diff removed | `cd build/tests && ctest -R '^CrashReporterTest$' --output-on-failure` | 8 | same `errorKind 5 != 4` failure. `test-4.log`. |
| Restored GREEN | `cd build/tests && ctest -R '^CrashReporterTest$' --output-on-failure` | 0 | 1/1 passed. `test-5.log`. |
| Affected family, first run | `cd build/tests && ctest -R 'Crash|crash' --output-on-failure` | 8 | 2/5 passed; 3 tests `Not Run` because their binaries were not built yet (`CrashReporterArmTest`, `RecordingCrashLossTest`, `CrashReportMenuTest`). `test-6.log`. |
| Affected family, after building those 3 targets | `cd build/tests && ctest -R 'Crash|crash' --output-on-failure` | 0 | 5/5 passed: CrashReporterArmTest, CrashReporterTest, RecordingCrashLossTest, CrashReportMenuTest, ControlCrashReporter. `test-7.log`. |
| Command-surface canary | `cd build/tests && ctest -R '^ControlCommandsSnapshot$' --output-on-failure` | 0 | 1/1 passed (no command id, description or schema changed). `test-8.log`. |
| GREEN, final test (with the `list_reports` assertion added) | `cd build/tests && ctest -R '^CrashReporterTest$' --output-on-failure` | 0 | 1/1 passed. `test-9.log`. |
| Reversal RED, final product diff removed | `cd build/tests && ctest -R '^CrashReporterTest$' --output-on-failure` | 8 | `FAIL! ... Actual (result.errorKind) 5, Expected (Busy) 4` at `tests/src/core/CrashReporterTest.cpp(554)`; 13 passed, 1 failed. `test-10.log`. |
| Restored GREEN, final product diff re-applied | `cd build/tests && ctest -R '^CrashReporterTest$' --output-on-failure` | 0 | 1/1 passed. `test-11.log`. |
| Final family + canary | `cd build/tests && ctest -R 'Crash|crash|ControlCommandsSnapshot' --output-on-failure` | 0 | 6/6 passed. `test-12.log`. |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | RESULT: PASS (8 rows, no FAIL). `gate-1.log`. |

## Build and reversal commands

| Command | Exit | Log |
|---|---:|---|
| `cmake --build build --target CrashReporterTest` (baseline) | 0 | `build-1.log` |
| `cmake --build build --target CrashReporterTest` (RED build) | 0 | `build-2.log` |
| `cmake --build build --target CrashReporterTest zene` (GREEN build) | 0 | `build-3.log` |
| `cmake --build build --target CrashReporterTest` (reversal RED build) | 0 | `build-4.log` |
| `cmake --build build --target CrashReporterTest zene` (restored GREEN build) | 0 | `build-5.log` |
| `cmake --build build --target CrashReporterArmTest RecordingCrashLossTest CrashReportMenuTest` | 0 | `build-6.log` |
| `cmake --build build --target CrashReporterTest` (final test build) | 0 | `build-7.log` |
| `cmake --build build --target CrashReporterTest` (final reversal RED build) | 0 | `build-8.log` |
| `cmake --build build --target CrashReporterTest zene` (final GREEN build) | 0 | `build-9.log` |
| `git diff -- src/core/ControlCommandsCrash.cpp > <logs>/fix.patch` | 0 | the product-only diff used for the reversal proof |
| `git apply -R <logs>/fix.patch` | 0 | product diff removed before the reversal RED run |
| `git apply <logs>/fix.patch` | 0 | product diff restored before the final GREEN run |

## Self-check

**The regression test FAILS with the product change reverted and PASSES with it.**
Reverting only the product hunks (`git apply -R <logs>/fix.patch`), rebuilding and running
`ctest -R '^CrashReporterTest$' --output-on-failure` from `build/tests` exits **8** with
`Actual (result.errorKind) 5, Expected (lmms::ControlErrorKind::Busy) 4` - the newest
line's "ALREADY acknowledged" pre-check answers `Refused` for a directory at the marker
path (`test-10.log`). Re-applying the patch and rebuilding gives exit **0** (`test-11.log`).
The first RED run, before the product code was touched at all, failed the same way
(`test-2.log`). No test was weakened: the case is new, and the assertions are the
accepted branch's ones plus the `crash.list_reports` read.

**The test is deterministic.** No clock, no sleep, no retry, no race: the fault state is
built with `QFile::write` for the report and `QDir::mkdir` for the directory, both
synchronous, and the command is invoked in-process through
`ControlRegistry::instance()->invoke(...)`. There is no timing assertion anywhere in the
case; the reporter's own timestamps are not read. The only seam is the reporter's public
API (`install`, `pendingReportPath`, `kOfferedMarkerName`), which is what the command
itself uses.

**The fix is at the cause and complete.** The cause is the conflation "a path exists at
the marker name" = "the marker was written". Both places in
`handleAcknowledgeReport` that answer that question now use the same rule
(`src/core/ControlCommandsCrash.cpp:303` and `:319-330`), and the read side that
publishes the same state (`crash.list_reports`' `offered`,
`src/core/ControlCommandsCrash.cpp:230-234`) uses it too. Checked and left consistent:
`crash.discard_report` answers a different question ("was there something at that path
before the unlink"), and it already reads the unlink back
(`pathsLeftAfterDiscard`, `src/core/ControlCommandsCrash.cpp:359-365`), so a directory at
the marker path is reported there as "could NOT be discarded" - honest, and unchanged;
the module's own `hasPendingReport()` is untouched, because it is the module's predicate
and the reply says so. The reply's `pending` field now carries the same value the
success decision used, so the reply cannot disagree with the decision that produced it.

**No collateral damage.** The change is confined to the state "a non-file occupies the
`offered`-marker path". Every normal path is untouched: a real marker file still refuses
a second acknowledge with `Refused` (the accepted branch's behaviour, and what
`tests/control-crash-reporter.py` asserts), an absent marker still writes the sentinel
and still answers `acknowledged: true`, and `crash.list_reports` still reports
`offered: true` once the sentinel is a file. No project state, no saved data, no
project file and no undo step is involved: the command touches only the reporter's
sentinel, as before. The command surface is unchanged - no new command, no changed
id, description or schema (the `ControlCommandsSnapshot` canary passes), so the MCP
bridge and the GUI menu are unaffected.

**Realtime, undo and commands.** Nothing was added to an audio-thread path; the changed
code runs in a control-command handler on the UI thread and does the same file I/O it
did before. No new command was added, and no reversibility class changed:
`crash.acknowledge_report` stays `Irreversible` (`src/core/ControlReversibilityTableScanAndCrash.cpp:155`),
because the sentinel it writes still has no inverse - the fix only makes the command
tell the truth about whether it wrote one. A failed acknowledge still records no
transaction (`countsAsChangedResult` in `src/core/ControlRegistry.cpp:362-366` keys on
`result.ok`), so a refused call still leaves the undo history as it was.

**The verdict is FIXED, not ALREADY-FIXED.** The newest line does stop the accepted
branch's exact false success - its pre-check answers `Refused` before the write, so
`acknowledged: true` cannot be reported for a marker the reporter never wrote. But it
answers with a false claim about the state ("ALREADY acknowledged" for a path that was
never written), and the success path's own check was still "a path exists" rather than
"the marker is a file and the report is no longer pending", which is the contract the
accepted fix was written to establish (`src/core/ControlCommandsCrash.cpp:292-330`
before and after this change, quoted in the evidence table above). The ported rule
closes both, and the regression proves it.

**Sibling commands in the files I changed** (`crash.*`, all registered in
`src/core/ControlCommandsCrash.cpp`): `crash.list_reports` - read side, updated to the
same marker rule and covered by the new case; `crash.acknowledge_report` - the fix
itself; `crash.discard_report` - deletes the report and the sentinel and reads the
unlink back, unchanged and consistent (a directory at the marker path is reported as
"could NOT be discarded", which is true); `crash.enable` / `crash.disable` - arm and
disarm the signal handlers, they never look at the marker path, unaffected;
`crash.upload_report` - a documented refusal, it never looks at the marker path,
unaffected.

**Portability.** `QFileInfo::isFile()` and `QFileInfo::exists()` are both plain Qt
(non-Windows-specific) and the change uses nothing newer than what the file already
uses; no `windows.h` name, no `std::filesystem`, no platform branch. The reporter's
module code is untouched, so its Windows no-op build is unchanged.

## Files committed

- `src/core/ControlCommandsCrash.cpp` - the fix (marker-is-a-file rule for the refusal,
  the success read-back and the `offered` read).
- `tests/src/core/CrashReporterTest.cpp` - the regression `acknowledge_refuses_directory_at_offered_marker`
  and the `cleanup()` that tears the engine down after it.
- `tests/upstream-modifications.txt` - the ledger entry for the inherited test file.
- `docs/qwen-fixes-2026-10/cluster-new-p11-19-port.md` - this report.
