# Cluster 5653 — Mastering rejects absolute Windows output paths

Verdict: **FIXED**

## The finding

`mastering.run` validated `out_dir` absoluteness with a leading-`/` check, so on
Windows a drive-letter or UNC absolute path (`C:\Masters`) was refused as
"not absolute" — the command was unusable on every Windows path.

## Evidence

- `src/core/ControlCommandsMasteringRun.cpp:151` (before the fix) held
  `if (!outDir->startsWith(QLatin1Char('/')))`.
- The fork's own convention is the platform's rule: 30+ other control commands
  already use `QFileInfo(path).isAbsolute()` (e.g. `ControlCommandsRenderQueue.cpp:172`
  for its `out_dir`, `ControlCommandsLivecode.cpp:130`, `ControlCommandsProjectLifecycle.cpp:61`).
  `ControlCommandsMasteringRun.cpp` was the lone outlier.
- The bug is real on Windows builds: the fork ships Windows-specific sources
  (`src/core/CrashReporterWindows.cpp`, `src/gui/MixerWindows.cpp`, the named-pipe
  transport in `tests/control_pipe_client.py`), so Windows is a supported platform.

## The fix

`src/core/ControlCommandsMasteringRun.cpp` — one rule swap plus the include:

```cpp
#include <QFileInfo>
...
	// The platform's own rule, not a leading '/': on Windows a drive-letter or
	// UNC path is absolute and must be accepted (cluster 5653, BUG 11).
	if (!QFileInfo(*outDir).isAbsolute())
	{
		return QStringLiteral("'out_dir' must be an absolute path");
	}
```

Behaviour-preserving on POSIX: on a Linux build `QFileInfo::isAbsolute` is
identical to a leading-`/` check (Qt's path predicates are compile-time
platform-dependent; `QC::hasDriveLetter` is false off-Windows). Verified
empirically with a small Qt6 program compiled against this host's Qt6Core:
`"/abs" true`, `"C:\Masters" false`, `"C:/Masters" false`, `"D:" false` —
i.e. the fix changes acceptance only on Windows builds, where drive-letter and
UNC paths become accepted.

## The regression test

`tests/control-mastering-commands.py` (the existing socket proof for this area):

- POSIX side (runs on this host): a drive-letter `out_dir` (`C:\Masters`) is
  added to the junk-refusal list — on a POSIX host it is a relative filename
  and must still be refused as `invalid_args`. This pins that the fix keeps
  host semantics rather than accepting foreign-platform names everywhere.
- Windows side (compiled path only runs on Windows hosts):
  `check_windows_absolute_outdir` asserts a drive-letter `out_dir` is NOT
  refused with the absolute-path message, and polls the started run to
  completion so it cannot leave a second render in flight.

Platform note on RED/GREEN: on this POSIX host the fix is behaviour-preserving
(the probe above), so no POSIX-observable assertion can be RED before the fix —
the RED lives on Windows builds, where the harness hands the run a drive-letter
`out_dir` (`tests/control_socket_harness.py:218`: `tempfile.mkdtemp(..., dir="/tmp" if os.path.isdir("/tmp") else None)` —
on Windows `/tmp` does not exist, so `instance.tmp` is e.g. `C:\Users\...\Temp\zctl-run-*`),
which before the fix was refused outright and failed the whole proof. The
Windows-side check pins the same rule explicitly.

## Commands run (copied; EXIT codes as seen)

```
cmake --build build --target zene > LOGS/build-1.log 2>&1; echo EXIT=$?
  EXIT=0
ctest -R ControlMastering --output-on-failure > LOGS/test-1.log 2>&1; echo EXIT=$?   (from build/tests)
  EXIT=0   # 3/3: ControlMasteringCommands Passed 7.95s, ControlMasteringRunBound Passed 44.10s, ControlMasteringQuit Passed 2.76s
bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1; echo EXIT=$?
  COMPLEXITY_EXIT=0
bash tests/file-length-gate.sh --check > LOGS/gate-2.log 2>&1; echo EXIT=$?
  LENGTH_EXIT=0
```

No RED baseline was run on this host: the fix is behaviour-preserving on POSIX
(verified by the probe), so a pre-fix run of the family here would be GREEN too;
the RED is the Windows-side assertion above, exercised only on Windows builds.

## Registration

`src/core/ControlCommandsMasteringRun.cpp` and `tests/control-mastering-commands.py`
are already registered as fork-new sources in `tests/fork-sources.txt`; no new
files were added, so no registration change was needed.
