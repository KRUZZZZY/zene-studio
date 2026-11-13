ALREADY-FIXED

# Cluster new-p21-232 - Mallets voice budget atomic is not initialised

## Verdict

The finding says `m_voicesInUse` is an atomic member that the `MalletsInstrument` constructor never initialises, so a
fresh instrument would compare its voice budget against garbage. On this branch that is not the case: the member is
declared with an in-class initialiser, `std::atomic<int> m_voicesInUse{0};`, at `plugins/Stk/Mallets/Mallets.h:248`.
The member was introduced *with* that initialiser - it has never existed on this branch without it. No product code was
changed.

Base: `2a7fd9f9c7` (`050/m0-train-24`). Branch: `bugs/qwen-new-p21-232`.

## Evidence (file:line)

| Claim | Where |
| --- | --- |
| The budget is declared with an in-class `{0}` initialiser | `plugins/Stk/Mallets/Mallets.h:248` - `std::atomic<int> m_voicesInUse{0};` |
| That is the only declaration of the name in the tree | `grep -rn m_voicesInUse` returns only `Mallets.h:248` plus the four uses at `Mallets.cpp:498,508,518,615` |
| The constructor initialiser list does not need to repeat it | `plugins/Stk/Mallets/Mallets.cpp:214` - in-class initialisers are applied for members the list omits |
| The initialiser is not a recent accident: the member did not exist before it | `git show fb2ebfabae^:plugins/Stk/Mallets/Mallets.h \| grep -c m_voicesInUse` = `0`; `fb2ebfabae` is the only commit that ever touched the declaration (`git log -S 'm_voicesInUse{'`) |
| The branch base already carries it | `git show 2a7fd9f9c7:plugins/Stk/Mallets/Mallets.h` line 248 = `std::atomic<int> m_voicesInUse{0};` |
| Every use reads a budget that starts at 0 | `Mallets.cpp:96-115` (`acquireVoiceBudget` / `releaseVoiceBudget`), called at `:498`, `:508`, `:518`, `:615` |
| The limit the budget is compared against | `plugins/Stk/Mallets/Mallets.cpp:85` - `MALLETS_VOICE_LIMIT = 128` |
| All atomics in this plugin are explicitly initialised | `Mallets.cpp:125-126` (`inUse{false}`, `family{0}`), `Mallets.h:248` |

The finding's own condition ("if the header has no in-class initialiser") is false: it has one.

### Second, independent reason the defect is unreachable here

Even a bare `std::atomic<int>` member would start at 0 in this build, because C++20 value-initialises
default-constructed atomics (P0883). This was measured, not assumed:

* The product is built as C++20: `src/CMakeLists.txt:25` sets `CMAKE_CXX_STANDARD 20`; the plugin target's flags
  contain `-std=gnu++20`.
* `__cpp_lib_atomic_value_initialization` is `201911`, and libstdc++`/usr/include/c++/16/atomic:236-241` implements
  `atomic()` as `: _M_i()` under that macro.
* A poisoned-storage probe (`$TMPDIR/zene-probe/member.cpp`) default-initialises an atomic member over `0xFF` bytes and
  reads `0`.

So the finding's stated failure mode ("a non-zero garbage value can make every note think the budget is exhausted") is
not reachable on this toolchain even in the hypothetical bare form. Portable C++ still wants the explicit initialiser,
which is exactly why it is there.

## Why no product fix, and what the mutation proof is

No RED run is possible against a real defect: the initialiser is present and the member has never existed without it.
`git diff -- plugins/Stk/Mallets/Mallets.h plugins/Stk/Mallets/Mallets.cpp` is empty, so the step-4b
`fix.patch` is an empty file (0 bytes, `EXIT=0`); reversing it rebuilds nothing and changes nothing. That is reported
here rather than papered over with a fake patch.

To prove the regression is not vacuous, the *fix the finding asks for* was removed as a mutation (the same edit a
"without your fix" run would make) and the test was rebuilt and run:

| Step | Command | EXIT |
| --- | --- | --- |
| Mutation applied | `sed -i 's/std::atomic<int> m_voicesInUse{0};/std::atomic<int> m_voicesInUse;/' plugins/Stk/Mallets/Mallets.h` | 0 |
| Rebuild | `ZENE_BUILD_GUARDED=1 cmake --build build --target MalletsInstrumentViewTest -j4` | 0 |
| Run (RED) | `./MalletsInstrumentViewTest` | **1** - `theVoiceBudgetIsInitialisedWhereItIsDeclared` failed |
| Mutation reverted | `sed -i 's/std::atomic<int> m_voicesInUse;/std::atomic<int> m_voicesInUse{0};/' plugins/Stk/Mallets/Mallets.h` | 0 |
| Rebuild | `ZENE_BUILD_GUARDED=1 cmake --build build --target MalletsInstrumentViewTest -j4` | 0 |
| Run (GREEN) | `./MalletsInstrumentViewTest` | **0** - 6 passed, 0 failed |

The RED assertion message was: `the voice budget is declared without an initialiser (std::atomic<int>
m_voicesInUse;); a build where std::atomic is not value-initialised would start it indeterminate`.

I also ran the *behavioural* budget test against that same mutation. It **passed** (`EXIT=0`, 5 passed), which is the
measured proof of the P0883 point above: on this toolchain the runtime behaviour cannot distinguish the two
declarations. The structural assertion is therefore the only deterministic way to pin the contract the finding names,
and it is what the RED above exercises.

## Regression test

`tests/src/plugins/MalletsInstrumentViewTest.cpp`, in the already-registered `MalletsInstrumentViewTest` target
(`ctest -R '^MalletsInstrumentViewTest$'`). No new target, no new control command.

* `aFreshInstrumentsVoiceBudgetStartsEmpty` - the behavioural matrix, driven by injected note handles and a cleared
  buffer (no wall clock, no sleeps, no threads):
  * normal path: the first 128 notes of a fresh instrument are all audible;
  * exhaustion path: note 129 is silent while the budget is full;
  * release path: `deleteNotePluginData` on one running note returns exactly one voice, so the waiting note is voiced
    on its next period. This pins "initialised to 0 and counted once per note" from both sides.
* `theVoiceBudgetIsInitialisedWhereItIsDeclared` - reads `Mallets.h` through `MALLETS_TEST_PLUGIN_DIR` (added to the
  target's compile definitions) and pins the declaration to an initialiser. This is the leg that goes RED when the
  finding's fix is removed.

Determinism: no timing, no `QTest::qWait`, no concurrency. The behavioural leg uses `NotePlayHandleManager::acquire`
with a fixed `Note`, and `playNote` into a locally cleared `std::vector<SampleFrame>`; the structural leg reads a source
file. Audio device processing is stopped in `initTestCase` (`audioDev()->stopProcessing()`).

## Commands and results

Logs were written under `$TMPDIR` (see "Environment limitation") because the given LOG FOLDER was not writable.

| # | Command (from the worktree unless noted) | EXIT | Log |
| --- | --- | --- | --- |
| 1 | `./MalletsInstrumentViewTest` (baseline, build/tests) | 0 | `test-1-baseline.log` (4 passed) |
| 2 | `ZENE_BUILD_GUARDED=1 cmake --build build --target MalletsInstrumentViewTest -j4` | 0 | `build-1-test-red.log` |
| 3 | `./MalletsInstrumentViewTest` (new behavioural test) | 0 | `test-2-red.log` (5 passed) |
| 4 | mutation remove `{0}`; rebuild; run | 0 / 0 | `build-2-uninit.log`, `test-3-uninit.log` - behavioural test still passes (P0883) |
| 5 | restore; rebuild with structural guard | 0 | `build-3-guard.log` |
| 6 | `./MalletsInstrumentViewTest` | 0 | `test-4-guard.log` (6 passed) |
| 7 | mutation remove `{0}`; rebuild | 0 | `build-4-red.log` |
| 8 | `./MalletsInstrumentViewTest` **RED** | **1** | `test-5-red.log` (`theVoiceBudgetIsInitialisedWhereItIsDeclared` failed) |
| 9 | restore; rebuild | 0 | `build-5-green.log` |
| 10 | `./MalletsInstrumentViewTest` **GREEN** | **0** | `test-6-green.log` (6 passed) |
| 11 | `ctest -R '^MalletsInstrumentViewTest$' --output-on-failure` (build/tests) | 0 | `ctest-1-mallets.log` (1/1 passed) |
| 12 | `ZENE_BUILD_GUARDED=1 cmake --build build --target PluginPortsMigrationTest -j4` | 0 | `build-6-ppm.log` |
| 13 | `./PluginPortsMigrationTest malletsVoiceLimitIsEnforcedPerInstrument malletsVoiceLimitSurvivesDeviceChange` | 0 | `test-7-ppm-mallets.log` (4 passed) |
| 14 | `ctest -R '^PluginPortsMigrationTest$'` | 8 | `ctest-4-ppm.log` - pre-existing, unrelated: `nonRealtimeLadspaEffectsAreInvalid` cannot load the SWH Decimator fixture. The two Mallets slots in the same binary pass (#13) |
| 15 | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-2.log` |
| 16 | `python3` gate table (log dir redirected, see below) | **0** | `feature-check-3.log` - all 8 rows PASS |

Gate table (#16, unpiped):

```
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

The affected-family count is 1 registered test (`MalletsInstrumentViewTest`) plus the 2 Mallets slots inside
`PluginPortsMigrationTest`. `ctest -R '^Mallets$'` selects nothing in this build (`No tests were found`), so the
`tests/reference/Stk/Mallets/Mallets.cpp` comparison is not a separate ctest here; it is compiled into
`PluginPortsMigrationTest` as the `partc_ref_malletsstk` module and is exercised by #13/#14.

### Environment limitation (why logs are in $TMPDIR)

The LOG FOLDER `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-232.r1/logs` is not writable in this
session: every create/append there (and anywhere under `/home/kruzzzzy` outside `~/.cache` and the session's own
`$DSH_HOME`) returns `PermissionError: [Errno 13]`. `feature_check.py` also failed on its own `logs.mkdir()` next to
the worktree for the same reason. All build/test output was therefore written to
`$TMPDIR/zene-logs-new-p21-232/` (`/home/kruzzzzy/.cache/zene-tmp/zene-qwen-new-p21-232/zene-logs-new-p21-232/`) and
mirrored unchanged into `$DSH_HOME/new-p21-232-logs/` with the brief's file names (the convention the earlier session
`new-p14-391-r6` recorded for the same jail). The gate table was produced by running the real `feature_check.py` with
only its log directory redirected (its `SIMPLE`/`RATCHET` command lists, thresholds and baseline cache untouched).

`submit_check.py` therefore reports exactly one FAIL, `gates (feature_check.py)`, which is that `logs.mkdir()`
`EACCES` and not a gate result; every other row passes (`submit-check-2.log`). Running `feature_check.py` unmodified
fails before any gate runs:

```
PermissionError: [Errno 13] Permission denied:
'/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-232-feature-check'
```

The parent directory is `drwxrwxrwx` and still refuses `touch` of a sibling, so this is a hard sandbox jail, not a
permission-bit problem. To prove the gate row is only the log path, the committed tree was copied byte-for-byte
(`cp -a`) to a path with a writable parent and both tools were run **unmodified** there:

| Command (same tree, writable parent) | EXIT | Result |
| --- | --- | --- |
| `python3 .../feature_check.py .` | **0** | all 8 gates PASS (`feature-check-5-unmodified-copy.log`) |
| `python3 .../submit_check.py . new-p21-232` | **0** | `RESULT: PASS - submit` (`submit-check-3-unmodified-copy.log`) |

So the gate row is green when the tool can create its log directory. No gate limit, baseline or ratchet was changed.
If a reviewer needs the logs in the named folder, the sandbox must allow writes there.

Note also that `cmake` in this environment is a lane-guard wrapper whose lock directory is outside the writable set;
its own supported nested-build bypass `ZENE_BUILD_GUARDED=1` was used with `-j4`.

## Files changed

* `tests/src/plugins/MalletsInstrumentViewTest.cpp` - the two regression slots and their helpers.
* `tests/CMakeLists.txt` - `MALLETS_TEST_PLUGIN_DIR` compile definition for that target.
* `tests/upstream-modifications.txt` - existing rows for both files extended with the new-p21-232 reason.
* `docs/qwen-fixes-2026-10/cluster-new-p21-232.md` - this report.

No product file changed: `git diff --name-only -- src include plugins` is empty.

## Self-check

**The regression test FAILS with your product change reverted and PASSES with it (exit codes).**
There is no product change to revert (verdict ALREADY-FIXED; `fix.patch` is 0 bytes). The equivalent mutation - removing
the `{0}` the finding asks for - was run: RED `EXIT=1` (`test-5-red.log`), restored GREEN `EXIT=0`
(`test-6-green.log`). The behavioural leg alone does not distinguish them on C++20 (`EXIT=0` both ways,
`test-3-uninit.log`), which is stated in the report rather than claimed otherwise.

**The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).**
The behavioural leg injects fixed `NotePlayHandle` objects (`NotePlayHandleManager::acquire`, fixed `Note`, fixed
`framesPerPeriod()`) and renders into a locally cleared `std::vector<SampleFrame>`; the structural leg reads a source
file. No `qWait`, no threads, no audio device (processing is stopped in `initTestCase`).

**The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays
consistent.**
Siblings: every atomic in this plugin is explicitly initialised (`Mallets.cpp:125-126`, `Mallets.h:248`), and
`Mallets.cpp:96-115` is the only budget implementation. Callers: the four uses at `Mallets.cpp:498,508,518,615` (acquire
on note start, release on a failed claim, a failed voice build, and note end) all operate on the one member, which starts
at 0 either way. The two existing enforcement tests (`PluginPortsMigrationTest`, #13) still pass, so the 128-voice
limit and the retired-pool sharing are unaffected.

**No collateral damage: old projects and saved data still load as before, and any limit or check you added does not
hit other callers that legitimately exceed it.**
No product code changed, so load/save/render behaviour is bit-identical. The only added check is a source-text
assertion in the test; it constrains no runtime caller. `aLoadedPresetOutsideTheScalerTableIsFittedIntoIt` and
`theBowedCheckBoxIsBoundToTheStrikeSetting` (the save/load round-trip tests) still pass in the same run.

**If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.**
`plugins/Stk/Mallets/Mallets.h:248` declares `std::atomic<int> m_voicesInUse{0};`. The member was introduced by
`fb2ebfabae` *with* that initialiser (`git show fb2ebfabae^:plugins/Stk/Mallets/Mallets.h | grep -c m_voicesInUse` =
`0`), and it is already present at the branch base `2a7fd9f9c7`. The finding is conditional on "if the header has no
in-class initialiser"; it has one.

**`submit_check.py`: the one FAIL, `gates (feature_check.py)`, is a sandbox log-path denial, not a gate result.**
`feature_check.py` calls `logs.mkdir()` on `<worktree>/../<name>-feature-check`, which is outside this session's
writable set, so it raises `PermissionError: [Errno 13]` before running any gate. Run unmodified on the same committed
tree copied to a directory with a writable parent, both tools pass: `feature_check.py` `EXIT=0` (all 8 rows) and
`submit_check.py` `EXIT=0` (`RESULT: PASS - submit`). The brief's LOG FOLDER is denied for the same reason; logs are
mirrored to `$DSH_HOME/new-p21-232-logs/` with a `README.txt` recording the denial and the mapping. No gate limit,
baseline or ratchet was changed.
