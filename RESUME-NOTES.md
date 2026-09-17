# RESUME NOTES — PLATFORM-DELTA wave (030/wplat-lin · 030/wplat-arm64 · 030/wplat-win · 030/wplat-mac)

Base `eba78f8ff`. Three lanes each wrote their own `RESUME-NOTES.md` at the worktree root;
they are merged here 2026-09-17 into ONE record, one section per lane, lane text unmodified
except that every heading inside a lane keeps its text and drops one level (the lane's own
`# ` title becomes the `## ` section heading) so the file still reads as one document.
`030/wplat-arm64` wrote none (`docs/reports/WPLAT-ARM64-REPORT.md` is its record).
Merge order on `release/0.3.0`: lin, arm64, win, mac.

---

## RESUME NOTES — PLATFORM-DELTA lane (LINUX) · 030/wplat-lin · 2026-09-16

Worktree: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wplat-lin`
(branch `030/wplat-lin`, base `eba78f8ff`). **Nothing was pushed.** Build tree `build/` exists here
(fresh Qt6 RelWithDebInfo, 5 targets: `ClipLinkTest ClipLinkPersistenceTest ImportDetectionTest
ControlShutdownHookTest SafeStartTest`). All three items are **fix committed + proved locally**;
the only unfinished deliverable is `docs/reports/WPLAT-LIN-REPORT.md` (not written — wind-down).

### Commits on this branch

| SHA | item |
|---|---|
| `87475243a` | the ClipLink/ImportDetection trio registers the offscreen Qt platform |
| `f387a2a57` | SafeStartTest's crashing child faults with the kernel's disposition |
| `272c9c42c` | ControlShutdownHookTest observes a re-created registry by state, not by address |

### Item 1 — ClipLinkTest (#16) / ClipLinkPersistenceTest (#17) / ImportDetectionTest (#57) · FIX COMMITTED `87475243a`

- **Root cause.** The three test BINARIES build a `QApplication`: each ends in `QTEST_MAIN`, and
  `QTEST_MAIN` expands to `QTEST_MAIN_SETUP()` → `QApplication app(argc, argv)` whenever
  `QT_WIDGETS_LIB` is defined (QtTest `qtest.h`) — which it is for every target in
  `tests/CMakeLists.txt` because they all link `${QT_LIBRARIES}` (Qt::Widgets). A `QApplication`
  with no display is a `qFatal` in its constructor. These three were the only Engine/control-surface
  tests not registering the headless platform ~85 siblings register.
- **CI evidence.** run 35126160372, job 104895806780: #16/#17/#57 "Subprocess aborted" after ~0.2 s,
  before their first line of output, with the `qt.qpa.xcb`/`no Qt platform plugin` text — while the
  same job's own backtrace step ran the same binaries with `QT_QPA_PLATFORM=offscreen` and passed
  them (ClipLinkTest `Totals: 8 passed, 0 failed`).
- **Local red (before the fix, Qt6 box).** `env -u DISPLAY ./build/tests/ClipLinkTest` → `EXIT=134`
  with the identical text; `env -u DISPLAY ctest -R "ClipLinkTest|ClipLinkPersistenceTest|ImportDetectionTest"`
  (from `build/tests`) → `CTEST_EXIT=8`, "0% tests passed, 3 tests failed out of 3", all
  `(Subprocess aborted)`. With `DISPLAY=:0` (the dev box) it was green — that is why it was CI-only.
- **Local green (after).** `env -u DISPLAY ctest -R <those three>` → `CTEST_EXIT=0`, 100% passed 3/3;
  with `DISPLAY=:0` → `CTEST_EXIT=0` 3/3; direct headless with the env the registration sets:
  ClipLinkTest `EXIT=0` (8 PASS/0 FAIL), ClipLinkPersistenceTest `EXIT=0` (4/0), ImportDetectionTest
  `EXIT=0` (7/0).
- **Files.** `tests/CMakeLists.txt` (the `set_tests_properties(... ENVIRONMENT "QT_QPA_PLATFORM=offscreen")`
  block + re-derived the stale ImportDetectionTest comment), `tests/upstream-modifications.txt`
  (the `tests/CMakeLists.txt` ledger reason, same commit). Logs: `/tmp/wplat-lin/ctest-trio-{before,after,withdisplay}.log`,
  `/tmp/wplat-lin/*.headless.log`.

### Item 2 — ControlShutdownHookTest::aHookSurvivesTheRegistryInstanceItWasRegisteredOn · FIX COMMITTED `272c9c42c`

- **Root cause.** The slot proved "instance() built a NEW registry" by comparing addresses
  (`recreated != registry`). A pointer is not an identity witness across delete/new: the allocator may
  hand the new object the freed bytes, and on the runner it did. Product is fine — the hook store is a
  function-local static (`shutdownHooks()` in `src/core/ControlRegistry.cpp`), so the hook does survive;
  `delete s_instance; s_instance = nullptr;` in `destroy()` is intact.
- **CI evidence.** run 35126160372: `FAIL! … 'recreated != registry' returned FALSE. (destroy() did not
  build a new instance for instance()) Loc: tests/src/core/ControlShutdownHookTest.cpp(86)`, while the
  other four slots of the same 0.03 s run passed on the same statics.
- **Measured here.** `sizeof(lmms::ControlRegistry)=128`; a temporary probe printed
  `registry=0x5b1f5a478c60 recreated=0x5b1f5a521ba0` (different → green here). An `LD_PRELOAD`
  malloc/free tracer of that size class (source `~/.cache/wplat-lin/mtrace.c`) shows the freed chunk is
  not handed back: next `malloc(128)` = `0x61478d42ba10`. Reuse is nevertheless the *normal* outcome of
  the plain pattern: `~/.cache/wplat-lin/alloc-reuse.cpp` prints `first=0x…82b0 second=0x…82b0
  same-address=YES`.
- **Fix.** The destroyed instance gets a marker in its own state (`QObject::objectName`, nothing in the
  product names the registry); the marker is asserted observable at all, and the re-created instance
  must not carry it. No product source changed.
- **Proof it is not vacuous (red).** With a temporary `destroy()` that deletes nothing (reverted, never
  committed): `FAIL! … 'recreated->objectName() != marker' returned FALSE … Loc: […ControlShutdownHookTest.cpp(101)]
  Totals: 2 passed, 1 failed`.
- **Green (final binaries).** `env -u DISPLAY ./build/tests/ControlShutdownHookTest` → `EXIT=0`,
  `Totals: 6 passed, 0 failed`; `env -u DISPLAY ctest -R "ControlShutdownHookTest|SafeStartTest"` →
  `CTEST_EXIT=0`, 2/2.

### Item 3 — SafeStartTest::realSignalLeavesTheMarkerAndTheNextLaunchStartsSafe (SIGSEGV) · FIX COMMITTED `f387a2a57`

- **Root cause.** The crashing child is a `fork()`, so it inherits the SIGSEGV handler QtTest installed
  in `qExec` — and **Qt 5.15.3's handler is not a re-raiser**: it dumps a stack through gdb, reports
  "Received a fatal error." through QTestResult and ends the process on its own path, so the waiter sees
  an abort/exit status and the `WTERMSIG(status) == SIGSEGV` assertion fails. Qt 6.4.2 re-raises
  (child comes back signaled 11) — which is why this box was green.
- **CI evidence.** run 35126160372: `QFATAL : … Received signal 11 … Function time: 3ms` plus that
  child's own QtTest dump (`=== Received signal at function time: 3ms … dumping stack ===` → gdb attach →
  `=== End of stack trace ===`) and then `FAIL! … 'WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV'
  returned FALSE … Loc: […/tests/src/core/SafeStartTest.cpp(186)]`.
- **Local red, real binary.** `LD_PRELOAD=~/.cache/wplat-lin/forkhook.so QT_QPA_PLATFORM=offscreen
  ./SafeStartTest realSignalLeavesTheMarkerAndTheNextLaunchStartsSafe` (the preload installs a
  Qt5-shaped handler in the child at the `fork()` return point) → `EXIT=1`, the same failure at
  `SafeStartTest.cpp(186)`. Unfixed binary kept at `~/.cache/wplat-lin/SafeStartTest.unfixed`
  (md5 `98138c99640c58626bc70008aa2c985c`).
- **Local red/green against the job's OWN QtTest** (ubuntu 22.04 `libqt5test5 5.15.3+dfsg-2ubuntu0.2`
  + `libqt5core5a` + `qtbase5-dev` + `qtbase5-dev-tools` + `libicu70` unpacked into
  `~/.cache/wplat-lin/qt5`, source `~/.cache/wplat-lin/ft5/forktest.cpp`):
  fork + child null-deref **without** the reset → `PARENT-VERDICT: signaled=1 termsig=6` → FAIL `EXIT=1`;
  **with** `signal(SIGSEGV, SIG_DFL)` in the child → `signaled=1 termsig=11` → 3 passed, 0 failed `EXIT=0`.
- **Fix.** In the child, `::signal(SIGSEGV, SIG_DFL);` before faulting (the assertion's own words: "as it
  would without any of this"). The SIGKILL slot needs nothing; `CrashReporterTest`'s children are
  unaffected because they install the product's handler, which restores SIG_DFL and re-raises.
- **Green (final binaries).** under the same fork-hook → `EXIT=0`, 3 passed; whole binary headless →
  `EXIT=0`, `Totals: 9 passed, 0 failed`; `env -u DISPLAY ctest -R "SafeStartTest|ControlShutdownHookTest"`
  → `CTEST_EXIT=0`.

### Gates (all run after the last commit, unpiped) — ALL GREEN

```
bash tests/complexity-gate.sh --check      -> COMPLEXITY_EXIT=0   (PASS, baseline not written)
bash tests/file-length-gate.sh --check     -> FILELENGTH_EXIT=0   (PASS)
python3 tests/scripted/check-namespace     -> NAMESPACE_EXIT=0    (0 errors)
bash tests/fork-sources-gate.sh            -> FORKSOURCES_EXIT=0  (660 fork / 1104 all-sources / 40 tools / 0 stale)
bash tests/no-upstream-regression-gate.sh  -> UPMOD_EXIT=0        (422 changed paths declared; ledger 460)
bash tests/all-sources-reproduce.sh        -> ALLSOURCES_EXIT=0   (REPRODUCES)
```
Logs in `/tmp/wplat-lin/gate-*.log`. Note: `include/ControlRegistry.h` is at **509** lines and
`tests/CMakeLists.txt` (~2900) — do not append to the capped header (LANE-BRIEF §5).

### NEXT ACTION (whoever picks this up)

1. Write `docs/reports/WPLAT-LIN-REPORT.md` from the three commit messages above (they already carry
   root cause + evidence + residual per item) and commit it.
2. Sanity re-run on a merge tip: `cd build/tests && env -u DISPLAY ctest -R "ClipLinkTest|ClipLinkPersistenceTest|ImportDetectionTest|ControlShutdownHookTest|SafeStartTest"` — expect 5/5, `CTEST_EXIT=0`.
3. Free the disk when done: delete this worktree's `build/` (~6 GB; 122 G free at handover).
4. Residuals to name in the report if it is written by someone else: (a) no full-suite `ctest -j4`
   run was made in this lane (only the five targets were built); (b) item 2's local green is
   address-luck — the assertion no longer depends on it, but this box does not reproduce the
   runner's reuse; (c) item 3 is proved on Qt6 + on the job's own Qt5.15.3 in isolation, not on a
   Qt5 build of the whole test binary (no Qt5 on this box: `WANT_QT6=ON` is the only local config).

---

## RESUME NOTES — PLATFORM-DELTA lane (WINDOWS), branch `030/wplat-win`

Worktree: `…/projects/lmms-fl-research/zene-030/wplat-win`. Base `eba78f8ff`. Written under a
wind-down order (owner shutting the machine down); **nothing was pushed**, and the branch holds two
commits. Everything below is either committed, or an exact next action.

Commits on this branch (newest first):

| sha | what |
|---|---|
| `6c90126de` | `fix(tests): the VST3 chunk-probe fixture lays its bundle out as the Windows loader opens it` — **item 4, committed** |
| `63d9fa970` | `fix(control): the Windows named-pipe instance takes PIPE_REJECT_REMOTE_CLIENTS in the PIPE mode…` — **item 5, committed** |

Gate state after both commits (unpiped, in this worktree): `complexity-gate.sh --check` EXIT=0,
`file-length-gate.sh --check` EXIT=0, `fork-sources-gate.sh` EXIT=0, `no-upstream-regression-gate.sh`
EXIT=0, `all-sources-reproduce.sh` EXIT=0, `prove-posix-unchanged.sh` EXIT=0,
`release-staging-path-gate.sh` EXIT=0.

### The run under work — everything below is from it

`run 35126160372` (branch `release/0.3.0`, head `eba78f8ff`). My jobs:
**msvc-x64 `104895806855`** (step "Run tests": 156/161, 5 red) and **mingw64 `104895806930`** (step
"Build": died at 64%). Pulled with
`zene-gh-token gh api "repos/KRUZZZZY/zene-studio/actions/jobs/<id>/logs" --allow-escape-sequences`;
locals: `/tmp/wplat/msvc.log`, `/tmp/wplat/mingw.log` (tmp — gone after a reboot; refetch with the
same recipe).

The five msvc reds: `ControlAutomationScriptTest`, `ControlAutomationModesTest`, `MpePlaybackTest`,
`Vst3ChunkProbeTest`, `ControlNamedPipeSmoke`.

---

### Item 1 — `ControlAutomationScriptTest` (10 passed / 2 failed) — **analysed, fix drafted, NOT written**

Failed slots: `automationModeSetSucceedsOnABareParameter` (line 241) and
`automationRecordModeSetRequiresAClip` (line 294), both `'!trackId.isEmpty()' returned FALSE
(no track with parameters in the fresh song)`.

**Root cause (same class as items 2/3).** The preamble `qputenv("LMMS_PLUGIN_DIR",
LMMS_TEST_PLUGIN_DIR)`, `Engine::init`, then `revtest::addInstrumentTrack()`
(`tests/src/core/ReversibilityTestSupport.h:98`, which returns empty when `firstLoadable("instrument")`
is empty). On Windows the module load fails → empty → `automation.get_state` reports tracks with **no**
parameters (log: `plugin-scan: 76 file(s), …, 76 scanned, 0 plugin(s)`), so the scan for a track with
parameters finds none. On Linux the same build reports 27 parameters for that track (the file's own
comment).

**Fix (drafted, not committed).** In `tests/src/core/ControlAutomationScriptTest.cpp`, immediately
before each of the two `QVERIFY2(!trackId.isEmpty(), …)` lines (241, 294):

```cpp
		if (trackId.isEmpty() && !testHostCanLoadPluginModules())
		{
			QSKIP("the song exposes no automatable device parameter: plugin modules link the zene "
				"executable, so on Windows their import descriptor names zene.exe and a test host "
				"cannot satisfy it; the product loads them inside zene.exe where that resolves by "
				"construction (CI msvc-x64: QLibrary::load -> ERROR_MOD_NOT_FOUND, 126)");
		}
```

plus one file-local predicate in an anonymous namespace (the convention five other files use —
`tests/src/plugins/AudioPluginTest.cpp:101`, `PluginPortsMigrationTest.cpp:155`,
`ScriptEngineTest.cpp`, `PhaseDSidechainTest.cpp`, `PluginScanCacheTest.cpp`):

```cpp
/*! Windows: a test host cannot load a plugin MODULE library at runtime. <mechanism, see below> */
constexpr auto testHostCanLoadPluginModules() -> bool
{
#ifdef Q_OS_WIN
	return false;
#else
	return true;
#endif
}
```

Data-driven on purpose: the skip fires only when a device parameter is genuinely absent, so a future
Windows that *can* load modules runs the slot instead of skipping it, and Linux keeps failing loudly.

**Next action:** apply the two guards + the predicate to that file, then `bash
tests/file-length-gate.sh --check` (467 lines now; keep under 500), commit, and run the same edit for
item 2.

### Item 2 — `ControlAutomationModesTest` (2 passed / 2 failed) — **analysed, fix drafted, NOT written**

Failed slots: `modeSetIsObservableForEveryMode` (line 105) and
`readRideThroughTheSocketCannotTouchTheRecordedAutomation` (line 170), both on the literal message
`'findAutomatableParameter(...)' returned FALSE. (this song exposes no automatable device parameter …
the binary needs the instrument modules (LMMS_TEST_PLUGIN_DIR, see tests/CMakeLists.txt))` — the test
diagnoses itself; the mechanism is the same module load.

**Fix (drafted).** Same predicate; in each slot replace the single `QVERIFY2(findAutomatableParameter(…))`
with a value-held form:

```cpp
		const bool haveParameter = findAutomatableParameter(registry, &trackId, &paramId, &minValue, &maxValue);
		if (!haveParameter && !testHostCanLoadPluginModules()) { QSKIP("<the item-1 sentence>"); }
		QVERIFY2(haveParameter, "this song exposes no automatable device parameter, …");
```

The failure message is preserved verbatim, so the non-Windows behaviour is unchanged. 346 lines now.

### Item 3 — `MpePlaybackTest` — **analysed, fix drafted, NOT written**

`initTestCase()` fails at `tests/src/core/MpePlaybackTest.cpp:162`
(`'info.descriptor != nullptr' returned FALSE`) after
`plugin-scan: 1 file(s), …, 1 scanned, 0 plugin(s)`: the fixture module
`build/tests/mpe-test-consumer/mpe_test_consumer.dll` is built by
`tests/CMakeLists.txt:635-661` linking **`zene`** — "so it links the same zene target every real plugin
links" — which is exactly the `zene.exe` import descriptor a Windows test host cannot resolve.

**Fix (drafted).** Skip the suite in `initTestCase()` — a skipped `initTestCase` skips every slot (the
tree already relies on this; `AudioPluginTest.cpp:145-158`) — placed at the *top* of `initTestCase()`
before `Engine::init`, and the class already guards `cleanupTestCase()` with `m_engineUp`, which stays
false, so the "a skip that crashed" defect recorded in
`tests/evidence/vst3-platform-fixtures/README.md` §"ZynSeparateProcessTest" does not repeat. Message:
the same mechanism sentence as item 1. 297 lines now.

**Alternative considered and rejected:** linking the fixture to `lmmsobjs` instead of `zene` would make
it loadable in a test host but gives the module its own copy of the engine's globals — a different
product shape and an unverifiable semantic change. Not taken.

### Item 4 — `Vst3ChunkProbeTest` — **fixed and committed (`6c90126de`)**

### Item 5 — `ControlNamedPipeSmoke` — **fixed and committed (`63d9fa970`)**

### Item 6 — mingw64 `104895806930`: "No space left on device" — **analysed, fix drafted, NOT written**

`/bin/x86_64-w64-mingw32-ar: unable to copy file 'CMakeFiles/TakeLaneTest.dir/objects.a'; reason: No
space left on device` (log line 4614) at 64%. The compile side is clean (0 × C4273/C2220; TakeLaneTest
is just where the disk ran out).

**Disk profile of that job** (all from its own log): vcpkg cache **hit** (`should_install_manifest:
NO`, so no vcpkg buildtrees this run); ccache cache **miss** — `Cache not found for input keys:
ccache-mingw-64-…`; the job builds ~2,900 targets including every test binary but has **no test step**
(0 × `Test #` in the log); ~200 test binaries + the product + plugins + `build/vcpkg_installed`
(x64-mingw-static, restored from cache) + the apt mingw/toolchain set all sit on the runner's `/`.

**Two measurable defects found, fix drafted:**

1. `CCACHE_MAXSIZE: 0` in the job env means **no limit** (ccache manual: *"Use 0 for no limit. The
   default value is 5GiB."*) and `CCACHE_NOCOMPRESS: 1`. The previous *successful* mingw run's own
   stats (`/tmp/wplat/mingw_prev.log`, job `104064095838`) show the mechanism: `Cacheable calls: 1618`,
   `Hits: 2`, `Misses: 1616` — i.e. **1,616 objects written to the cache during one build**, on the same
   filesystem as the build tree, then clamped to 500 MB by the final step (`Cache size (GB): 0.5 / 0.5`,
   `Cleanups: 188`).
2. The cache step points at `~/.ccache` while ccache 4.9's default `cache_dir` is `~/.cache/ccache`
   (printed by that same job: `(default) cache_dir = /home/runner/.cache/ccache`), so the cache is
   **never restored and never saved** (`[warning]Path Validation Error: … do(es) not exist, hence no
   cache is being saved`). Every run recompiles everything *and* re-fills a cache nothing reads.

**Fix (drafted, `.github/workflows/build.yml`, mingw job only — no `-Werror` change, no test skipped):**

- a `Free disk space (this job builds a Windows target and runs no tests)` step before Configure that
  removes the pre-installed trees a mingw cross build cannot use (`/usr/share/dotnet`,
  `/usr/local/lib/android`, `/opt/ghc`, `/opt/hostedtoolcache/CodeQL`, `docker image prune -a -f`) and
  prints `df -h /` before and after, so the next log carries the measured number. Deliberately **not**
  touched: `$AGENT_TOOLSDIRECTORY` (Python discovery at Configure) and `/usr/local/share/boost`
  (Configure might find its headers) — both named in the report as the reason for the narrower list.
- `CCACHE_MAXSIZE: 2G` in the mingw job env (was `0`), keeping the job's own post-build 500 M clamp.

**Next action:** apply those two edits, then `bash tests/release-staging-path-gate.sh` (it asserts the
staging shape in this file) and `bash tests/file-length-gate.sh --check`; commit with the two measured
evidence blocks above.

**Options NOT taken (recorded with their trade-offs, for the report):** (a) build the test targets in
the mingw job behind an option — the brief forbids reducing coverage; (b) move the build tree to the
runner's larger `/mnt` volume — bigger workflow surgery than the disk reclamation above, keep as the
next step if the mitigation does not hold; (c) drop `RelWithDebInfo` for the mingw build — it is the
release packaging job's build type.

### Cross-cutting notes for whoever resumes

- **Hotspot / collision:** `tests/data/vst3-chunk-probe/CMakeLists.txt` — my `WIN32` branch is
  committed there and the **macOS platform lane (`030/wplat-mac`, worktree `zene-030/wplat-mac`) owns
  the `APPLE` branch** (it needs `Contents/MacOS/<stem>`, `macmain.cpp` and an `Info.plist`; the worked
  example is `tests/data/vst3-test-effect/CMakeLists.txt`). `Vst3ChunkProbeTest` is red on the macOS
  jobs for the same reason (job `104895806868`, test #152, `Vst3ChunkProbeTest.cpp(132)`), and the
  merge must keep both branches. Other sibling lanes: `030/wplat-lin`, `030/fixup-tests-2`.
- **Hotspot / cap:** `src/core/ControlServerWin32.cpp` is now **499 of the 500-line** file-length cap.
  Any further edit to it must trim lines first or split the file (`ControlServer.cpp`'s split precedent:
  `ControlServerSocket.cpp`).
- The plugin-load mechanism (items 1–3) is **proved by the msvc job's own dumpbin diagnostic step**
  (`.github/workflows/build.yml:1007-1026`): `build\tests\synthetic_audio_plugin.dll` has `zene.exe` in
  its import table and `zene.exe` exports the core symbols — i.e. the module links the executable, so a
  test host that is not `zene.exe` cannot resolve the import. The same mechanism is already documented
  and skipped for five other suites by commit `434f29d98`.
- The named-pipe fix (item 5) rests on the MSDN parameter tables (a flag is only legal in the table it
  is listed in; the remote-client modes are listed under `dwPipeMode`) plus two production callers that
  put `PIPE_REJECT_REMOTE_CLIENTS` in the pipe mode (Qt `qlocalserver_win.cpp`, Rust `std`
  `sys/windows/pipe.rs`). The box's `tools/local-ci.sh` cannot cover any of this: no MSVC, no MinGW.
- No report file was written yet: `docs/reports/WPLAT-WIN-REPORT.md` is the remaining deliverable (the
  per-item root cause / fix / evidence / what the next CI round must prove, per the brief). Everything
  it needs is in this file and in the two commit messages.

---

## RESUME NOTES — PLATFORM-DELTA lane (MACOS), branch `030/wplat-mac`

**Base:** `eba78f8ff` (run 35126160372 head). **Commits on this branch (newest last):**

| # | commit | state |
|---|---|---|
| 1 | `dea96ef65` | ControlMasteringCommands (mac arm64) — **fix committed**, CI-proof required |
| 2 | `e021de5dd` | MmpzGitDepthTest (both mac arches) + the mac backtrace step — **fix committed**, CI-proof required |
| 3 | `c1a2f9dcb` | Vst3ChunkProbeTest (both mac arches + msvc-x64) — **fix committed**, CI-proof required |
| 4 | `85103e377` | SafeStartTest (both mac arches) — **fix committed, NOT compiled** (no Qt5/macOS here), CI-proof required |

Nothing is pushed. `git status` is clean apart from RESUME-NOTES.md itself (this commit).

### Logs pulled (all in `/tmp/wplatmac-logs/` on this box — re-pull with the recipe below if gone)

```
macos-arm64  mac-run-tests.log   job 104895806458   (6 failed / 206)
macos-x86_64 mac-x86.log         job 104895806868   (3 failed / 206)
linux-x86_64 linux-run-tests.log job 104895780 (104895806780)  (5 failed / 209)
win_*.log                        msvc 104895806855, mingw 104895806930, win-arm64 104895806707
recipe: zene-gh-token gh api "repos/KRUZZZZY/zene-studio/actions/jobs/<ID>/logs" --allow-escape-sequences
```

Failed set, macos-arm64: 39 ControlShutdownHookTest, 48 SafeStartTest, 152 Vst3ChunkProbeTest,
186 ControlMasteringCommands, 189 ControlGoldenAudio, 205 MmpzGitDepthTest.
macos-x86_64: only 48, 152, 205. macos jobs have NO ctest-log artifact (only msvc uploads one).

---

### 1 · ControlMasteringCommands (186) — COMMITTED `dea96ef65`

* **Root cause (proven from the log).** The ctest line is `/usr/local/bin/python3` (mac log 7708)
  while the provisioning step's own `python3` was `/opt/homebrew/opt/python@3.14/bin/python3.14`
  (6832): `externally-managed-environment` (6786) → `--break-system-packages` retry → "Successfully
  installed numpy-2.5.3 scipy-1.18.1" (6830) into **Homebrew's** python, so the test died for the
  missing module anyway (7253). macos-x86_64 passed because Homebrew IS `/usr/local` there.
  `tools/auto-mastering-demo.py` is loaded **by path in the test's own interpreter**
  (`tests/mastering_probe_lib.py:shipped_fixture()`), so only that interpreter matters.
* **Fix.** The mac step reads `PYTHON3_EXECUTABLE` out of `build/CMakeCache.txt` (the exact value
  `find_program()` at `tests/CMakeLists.txt:1896` feeds every python ctest), provisions it, keeps the
  PEP 668 retry, fails loudly if the cache names no usable interpreter.
* **Proven here:** YAML OK + yamllint EXIT=0; cache extraction against a fixture cache; guard fires
  on an empty value. **Next action:** confirm the mac job's new line prints
  `/usr/local/bin/python3 2.5.3 1.18.1` and 186 is green.

### 2 · MmpzGitDepthTest (205) + the mac backtrace step — COMMITTED `e021de5dd`

* **Root cause (one, two faces).** `timeout` is GNU coreutils, absent on macOS:
  `FileNotFoundError: [Errno 2] No such file or directory: 'timeout'` from
  `test_mmpz_git.py:482 _loads()` (2 errors, mac log 8010-8035), and the same absence makes the
  "Signal-death backtrace" step a no-op on mac (mac log 8067: `line 19: timeout: command not found`)
  — which is why no frame exists for any mac red in this run.
* **Fix.** `_loads()` bounds the DAW call with `subprocess.run(..., timeout=180)` +
  `TimeoutExpired` → an explicit hang failure; the step resolves `timeout`/`gtimeout`/neither once and
  uses it (all three job copies stay byte-identical). File kept at its ratchet value: 966 lines.
* **Proven here:** `BinarySafety` → OK 2 tests EXIT=0 against a stand-in `build/zene`; hang path with
  the bound temporarily 1 s → `AssertionError: ... the DAW hung loading ... (180 s)`, EXIT=1 in
  1.086 s; the three timeout-resolution branches run rc=0; YAML/yamllint/bash -n OK.
* **Next action:** both mac arches 205 green; the mac backtrace step must print lldb output.

### 3 · Vst3ChunkProbeTest (152) — COMMITTED `c1a2f9dcb`

* **Root cause (proven).** The fixture hardcoded the Linux bundle layout and entry point on every
  platform: mac log 7181 "The bundle "vst3-chunk-probe.vst3" couldn't be loaded because its executable
  couldn't be located." (CFBundle) with `linuxmain.cpp.o` in the build log (2618), and msvc
  `LoadLibraryW failed for ...\Contents\x86_64-win\vst3-chunk-probe.vst3` (win_104895806855.log:5320).
  `vst3-test-effect` (which passes on mac) already carries the correct per-platform contract.
* **Fix.** Per-platform subdir/file/entry-main (+ `Info.plist.in` for APPLE, + two configure-time
  contract assertions), mirroring `tests/data/vst3-test-effect/CMakeLists.txt`.
* **Proven here:** three configures against a stubbed SDK (linux / `-DAPPLE=TRUE` / `-DWIN32=TRUE`),
  all EXIT=0; the *generated* `build.make` rules read back: `Contents/MacOS/vst3-chunk-probe` +
  `Contents/Info.plist` + `macmain.cpp`, `Contents/x86_64-win/vst3-chunk-probe.vst3` + `dllmain.cpp`,
  `Contents/x86_64-linux/…so` + `linuxmain.cpp`; generated plist `CFBundleExecutable =
  vst3-chunk-probe`.
* **Next action:** 152 green on both mac arches (and msvc-x64, same defect, same fix).

### 4 · SafeStartTest (48) — COMMITTED `85103e377` (NOT compiled here: no macOS, no Qt5 headers)

* **Mac evidence (no diagnosis in the log).** Both slots failed on BOTH mac arches, silently, with no
  QFATAL/child output/crash report, and the SIGKILL slot — whose child kills itself with an
  uncatchable signal — failing means the child never reached that line as intended. In isolation the
  same binary passes (mac `ctest --rerun-failed -V`: "Totals: 6 passed" — that was
  ControlShutdownHookTest; for SafeStart the -V rerun fails both slots identically).
* **Measured mechanism on linux-x86_64 (provable):** that child ran qtestlib's inherited
  `FatalSignalHandler` (`=== Received signal at function time: 3ms ...` + `QFATAL : ... Received
  signal 11`, linux log 10208/10226) and died by the SIGABRT `qFatal()` raises — not SIGSEGV.
* **Fix (assertions unchanged):** the SIGSEGV child resets the fatal signals to `SIG_DFL` before its
  fault; `waitBounded()` continues a merely-STOPPED child instead of reading it as a death; both
  failure messages decode the wait status in words. File is exactly 500 lines (whole-tree limit) —
  the long reasoning is in the commit message and in the not-yet-written report.
* **Next action / the honest gap:** if macOS's mechanism is a third one (e.g. the child dying inside
  `install()/beginSession()`), the next mac run's message will name it — then fix that, do not widen
  the assertion. Also worth noting: `CrashReporterTest` (47) passes on mac because its child *arms the
  product handler*, which re-raises with SIG_DFL — the same shape this fix gives the SafeStart child.

### 5 · ControlGoldenAudio (189) — ANALYSED, NO FIX YET (mac arm64 only)

* Not a numpy failure (that message belongs to 186) and not a fixture defect:
  `peak envelope delta 1748.015 LSB  limit 1  FAIL` while `frames`, `window count`, `envelope delta
  0.000049 dB` and `level delta +0.000011 dB` are all ok (mac log 7745-7759). Passes on
  macos-x86_64 (44.73 s) and on both linux jobs.
* Reading of the term (`tests/golden_audio_record.py:compare_fingerprints`): the per-window PEAK term
  is compared in LSB and is **not** skipped for windows that are digital silence in the record (only
  the -inf dBFS envelope term is), so 1748 LSB = 0.0533 in normalised amplitude is consistent with a
  small (~-25 dBFS) blip in one window the record has at `0.000000` — one window of the 103.
* **Next action (drafted, not implemented):** in `golden_audio_record.py:compare_fingerprints`
  (359 lines, fork scope, CCN 17 grandfathered — add no branch) print the worst peak window's index,
  the record's value and the measured value, so the next mac run names the window and the direction.
  Then re-run mac arm64 and decide: real arm64 render delta (a product defect) vs a re-record.
  **Do not touch `tests/control-golden-audio.py`** — it is exactly 500 lines.

### 6 · ControlShutdownHookTest (39) — MAC EVIDENCE ONLY, DELIBERATELY NOT TOUCHED

* Mac evidence: `FAIL! : ...aHookSurvivesTheRegistryInstanceItWasRegisteredOn() 'recreated != registry'
  returned FALSE. (destroy() did not build a new instance for instance())` at
  `tests/src/core/ControlShutdownHookTest.cpp:86` in the -j3 run; the isolated `--rerun-failed -V`
  passes (6/6). Identical failure on linux-x86_64 (both the -j3 run and its rerun).
* Root cause: `ControlRegistry::destroy()` deletes and nulls the singleton
  (`src/core/ControlRegistry.cpp:141-145`) and the re-created object *may* land on the same address, so
  comparing raw pointers is unsound on any allocator. Shared with the linux instance (a sibling lane is
  on those), so per the dispatch this file was left untouched to avoid a duplicate edit on the same
  line — **the parent resolves it.** A sound re-derivation: assert the re-created instance's own
  observable state (the surviving hook runs: `ran == 1`, count drains to 0 — already asserted) and drop
  the pointer comparison, or compare a monotonic instance serial if one is ever added (note:
  `include/ControlRegistry.h` is AT the 500-line cap, so a member cannot be added there).

### Gates (run after each commit, unpiped)

```
bash tests/complexity-gate.sh --check                 -> PASS (no regressions)
bash tests/file-length-gate.sh --check                -> PASS (no regressions)
bash tests/file-length-gate.sh --check --scope all    -> FAIL, but NOT on this lane's files:
     pre-existing from other lanes (ScriptBindingsTest 741->752, ScriptEngineTest 627->651,
     ClapHostTest 705 new). SafeStartTest.cpp is 500 lines and is no longer flagged.
```

### Still to do (in order)

1. Write `docs/reports/WPLAT-MAC-REPORT.md` (per item: root cause, fix, evidence, residuals) — the
   whole reasoning above is the draft; commit it.
2. Item 5's window-level diagnostic (small, additive, no new branch), then ask for one mac arm64 run.
3. Re-run the namespace + manifest gates (`fork-sources-gate.sh`, `no-upstream-regression-gate.sh`,
   `all-sources-reproduce.sh`, `unregistered-tests-gate.sh`) on the finished tree.
4. `hotspot:` `tests/src/core/SafeStartTest.cpp` and `tests/src/core/ControlShutdownHookTest.cpp` —
   both are shared with the linux-instance lane; expect merge work in exactly those two files.
