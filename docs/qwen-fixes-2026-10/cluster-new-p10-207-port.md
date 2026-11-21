# Cluster new-p10-207-port: `--report` is silently ignored outside `master`

**Verdict: FIXED**

## What round 2 changed (the review's rework)

Round 1 refused `--report` where the second parsing stage ends - too late, and
on the wrong fact. The review found both gaps and they are real:

- **the early-return actions**: `upgrade`, `makebundle`, `dump` and `compress`
  return from the second parsing stage, so a `--report` given to one of them was
  never looked at - the action ran and the report was silently ignored;
- **the empty path**: the guard tested whether the stored path was nonempty,
  not whether `--report` was given, so `--report ''` was accepted.

Round 2 therefore (1) decides the option in the **first** parsing stage, which
every command line passes through before any action; (2) tracks whether the
option was GIVEN separately from the path it carries, so an empty path is a
refusal too; and (3) adds the two CLI regressions the review asked for (a
`--report` before an early-returning action, and `render ... --report ''`),
each asserting the refusal **and** that the action's output is absent.

## The finding on this branch

REAL, and still unfixed on the newest line before this change. On this tree (base
`ded355440`, the `050/m0-train-5` line):

- `src/core/main.cpp:967-977` parses `--report <path>` into `masteringReportPath`
  for **every** action;
- the only reader is the mastering branch, `src/core/main.cpp:1432-1433`
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
`src/core/main.cpp:782`, `compress:` at `:837`, `master:` at `:1435`, the control
socket at `:1357`). A refusal nobody can read is not a refusal.

## The fix

`src/core/main.cpp:496-505` (the two facts that are tracked), `src/core/main.cpp:559-569`
(the first parsing stage sees the option) and `src/core/main.cpp:656-690` (the
refusals).

Three things are wrong with the option and all three are fixed at the cause:

1. **The decision was taken too late.** The second parsing stage is where
   `upgrade`, `makebundle`, `dump` and `compress` live, and each of them
   `return`s from it. A `--report` given to one of them was therefore never even
   looked at: the action ran and the report was silently ignored. The decision is
   now made in the **first** parsing stage, which runs before every action:

   ```cpp
   		else if (arg == "--report")
   		{
   			// The report option belongs to "master" alone, so this stage - the
   			// one that runs before ANY action - is where it is noticed. The
   			// value belongs to the option, so it is consumed here as well: a
   			// path that reads like an action keyword ("master") must not be
   			// taken for that action.
   			reportGiven = true;
   			++i;
   			reportPathEmpty = ( i >= argc ) || QString::fromLocal8Bit( argv[i] ).isEmpty();
   		}
   ```

2. **"The option was given" and "the path it carries" are different facts.**
   `--report ''` gave an empty path, which the old guard read as "no report was
   asked for" and let through. `reportGiven` is now tracked separately from the
   path, and an empty path is refused like any other unusable path.

3. **The refusal has to be visible** (the toolchain paragraph above).

The refusals themselves, before the second parsing stage and before the
application exists:

```cpp
   	if( reportGiven )
   	{
   		if( !mastering )
   		{
   			fprintf( stderr, "\n--report is only valid with master.\n\nTry \"%s --help\" for more "
   				"information.\n\n", argv[0] );
   			return EXIT_FAILURE;
   		}
   		if( reportPathEmpty )
   		{
   			fprintf( stderr, "\nNo report path specified.\n\nTry \"%s --help\" for more "
   				"information.\n\n", argv[0] );
   			return EXIT_FAILURE;
   		}
   	}
```

The second refusal keeps the wording the second parsing stage already used for a
missing path (`No report path specified`, `src/core/main.cpp:973`), so
`zene master ... --report` (no value) says what it always said.

The order of the arguments does not matter: the first stage saw the option
wherever it stood, and the option's value is consumed there, so a path that
reads like an action keyword (`--report master`) is a path, not the action.

Nothing else changed: the option's parse, the help text, the master action and
the report writer are untouched, and no project or saved data is affected (the
check is on the command line, before anything is loaded).

## The regressions

`tests/src/core/MasteringTest.cpp:499-568` - four CLI rows that drive the
**real** binary and assert the refusal and that the action's output is absent:

| Row | Command | Asserted |
|---|---|---|
| `reportOptionIsRejectedOutsideMaster` | `render <project> --output <wav> --report <json>` | non-zero exit, the diagnostic, no report file, **no render** |
| `reportOptionIsRefused(a report before an early-returning action)` | `--report <json> upgrade <project> <out.mmp>` | non-zero exit, the diagnostic, no report file, **no upgraded file** - the action did not run |
| `reportOptionIsRefused(an empty report path)` | `render <project> --output <wav> --report ''` | non-zero exit, the diagnostic, **no render** |
| `reportOptionIsRefused(an empty report path for master)` | `master <project> -o <dir> --report ''` | non-zero exit, `No report path specified`, **no candidate directory** |

The application binary's path is compiled in
(`tests/CMakeLists.txt:1349-1357`: `ZENE_APP_BINARY="$<TARGET_FILE:zene>"` plus
`add_dependencies(MasteringTest zene)`), so the child is the binary of **this**
build and is never missing or stale - the original test guessed `../zene`
relative to the test binary, which is a layout guess and can be a stale binary.
The runner itself lives in `tests/src/core/MasteringTestSupport.h` (the fixture
and helper header of this test), because `MasteringTest.cpp` is a fork source
under the 500-line file-length gate and the four rows would have pushed it over.

## Evidence

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-207-port.r1/logs`.
All ctest runs were made from `build/tests`; all builds from the worktree root.

| Evidence | Exact command | Exit | Result / log |
|---|---|---:|---|
| Build with the fix | `cmake --build build --target zene MasteringTest -j 20` | 0 | `build-14.log` |
| **GREEN** (fix applied) | `ctest -R '^MasteringTest$' --output-on-failure` | **0** | 1/1 passed, 16 QtTest rows - `test-16.log` |
| Capture the product patch | `git diff -- src/core/main.cpp > .../logs/fix.patch` | 0 | (the product file only) |
| Reverse the product patch | `git apply -R .../logs/fix.patch` | 0 | `fix-revert-3.log` |
| Rebuild without the new part | `cmake --build build --target zene MasteringTest -j 20` | 0 | `build-15.log` |
| **RED** (the new part reverted: the first round's fix still stands) | `ctest -R '^MasteringTest$' --output-on-failure` | **8** | 13 passed, **3 failed** - the three new rows: `'run.exitCode != 0' returned FALSE` - `test-17.log` |
| The original code (branch base `ded355440`) | `git show ded355440:src/core/main.cpp > src/core/main.cpp` | 0 | (the pre-fix file; the fixed one is restored right after) |
| Rebuild on the original code | `cmake --build build --target zene MasteringTest -j 20` | 0 | `build-16.log` |
| **RED** (no fix at all) | `ctest -R '^MasteringTest$' --output-on-failure` | **8** | 12 passed, **4 failed** - every CLI row, including the one from the first round - `test-18.log` |
| Restore the fix | `git apply .../logs/fix.patch` | 0 | `fix-reapply-3.log` |
| Rebuild with the fix | `cmake --build build --target zene MasteringTest -j 20` | 0 | `build-18.log` |
| **GREEN again** | `ctest -R '^MasteringTest$' --output-on-failure` | **0** | 1/1 passed - `test-20.log` |
| Sibling family (the `master --report` path) | `ctest -R '^ControlMastering(RunBound\|Commands\|Quit)$' --output-on-failure` | **0** | 3/3 passed - `test-14.log` |

Affected family: **1 CTest target (`MasteringTest`)**, 16 QtTest rows
(`initTestCase`, 14 test rows, `cleanupTestCase`). The reverse-patch proof
patches **only** the product file (`src/core/main.cpp`); the test file, the
support header and the CMake wiring stay in place, so the proof is about the
product change alone.

Manual check of the shipped binary (fix applied):

```text
$ build/zene --allowroot render <fixture>.mmp --output <out>.wav --report <report>.json
(stderr) --report is only valid with master.            EXIT=1   (no report, no render)
$ build/zene --allowroot --report <report>.json upgrade <project> <out>.mpp
(stderr) --report is only valid with master.            EXIT=1   (no out.mmp: the action did not run)
$ build/zene --allowroot render <fixture>.mmp --output <out>.wav --report ''
(stderr) --report is only valid with master.            EXIT=1   (no render)
$ build/zene --allowroot master <fixture>.mmp -o <dir> --report ''
(stderr) No report path specified.                      EXIT=1   (no candidate directory)
$ build/zene --allowroot master <fixture>.mmp -o <dir> --report <report>.json
EXIT=0   # 6 candidates + report.json written - the master path is unchanged
```

## Gates

| Gate | Command | Exit | Log |
|---|---|---:|---|
| All gates (authoritative) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** | `gate-10.log` - `RESULT: PASS` (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures all exit 0; complexity and file-length "no new item in your files") |
| Complexity ratchet (standalone) | `bash tests/complexity-gate.sh --check` | 1 | `gate-11.log` - red **on the train**: 6 pre-existing CCN items in other files and 5 dead baseline entries; **no item in the files this change touches** |
| File-length ratchet (standalone) | `bash tests/file-length-gate.sh --check` | 1 | `gate-12.log` - red **on the train**: **19** fork-scope files exceed 500 code lines, of which **14** are `REGRESSION: new file over 500 lines` entries (the other 5 are grandfathered in `tests/file-length-baseline.tsv`); **no item in the files this change touches** |

The two standalone ratchets are red before this change as well. Their findings
name only `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`,
`ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`,
`ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp` (complexity) and
`SmfInterchangeReader.cpp`, `TakeLane.cpp`, `CollabTransportTest.cpp`,
`ControlAutomationScriptTest.cpp`, `ControlDeviceCatalogueTest.cpp`,
`ControlEditCommandsTest.cpp`, `CustomAccessTest.cpp`, `DeviceDspTest.cpp`,
`HardwareIoTest.cpp`, `NotePlaybackTest.cpp`, `RackMacrosTest.cpp`,
`RevisionTimelineTest.cpp`, `VideoTrackTest.cpp`, `ArrangerTimelineTest.cpp`
(the 14 file-length regressions; the 5 grandfathered ones are
`ScriptBindings.cpp`, `ScriptEngine.cpp`, `AudioPorts.h`, `Vst3Host.cpp` and
`WasmSandbox.cpp`). The gate that decides this change, `feature_check.py`,
compares against the branch base and reports PASS.

**The file-length count, stated exactly** (the review asked for this): the gate's
headline is `file-length-gate: 1792 fork-scope sources measured; 19 exceed 500
code lines`, and 19 - not 14 - is that headline number. The **14** is the
number of `REGRESSION:` lines it prints (the files that are over the limit and
are NOT grandfathered in the baseline); the other 5 of the 19 are grandfathered
and are reported as entries, not as regressions. The first round's report quoted
the 14 without the headline, which is what the review picked up.

## Registration

- `src/core/main.cpp` is an upstream-inherited file: its divergence entry in
  `tests/upstream-modifications.txt:1335` carries this change's reason (the
  ledger's reader keeps the last entry for a path, so the reason is on the
  effective line). Gate 6 (`no-upstream-regression`) exits 0.
- `tests/src/core/MasteringTest.cpp` is a **fork** source: it is already listed
  in `tests/fork-sources.txt:2299`, so no ledger entry is needed.
  `tests/src/core/MasteringTestSupport.h` is registered in
  `tests/all-sources.txt` (the whole-tree scope). No new file was added, so no
  manifest change was needed (`all-sources-reproduce` exits 0).
- `tests/CMakeLists.txt` is under `tests/`, which Gate 6 allows by
  construction; the change adds the binary path and the build dependency for
  the regression.

## Files committed

- `src/core/main.cpp` - the refusals
- `tests/src/core/MasteringTest.cpp` - the four CLI regressions
- `tests/src/core/MasteringTestSupport.h` - the CLI runner the regressions share
- `tests/CMakeLists.txt` - the binary path + build dependency for the regression
- `tests/upstream-modifications.txt` - the divergence reason
- `docs/qwen-fixes-2026-10/cluster-new-p10-207-port.md` - this report

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with
   it.** On the original product code (the branch base): `ctest -R
   '^MasteringTest$'` exits **8** with **4 failed** rows - every CLI row -
   (`test-18.log`). With the fix: the same command exits **0**, 1/1 passed, 16
   rows (`test-20.log`). With only the new part reverted (the first round's fix
   still in place): exit **8**, 13 passed / 3 failed (`test-17.log`), which is
   the proof that the new rows test the new behaviour and not the old one.
2. **The test is deterministic.** No wall-clock timing, no sleep, no race: each
   child is a real process that is waited for with a timeout
   (`waitForFinished(120000)` - a bound, not a measurement), and every assertion
   is on the child's exit code, its output text and the absence of files. The
   fixture project is written into the test's own `QTemporaryDir` before any
   child starts, so the child's inputs exist before it is spawned. No value is
   read from a clock.
3. **The fix is at the cause and complete.** The cause is that the parsed
   `masteringReportPath` had no owner outside the mastering branch
   (`src/core/main.cpp:1432` is its only reader), so the option is now decided
   in the one parsing stage that every command line passes through - the first -
   and the actions that return early can no longer run past it. Siblings
   checked: the `master` action still writes the report (manual run above, and
   `ControlMasteringRunBound`, `ControlMasteringCommands`, `ControlMasteringQuit`
   - 3/3 passed, `test-14.log`); the only other producer of a `--report`
   argument in the tree is the control surface's mastering child
   (`src/core/ControlCommandsMasteringRun.cpp:214`), which passes `master`;
   `tools/mmpz-git` and `tests/agent-surface-gate.py` pass `--report` to their
   own tools, not to the `zene` CLI. The option's parse, the help text and
   `writeMasteringReport()` are unchanged.
4. **No collateral damage.** The refusals trigger only when `--report` is given
   - a combination that previously did nothing. Old projects and saved data are
   untouched (the check is on the command line, before a project is opened). No
   limit or threshold was added, so no other caller can be hit by it. No new
   control command, no change to the command snapshot, no change to the audio
   thread, the undo stack or the save format. The only message that changed
   channel is `No report path specified` for `master --report` (no value): it
   used to be printed through `usageError()`'s `qCritical`, which this
   toolchain swallows, and is now printed on stderr - the same refusal, the same
   exit code, visible.
5. **Portability (Qt 5.15 / MSVC / macOS).** The product change uses only
   `fprintf` and `QString::fromLocal8Bit` - no Qt-6-only API, no `windows.h`
   name. The test's binary path comes from `$<TARGET_FILE:zene>`, which is the
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
this bug, so `usageError()` was left alone; only the `--report` refusals print
to stderr directly.
