# RESUME NOTES — PLATFORM-DELTA lane (WINDOWS), branch `030/wplat-win`

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

## The run under work — everything below is from it

`run 35126160372` (branch `release/0.3.0`, head `eba78f8ff`). My jobs:
**msvc-x64 `104895806855`** (step "Run tests": 156/161, 5 red) and **mingw64 `104895806930`** (step
"Build": died at 64%). Pulled with
`zene-gh-token gh api "repos/KRUZZZZY/zene-studio/actions/jobs/<id>/logs" --allow-escape-sequences`;
locals: `/tmp/wplat/msvc.log`, `/tmp/wplat/mingw.log` (tmp — gone after a reboot; refetch with the
same recipe).

The five msvc reds: `ControlAutomationScriptTest`, `ControlAutomationModesTest`, `MpePlaybackTest`,
`Vst3ChunkProbeTest`, `ControlNamedPipeSmoke`.

---

## Item 1 — `ControlAutomationScriptTest` (10 passed / 2 failed) — **analysed, fix drafted, NOT written**

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

## Item 2 — `ControlAutomationModesTest` (2 passed / 2 failed) — **analysed, fix drafted, NOT written**

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

## Item 3 — `MpePlaybackTest` — **analysed, fix drafted, NOT written**

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

## Item 4 — `Vst3ChunkProbeTest` — **fixed and committed (`6c90126de`)**

## Item 5 — `ControlNamedPipeSmoke` — **fixed and committed (`63d9fa970`)**

## Item 6 — mingw64 `104895806930`: "No space left on device" — **analysed, fix drafted, NOT written**

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

## Cross-cutting notes for whoever resumes

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
