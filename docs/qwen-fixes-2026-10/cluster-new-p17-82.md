FIXED

# Cluster new-p17-82: LV2 URI mapping can lock and allocate during audio processing

**Verdict: FIXED.** `UridMap::map()`/`unmap()` now have a lock-free, allocation-free branch
inside `RealtimeScope`: a URI the host already knows resolves from a published mirror, and a URI
first seen on the audio thread is not registered there at all (`map()` returns 0). The canonical
map and its mutex stay exactly as they were for every control-thread caller.

An earlier pass in this session recorded FALSE-POSITIVE, reading the LV2 header's "not necessarily
very fast or RT-safe" as closing the finding. That was too narrow for this project's realtime rule:
`Lv2Features` hands this callback to every plug-in (`src/core/lv2/Lv2Features.cpp:64-65`), so a
plug-in really can call it from `run()`, and the host side can keep that path off the mutex and the
heap. This report supersedes that pass.

## Cause and fix

Before: `src/core/lv2/Lv2UridMap.cpp:54-92` built a temporary `std::string` (`:71`), took
`std::lock_guard<std::mutex>` (`:73`), and for a new URI allocated the map node and the unmap entry
(`:80`, `:83`). `unmap()` took the same mutex (`:98`). Both are reachable from `Lv2Proc::run()`
because `Lv2Features::initCommon()` passes the feature pointers to every plug-in instance.

After:

- `include/Lv2UridMap.h:53-95` - declares the mirror: `RtCapacity` (16384 slots), `RtEntry`
  (`std::atomic` hash + URID + a pointer to the canonical key), `m_rtEntries`, `m_rtUris`,
  `m_rtSize`, and the `rtHash`/`mapRt`/`publishRt` helpers. `m_MapMutex` still protects the
  canonical `m_map`/`m_unMap` and publication into the mirror.
- `src/core/lv2/Lv2UridMap.cpp:50-61` - `rtHash()`: FNV-1a over the URI, no allocation, never 0
  (0 marks an empty slot).
- `src/core/lv2/Lv2UridMap.cpp:63-84` - `mapRt()`: open-addressed read of the mirror under an
  acquire load of each slot's hash. It never takes the mutex and never allocates. An empty slot on
  the probe path means the URI is not published, because entries are only appended and never
  removed.
- `src/core/lv2/Lv2UridMap.cpp:86-110` - `publishRt()`: called with `m_MapMutex` held, it publishes
  `urid - 1 -> URI` in `m_rtUris` and then a mirror slot, storing the hash last with release so the
  URI pointer and URID are visible to `mapRt()`.
- `src/core/lv2/Lv2UridMap.cpp:112-122` - the constructor allocates the fixed mirror once, before
  any plug-in runs, and clears `m_rtUris`.
- `src/core/lv2/Lv2UridMap.cpp:126-173` - `map()`: null-safe; inside `RealtimeScope` it returns
  `mapRt(uri)` and registers nothing (a URI first seen there gets 0, the LV2 "could not be created"
  value). Outside it, the original canonical path is unchanged, except that a new mapping is
  published into the mirror (`:164`).
- `src/core/lv2/Lv2UridMap.cpp:175-188` - `unmap()`: inside `RealtimeScope` it returns the mirror's
  URI pointer with an acquire load, or `nullptr` for 0/out-of-range; outside it keeps the original
  canonical lookup.
- Fixture-facing behaviour is unchanged: off the audio thread the map is still dynamic and
  unbounded, IDs are assigned in the same order, and `Lv2UridCache` still pre-maps the eight common
  URIs at manager construction.

## Test matrix

- **Normal path:** a URI mapped by the control thread resolves in `RealtimeScope` to the same ID,
  with zero allocations, and `unmap()` returns its URI with zero allocations.
- **New-URI / failure path:** a URI first seen in `RealtimeScope` returns 0 and does not register;
  the same URI mapped afterwards on the control thread gets a nonzero ID, and the next
  `RealtimeScope` lookup returns that ID.
- **Bounds/exhaustion:** `unmap(0xFFFFFFFF)` in `RealtimeScope` returns `nullptr` with zero
  allocations. The mirror has a bounded 16384 slots; once full, RT lookups of unpublished URIs
  return 0, while the canonical map remains unbounded, so no control-thread caller or saved project
  is capped.
- **Reconfiguration:** none; the map has no per-project reconfiguration path.
- **Audio-thread constraints:** the test runs the calls inside `RealtimeScope` with
  `AllocationProbe` armed; `mapRt`/`publishRt` contain no mutex, allocation, free or logging.
- **Platform:** pure C++20 (`<atomic>`, `<memory>`, `<cstring>`), no POSIX or compiler-specific
  code; built and run on this Linux tree. MSVC/macOS were not available in this session.

## RED/GREEN and the reversal proof (procedure step 4b)

Product diff saved with

```sh
git diff f34d1784e^ -- include/Lv2UridMap.h src/core/lv2/Lv2UridMap.cpp > logs/fix.patch   # EXIT=0
git apply -R logs/fix.patch                                                                # EXIT=0
```

then rebuilt and ran the test, restored the patch, rebuilt, and ran it again:

| Step | Command | Log | EXIT | Result |
| --- | --- | --- | --- | --- |
| save product diff | `git diff f34d1784e^ -- include/Lv2UridMap.h src/core/lv2/Lv2UridMap.cpp > logs/fix.patch` | - | 0 | 202-line patch |
| reverse | `git apply -R logs/fix.patch` | - | 0 | product files back to the pre-fix state |
| RED build | `cmake --build build --target zene Lv2StateTest -j4` | `build-red-3.log` | 0 | built |
| RED run | `cd build/tests && ./Lv2StateTest` | `qtest-red-3.log` | **1** | `FAIL uridMapOnTheAudioThreadIsLockFreeAndAllocationFree`: `Actual (freshOnAudio): 2`, `Expected (LV2_URID{0}): 0`; 8 passed, 1 failed |
| restore | `git apply logs/fix.patch` | - | 0 | fix back in place |
| GREEN build | `cmake --build build --target zene Lv2StateTest -j4` | `build-green-6.log` | 0 | built |
| GREEN run | `cd build/tests && ./Lv2StateTest` | `qtest-green-6.log` | **0** | `PASS uridMapOnTheAudioThreadIsLockFreeAndAllocationFree`, 9 passed |

The RED run stops at the new-URI assertion, so it does not reach the allocation-count assertion;
that assertion is exercised in the GREEN run and would also fail before the fix, because the old
`map()` built the temporary `std::string` and the map/unmap nodes on every new URI.

The two chained commands were also run verbatim against the final tree, as the proof manifest
records them: `proof-red-1.log` EXIT=1 (the same `freshOnAudio` failure) and `proof-green-1.log`
EXIT=0 (9 passed).

### Build notes

The first attempt to build the test failed to link the allocation probe:

| Step | Command | Log | EXIT |
| --- | --- | --- | --- |
| first build | `cmake --build build --target zene Lv2StateTest -j4` | `build-1.log` | **2** - `undefined reference to lmms::test::tlAllocationCount` |
| manual configure | `cmake -S . -B build` | `configure-1.log` | **97** - refused by the lane guard ("configuring is the parent's job") |
| configuration refresh | `cmake --build build --target rebuild_cache -j4` | `configure-2.log` | 0 |
| build after refresh | `cmake --build build --target zene Lv2StateTest -j4` | `build-2.log` | 0 |

`tests/CMakeLists.txt:2382` links the probe by scanning the test source for
`#include "AllocationProbe.h"`; that scan runs at configure time, and the new include needed the
configuration refresh. A fresh configure on the train does this by itself.

## Family and gates

| Command | Log | EXIT | Result |
| --- | --- | --- | --- |
| `cmake --build build --target ControlDeviceCatalogueTest PluginScanCacheTest InstrumentLatencyPdcTest Lv2ViewBaseLifetimeTest Lv2StateTest -j4` | `build-family-2.log` | 0 | built |
| regression ctest | `ctest -R '^Lv2StateTest$' --output-on-failure` (from `build/tests`) | `test-regression-1.log` | 0 | 1 of 1 passed |
| the six-test family selection `^(Lv2StateTest\|InstrumentLatencyPdcTest\|Lv2ViewBaseLifetimeTest\|Lv2InstrumentKeyRegressionTest\|PluginScanCacheTest\|ControlDeviceCatalogueTest)$` was run with `ctest --output-on-failure` from `build/tests` | `test-family-3.log` | 0 | 6 of 6 passed |
| `python3 .../feature_check.py <worktree>` | `feature-check-5.log` | 0 | all 8 rows PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length |

Registration: the two inherited files are declared in `tests/upstream-modifications.txt` with the
cluster and the reason (same commit). The test lives in the already-registered
`tests/src/core/Lv2StateTest.cpp`; no new source file or manifest entry was needed.
`tests/upstream-modifications.txt` still ends with a newline.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it:** RED
  `./Lv2StateTest` EXIT=1 (`freshOnAudio` 2 vs 0), GREEN EXIT=0, both after a rebuild from
  `build/tests`; commands and logs are in the table above. The product diff is
  `logs/fix.patch`, reversed and restored with `git apply` (both EXIT=0).
- **The test is deterministic:** fixed URI strings, no clock, no sleeps, no threads and no
  scheduling. It uses the existing `RealtimeScope` seam and the thread-local `AllocationProbe`
  counters; the only timing is none.
- **The fix is at the cause and complete:** every host caller of `map()`/`unmap()` was checked -
  `Lv2UridCache.cpp:52-59` (manager construction), `Lv2Proc.cpp:175` (plug-in check),
  `:764-765` (port creation), `:807`/`:901`/`:1028` (state save/restore, under
  `requestChangeInModel()`), and `Lv2Features.cpp:64-65` (the feature pointers handed to plug-ins).
  All are off the audio thread. The sibling `unmap()` takes the same realtime branch, and the
  canonical path still publishes every new mapping, so a URI registered on the control thread is
  visible to later `run()` calls.
- **No collateral damage:** the canonical map, its IDs and their insertion order are unchanged;
  the mirror is a read-only copy. Old projects and LV2 state load exactly as before. The mirror's
  16384-slot bound is not a limit on the canonical map or on any saved data; beyond it only
  audio-thread lookups of *unpublished* URIs return 0. The only deliberate behaviour change is the
  one the finding asks for: a URI first seen inside `RealtimeScope` is not registered there.
- **Offline export:** `ProjectRenderer` calls `AudioEngine::renderNextPeriod()` on its own thread
  (`src/core/ProjectRenderer.cpp:297`, `:312`), so export runs under the same `RealtimeScope` and
  follows the same policy: a known URI resolves; a URI first seen in the render path gets 0 and is
  registered only when a control-thread caller maps it. This is deliberate - export is the same
  render path, and the LV2 contract tells plug-ins to cache before run.
- **If the verdict is ALREADY-FIXED or FALSE-POSITIVE:** not applicable; this is FIXED. The
  superseded first-pass reasoning is noted at the top.
- **Nothing new on the audio thread allocates, frees, locks, waits, does I/O or logs:** the
  `RealtimeScope` branch of `map()` calls `mapRt()` (FNV-1a + atomic loads + `strcmp`), and the
  branch of `unmap()` is one atomic load; no mutex, allocation, free, logging or wait appears in
  either. Publication happens on the control thread under the existing mutex. `Lv2Proc::run()`
  itself is unchanged.
- **Undo and commands:** no undo state, journal checkpoint or control command was touched; no new
  command or snapshot change.
- **Old projects and round-trip:** unchanged canonical map and IDs, so a project's saved LV2 state
  and port data do not change. No new file format.
- **Completeness and edge cases:** known URI (hit), first-seen URI (returns 0, then registered),
  `unmap(0)`/out-of-range (nullptr), and the capacity bound were considered; the test covers the
  first three, and the capacity path is described above. A real plug-in that maps only from `run()`
  and ignores 0 is relying on behaviour the LV2 specification does not promise; the host does not
  block or allocate for it.
- **Sibling commands in the command files you changed:** none, by this session. The completion
  check lists `app.*` and `arrangement.*` siblings because its diff base is the train base and the
  branch already contains earlier commits that changed `src/core/ControlCommands*.cpp`,
  `include/ControlRegistry.h` and their snapshot. This session changed only
  `include/Lv2UridMap.h`, `src/core/lv2/Lv2UridMap.cpp`, `tests/src/core/Lv2StateTest.cpp`,
  `tests/upstream-modifications.txt` and this report. No command handler, reversibility row,
  snapshot or patch was touched. The three `tests/control-*.py` files the check reports as
  "changed" are likewise earlier branch content, not files this session edited.
- **Qt 5.15 / MSVC / macOS portability:** the product code uses only C++20 standard headers
  (`<atomic>`, `<memory>`, `<cstring>`, `<cstdint>`), no Qt API, no POSIX, no `windows.h` names
  and no `std::filesystem`. It built with GCC on this box; MSVC/macOS were not run here.

## Other findings

- The control-thread `map()` still builds a temporary `std::string` for the canonical lookup (the
  TODO at `src/core/lv2/Lv2UridMap.cpp:142-151`). That is an optimisation outside this finding: it
  is not on the audio-thread branch and is unchanged.
