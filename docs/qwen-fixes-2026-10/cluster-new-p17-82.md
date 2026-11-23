FIXED

# Cluster new-p17-82: LV2 URI mapping can lock and allocate during audio processing

**Verdict: FIXED.** `UridMap::map()`/`unmap()` have a lock-free, allocation-free branch inside
`RealtimeScope`: a URI the host already knows resolves from an append-only mirror, and a URI first
seen on the audio thread is not registered there at all (`map()` returns 0). The canonical map and
its mutex stay exactly as they were for every control-thread caller.

Round 2 replaces round 1's fixed-size mirror. The reviewer was right: after 16,384 unique
control-thread mappings, a URI assigned ID 16,385 was in the canonical map but the realtime mirror
returned 0 and realtime unmap returned null. The mirror is now append-only and unbounded in the
number of URIs: a fixed bucket count and a fixed chunk size are hash/performance parameters, not
capacity limits. Every URI successfully mapped on the control path is published before `map()`
returns (a publication allocation failure is caught, the canonical ID is still returned, and the
next `map()` of that URI retries publication).

## Cause and fix

Before: `src/core/lv2/Lv2UridMap.cpp:54-92` (round 1 line numbers) built a temporary `std::string`,
took `std::lock_guard<std::mutex>`, and for a new URI allocated the map node and the unmap entry.
`unmap()` took the same mutex. `Lv2Features::initCommon()` hands the feature pointers to every
plug-in, so a plug-in can reach this from `run()`.

After:

- `include/Lv2UridMap.h:59-111` - declares the mirror:
  - `RtBucketCount` (`:65`, 65536 buckets) and `UridChunkSize` (`:67`, 4096 URIDs per chunk) are
    hash/chunk parameters, not limits on how many URIs can be mapped.
  - `RtNode` (`:70-76`) is an append-only, immutable hash node (`hash`, `urid`, `uri`, `next`).
  - `UridChunk` (`:79-83`) is an append-only URID list chunk (`atomic<const char*>` slots plus an
    atomic `next`).
  - `m_rtBuckets` (`:93`), `m_uridChunks` (`:95`), `m_uridTail` (`:97`) and the published chunk
    count (`:99`). The mutex still guards `m_map`/`m_unMap` and publication.
- `src/core/lv2/Lv2UridMap.cpp:50-60` - `rtHash()`: FNV-1a, no allocation, no lock.
- `src/core/lv2/Lv2UridMap.cpp:62-74` - `mapRt()`: acquire-loads one bucket head and walks its
  immutable chain. It takes no mutex and allocates nothing; a lookup that misses returns 0.
- `src/core/lv2/Lv2UridMap.cpp:76-88` - `ensureUridChunk()`: appends as many chunks as the URID
  needs, links each with a release store, and publishes the new count with a release store. Called
  on the control thread with `m_MapMutex` held.
- `src/core/lv2/Lv2UridMap.cpp:90-121` - `publishRt()`: publishes `urid - 1 -> URI` in the chunked
  list, then inserts a node at the bucket head with a release store. It is idempotent (a retry
  after an allocation failure does not add a second node), so a control-thread mapping is
  resolvable and unmappable from later realtime calls.
- `src/core/lv2/Lv2UridMap.cpp:123-138` - the constructor allocates the bucket array and the first
  URID chunk once, before any plug-in runs.
- `src/core/lv2/Lv2UridMap.cpp:140-159` - the destructor frees the published nodes and chunks; no
  node or chunk is freed on the audio thread.
- `src/core/lv2/Lv2UridMap.cpp:161-217` - `map()`: null-safe; inside `RealtimeScope` it returns
  `mapRt(uri)` and registers nothing. Outside it, the canonical path is unchanged; every result
  (new mapping at `:203`, existing mapping at `:210`) is published, and a publication failure is
  caught without discarding the canonical ID.
- `src/core/lv2/Lv2UridMap.cpp:219-241` - `unmap()`: inside `RealtimeScope` it walks the chunk list
  by URID and returns the atomic slot (or nullptr); the chunk count rejects an absurd URID without
  a long walk. Outside it keeps the canonical lookup.
- The eight common URIs are still pre-mapped at manager construction by `Lv2UridCache`.

## Test matrix

- **Normal path:** a known URI resolves in `RealtimeScope` to the same ID with zero allocations, and
  `unmap()` returns its URI with zero allocations.
- **New-URI / failure path:** a URI first seen in `RealtimeScope` returns 0 and does not register;
  the same URI mapped afterwards on the control thread gets a nonzero ID, and the next
  `RealtimeScope` lookup returns that ID.
- **Exhaustion / the reviewer's case:** the test maps **16,385 unique URIs** on the control path
  (plus the known one), well past the old 16,384-slot limit; it asserts the last ID is above 16384,
  then resolves and unmaps that last ID from `RealtimeScope` with zero allocations. The mirror has
  no URI-count limit; the bucket count and chunk size only bound probe and walk work.
- **Bounds:** `unmap(0xFFFFFFFF)` from `RealtimeScope` returns `nullptr` with zero allocations,
  rejected by the published chunk count instead of walking a chain that cannot contain it.
- **Reconfiguration:** none; the map has no per-project reconfiguration path.
- **Audio-thread constraints:** the test runs the calls inside `RealtimeScope` with
  `AllocationProbe` armed; `mapRt`, the URID walk and `unmap` contain no mutex, allocation, free or
  logging.
- **Concurrency:** the bucket hash and chunk list are append-only; a single control-thread writer
  is serialised by `m_MapMutex`, and readers only see fully published nodes/chunks via
  acquire/release. Local TSan is not available on this box; the design is reasoned above rather
  than race-tested.
- **Platform:** pure C++20 (`<array>`, `<atomic>`, `<memory>`, `<cstring>`), no POSIX or
  compiler-specific code; built and run on this Linux tree. MSVC/macOS were not available.

## RED/GREEN and the reversal proofs

Two RED proofs were recorded: one against round 1's fixed 16,384 mirror (the reviewer's exact
capacity defect), and the procedure's full product-change reversal.

### Targeted capacity RED (round 1 mirror restored, round 2 test kept)

```sh
git diff HEAD -- include/Lv2UridMap.h src/core/lv2/Lv2UridMap.cpp > logs/cap-fix.patch   # EXIT=0
git apply -R logs/cap-fix.patch                                                         # EXIT=0
cmake --build build --target zene Lv2StateTest -j4                                      # EXIT=0
cd build/tests && ./Lv2StateTest                                                         # EXIT=1
git apply logs/cap-fix.patch                                                             # EXIT=0
cmake --build build --target zene Lv2StateTest -j4                                      # EXIT=0
cd build/tests && ./Lv2StateTest                                                         # EXIT=0
```

| Step | Log | EXIT | Result |
| --- | --- | --- | --- |
| reverse round-2 delta | `cap-fix.patch` | 0 | 292-line patch |
| rebuild round-1 mirror | `build-cap-red-1.log` | 0 | built |
| run | `qtest-cap-red-1.log` | **1** | `Actual (lastOnAudio): 0`, `Expected (lastId): 16386`; 8 passed, 1 failed |
| restore | - | 0 | round-2 fix back |
| rebuild | `build-green-2.log` | 0 | built |
| run | `qtest-green-2.log` | **0** | 9 passed, 0 failed |

### Full product-change reversal (procedure step 4b)

The product diff from the pre-fix state (`f34d1784e^`, which holds no URID mirror) to the final
working tree:

```sh
git diff f34d1784e^ -- include/Lv2UridMap.h src/core/lv2/Lv2UridMap.cpp > logs/fix.patch   # EXIT=0
git apply -R logs/fix.patch                                                                 # EXIT=0
cmake --build build --target zene Lv2StateTest -j4                                          # EXIT=0
cd build/tests && ./Lv2StateTest                                                             # EXIT=1
git apply logs/fix.patch                                                                     # EXIT=0
cmake --build build --target zene Lv2StateTest -j4                                          # EXIT=0
cd build/tests && ./Lv2StateTest                                                             # EXIT=0
```

| Step | Log | EXIT | Result |
| --- | --- | --- | --- |
| save/ reverse product diff | `fix.patch` | 0 | 280-line patch |
| RED build | `build-red-1.log` | 0 | built |
| RED run | `qtest-red-1.log` | **1** | `Actual (freshOnAudio): 16387`, `Expected (LV2_URID{0}): 0`; 8 passed, 1 failed |
| restore | - | 0 | fix back |
| GREEN build | `build-green-3.log` | 0 | built |
| GREEN run | `qtest-green-3.log` | **0** | 9 passed, 0 failed |

The test's first RT assertion is the unknown-URI one; the RED run stops there. The last-ID
assertion is exercised in the capacity RED above and in every GREEN run. The allocation count is
also asserted in every GREEN run; before the fix the old `map()` built a temporary `std::string`
and map nodes.

The two chained commands were also run verbatim against the final tree, as the proof manifest
records them: `proof-red-1.log` EXIT=1 (the same `freshOnAudio` failure) and `proof-green-1.log`
EXIT=0 (9 passed).

## Family, gates and the focused ctest

| Command | Log | EXIT | Result |
| --- | --- | --- | --- |
| `ctest -R '^Lv2StateTest$' --output-on-failure` (from `build/tests`) | `test-regression-1.log` | 0 | 1 of 1 passed |
| `cmake --build build --target ControlDeviceCatalogueTest PluginScanCacheTest InstrumentLatencyPdcTest Lv2ViewBaseLifetimeTest Lv2StateTest -j4` | `build-family-1.log` | 0 | built |
| the six-test family selection `^(Lv2StateTest\|InstrumentLatencyPdcTest\|Lv2ViewBaseLifetimeTest\|Lv2InstrumentKeyRegressionTest\|PluginScanCacheTest\|ControlDeviceCatalogueTest)$` was run with `ctest --output-on-failure` from `build/tests` | `test-family-1.log` | 0 | 6 of 6 passed |
| `python3 .../feature_check.py <worktree>` | `feature-check-1.log` | 0 | all 8 rows PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length |

Registration: the two inherited files are declared in `tests/upstream-modifications.txt` with the
cluster and the reason (same commit). The test lives in the already-registered
`tests/src/core/Lv2StateTest.cpp`; no new source file or manifest entry was needed.
`tests/upstream-modifications.txt` ends with a newline.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it:** full reversal
  RED `./Lv2StateTest` EXIT=1 (`freshOnAudio` 16387 vs 0), GREEN EXIT=0; the capacity reversal
  against round 1 is RED EXIT=1 (`lastOnAudio` 0 vs 16386) and GREEN EXIT=0. Commands, logs and
  exits are in the two tables above.
- **The test is deterministic:** fixed URI strings built from an integer loop, no clock, no
  sleeps, no threads, no scheduling. The only seam is `RealtimeScope` plus the thread-local
  `AllocationProbe` counters.
- **The fix is at the cause and complete:** every host caller of `map()`/`unmap()` was checked -
  `Lv2UridCache.cpp:52-59` (manager construction), `Lv2Proc.cpp:175` (plug-in check), `:764-765`
  (port creation), `:807`/`:901`/`:1028` (state save/restore under `requestChangeInModel()`), and
  `Lv2Features.cpp:64-65` (feature pointers). All are off the audio thread. The sibling `unmap()`
  is updated with the same append-only structures; both new and existing control-thread mappings
  are published, and an existing canonical mapping retries publication.
- **No collateral damage:** the canonical map, its IDs and insertion order are unchanged. The
  bucket count and chunk size do not reject any URI - they are hash and walk parameters, and the
  test maps past the round-1 limit. Old projects and LV2 state load exactly as before. The only
  deliberate behaviour change is the finding's: a URI first seen inside `RealtimeScope` is not
  registered there.
- **If the verdict is ALREADY-FIXED or FALSE-POSITIVE:** not applicable; this is FIXED.
- **Nothing new on the audio thread allocates, frees, locks, waits, does I/O or logs:** the
  `RealtimeScope` branch of `map()` calls `mapRt()` (FNV-1a, atomic bucket load, immutable chain
  walk, `strcmp`); `unmap()` is an atomic chunk walk plus one atomic slot load. Publication
  allocates on the control thread under the existing mutex; the destructor frees at shutdown, not
  on the audio thread. `Lv2Proc::run()` itself is unchanged.
- **Undo and commands:** no undo state, journal checkpoint or control command was touched; no new
  command or snapshot change.
- **Old projects and round-trip:** unchanged canonical map and IDs, so saved LV2 state and port data
  do not change. No new file format.
- **Completeness and edge cases:** known URI (hit), first-seen URI (returns 0, then registered),
  `unmap(0)`/absurd URID (nullptr), the old 16,384 boundary (last ID above it resolves and unmaps),
  many URIDs (chunked list), and a retry after a publication failure (idempotent publish) were
  considered. The test covers the first four and the capacity case.
- **Sibling commands in the command files you changed:** none, by this session. The completion
  check lists `app.*` and `arrangement.*` siblings because its diff base is the train base and the
  branch already contains earlier commits that changed `src/core/ControlCommands*.cpp`,
  `include/ControlRegistry.h` and their snapshot. This session changed only
  `include/Lv2UridMap.h`, `src/core/lv2/Lv2UridMap.cpp`, `tests/src/core/Lv2StateTest.cpp`,
  `tests/upstream-modifications.txt` and this report. No command handler, reversibility row,
  snapshot or patch was touched. The three `tests/control-*.py` files the check reports as
  "changed" are likewise earlier branch content, not files this session edited.
- **Qt 5.15 / MSVC / macOS portability:** the product code uses only C++20 standard headers
  (`<array>`, `<atomic>`, `<memory>`, `<cstring>`, `<cstdint>`), no Qt API, no POSIX, no
  `windows.h` names and no `std::filesystem`. It built with GCC on this box; MSVC/macOS were not
  run here.

## Other findings

- The control-thread `map()` still builds a temporary `std::string` for the canonical lookup (the
  TODO at `src/core/lv2/Lv2UridMap.cpp:177-186`). That is an optimisation outside this finding: it
  is not on the audio-thread branch and is unchanged.
- Publication allocates one `RtNode` and grows the URID chunk list on the control thread. Those
  nodes and chunks are freed only in `~UridMap`; the map's lifetime is the process, as before.
