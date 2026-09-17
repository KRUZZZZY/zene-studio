# WPLAT-WIN — the Windows platform lane: what msvc-x64 and mingw64 proved, and the six fixes it left

**Lane:** the Windows platform delta. **Branch:** `030/wplat-win`, worktree
`projects/lmms-fl-research/zene-030/wplat-win` of `lmms/.git`. **Base:** `eba78f8ff` (the merged
`release/0.3.0` tip). **Tip when this report was written:** `d6ed6d2bc`. **Date:** 2026-09-17.

Nothing was pushed and nothing was merged: `origin` is LMMS/lmms upstream and `product` is the
fork, and this lane pushes to neither. The branch carries six item commits on top of the base —
two (`6c90126de`, `63d9fa970`) from the first session, four (`0637e3fa9`, `e273b5ebc`, `f8b1e2698`,
`d6ed6d2bc`) from the session that wrote this report, one per logical unit:

| item | commit | what it changes |
|---|---|---|
| 1 | `0637e3fa9` | `ControlAutomationScriptTest` — the two slots whose device parameter cannot exist in a Windows test host skip, data-driven |
| 2 | `e273b5ebc` | `ControlAutomationModesTest` — the same, for its two mode slots |
| 3 | `f8b1e2698` | `MpePlaybackTest` — the suite skips in `initTestCase()`, where the fixture module cannot be loaded |
| 4 | `6c90126de` | `Vst3ChunkProbeTest` — the chunk-probe fixture lays its bundle out as the Windows loader opens it |
| 5 | `63d9fa970` | `ControlNamedPipeSmoke` — the Windows named-pipe instance takes `PIPE_REJECT_REMOTE_CLIENTS` in the **pipe mode** |
| 6 | `d6ed6d2bc` | mingw64 — a Free-disk-space step in the mingw job and `CCACHE_MAXSIZE: 2G` |

---

## The run under work

`run 35126160372`, branch `release/0.3.0`, head `eba78f8ff`. Two of its jobs are this lane's
subject; both logs were re-fetched for this report with
(`zene-gh-token gh api "repos/KRUZZZZY/zene-studio/actions/jobs/<id>/logs" --allow-escape-sequences`
(msvc-x64 `104895806855`, mingw64 `104895806930`), and every number below is read out of them. The
run's own checkout commands name the ref it built: `git rev-parse
refs/remotes/origin/release/0.3.0` → `eba78f8ff91cc358b708c18505635a5115f16d97`.

**msvc-x64 `104895806855`** — "Run tests" printed

```
97% tests passed, 5 tests failed out of 161
Total Test time (real) = 146.67 sec

The following tests did not run:
 	161 - ControlMcpGroupCoverage (Skipped)

The following tests FAILED:
 	 21 - ControlAutomationScriptTest (Failed)
 	 22 - ControlAutomationModesTest (Failed)
 	 73 - MpePlaybackTest (Failed)
 	152 - Vst3ChunkProbeTest (Failed)
 	157 - ControlNamedPipeSmoke (Failed)
```

This is the first run that exercised the Windows halves of these features at all: the fork's
control surface, the MPE expression path and the VST3 host had never been through a Windows
build, and three of the five reds turned out to be one mechanism.

**mingw64 `104895806930`** — died at `[ 64%]` inside Build:

```
[ 64%] Linking CXX executable TakeLaneTest.exe
/bin/x86_64-w64-mingw32-ar: unable to copy file 'CMakeFiles/TakeLaneTest.dir/objects.a'; reason: No space left on device
```

(log line 4614; `gmake[2]: *** [... tests/TakeLaneTest.exe] Error 1` follows). Nothing before it
reported a defect — that log carries **0 `warning:` and 0 `error:` lines** — so TakeLaneTest is
only where the runner's `/` filled up.

---

## The one mechanism behind three of the five reds

On Windows a **test host cannot load a plugin MODULE library**. Every plugin module links the
product executable, so an MSVC module's import descriptor names `zene.exe`, and
`QLibrary::load` from a host that is not `zene.exe` fails with `ERROR_MOD_NOT_FOUND` (126).

This is not an inference: the msvc job carries a diagnostic step that answers exactly this
question (`build.yml`, the `dumpbin` block of the "Import tables" step, lines 1041-1057 at this
tip), and the run's own output is

```
echo === synthetic_audio_plugin.dll: import tables ===
dumpbin /imports build\tests\synthetic_audio_plugin.dll | findstr /i "zene.exe"
echo === migrated plugin module (same question) ===
dumpbin /imports build\plugins\amplifier.dll | findstr /i "zene.exe"
echo === does the executable export the core symbols? ===
dumpbin /exports build\zene.exe | findstr /i "AudioPortsModel"

Dump of file build\tests\synthetic_audio_plugin.dll
File Type: DLL
  Image has the following dependencies:
    zene.exe
    Qt6Core.dll
    MSVCP140.dll
    VCRUNTIME140.dll
    ...
=== migrated plugin module (same question) ===
    zene.exe
=== does the executable export the core symbols? ===
  30   1D 0003198F ??0AudioPortsModel@lmms@@QEAA@GG_NPEAVModel@1@@Z = @ILT+199050(…)
  329  148 00032CA9 ??1AudioPortsModel@lmms@@UEAA@XZ = @ILT+203940(…)
  623  26E 00B96B68 ??_7AudioPortsModel@lmms@@6BModel@1@@ = … (const lmms::AudioPortsModel::`vftable')
```

(The full export dump is long; those are three of its `AudioPortsModel` symbols, quoted verbatim.)

Five suites already handle it with a file-local predicate and a skip — commit `434f29d98`
("test: skip the plugin-module suites on Windows with the named mechanism"), in
`AudioPluginTest.cpp`, `PluginPortsMigrationTest.cpp`, `ScriptEngineTest.cpp`,
`PhaseDSidechainTest.cpp` and `PluginScanCacheTest.cpp`. Items 1-3 are the same class, applied
to the three suites that were still red, and they keep that convention: one file-local
`constexpr auto testHostCanLoadPluginModules() -> bool` returning `false` under `#ifdef Q_OS_WIN`,
with the mechanism and the CI evidence in its doc comment.

The driver of the failure in all three suites is visible in the logs as one line:

```
QINFO  : ControlAutomationScriptTest::initTestCase() plugin-scan: 76 file(s), 0 quarantined,
         0 served from cache, 0 known-bad skipped, 76 scanned, 0 plugin(s)
```

76 module files scanned, 0 loaded. The song then has its track but no device on it, so it
exposes no automatable parameter — on Linux the same test reports **27** parameters for that
track (the file's own comment). Nothing to drive is a test-host limitation, not a product defect:
the product loads its modules inside `zene.exe`, where the import resolves by construction.

### What items 1-3 deliberately do *not* do

The guards are **data-driven on the reported state, not on the platform**: the skip fires only
when the parameter (item 1), the finder's return value (item 2) or the fixture discovery (item 3)
has genuinely failed. A future Windows that *can* load modules therefore runs the slots instead
of skipping them, and Linux keeps failing loudly, because on Linux the state the guard tests for
never arises. In `ControlAutomationModesTest` and `MpePlaybackTest` the pre-existing failure
messages/QVERIFY2s are preserved verbatim, so a Linux run's text is byte-identical to before.

---

## Item 1 — `ControlAutomationScriptTest` (10 passed / 2 failed) — `0637e3fa9`

**Root cause.** `initTestCase()` points `LMMS_PLUGIN_DIR` at the build tree's modules,
`Engine::init(true)` runs, and then `revtest::addInstrumentTrack()`
(`tests/src/core/ReversibilityTestSupport.h:98`) returns empty because
`firstLoadable("instrument")` is empty — the modules could not be loaded. The two failing slots
then looked for a track with parameters and found none:

```
FAIL!  : ControlAutomationScriptTest::automationModeSetSucceedsOnABareParameter()
         '!trackId.isEmpty()' returned FALSE. (no track with parameters in the fresh song)
FAIL!  : ControlAutomationScriptTest::automationRecordModeSetRequiresAClip()
         '!trackId.isEmpty()' returned FALSE. (no track with parameters in the fresh song)
Totals: 10 passed, 2 failed, 0 skipped, 0 blacklisted, 1913ms
```

**Fix.** The file-local predicate in an anonymous namespace, plus a guard immediately before each
of the two `QVERIFY2(!trackId.isEmpty(), …)`s:

```cpp
if (trackId.isEmpty() && !testHostCanLoadPluginModules())
{
    QSKIP("the song exposes no automatable device parameter: plugin modules link the zene "
        "executable, so on Windows their import descriptor names zene.exe and a test host "
        "cannot satisfy it; the product loads them inside zene.exe where that resolves by "
        "construction (CI msvc-x64: QLibrary::load -> ERROR_MOD_NOT_FOUND, 126)");
}
```

**What the next CI round must prove.** Both slots report as **SKIP** (not PASS) on msvc-x64 while
the other 10 slots still pass, and all 12 still *run* on Linux.

**Residual.** The file is now **499 of the 500-line file-length cap**. Any further edit to it must
free lines first or the ratchet fails.

## Item 2 — `ControlAutomationModesTest` (2 passed / 2 failed) — `e273b5ebc`

**Root cause.** The same module load, one step later: the slots call the file's own
`findAutomatableParameter(...)`, which needs a device on the track, and the test says so itself:

```
FAIL!  : ControlAutomationModesTest::modeSetIsObservableForEveryMode()
         'findAutomatableParameter(registry, &trackId, &paramId, &minValue, &maxValue)'
         returned FALSE. (this song exposes no automatable device parameter, so the mode
         commands have nothing to drive: the binary needs the instrument modules
         (LMMS_TEST_PLUGIN_DIR, see tests/CMakeLists.txt))
FAIL!  : ControlAutomationModesTest::readRideThroughTheSocketCannotTouchTheRecordedAutomation()
         the same.
Totals: 2 passed, 2 failed, 0 skipped, 0 blacklisted, 1534ms
```

**Fix.** The same predicate and guard shape as item 1, with the finder's result held in a value so
the skip can only fire when it really is false:

```cpp
const bool haveParameter = findAutomatableParameter(registry, &trackId, &paramId, &minValue, &maxValue);
if (!haveParameter && !testHostCanLoadPluginModules()) { QSKIP( …item 1's message… ); }
QVERIFY2(haveParameter, "this song exposes no automatable device parameter, …");   // message unchanged
```

**What the next CI round must prove.** Both slots SKIP on msvc-x64, the file's other two slots
(`initTestCase` and `cleanupTestCase`) still pass, and nothing skips on Linux.

## Item 3 — `MpePlaybackTest` — `f8b1e2698`

**Root cause.** The fixture module *is* the suite's subject, and Windows cannot load it:

```
FAIL!  : MpePlaybackTest::initTestCase() 'info.descriptor != nullptr' returned FALSE.
         (the MPE test consumer fixture was not discovered in
          D:/a/zene-studio/zene-studio/build/tests/mpe-test-consumer:
          Cannot load library …\mpe_test_consumer.dll: The specified module could not be found.;
          scan: plugin-scan: 1 file(s), …, 1 scanned, 0 plugin(s))
Totals: 1 passed, 1 failed, 0 skipped, 0 blacklisted, 1262ms
```

`tests/CMakeLists.txt:635-661` builds that fixture and links **`zene`** on purpose — "so it links
the same zene target every real plugin links" — which is exactly the `zene.exe` import descriptor
of the mechanism above.

**Fix.** The skip goes at the **top of `initTestCase()`**, before the engine starts, which is the
shape `AudioPluginTest.cpp` already uses: a skipped `initTestCase` skips every slot. The class's
`cleanupTestCase()` is guarded by `m_engineUp`, which this path never sets, so the "a skip that
crashed" defect recorded in `tests/evidence/vst3-platform-fixtures/README.md`
(§"ZynSeparateProcessTest") does not repeat here.

**One deliberate deviation from the draft.** The QSKIP message keeps the mechanism sentence of
items 1-2 byte-identical but replaces its lead-in ("the song exposes no automatable device
parameter"), which is not this suite's failure, with the one this suite actually reports ("the MPE
test consumer fixture cannot be loaded in a Windows test host"). A verbatim copy would have made
the skip say something the log contradicts.

**Alternative considered and rejected.** Linking the fixture to `lmmsobjs` instead of `zene`
would make it loadable in a test host but gives the module its own copy of the engine's globals —
a different product shape and an unverifiable semantic change, in a lane with no Windows
toolchain to measure it with.

**What the next CI round must prove.** `MpePlaybackTest` reports as SKIP on msvc-x64 (one skipped
`initTestCase`, no slot failures) and still runs green on Linux.

## Items 4 and 5 — already committed before this session

**4 · `Vst3ChunkProbeTest` (`6c90126de`).** The fixture laid its bundle out at
`Contents/<uname -m>-linux/<name>.so` on every platform, because both the bundle path and the
entry-point translation unit were hardcoded to the Linux layout, so the Windows loader looked for
`vst3-chunk-probe.vst3\Contents\x86_64-win\vst3-chunk-probe.vst3` and found nothing:

```
FAIL!  : lmms::vst3::Vst3ChunkProbeTest::initTestCase() 'm_plugin.load(...)' returned FALSE.
         (LoadLibraryW failed for path …\Contents\x86_64-win\vst3-chunk-probe.vst3:
          The specified module could not be found.)
```

The fix is the `WIN32` branch `tests/data/vst3-test-effect/CMakeLists.txt` already carries (the
arch directory and the file name are the SDK loader's own), plus that fixture's two configure-time
guards. **macOS is not changed** and the chunk-probe fixture is still laid out in the `-linux`
location there: it needs `Contents/MacOS/<stem>`, `macmain.cpp` and an `Info.plist` naming the
stem, and that is the macOS lane's file.

**5 · `ControlNamedPipeSmoke` (`63d9fa970`).** The Windows control transport failed at its first
pipe instance with `Windows error 87` (ERROR_INVALID_PARAMETER): `PIPE_REJECT_REMOTE_CLIENTS`
(0x8) was OR'ed into `dwOpenMode`, and `CreateNamedPipe` accepts that flag only in `dwPipeMode`
(its bit space there is already taken by `PIPE_NOWAIT` 0x1 / `PIPE_READMODE_MESSAGE` 0x2 /
`PIPE_TYPE_MESSAGE` 0x4). The flag moved into the pipe-mode argument — same flag, same
"local only" property — which is how Qt's `QLocalServer` and Rust's `std` name that pipe too. The
same commit reads `GetLastError()` into a local before the `qWarning`'s two Qt string conversions,
because the order function arguments are evaluated in is the compiler's.

## Item 6 — mingw64: "No space left on device" — `d6ed6d2bc`

**The disk profile of that job**, all from its own log: the vcpkg cache was a **HIT**
(`should_install_manifest: NO`, so no vcpkg buildtrees this run); the ccache cache was a **MISS**
(`Cache not found for input keys: ccache-mingw-64-refs/heads/release/0.3.0-35126160372, …`); the
job compiles its way through the same tree the msvc job builds as 2,888 targets (**1,034
`Building CXX object` lines by the 64% mark**), ~200 of them test binaries it **never runs** —
the job has no test step (`0 × "Test #"`, `0 × ctest` in its log) — on top of the restored
`build/vcpkg_installed` (`x64-mingw-static`) and the apt mingw toolchain, all on the runner's `/`.

**Two measurable defects:**

1. `CCACHE_MAXSIZE: 0` in the job's env means **no limit** (ccache manual: *"Use 0 for no limit.
   The default value is 5GiB."*), with `CCACHE_NOCOMPRESS: 1` on top. The previous *successful*
   mingw run (job `104064095838`) shows the mechanism in its own stats: `Cacheable calls: 1618`,
   `Hits: 2`, `Misses: 1616` — **1,616 objects written into the cache during one build**, on the
   same filesystem as the build tree — and then clamped to 500 MB by the final step
   (`Cache size (GB): 0.5 / 0.5`, `Cleanups: 188`).
2. The "Cache ccache data" step points at `~/.ccache` while ccache 4.9's default `cache_dir` is
   `~/.cache/ccache` (printed by that same job: `(default) cache_dir =
   /home/runner/.cache/ccache`), so the cache is **never restored and never saved**
   (`[warning]Path Validation Error: Path(s) specified in the action for caching do(es) not
   exist, hence no cache is being saved`). Every run recompiles everything *and* re-fills a cache
   nothing reads.

**Fix (mingw job only — no `-Werror` change, no test skipped, no target dropped):**

- a `Free disk space (this job builds a Windows target and runs no tests)` step **before
  Configure** that removes the pre-installed trees a mingw cross build cannot use
  (`/usr/share/dotnet`, `/usr/local/lib/android`, `/opt/ghc`, `/opt/hostedtoolcache/CodeQL`,
  `docker image prune -a -f`) and prints `df -h /` before and after, so the next log carries the
  measured headroom instead of an inference from the failure. Deliberately **not** touched:
  `$AGENT_TOOLSDIRECTORY` (Configure runs the Python discovery through it) and
  `/usr/local/share/boost` (Configure might find its headers) — both named here as the reason the
  list is narrower than the stock free-disk-space action.
- `CCACHE_MAXSIZE: 2G` in the job env (was `0`), keeping the job's own post-build 500 M clamp.

**What the next CI round must prove.** Build reaches 100% and `Package` produces `zene-*.exe`; the
new step's `df -h /` lines show the headroom it bought; the ccache cache stays ≤ 2G during the
build.

**Defect 2 is recorded, not fixed** — correcting the cache path changes caching behaviour, which
deserves its own evidence round. It is the next lever if this mitigation does not hold.

**Options NOT taken for the disk, with their trade-offs:**

| option | why not |
|---|---|
| build the test targets in the mingw job behind an option | the brief forbids reducing coverage |
| move the build tree to the runner's larger `/mnt` volume | bigger workflow surgery than the reclamation above; keep as the next step if the mitigation does not hold |
| drop `RelWithDebInfo` for the mingw build | it is the release packaging job's own build type |

---

## What was measured on this box, and what could not be

This machine has **no MSVC and no MinGW**, so everything that needs a Windows toolchain — the
loader behaviour, the skips firing, the disk headroom — is CI-only and stated as such here. What
*could* be run was run, unpiped, from the worktree root unless a gate says otherwise:

| check | exit |
|---|---|
| `bash tests/file-length-gate.sh --check` (+ `--scope tools`) | 0 |
| `bash tests/complexity-gate.sh --check` (+ `--scope tools`) | 0 |
| `bash tests/fork-sources-gate.sh` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | 0 |
| `bash tests/all-sources-reproduce.sh` | 0 |
| `bash tests/duplication-gate.sh` (+ `--scope tools`) | 0 |
| `bash tests/no-tautology-gate.sh` | 0 |
| `bash tests/evidence-gate.sh` (+ `--self-test`) | 0 |
| `python3 tests/rt-safety-sweep.py --check` | 0 |
| `bash tests/unregistered-tests-gate.sh` | 0 |
| `bash tests/prove-posix-unchanged.sh` | 0 |
| `bash tests/release-staging-path-gate.sh` | 0 |
| `bash tests/test-package-upload-guard.sh` | 0 |
| `yamllint` over every git-tracked `*.yml` | 0 |
| `tests/scripted/check-namespace` | 0 (0 errors) |
| `tests/scripted/check-strings` | 0 (0 errors), after the submodules were initialized |
| `tests/scripted/verify` | 0 |

`tests/scripted/check-strings` exits **1** in a worktree whose submodules are uninitialized: it
dies in a `subprocess.run` on `plugins/CarlaBase/carla//.git`, before it reads a single source
file. Reproduced byte-identically at the base commit `6fa6ad6a7` in a scratch worktree. With the
submodules initialized — which the local build below did, and which CI's `scripted-checks` job
does before running it — it exits **0** with **0 errors** (re-run after the build, same tip).

### A local Linux build was possible after all — and it was run

No Windows toolchain exists here, but the *other* half of the contract can be measured: `#ifdef
Q_OS_WIN` returning `true` on this platform means the three edited suites must run their slots
here, unskipped. So the tree was built and the suites were run from `build/tests`:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DWANT_QT6=ON     CONFIGURE_EXIT=0
cmake --build build -j 20                                      BUILD_EXIT=0   (0 warning: lines)
  61 plugin modules in build/plugins
QT_QPA_PLATFORM=offscreen ctest -R "ControlAutomationScriptTest|ControlAutomationModesTest|MpePlaybackTest" -V
  Test #21 ControlAutomationScriptTest ......   Passed    2.05 sec   Totals: 12 passed, 0 failed, 0 skipped
  Test #22 ControlAutomationModesTest .......   Passed    1.97 sec   Totals:  4 passed, 0 failed, 0 skipped
  Test #73 MpePlaybackTest ..................   Passed    1.77 sec   Totals:  4 passed, 0 failed, 0 skipped
  100% tests passed, 0 tests failed out of 3                     CTEST_EXIT=0
QT_QPA_PLATFORM=offscreen ctest -j4 --output-on-failure
  100% tests passed, 0 tests failed out of 198 (316.77 sec)      CTEST_ALL_EXIT=0
```

Two things that run is worth more than a green tick for:

1. **The guards are data-driven, demonstrated, not asserted.** An earlier run of the same
   binaries against a *partial* build — only the three test targets, so `build/plugins` held 0
   modules and the scan reported `plugin-scan: 0 file(s), …, 0 plugin(s)` — failed on Linux
   **without skipping**, with the original messages:
   `'!trackId.isEmpty()' returned FALSE. (no track with parameters in the fresh song)` and
   `'haveParameter' returned FALSE. (this song exposes no automatable device parameter, …)`.
   That is the property the brief asked for: an absent parameter on a platform that *can* load
   modules still fails loudly, and only the genuine Windows limitation skips.
2. **The messages did not drift.** The `ControlAutomationModesTest` failure text in that partial
   build is byte-identical to the pre-change CI text, `haveParameter` being the only difference —
   which is the value-held form the draft specified.

What this still does not prove: the `#ifdef Q_OS_WIN` branch itself, and every Windows-side
behaviour named per item above. Those stay CI-only.

`tests/complexity-gate.sh` (ratchet mode) *rewrites* `tests/complexity-baseline.tsv` as a side
effect even when nothing regressed; the write was reverted (`git checkout --`) and the read-only
`--check` mode used instead. Anyone running that gate here should use `--check`.

**Beware the top of the cap.** Three files this lane touched are close to the 500-line limit:
`tests/src/core/ControlAutomationScriptTest.cpp` is at **499**, and `src/core/ControlServerWin32.cpp`
(item 5's file) is at **499**. A further edit to either must free lines first or split the file.

## Collisions with the sibling lanes

- `.github/workflows/build.yml` — this lane edited the **mingw** job's env and step list only. The
  macOS platform lane (`030/wplat-mac`) edits the same file in the **macOS** job's steps; the
  merge must keep both. A merge must not renumber away the diagnostic step the msvc job carries
  (`build.yml`, `dumpbin` block, lines 1041-1057 at this tip).
- `tests/data/vst3-chunk-probe/CMakeLists.txt` — this lane owns its **`WIN32`** branch; the macOS
  lane owns the **`APPLE`** branch (`Contents/MacOS/<stem>`, `macmain.cpp`, an `Info.plist`).
  Both must survive the merge. `Vst3ChunkProbeTest` is red on the macOS jobs for exactly that
  reason (job `104895806868`, test #152).
- Other sibling lanes: `030/wplat-lin`, `030/fixup-tests-2`.

## Next actions

1. Push the branch into the next merge train and re-run `run`'s two jobs; the six items above each
   name what their job must prove.
2. If mingw64 still exhausts the disk, take defect 2 first (the ccache path — measured, cheap) and
   only then move the build tree to `/mnt`.
3. Re-place the three test suites' skips if a Windows test host ever gains the ability to load
   module libraries: the guards are data-driven, so that future removes the skips by simply
   passing.
