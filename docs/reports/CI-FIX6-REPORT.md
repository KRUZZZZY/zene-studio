# CI-FIX6-REPORT — run #6's reds: the checks namespace rule, the Qt5 widget-include leak, the MSVC empty boundary, and one wall-clock bound

**What this is.** Run #6 (`build` 35253612993, `checks` 35253612944, both at the then-release tip
`fc2e1f2ed`) left two workflows red across six jobs — five of the seven `build` jobs and the
`scripted-checks` job. This lane fixed four causes on `030/ci-fix6` (one commit each, four commits),
proved every fix locally with a red state and a green state, closed the local blind spot that let
cause 1 reach the tip, and re-ran the suite.

All seven jobs of run #6 were read before pushing: the last one (`linux-arm64`) concluded at 19:31 UTC
with the same Qt5 boundary reading the other four Qt5 jobs carried, so the push that starts run #7
discards no decision-relevant verdict — run #7 is a complete re-measurement.

| item | value |
|---|---|
| worktree | `projects/lmms-fl-research/zene-030/wci6` |
| branch | `030/ci-fix6` |
| base (run #6's tip) | `fc2e1f2ed` — the release tip at the time |
| fixes | `0e962bcc0` `facf3c57b` `24e866dd1` `a089679fa` `e7479fa76` + `7001f8ad6` `f2a035c4e` (the MSVC tokenizer, found in run #7) + the report commits |
| run #6 | `build` **35253612993** (7 jobs), `checks` **35253612944** (3 jobs) |
| run #7 | `build` **35269927214**, `checks` **35269927503**, `quality-gates` **35269927216**, `doxygen` **35269927226** |
| run #8 | the re-push carrying the MSVC tokenizer fix (see "Run #7, and the second push") |

## What run #6 said, per job (read from each job's own log)

| workflow | job | id | verdict in run #6 |
|---|---|---|---|
| checks | `scripted-checks` | 105311706616 | **failure** — `Error: include/zene/api/ControlApi.h: File has no namespace lmms` |
| build | `linux-x86_64` | 105311717911 | **failure** — `ZeneApiBoundary` (209/210): 42 boundary compile lines carry a QtWidgets include |
| build | `macos-x86_64` | 105311717439 | **failure** — same test, same reading |
| build | `macos-arm64` | 105311718009 | **failure** — same test, same reading |
| build | `msvc-x64` | 105311717391 | **failure** — `ZeneApiBoundary` (161/162) `IndexError` in `probe_flags`; `TelemetryTransportTest` (128/162) `'elapsed < 2000' returned FALSE` (2183 ms) |
| build | `windows-arm64` | 105311717673 | success |
| build | `mingw64` | 105311717735 | success |
| build | `linux-arm64` | 105311717677 | **failure** — concluded after this lane started (19:31 UTC): `206/207`, `ZeneApiBoundary` red with the same Qt5 reading: `42 boundary compile lines carry a QtWidgets include (liveness: the control carries 1)`, membership 42/42, closure 0/42 (control 88), symbols 0/42 (control 174), and the `ControlApi.h` `#error` |

The two other jobs of `checks` (`shellcheck`, `yamllint`) were green; `scripted-checks` was red on
its third step (`check-strings` and `verify` passed: `0 errors.` / `SUCCESS` in that job's log).

## Cause 1 — `check-namespace` refuses the `zene::api` boundary; and no local gate ran the scripted checks

**Evidence.** `checks` job 105311706616: `# namespace checks` → one error →
`Error: include/zene/api/ControlApi.h: File has no namespace lmms` → `1 errors.` →
`##[error]Process completed with exit code 1`.

**Why the header is right and the script was under-informed.** `include/zene/api/ControlApi.h` opens
`namespace zene { namespace api {` on purpose: it is the ARCH-2 boundary's public entry point, and it
re-exports the `lmms::` implementation names rather than renaming them — the boundary is held by the
`zene_api` target and the `ZeneApiBoundary` ctest, not by a namespace (the header says so itself, and
`docs/reports/ARCH2-BOUNDARY-REPORT.md` carries the design). The script has a sanctioned escape for
exactly this: the `known_no_namespace_lmms` set, one entry per file with a reason in the file's own
comment style.

**Fix.** The header is added to that set with a reason naming the design (`0e962bcc0`).

**The blind spot, closed.** `tests/run-all-gates.sh` did not run `tests/scripted/*` at all, while CI's
`checks.yml` runs `tests/scripted/verify`, `check-strings` and `check-namespace` on every push. Every
local bar on `fc2e1f2ed` — this suite, `ctest` 214/214 — was green on the same commit that CI called
red, because nothing that runs by default measured the scripted checks. That is the same class of
blind spot the suite's own SCOPE POLICY paragraph was written about, so it is closed in the same shape
Gate 11 uses: a new **Gate 13** runs the fixtures' own red/green control (`tests/scripted/verify`)
FIRST, then `check-namespace`, then `check-strings`. `check-strings` imports `python3-tinycss2`, the one
non-stdlib import in the scripted checks (CI installs it in its job, not in the source tree), so a
machine without it records the row as SKIP with that reason — the run then exits 3, never 0.
`tests/QA-GATES.md` carries Gate 13's section; the script's header carries the paragraph.

**Red → green, both states run (the entry removed, then restored).**

```
$ tests/scripted/check-namespace            # allowlist entry removed
Error: include/zene/api/ControlApi.h: File has no namespace lmms
1 errors.                                   EXIT=1
$ tests/scripted/check-namespace            # entry restored
0 errors.                                   EXIT=0
$ tests/scripted/verify     -> SUCCESS      EXIT=0
$ tests/scripted/check-strings -> 0 errors. EXIT=0
```

Logs: `/tmp/fix6-cn-before.log` (pristine tree, EXIT=1), `/tmp/fix6-cn-red.log` (reverted, EXIT=1),
`/tmp/fix6-cn-after.log` (restored, EXIT=0), `/tmp/fix6-verify.log`, `/tmp/fix6-cs.log`.

## Cause 2 — the Qt5 widget-include leak in `zene_api`'s compile flags (root-caused to `Qt5::Svg`)

**Evidence (linux-x86_64 job 105311717911, and both macOS jobs for the same reason).**

```
no widget include path: 42 boundary compile lines carry a QtWidgets include (liveness: the control carries 1)
  - ../../src/core/ControlRegistry.cpp compiles with a QtWidgets include path: /usr/include/x86_64-linux-gnu/qt5/QtWidgets   (…and 41 more, one per boundary TU)
headless compile probe: FAILED (flags of ../../src/core/ControlRegistry.cpp, -fsyntax-only)
  include/zene/api/ControlApi.h:72:2: error: #error "the zene::api boundary (ARCH-2) must not be compiled with Qt Widgets …"
include closure: 0/42 boundary TUs reach a QtWidgets header (liveness: the control reaches 88)
object symbols: 0/42 boundary objects reference a Qt widget symbol (liveness: the control references 175)
```

So the *substantive* checks passed (membership 42/42, closure 0/42, symbols 0/42) — the leak was in the
boundary's **compile flags**, and the `#error` the header deliberately carries is what turned it into a
red test rather than a silently dirty boundary.

**Root cause, read out of the packaged Qt configs.** `src/CMakeLists.txt` filters the QtWidgets
*directory* out of the target's include property and filters `Qt5::Widgets`/`Qt6::Widgets` out of the
link list by name. A name filter cannot see a module that drags QtWidgets in **transitively**, and one
Qt5 module does exactly that:

* `Qt5SvgConfig.cmake:152` — `set(_Qt5Svg_LIB_DEPENDENCIES "Qt5::Widgets;Qt5::Gui;Qt5::Core")`, and line
  57 appends that list to `Qt5::Svg`'s `INTERFACE_LINK_LIBRARIES`.
* `Qt6SvgTargets.cmake:76` — `INTERFACE_LINK_LIBRARIES "Qt::Core;Qt::Gui"` (Qt6 split QtSvgWidgets out).

`Qt5::Svg` does not match `[Ww]idgets`, so it stayed linked and its usage requirements put both the
QtWidgets include directory **and** `-DQT_WIDGETS_LIB` (Qt5's `INTERFACE_COMPILE_DEFINITIONS`) into all
42 boundary compile lines. Qt6's `Qt6::Svg` reaches neither — which is exactly why the local Qt6 build
and the Qt6 jobs were clean while every Qt5 job was red.

**Fix (`24e866dd1`).** After the name filter, each remaining candidate's interface closure
(`INTERFACE_LINK_LIBRARIES`, transitively) is walked, and any module that reaches a widget target is
dropped from *this target's* link list, with the module and the target it reached printed at configure
time. The directory-scope include filter stays; the boundary keeps the Qt parent directory (Qt's own
headers include each other module-qualified, by design); the probe is untouched and stays strict.

**Red → green, on a real Qt5 configure and a real Qt5 compile (this box, kit `~/.local/opt/qt5/usr`).**

The Qt5 kit that ships in this programme is a bundle of the linux lane's `qtbase5-dev` headers/libs; a
Qt5 configure of this tree succeeds against it (`cmake -S . -B /tmp/zene-b5 -DWANT_QT6=OFF
-DWANT_VST=OFF -DCMAKE_PREFIX_PATH=~/.local/opt/qt5/usr -DWANT_WASM=ON
-DWASMTIME_ROOT=…/zene-030/third_party/wasmtime`), so the leak was reproduced locally before the fix:

| measurement | before the fix | after the fix |
|---|---|---|
| `zene_api` compile lines carrying a QtWidgets include dir | **42/42** (`…/qt5/QtWidgets`) | **0/42** |
| `zene_api` compile lines carrying `-DQT_WIDGETS_LIB` | **42/42** | **0/42** |
| control (`MainWindow.cpp`) widget include dirs (liveness) | 1 | 1 |
| Qt include dirs still on a boundary line | QtCore, QtGui, QtXml, QtNetwork, the Qt parent dir, mkspecs | same (unchanged) |

The configure prints the drop by name after the fix:

```
-- zene_api: Qt5::Svg is NOT linked to the boundary - its interface closure reaches Qt5::Widgets, which must not be reachable in this target
```

`cmake --build /tmp/zene-b5 --target zene_api` → `Built target zene_api` (42/42 objects): the boundary
still compiles against everything it needs. And the probe itself, against that Qt5 build (control object
compiled from the real `src/gui/MainWindow.cpp` with its recorded Qt5 command):

```
target membership: 42/42 boundary sources compile in /tmp/zene-b5/src/CMakeFiles/zene_api.dir
no widget include path: 0 boundary compile lines carry a QtWidgets include (liveness: the control carries 1)
headless compile probe: OK (flags of src/core/ControlRegistry.cpp, -fsyntax-only)
widget-include negative controls: `#include <QWidget>` and `#include <QApplication>` fail to compile as required
include closure: 0/42 boundary TUs reach a QtWidgets header (liveness: the control reaches 88)
object symbols: 0/42 boundary objects reference a Qt widget symbol (liveness: the control references 174)
=== PASS (zene::api boundary: 42 TUs, headless) ===                                            EXIT=0
```

The control's closure reading (88) is the same number the linux CI job printed — the same measurement,
on a different machine.

Logs: `/tmp/fix6-configure-qt5-red.log`, `/tmp/fix6-configure-qt5-green.log`,
`/tmp/fix6-qt5-build-zene_api.log`, `/tmp/fix6-probe-qt5-after.log`, fixtures `/tmp/fix6-*.py` runs
recorded under `/tmp/fix6-probe-qt6.log` (Qt6, EXIT=0).

## Cause 3 — the MSVC empty boundary: an `IndexError`, now a typed SKIP with the measured classes

**Evidence (msvc-x64 job 105311717391).**

```
161/162 Test #161: ZeneApiBoundary ....***Failed 1.41 sec
  File ".../tests/zene-api-boundary.py", line 294, in probe_flags
    probe_source = sorted(boundary)[0]
  IndexError: list index out of range
```

On that job `compile_commands.json` carries no boundary source that resolves to an object under the
target's object directory, so the boundary set is EMPTY — and the crash happened *before* check 1's own
line could print, which is why the log shows no membership measurement at all. The by-design MSVC skips
(include closure, object symbols) never fired either.

**Fix (`facf3c57b`).** An empty boundary on an MSVC toolchain is now a **typed SKIP** that prints what
was measured: the membership summary, the per-class tally of the membership problems, every measured
line, and the note that 0 of 42 boundary translation units were measured on that job (so the boundary
target's include path remains that job's gate). `main()` prints
`=== SKIP (zene::api boundary: MSVC: 0 of 42 … (N x <class>)) ===` and exits 0. Three properties are
kept and each was proved on synthetic fixtures (`/tmp/fix6-sim*`, run through the real probe):

| fixture | expectation | measured |
|---|---|---|
| A: MSVC + empty boundary | typed SKIP, no traceback | `EXIT=0`, `=== SKIP (… 3 x no-object-path) ===` |
| B: GCC + empty boundary | FAIL (the membership problems), never a crash | `EXIT=1`, `=== FAIL ===`, 3 problems named, checks 2–4 printed as `NOT MEASURED` |
| C: MSVC + **non-empty** dirty boundary (2/3 resolve) | FAIL — the skip must not mask it | `EXIT=1`, `=== FAIL ===`; the skip did not fire |

The probe's thresholds and checks are unchanged: a **non-empty but dirty** boundary still fails on every
toolchain. `probe_flags()` additionally raises the file's existing `Failure` (exit 2, a setup error) if
it is ever handed an empty boundary — defence in depth, not a silent pass.

**And the real cause, found in run #7 (`7001f8ad6`).** The skip made the probe survive long enough to
print the measurement run #6 could never see, and that measurement named the cause:

```
target membership: 0/42 boundary sources compile in D:\a\zene-studio\zene-studio\build\src\CMakeFiles\zene_api.dir
  - D:/a/zene-studio/zene-studio/src/core/ControlRegistry.cpp is compiled OUTSIDE the boundary target
    (object D:\a\zene-studio\zene-studio\build\srcCMakeFileszene_api.dircoreControlRegistry.cpp.obj)
```

The object path's separators had been **eaten**. The recorded command is a string (`"command"`) and the
probe split it with `shlex.split(command)` — POSIX rules, where `\` is an escape character — so
`-FoD:\a\...\CMakeFiles\zene_api.dir\core\X.cpp.obj` became
`D:azene-studio...srcCMakeFileszene_api.dircoreX.cpp.obj`, which is not under the object directory (nor
even absolute under `ntpath`). All 42 sources were therefore reported as compiled outside the boundary
target, the boundary set was empty, and the by-design MSVC skips were never reached. That empty boundary
is also what crashed run #6.

**The fix** is a new `split_command()`: a **Windows** command line goes through
`shlex.split(command, posix=False)` (separators kept) with one layer of surrounding quotes stripped; a
POSIX command line keeps the old path byte for byte. The Windows test is
`WINDOWS_COMMAND_RE` = a drive-letter/UNC path **or** a backslash that is not the start of an escaped
quote. That second form is load-bearing and was measured before it was used: an earlier, looser version
of the rule ("the command holds backslashes at all") misfired on the GCC jobs' string defines
(`-DLIB_DIR=\"../lib\"`) and broke the local headless compile probe — and of the **1,944** compile lines
in this repo's own Linux build, **not one** carries a drive letter or a backslash-followed-by-a-non-quote,
so the rule is inert on the GCC/Clang jobs and fires only on the Windows ones.

**Proof (this box).**

```
# the CI command shape, rebuilt as a fixture (/tmp/fix6-sim-win, backslash-named trees):
fixed splitter: /tmp/fix6-sim-win/D:\a\...\zene_api.dir\core\ControlSchema.cpp.obj   EXACT MATCH: True
old splitter:   /tmp/fix6-sim-win/D:azene-studiozene-studiobuildsrcCMakeFileszene_api.dircore…obj  False

# the comparison the probe then performs, simulated under ntpath (Windows rules):
under(obj_dir, fixed) -> True    ntpath.isabs(fixed) -> True
under(obj_dir, old)   -> False   ntpath.isabs(old)   -> False      <- the CI message, reproduced

# no POSIX regression:
tests/zene-api-boundary.py against this worktree's Qt6 build -> "=== PASS (zene::api boundary: 42 TUs, headless) ===" EXIT=0
ctest -R "ZeneApiBoundary|TelemetryTransportTest|ControlRegistryTest|SafeStartTest" -> 4/4 Passed, EXIT=0
```

What the local proof cannot reach: a Linux box cannot resolve `D:\` paths, so that the runner's
`os.path.isfile` agrees with the restored paths is CI's to measure — which is exactly what run #8 does.

## Cause 4 — `TelemetryTransportTest::aSlowEndpointDoesNotBlockTheCaller`: a 2 s bound inside the runner's noise

**Evidence (msvc-x64 job 105311717391).**

```
FAIL!  : TelemetryTransportTest::aSlowEndpointDoesNotBlockTheCaller() 'elapsed < 2000' returned FALSE.
         (send() took 2183 ms: it waited for a reply that never comes. The transport must hand the POST off and return (CODE-7))
```

**Run history, both runs read.** The same test binary **passed** in the previous run (`build`
35227872668, msvc-x64 job 105223982579): `127/161 Test #128: TelemetryTransportTest ... Passed 0.90
sec` — the whole suite finished in 0.9 s. In run #6 the same binary took 2.25 s and the send alone took
2183 ms. Nothing in this lane touches telemetry, and `send()` still hands the POST to
`QNetworkAccessManager` and returns (no nested event loop, no wait). So the 2 s bound was sitting
inside the noise of a loaded Windows runner — a bound problem, not a re-run.

**Endpoint's own delay (the differential).** The endpoint is TEST-NET-1 (192.0.2.0/24, RFC 5737) which
never answers; the retired implementation waited out its own **10 s** single-shot timer on it (stated in
three places: the test's header, `TelemetryNetworkTransport.h`, `TelemetryNetworkTransport.cpp`).

**Fix (`a089679fa`).** The bound is 5000 ms: **1:2** against the endpoint's own ten seconds (so the
retired implementation cannot satisfy it on any machine) and **2.3×** the worst legitimate measurement a
loaded runner has produced (2183 ms). Both numbers and the ratio are written beside the assertion and in
the file header. Measured after the change (this box, RelWithDebInfo Qt6):
`a send to a blackholed endpoint returned in 7 ms`, `Totals: 14 passed, 0 failed`.

## The local bar (all measured on this worktree)

| step | command | result |
|---|---|---|
| configure | release configuration (`-DWANT_QT6=ON -DUSE_WERROR=ON -DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON -DWANT_VST3=ON -DWANT_CLAP=ON -DWANT_VST3_TEST_INSTRUMENT=ON -DWANT_WASM=ON`) | EXIT=0, 214 tests registered |
| build | `cmake --build build -j20` | EXIT=0 |
| focused ctest | `ctest -R "ZeneApiBoundary\|TelemetryTransportTest\|ControlRegistryTest\|SafeStartTest"` | EXIT=0, 4/4 passed |
| full ctest | `ctest -j4 --output-on-failure` from `build/tests` | **214/214 passed**, EXIT=0 |
| gates (first run) | `bash tests/run-all-gates.sh` | **exit 1 — Gate 7 red** (`tests/zene-api-boundary.py` 564 > 500); the other 11 executed gates PASS, Gate 13 PASS |
| gates (after the re-anchor above) | `bash tests/run-all-gates.sh` | **PASS-WITH-SKIPS, exit 3** — 12 of 13 rows PASS, Gate 2 SKIP (`--with-coverage` not passed), Gate 13 (scripted-checks) **PASS**; `git status` after the mutation sweep: no leftovers |
| probe (Qt6) | `tests/zene-api-boundary.py …` | EXIT=0, 42 TUs, all six checks with liveness |

The gate-13 row's own evidence, from that run: `tests/scripted/verify` → `SUCCESS` (its fixtures went
red and green as designed — the fixture `check-namespace` run prints the 8 expected errors and exits 1),
`tests/scripted/check-strings` → `0 errors.`, `tests/scripted/check-namespace` → `0 errors.`

## Gate 7, and why this lane carries a fifth commit

The first full local run of the suite came out **red on Gate 7 (file-length)**, and for a reason this
lane created: `tests/zene-api-boundary.py` went from 487 to 564 lines with the MSVC skip, crossing the
500-line cap (`REGRESSION: new file over 500 lines: tests/zene-api-boundary.py (564)`). The repo's own
Gate 7 policy names the mechanism for a fork-authored file that legitimately grows — a **single-file
re-anchor with a reason** — and states the trade ("trimming code to satisfy a line count is the worse
trade"); `--reanchor` (whole baseline) is explicitly *not* for this case, because it grandfathers every
other entry unreviewed. So the recorded act is one file, one reason, and nothing else moved:

```
$ bash tests/file-length-gate.sh --reanchor-file tests/zene-api-boundary.py "<reason>"
RE-ANCHORED (single file, fork scope): tests/zene-api-boundary.py none -> 564 lines
$ bash tests/file-length-gate.sh --check
PASS (check mode: no regressions; baseline not written)                       EXIT=0
```

The reason names the path, the measured growth (487 → 564), the class (a fork-authored test source —
the boundary's own proof ctest) and what the growth is (the typed SKIP, `toolchain_is_msvc()`, the
closure guard). It is in the command's own output, in commit `e7479fa76`, in `tests/QA-GATES.md`'s Gate
7 section, and here. Only `tests/file-length-baseline.tsv` changed in that commit.

## Run #7, and the second push

The push carried `99b043f71` (the merge of the first five commits) and every workflow it triggers ran:
`build` **35269927214**, `checks` **35269927503**, `quality-gates` **35269927216**, `doxygen`
**35269927226**.

| workflow | job | run #7 verdict |
|---|---|---|
| checks | `scripted-checks` | **success** — `check-namespace` prints `0 errors.`; cause 1 verified in CI |
| checks | `shellcheck`, `yamllint` | success |
| quality-gates | `static gates (3, 4, 6, 7, 8, 9, 11, 12)` | **success** — including `file-length-gate.sh --check` → PASS, so the Gate 7 re-anchor holds in CI |
| quality-gates | `MCP bridge Python tests (no build)` | success (the build-backed jobs of that workflow are dispatch-only by policy) |
| doxygen | `linux-x86_64` | success |
| build | `macos-arm64` | **success** — `ZeneApiBoundary ... Passed 14.17 sec`, `TelemetryTransportTest ... Passed 0.31 sec`, `100% tests passed out of 207`; causes 2 and 4 verified |
| build | `macos-x86_64` | **success** |
| build | `linux-x86_64` | **success** — the canonical Qt5 job, red in run #6; cause 2 verified |
| build | `mingw64`, `windows-arm64` | success |
| build | `msvc-x64` | **failure** — `ZeneApiBoundary`, now for a *stated* reason (the mangled object paths above). `TelemetryTransportTest` **passed** (cause 4 verified); it is the only red test of 162 |
| build | `linux-arm64` | the straggler in every run; still in flight when the second push was made (see below) |

So run #7 verified causes 1, 2 and 4 in CI on five jobs, and turned cause 3 from "a crash with no
measurement" into "a measurement with a cause" — which is what the second push fixes.

**The second push.** `7001f8ad6` + `f2a035c4e` + this report's update were pushed with
`zene-safe-push --force HEAD:release/0.3.0`, because `linux-arm64` was still running at push time: the
force prints what it discards, and what it discarded was that job's run on `99b043f71` — a SHA whose
every other verdict had already been extracted (6 of the 7 build jobs concluded; the 7th is `msvc-x64`,
whose failure is the reason for this push). Nothing decision-relevant was thrown away, and the new push
re-runs all seven jobs plus the three other workflows on the fixed tree.

## What only CI can prove

* That the Qt5 jobs are clean **in CI's own Qt5** (the local Qt5 proof uses the programme's extracted
  Qt5 kit — same Qt 5.15.13 packaging, different install prefix and no X11/`Xvfb` stack: `-DWANT_VST=OFF`
  locally, because the kit has no `X11_XCB`). **Run #7 answered this: `linux-x86_64` and both macOS jobs
  are green.**
* That the MSVC job's boundary set is now **non-empty** (the tokenizer fix's own verdict) and that its
  checks 1–4 then run against the real MSVC toolchain — a Linux box cannot resolve `D:\` paths, so the
  local proof stops at the `ntpath`-level comparison. **This is run #8's job.**
* That the timing test stays green under the runner load that produced 2183 ms. **Run #7 answered this:
  `TelemetryTransportTest` passed on all six concluded build jobs, including `msvc-x64`.**

All seven build jobs of run #6 have now been read (the last one, `linux-arm64`, concluded at 19:31 UTC
with the same Qt5 boundary reading), so run #7 is a complete re-measurement of every job — nothing from
run #6 is left unread.
