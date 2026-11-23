FALSE-POSITIVE

# Cluster new-p17-82: LV2 URI mapping can lock and allocate during audio processing

**Verdict: FALSE-POSITIVE.** The code does lock and allocate, but only when a plug-in calls the
`LV2_URID_Map` callback. That callback is defined by the LV2 contract as *not necessarily RT-safe*,
with the caching obligation on the plug-in; the host itself never maps a URI from the audio thread.
Making this host-side map real-time safe would either cap the dynamic map (refusing legitimate URIs)
or return 0 for runtime registrations (breaking the "dynamic map" and referential-transparency
contract the same specification states). Neither is a fix to a host defect.

## Evidence

### 1. The code does what the finding says - when the plug-in calls it

- `src/core/lv2/Lv2UridMap.cpp:54-92` - `UridMap::map()` builds a temporary `std::string` (`:71`),
  takes `std::lock_guard<std::mutex> guard(m_MapMutex)` (`:73`), looks the URI up (`:75`) and, for a
  new URI, allocates map/vector nodes (`:80`, `:83`). `unmap()` also takes the same mutex (`:98`).
- `src/core/lv2/Lv2Features.cpp:64-65` - the `LV2_URID__map` / `LV2_URID__unmap` features are handed
  to every plug-in instance, so a plug-in can retain the callback and invoke it from `run()`.

This is the mechanism the finding describes. It says nothing yet about whose thread invokes it.

### 2. The LV2 contract puts that call and its cache on the plug-in

Installed specification header this build compiles against, `/usr/include/lv2/urid/urid.h:75-81`:

> This function is not necessarily very fast or RT-safe: plugins SHOULD cache any IDs they might
> need in performance critical situations.
>
> The return value 0 is reserved and indicates that an ID for that URI could not be created for
> whatever reason. However, hosts SHOULD NOT return 0 from this function in non-exceptional
> circumstances (i.e. the URI map SHOULD be dynamic).

The same specification requires the map to be dynamic (new URIs create IDs for the life of an
instance) and referentially transparent (the same URI returns the same ID). A lock-free host map
that rejected runtime registrations, or that returned 0 on the audio thread and an ID later, would
violate that contract. A bounded preallocated map would instead create a capacity limit that can
refuse a legitimate URI, which the procedure's collateral rule forbids. The specified design is a
dynamic, possibly-blocking map plus a plug-in that caches the IDs it needs before `run()`.

The tree already records the same intent: `include/Lv2UridMap.h:33` includes `<mutex>` with the
comment *"TODO: use semaphore, even though this is not realtime critical"*, and `:51-53` says the
mutex is required because the URID map is global.

### 3. Host code never calls `map()`/`unmap()` from the audio thread

`Lv2Proc::run()` is the host's per-period entry (`src/core/lv2/Lv2Proc.cpp:417-441`): after
`m_worker->emitResponses()` it calls `lilv_instance_run()` (`:425`) and then reads a latency port and
notifies the worker. It never maps a URI. Every host call site of `UridMap::map()` is off that path:

| Host call site | Thread / phase |
| --- | --- |
| `src/core/lv2/Lv2UridCache.cpp:52-59` | `Lv2Manager` construction (`src/core/lv2/Lv2Manager.cpp:171`), before the engine runs |
| `src/core/lv2/Lv2Proc.cpp:175` | plug-in check/scan |
| `src/core/lv2/Lv2Proc.cpp:764-765` | `createPorts()` during instantiation |
| `src/core/lv2/Lv2Proc.cpp:807` | `saveState()`, explicitly under `requestChangeInModel()` (`:806`, `:809`) |
| `src/core/lv2/Lv2Proc.cpp:901` | `decodeStateProperty()`, called from `restoreState()` |
| `src/core/lv2/Lv2Proc.cpp:1028` | `restoreState()`, under `requestChangeInModel()` (`:1035`, `:1037`) |

`unmap()` is reached only through the same feature and `lilv_state_to_string()` in `saveState()`
(the control thread). None of these is rendered per period.

The only route from `Lv2Proc::run()` into `UridMap::map()` is a plug-in calling the callback from its
own `run()`. The project's own LV2 fixture shows the intended pattern: it maps every URI it needs in
`instantiate()` (`tests/data/lv2-test-instrument/lv2-test-instrument.c:101-106`) and its `run()`
(`:167`) maps nothing.

### 4. Reproduction of the mechanism (diagnostic probe, not a regression test)

To see exactly what a plug-in's callback costs in this tree, I compiled a scratch probe against
`src/core/lv2/Lv2UridMap.cpp` with a counting `operator new` (source:
`logs/urid-probe.cpp`, removed from the tree; it is not committed):

```sh
g++ -std=c++20 -O1 -Iinclude -Ibuild <logs>/urid-probe.cpp src/core/lv2/Lv2UridMap.cpp -o ${TMPDIR}/urid-probe \
  > <logs>/probe-build-1.log 2>&1; echo EXIT=$?
```

`EXIT=0`. Run:

```sh
${TMPDIR}/urid-probe > <logs>/probe-run-1.log 2>&1; echo EXIT=$?
```

`EXIT=0`, with:

```
known  first-id=1 second-id=1 allocations=1
fresh  id=2 allocations=4
unmap  uri=urn:zene:test:known allocations=0
fresh2 id=2 allocations=1
```

So a plug-in that calls `map()` from `run()` does allocate once for a known URI and four times for a
new one. That confirms the mechanism, not a host defect: the call is the plug-in's, and the
specification above tells plug-ins to cache instead. The probe is deliberately not committed as a
regression test because there is no product behaviour to change.

### Commands run (logs in `logs/` of this run)

| Command | Log | EXIT |
| --- | --- | --- |
| `g++ -std=c++20 -O1 -Iinclude -Ibuild <logs>/urid-probe.cpp src/core/lv2/Lv2UridMap.cpp -o ${TMPDIR}/urid-probe` | `probe-build-1.log` | 0 |
| `${TMPDIR}/urid-probe` | `probe-run-1.log` | 0 |
| `python3 .../feature_check.py <worktree>` | `feature-check-1.log` | 0 |
| `git status --porcelain` before the report commit | (empty) | 0 |

`feature_check.py` printed `RESULT: PASS` with all eight rows PASS (`fork-sources`,
`all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`,
`doc-figures`, `complexity`, `file-length`).

## Self-check

- **Regression test fails with the product change reverted and passes with it:** not applicable. The
  verdict is FALSE-POSITIVE with no product change, so there is no RED/GREEN pair to run. The
  diagnostic probe above is deterministic evidence of the mechanism, and the code-reference below
  is the evidence for the verdict, as the procedure's not-real branch requires.
- **Deterministic:** the probe uses fixed URI strings and a thread-local-free counting `operator
  new`; no clocks, sleeps, threads or scheduling. It is not a regression test and is not committed.
- **Fix at the cause and complete:** no fix was made. I checked every host caller of `UridMap::map()`
  and `unmap()` (table above) and its siblings: `Lv2UridCache`, `Lv2Features`, `Lv2Proc` port
  creation, state save/restore and plug-in checking. All are control-thread/instantiation paths.
- **No collateral damage:** no production file changed; no saved format, limit or check changed.
  Old projects and plug-in state load exactly as before.
- **False-positive evidence:** the LV2 specification at `/usr/include/lv2/urid/urid.h:75-81`
  (map is not necessarily RT-safe; plug-ins SHOULD cache; the map SHOULD remain dynamic), the
  host's non-realtime-only call sites above, and the fixture's instantiate-time mapping at
  `tests/data/lv2-test-instrument/lv2-test-instrument.c:101-106`.
- **Realtime:** nothing was added on the audio thread. The existing callback can run there only when
  a plug-in invokes it; the host's own `Lv2Proc::run()` (`src/core/lv2/Lv2Proc.cpp:417-441`) does not
  map or unmap.
- **Undo and commands:** no undo state and no control command was touched.
- **Old projects / round-trip:** unchanged (no production edit).
- **Completeness and user path:** the behaviour is a plug-in callback, not a user action; the agent
  surface and GUI are unaffected.

## Other findings

- The URID map's known-URI lookup still allocates the temporary `std::string` described by the TODO
  at `src/core/lv2/Lv2UridMap.cpp:62-71`. That is an optimisation (C++20 transparent hashing would
  remove it), not the reported realtime defect, and it is not changed here.
