# Cluster new-p10-207-port: `--report` is silently ignored outside `master`

**Verdict: FIXED**

## The finding on this branch

REAL, and still unfixed on the newest line before this change. On this tree (base
`ded355440`, the `050/m0-train-5` line):

- `src/core/main.cpp:912-922` parses `--report <path>` into `masteringReportPath`
  for **every** action;
- the only reader is the mastering branch, `src/core/main.cpp:1398-1399`
  (`writeMasteringReport( job, masteringReportPath, &error )`);
- the help text documents the option under **"Options for \"master\""** only
  (`src/core/main.cpp:294-306`).

So `zene render project.mmp --report report.json` accepted the path, stored it,
rendered, exited 0 and never wrote the report. Measured on the unmodified tree
before the fix (child process, same command the regression drives):

```text
$ build/zene --allowroot render <fixture>.mmp --output <out>.wav --report <report>.json
Loading project...
Done
EXIT=0
# report.json: not written
```

## Why the original branch's fix failed on train 5

The accepted fix on `bugs/qwen-new-p10-207` was the same refusal, but it refused
through `usageError()`, which prints with `qCritical()`
(`src/core/main.cpp:331-336`). **On this toolchain that message never reaches
stderr.** Qt 6.12 here writes its log to the system journal unless stderr is a
console, so a `qCritical()` in a headless child is swallowed. Measured with a
6-line Qt program against the same Qt as the build:

```text
$ qcritical-test                       # stderr redirected to a file
stderr: (empty)
$ QT_FORCE_STDERR_LOGGING=1 qcritical-test
hello critical
```

The original regression asserted `output.contains("only valid with master")` on
the child's stdout+stderr, so on train 5 it failed on the *message*, not on the
behaviour: the refusal itself worked (exit code 1) but said nothing. That run is
recorded here: `logs/test-2.log`, `FAIL! : MasteringTest::reportOptionIsRejectedOutsideMaster()
'output.contains("only valid with master")' returned FALSE. ()` - the empty
parentheses are the child's empty output.

The port therefore keeps the refusal and makes it **visible**: the message goes
to stderr directly, the way this file's other CLI refusals do (`dump:` at
`src/core/main.cpp:727`, `compress:` at `:782`, `master:` at `:1401`, the control
socket at `:1323`). A refusal nobody can read is not a refusal.

## The fix

`src/core/main.cpp:1185-1204` - after the complete option parse (so the argument
order does not matter) and before any project access or rendering:

```cpp
	if( !mastering && !masteringReportPath.isEmpty() )
	{
		fprintf( stderr, "\n--report is only valid with master.\n\nTry \"%s --help\" for more "
			"information.\n\n", qApp->arguments()[0].toUtf8().constData() );
		return EXIT_FAILURE;
	}
```

`--report` is refused for every action except `master`, which is the only action
that writes the document (the control surface's `mastering.run` reads it back
from its `zene master ... --report <path>` child,
`src/core/ControlCommandsMasteringRun.cpp:211-215`). Nothing else changed: the
option's parse, the help text, the master action and the report writer are
untouched, and no project or saved data is affected (the check is on the command
line only, before anything is loaded).

## The regression

`tests/src/core/MasteringTest.cpp:499-541` -
`MasteringTest::reportOptionIsRejectedOutsideMaster()`. It drives the **real**

CLI (`zene --allowroot render <fixture> --output <out> --report <report>`) and
asserts: the child exits non-zero, the diagnostic names the option, no report
file was written, and the render did not happen either (the refusal is before
the render, not after it).

The application binary's path is compiled in
(`tests/CMakeLists.txt:1349-1357`: `ZENE_APP_BINARY="$<TARGET_FILE:zene>"` plus
`add_dependencies(MasteringTest zene)`), so the child is the binary of **this**
build and is never missing or stale - the original test guessed `../zene`
relative to the test binary, which is a layout guess and can be a stale binary.

## Evidence

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-207-port.r1/logs`.
All ctest runs were made from `build/tests`; all builds from the worktree root.

| Evidence | Exact command | Exit | Result / log |
|---|---|---:|---|
| Baseline build | `cmake --build build --target MasteringTest -j 20` | 0 | `build-1.log` |
| **RED** (new test, original product code) | `ctest -R '^MasteringTest$' --output-on-failure` | **8** | 12 passed, 1 failed: `'process.exitCode() != 0' returned FALSE` (the child rendered and exited 0) - `test-1.log` |
| Build with the first (usageError) refusal | `cmake --build build --target zene MasteringTest -j 20` | 0 | `build-3.log` |
| Train-5 failure reproduced | `ctest -R '^MasteringTest$' --output-on-failure` | 8 | `'output.contains("only valid with master")' returned FALSE` with empty child output - `test-2.log` |
| Build with the final refusal | `cmake --build build --target zene MasteringTest -j 20` | 0 | `build-4.log` |
| **GREEN** (fix applied) | `ctest -R '^MasteringTest$' --output-on-failure` | **0** | 1/1 passed (13 QtTest rows) - `test-3.log` |
| Sibling family (the `master --report` path) | `ctest -R '^ControlMastering(RunBound\|Commands\|Quit)$' --output-on-failure` | **0** | 3/3 passed - `test-9.log` |
| Reverse the product patch | `git diff -- src/core/main.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` | 0 | `fix-revert-1.log` |
| Rebuild without the fix | `cmake --build build --target zene MasteringTest -j 20` | 0 | `build-7.log` |
| **RED again** (fix reverted) | `ctest -R '^MasteringTest$' --output-on-failure` | **8** | 12 passed, 1 failed: `'process.exitCode() != 0' returned FALSE` - `test-7.log` |
| Re-apply the product patch | `git apply .../logs/fix.patch` | 0 | `fix-reapply-1.log` |
| Rebuild with the fix | `cmake --build build --target zene MasteringTest -j 20` | 0 | `build-8.log` |
| **GREEN again** (fix restored) | `ctest -R '^MasteringTest$' --output-on-failure` | **0** | 1/1 passed - `test-8.log` |

Affected family: **1 CTest target (`MasteringTest`)**, 13 QtTest rows
(`initTestCase`, 11 test rows, `cleanupTestCase`) - the RED runs had 12 passed
and 1 failed. The reverse-patch proof patches **only** the product file
(`src/core/main.cpp`); the test file and the CMake wiring stay in place, so the
proof is about the product change alone.

Manual check of the shipped binary (fix applied):

```text
$ build/zene --allowroot render <fixture>.mmp --output <out>.wav --report <report>.json
(stderr) --report is only valid with master.
(stderr) Try ".../build/zene --help" for more information.
EXIT=1
# no report.json, no render output
$ build/zene --allowroot master <fixture>.mmp -o <dir> --report <dir>/report.json
EXIT=0   # 6 candidates + report.json written - the master path is unchanged
```

## Gates

| Gate | Command | Exit | Log |
|---|---|---:|---|
| All gates (authoritative) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** | `gate-4.log` - `RESULT: PASS` (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures all exit 0; complexity and file-length "no new item in your files") |
| Complexity ratchet (standalone) | `bash tests/complexity-gate.sh --check` | 1 | `gate-5.log` - red **on the train**: 6 pre-existing CCN items in other files and 5 dead baseline entries; **no item in the files this change touches** |
| File-length ratchet (standalone) | `bash tests/file-length-gate.sh --check` | 1 | `gate-6.log` - red **on the train**: 14 pre-existing files over 500 lines; **no item in the files this change touches** |

The two standalone ratchets are red before this change as well: their findings
name only `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`,
`ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`,
`ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp` (complexity) and
`SmfInterchangeReader.cpp`, `TakeLane.cpp`, `CollabTransportTest.cpp`,
`ControlAutomationScriptTest.cpp`, `ControlDeviceCatalogueTest.cpp`,
`ControlEditCommandsTest.cpp`, `CustomAccessTest.cpp`, `DeviceDspTest.cpp`,
`HardwareIoTest.cpp`, `NotePlaybackTest.cpp`, `RackMacrosTest.cpp`,
`RevisionTimelineTest.cpp`, `VideoTrackTest.cpp`, `ArrangerTimelineTest.cpp`
(file length). The gate that decides this change, `feature_check.py`, compares
against the branch base and reports PASS.

## Registration

- `src/core/main.cpp` is an upstream-inherited file: its divergence entry in
  `tests/upstream-modifications.txt:1335` carries this change's reason (the
  ledger's reader keeps the last entry for a path, so the reason is on the
  effective line). Gate 6 (`no-upstream-regression`) exits 0.
- `tests/src/core/MasteringTest.cpp` is a **fork** source: it is already listed
  in `tests/fork-sources.txt:2299`, so no ledger entry is needed. No new file
  was added, so no manifest change was needed (`all-sources-reproduce` exits 0).
- `tests/CMakeLists.txt` is under `tests/`, which Gate 6 allows by
  construction; the change adds the binary path and the build dependency for
  the regression.

## Files committed

- `src/core/main.cpp` - the refusal
- `tests/src/core/MasteringTest.cpp` - the regression
- `tests/CMakeLists.txt` - the binary path + build dependency for the regression
- `tests/upstream-modifications.txt` - the divergence reason
- `docs/qwen-fixes-2026-10/cluster-new-p10-207-port.md` - this report

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with
   it.** Reverted: `ctest -R '^MasteringTest$'` exits **8** with
   `'process.exitCode() != 0' returned FALSE` (`test-7.log`); restored: the same
   command exits **0**, 1/1 passed (`test-8.log`). The first RED, before any
   product change, is `test-1.log` (exit 8, same assertion).
2. **The test is deterministic.** No wall-clock timing, no sleep, no race: the
   child is a real process that is waited for with a timeout
   (`waitForFinished(120000)` - a bound, not a measurement), and every
   assertion is on the child's exit code, its output text and the absence of two
   files. The fixture is written into the test's own `QTemporaryDir` before the
   child starts, so the child's inputs exist before it is spawned. No value is
   read from a clock.
3. **The fix is at the cause and complete.** The cause is that the parsed
   `masteringReportPath` had no owner outside the mastering branch
   (`src/core/main.cpp:1398` is its only reader), so the check sits where the
   option is defined as belonging to `master` (the help text,
   `src/core/main.cpp:294-306`). Siblings checked: the `master` action still
   writes the report (manual run above, and `ControlMasteringRunBound`,
   `ControlMasteringCommands`, `ControlMasteringQuit` - 3/3 passed, `test-9.log`);
   the only other producer of a `--report` argument in the tree is the control
   surface's mastering child (`src/core/ControlCommandsMasteringRun.cpp:214`),
   which passes `master`; `tools/mmpz-git` and `tests/agent-surface-gate.py`
   pass `--report` to their own tools, not to the `zene` CLI. The parse itself,
   the help text and `writeMasteringReport()` are unchanged.
4. **No collateral damage.** The check triggers only when `--report` is given
   and `master` is not selected - a combination that previously did nothing.
   Old projects and saved data are untouched (the check is on the command line,
   before a project is opened). No limit or threshold was added, so no other
   caller can be hit by it. No new control command, no change to the command
   snapshot, no change to the audio thread, the undo stack or the save format.
5. **Portability (Qt 5.15 / MSVC / macOS).** The product change uses only
   `fprintf` and `QString::toUtf8()` - no Qt-6-only API, no `windows.h` name.
   The test's binary path comes from `$<TARGET_FILE:zene>`, which is the
   `.exe` on Windows; the `../zene` fallback (with `.exe` appended) is kept for
   a build without the definition.
6. **The ALREADY-FIXED / FALSE-POSITIVE question does not apply** - the verdict
   is FIXED, and the evidence above is the code, the measured CLI behaviour and
   the RED/GREEN runs.

### Note for the reviewer (not a defect in this change)

Every other option refusal in `main()` goes through `usageError()` and is
therefore **silent on this toolchain** (Qt 6.12 logs to the journal when
stderr is not a console). That is a pre-existing, product-wide condition
(`zene --bogus-option` prints nothing and exits 1 here) and is out of scope for
this bug, so `usageError()` was left alone; only the new refusal prints to
stderr directly.
