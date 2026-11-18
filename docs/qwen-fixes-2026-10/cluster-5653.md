# Cluster 5653 — Mastering rejects absolute Windows output paths

**Verdict: FIXED** (round 3, on top of the round-2 commit `2da2625d7`).

## The finding (Luna patrol, reported twice)

```
BUG 11: Mastering rejects absolute Windows output paths
  where: src/core/ControlCommandsMasteringRun.cpp:151-153
  what: Absolute paths are detected by checking for a leading `/`, which rejects
        drive-letter and UNC paths.
  when: On Windows, call `mastering.run` with an absolute path such as `C:\Masters`;
        the command refuses it.
  fix: Use `QFileInfo(path).isAbsolute()`.
  confidence: high
```

## What the code does now

`src/core/ControlCommandsMasteringRun.cpp:154-161` (`validateRunArgs`):

```cpp
    // Qt resource paths (":/...", ":x") are absolute to QFileInfo on EVERY
    // platform, but they are not filesystem output paths - reject them so the
    // POSIX rule stays "leading '/' only" while Windows drive-letter/UNC paths
    // are accepted by the platform's own absoluteness rule.
    if (outDir->startsWith(QLatin1Char(':')) || !QFileInfo(*outDir).isAbsolute())
    {
        return {false, {"'out_dir' must be an absolute path"}};
    }
```

`#include <QFileInfo>` at line 28. No header changes, no new files, no new
control commands, refusal message unchanged.

## Corrected POSIX-behavior claim

Round 2 claimed `QFileInfo::isAbsolute()` "keeps rejecting junk like
`C:\Masters` on POSIX". That was incomplete and the POSIX claim was wrong where
it mattered: measured against this host's Qt6Core (`/tmp/qtpathprobe.cpp`,
compiled with the same Qt the project uses), `QFileInfo::isAbsolute()` returns
**true** for Qt resource paths on **every** platform:

```
:/res      ABS      :res  ABS      :  ABS
C:\Masters rel      C:/Masters rel      D: rel
```

So the bare `QFileInfo` check (round 2) silently **accepted** `:/...` as an
`out_dir` on POSIX — a behaviour change from the original leading-`/` rule, which
rejected all of those. Round 3 restores the original POSIX rule (leading `/`
only) by rejecting any `:`-prefixed path before the platform check, while
Windows drive-letter and UNC paths remain accepted by
`QFileInfo::isAbsolute()`.

## Luna's round-2 review (REWORK) — addressed point by point

1. *"Reject Qt resource paths such as `:/...` while retaining platform-specific
   filesystem absolute paths, and add a POSIX regression for that case."*
   Done: the `:`-prefix guard above, plus a POSIX regression in
   `tests/control-mastering-commands.py` that submits `out_dir ":/win-abs-masters"`
   and requires an `invalid_args` refusal (and, if a build ever accepted it,
   drains the stray run before asserting, so the failure names the real bug
   instead of collateral "busy").
2. *"Strengthen the Windows test to require an accepted `mastering.run`
   response, completion within the bound, and the expected output."*
   `check_platform_absolute_outdir` (nt branch) now requires
   `ack.get("ok") is True`, polls `mastering.get_state` to `"completed"` within
   `RUN_COMPLETION_BOUND` (120 s) via the new `wait_for_mastering_run` helper,
   and asserts the expected output (`len(files) == CANDIDATES + 1` including
   `source_render.wav`). A polling timeout or a wrong-kind reply fails.
3. *"Correct the report's POSIX behavior claim and provide a Windows baseline
   RED and patched GREEN run."* This section is the correction; the runs are
   below.

## Regression + RED/GREEN (this Linux host)

The check is host-aware, so it runs on every host: on POSIX it asserts the
`:/...` refusal (the case Luna asked for); on Windows (`os.name == "nt"`) it
asserts the drive-letter path is accepted, completes, and produces the expected
outputs. It is called unconditionally from `main()` (the round-2
`if os.name == "nt":` gate is gone).

Baseline for this round's RED is the committed round-2 code (bare
`QFileInfo::isAbsolute()`), which — per the probe above — accepts
`:/win-abs-masters` on POSIX.

Baseline RED (round-2 committed code, before this round's source edit):

```
$ cmake --build build --target zene > "$LOGS/build-1.log" 2>&1; echo EXIT=$?
EXIT=0
$ (cd build/tests && ctest -R ControlMastering --output-on-failure) > "$LOGS/test-1.log" 2>&1; echo EXIT=$?
EXIT=8
$ (cd build/tests && ctest -R ControlMastering --output-on-failure) > "$LOGS/test-2.log" 2>&1; echo EXIT=$?
EXIT=8
```

`test-1.log` (first RED form, before the stray-run drain was added):

```
FAIL: mastering.run with a POSIX-hostile junk out_dir 'C:\Masters' is refused
FAIL: the mastering.* socket proof has 1 failed check(s)
```

`test-2.log` (clean RED, the check as shipped):

```
FAIL: the mastering.* socket proof has 1 failed check(s)
67% tests passed, 1 tests failed out of 3
1 = ControlMasteringCommands (Failed)
```

The failing check is the new POSIX regression: with the committed bare-`QFileInfo`
code the harness replies `{'ok': True, ...}` — the command **accepted** the Qt
resource path `:/win-abs-masters` instead of refusing it. (ctest returned EXIT=8
with one failed test; the log content above is what was observed.)

Patched GREEN (after the `:`-prefix guard, rebuilt):

```
$ cmake --build build --target zene > "$LOGS/build-2.log" 2>&1; echo EXIT=$?
EXIT=0
$ (cd build/tests && ctest -R ControlMastering --output-on-failure) > "$LOGS/test-3.log" 2>&1; echo EXIT=$?
EXIT=0
$ grep -c "Test.*Passed" "$LOGS/test-3.log"
3
```

`test-3.log`: `100% tests passed, 0 tests failed out of 3` —
`ControlMasteringCommands`, `ControlMasteringRunBound`, `ControlMasteringQuit`
all Passed (ctest prints no per-check lines for passing tests; the new check's
activity is proven by the RED runs above).

## Windows-side RED/GREEN mapping (documented, not executed here)

This host is Linux, so the `os.name == "nt"` branch of the same check cannot
execute here (drive-letter paths are relative on POSIX — probe: `C:\Masters` rel).
On a Windows host the same check produces the cycle Luna asked for:

- baseline RED — the pre-fix rule (`outDir->at(0) != '/'`) refuses the harness
  tmp dir `C:\Users\<user>\AppData\Local\Temp\zctl-run-XXXX` (no leading `/`),
  so `ack.get("ok") is True` fails and the check reports
  `mastering.run with an absolute Windows out_dir ... was not accepted`;
- patched GREEN — `QFileInfo::isAbsolute()` accepts it, the run completes within
  the bound, and the six expected wavs (`CANDIDATES + source_render`) are
  asserted.

The POSIX `:/...` case above is the same check's other branch and is the RED/GREEN
cycle demonstrable on this host.

## Gates

```
$ bash tests/complexity-gate.sh --check > "$LOGS/gate-1.log" 2>&1; echo EXIT=$?
EXIT=1   # first run: REGRESSION: new function over target:
         # check_platform_absolute_outdir@178-227@tests/control-mastering-commands.py (CCN 12)
$ bash tests/complexity-gate.sh --check > "$LOGS/gate-1.log" 2>&1; echo EXIT=$?
EXIT=0   # after extracting the wait_for_mastering_run helper (CCN <= 10)
$ bash tests/file-length-gate.sh --check > "$LOGS/gate-2.log" 2>&1; echo EXIT=$?
EXIT=0
```

## Why it is safe

- Windows: drive-letter and UNC absolutes now pass the platform's own rule, so
  `mastering.run` accepts them (the reported bug).
- POSIX: leading-`/` acceptance is unchanged; `:`-prefixed paths (Qt resource
  paths, the round-2 leak) are refused again, matching the original rule.
- Relative paths are still refused on both platforms.
- The test change strengthens assertions (accepted ack + completion within the
  bound + expected outputs) and adds a POSIX regression; nothing was weakened.

## Registration

`src/core/ControlCommandsMasteringRun.cpp`, `tests/control-mastering-commands.py`,
and this report are already registered in `tests/fork-sources.txt`; no new
product files and no inherited-file changes, so `tests/upstream-modifications.txt`
needs no update.
