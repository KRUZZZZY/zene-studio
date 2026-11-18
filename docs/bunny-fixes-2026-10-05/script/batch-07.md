# Script lane — batch 07

Worktree `zene-bunny-script`, branch `bugs/bunny-script-2026-10-05`. Six findings, all six in the WASM sandbox
(`src/wasm/WasmOfflineRender.cpp`, `src/wasm/WasmSandbox.cpp`). None fixed: the WASM sandbox is **compiled out of
this build tree**, so no regression test for any of them can be built or run here, and rule "RED before the fix"
forbids a product change without one. One finding is a false positive against the project's own ABI contract;
five are real defects left untouched and described precisely below.

## Why nothing was fixed — the sandbox is not built here

`build/CMakeCache.txt:767` has `Wasmtime_INCLUDE_DIR:FILEPATH=Wasmtime_INCLUDE_DIR-NOTFOUND` and `:749`
`WASMTIME_ROOT:PATH=` (empty). No wasmtime headers or library exist on this box (`ls /usr/include/wasmtime*`,
`/usr/local/include/wasmtime*` → no such file; `ldconfig -p | grep -i wasmtime` → nothing). `WANT_WASM:BOOL=ON`
in the cache is therefore degraded to OFF by `CMakeLists.txt:957-963`, and `tests/CMakeLists.txt:1854` wraps every
WASM test in `if(WANT_WASM)`. Confirmed by the build tree:

```
cmake --build build --target WasmWorkerPoolTest   →  EXIT=2  ("No rule to make target 'WasmWorkerPoolTest'")
ls build/tests/CMakeFiles | grep -i wasm           →  (nothing)
```

So `WasmSandboxTest`, `WasmAbiConformanceTest` and `WasmWorkerPoolTest` do not exist here; `src/wasm/*.cpp` is
not compiled into `zene` either (they include the wasmtime C API headers). `docs/KNOWN-LIMITATIONS.md:389-422`
records exactly this ("the WASM DSP sandbox is compiled out of these builds … None of the seven CI jobs
provisions wasmtime"). A regression test written for any of these findings could not be compiled, let alone run
— and a test that is never executed is not evidence, so per the lane procedure they are recorded REAL-UNTESTED
with the exact fix each one needs, rather than shipped unproven.

### Cluster 3647 — Huge offline render requests can exhaust memory or loop forever
Verdict: REAL-UNTESTED
Evidence: `src/wasm/WasmOfflineRender.cpp:261-266` — `renderOffline()` validates `modulePath`, `frames != 0`,
`blockFrames` and `blockFrames <= WasmWorker::maxBlockFrames`, but sets **no upper bound** on `frames`. Each
render then allocates the whole output up front (`render.output.assign(frames, 0.0f)` at `:90` inline and
`:165` pooled), and `renderOffline` holds 2 control renders plus `repeats` clamped to `[2,8]` (`:267`) — up to ten
vectors of `frames * 4` bytes each, none released until the call returns. Reachable from the control surface:
`src/core/ControlCommandsWasmRender.cpp:204-206` casts `args["frames"].toInt()` straight to `std::uint32_t` with
no clamp, so `frames: -1` (or any negative int) becomes 4294967295 with no error. The loop offsets
(`:94`, `:171`) are `std::uint32_t`, so once `frames` is within `blockFrames` of `UINT32_MAX`,
`offset += blockFrames` wraps and the loop never terminates; a 16 GiB `assign` that does not throw first.
Fix needed: reject `frames > <practical limit>` in `renderOffline()` before any allocation (a
`maxOfflineRenderFrames` constant next to the ABI limits; ~1<<24 frames is ~5.8 min at 48 kHz and makes the
`uint32_t` wrap unreachable), and use a `std::size_t` loop offset for belt-and-braces.
Commit: none
Test: none — `WasmWorkerPoolTest` is the right home (it already calls `renderOffline` at `:208`) and cannot be
built here. Note for whoever has a wasmtime build: a RED case that passes `frames = maxOfflineRenderFrames + 1`
with a **nonexistent module path** (so the RED run cannot itself exhaust memory) and asserts the refusal names
`frames` and starts no render is deterministic; the boundary case `frames == maxOfflineRenderFrames` must fail
on the module load instead, proving the limit is not off by one.
Red/green: none

### Cluster 3648 — Invalid sample rates are passed through to WASM
Verdict: REAL-UNTESTED
Evidence: `src/wasm/WasmOfflineRender.cpp:251-266` — `sampleRate` is never validated; it is stored in the
outcome (`:259`) and handed to both paths (`renderInline` → `sandbox.callProcess(..., sampleRate)` at `:125`,
`renderThroughPool` → `worker.submit(..., sampleRate)` at `:175`). `src/wasm/WasmWorker.cpp:151` compares
`sampleRate != m_loadedSampleRate` on **every** block, and `m_loadedSampleRate` is a plain `float`
(`WasmWorker.h`), so a NaN rate is never equal to itself: every block takes the `reinstantiate(sampleRate)` branch
(`WasmWorker.cpp:287-297`), re-instantiating the module per block for the whole render, with the audio path
seeing `State::Loading` each time. Zero, negative and infinite rates reach the module as the `f32` `sample_rate`
argument. `rg -n "sampleRate" src/wasm/WasmWorker.cpp` shows no range or finiteness check on the way in
(`submit()` at `:301-306` rejects only `frames` and state).
Fix needed: `if (!std::isfinite(sampleRate) || sampleRate <= 0.0f)` → refuse in `renderOffline()` next to the
existing argument check; the same guard in `WasmWorker::submit()` covers the live path (it is a lane-side,
allocation-free comparison, so the audio-thread rule holds).
Commit: none
Test: none — `WasmWorkerPoolTest::testRenderRefusesAnUnusableSampleRate` is the natural case (assert `!outcome.ok`
for `0.0f`, `-1.0f`, `quiet_NaN()` and `infinity()`, that the error names `sample_rate`, and that
`runs`/`control` stay empty), with `TestSampleRate` still rendering as the control on the same case. It is not
committed because it cannot be compiled here and it would reference the not-yet-added guard.
Red/green: none

### Cluster 5440 — Offline rendering can read a stale linear-memory pointer after `memory.grow`
Verdict: REAL-UNTESTED
Evidence: `src/wasm/WasmOfflineRender.cpp:100` caches `std::uint8_t* memory = sandbox.memoryData()` once per
block; `callProcess` is invoked at `:124` and the output plane is then read through that same cached pointer at
`:137-141` (`reinterpret_cast<const float*>(memory + outBase)`). Nothing re-resolves the pointer between the
call and the read, so a module that grows its memory during `process()` can invalidate it. The pooled path does not
have this bug — `WasmWorker.cpp:168/171` re-resolves per block — so only the inline control path (which also
supplies the determinism **floor**) is affected. `docs/WASM-EFFECT-ABI.md:184-189` documents this as UNKNOWN
precisely because whether `wasmtime_memory_data` returns a fresh pointer after a growth is a wasmtime-version
question.
Commit: none
Test: none — and note this one is not merely blocked by the missing sandbox: even with wasmtime, a RED case is
not deterministic. If wasmtime keeps the base pointer across `memory.grow` (the usual mmap-reservation
behaviour) the stale read is accidentally correct and the case passes with or without the fix; if it moves, the
case fails. A test keyed on output samples would therefore be platform- and version-dependent, which is why this
one is reported as REAL-UNTESTED rather than fixed blind.
Fix needed (2 lines, behaviour-identical when memory does not move): re-acquire
`std::uint8_t* memory = sandbox.memoryData();` after the `callProcess` loop and re-check
`sandbox.memorySize() >= needed` before reading the plane out.

### Cluster 5441 — Offline determinism can pass while a run pair exceeds the measured floor
Verdict: REAL-UNTESTED
Evidence: `src/wasm/WasmOfflineRender.cpp:314-319` — a pair replaces the recorded worst when **either** metric
grows (`pair.maxAbsDifference > worst.maxAbsDifference || pair.differingFrames > worst.differingFrames`). With
three or more pooled runs, an earlier pair with `maxAbsDifference = 0.5` and 1 differing frame can be displaced by
a later pair with 10 differing frames and `maxAbsDifference = 0.01`; `RenderDifference::covers()`
(`src/wasm/WasmOfflineRender.h:87-90`) requires `other` to be at most as large in **both** respects, so the recorded pair
passes the floor at `:335` and the 0.5 difference is never judged. The same pattern repeats for
`subjectAgainstControl` at `:323-329`.
Commit: none
Test: none — the selection is a local `worst` inside `renderOffline`, and the pooled runs cannot be steered from
a test, so an end-to-end case cannot be made deterministic. It needs a small extracted helper (a component-wise
worst-of merge, or "every pair must be covered") exposed for direct testing, which is a header/API change and must
not be landed untested.
Fix needed: track the two maxima independently (merge pairs component-wise instead of choosing one pair), or
require **every** pair to be covered by the floor. Both must keep the existing semantics that `frames` always
reports what was compared (`:311-313`).

### Cluster 4562 — The WASM host does not enforce the declared ABI version
Verdict: REAL-UNTESTED
Evidence: `src/wasm/WasmSandbox.cpp:403-418` reads the `channels` and `latency` globals (`readGlobalI32` at
`:254-268`) and clamps them, but never reads `abi::abiVersionExport`; `src/wasm/WasmAbi.h:32-34` says in a
comment "The host refuses to load modules that export a different value" and `:53-54` defines
`abiVersionExport = "abi"` with `version = 1`. `rg -n 'abiVersionExport|abi::version' src tests` finds the
declaration in the header and no reader, so a module exporting `abi = 0` or `abi = 999` is accepted and called
with the v1 contract. `docs/WASM-EFFECT-ABI.md:31` and §4.3 document this exact gap, and §12 item 1 marks the
intent ("Whether `abi` is meant to be enforced") **UNKNOWN** — so the header comment and the code disagree, and
which one is normative is a decision the parent owns.
Commit: none
Test: none — `WasmAbiConformanceTest` already has the fixtures this needs (`channelsGlobal()` at `:101`,
`moduleWat()`, `effectiveChannels()` at `:303`), but it is inside `if(WANT_WASM)` and cannot be built here. The
case would be one `abiGlobal(2)` module refused with an error naming the version, one `abiGlobal(1)` accepted,
and one module with no `abi` export accepted (missing defaults to 1, `WasmAbi.h:53`).
Red/green: none
Fix needed, once the intent is settled: `int declaredVersion = abi::version;
m_impl->readGlobalI32(abi::abiVersionExport, declaredVersion); if (declaredVersion != abi::version) { error =
"module declares ABI version N, this host implements 1"; return false; }` in `loadModuleBytes()`, plus updating
`docs/WASM-EFFECT-ABI.md` §4.3/§12 so the doc stops calling the gap known-but-unimplemented.

### Cluster 5438 — A nonzero WASM `process()` result is treated as success
Verdict: FALSE-POSITIVE
Evidence: this is the ABI's documented, test-pinned behaviour, not a defect.
`src/wasm/WasmAbi.h:40` states `process(in_ptr, out_ptr, frames, sample_rate) -> i32 (0 = ok)` and
`WasmSandbox.cpp:460-466` records the value in `CallResult::returnValue` — which
`docs/WASM-EFFECT-ABI.md:239-244` documents as deliberate: "**A module cannot signal an error by returning a
non-zero value**: the block is still treated as good and the output planes are still read back … `WasmAbi.h:40`
documents the convention `0 = ok`; the host does not implement a check for it." An existing conformance case
asserts precisely this: `tests/src/wasm/WasmAbiConformanceTest.cpp:405-418`
`abiProcessReturnValueIsRecordedNotActedOn` loads a module returning `7` and requires
`QVERIFY(result.ok()); QVERIFY(!result.trapped()); QCOMPARE(result.returnValue, 7);` with the comment "A
non-zero result must NOT be treated as an error (s8)". Enforcing the return code would fail that case, i.e. it
would mean weakening an existing test to get green. The only real consumers branch on the *wasmtime call status*
(`WasmOfflineRender.cpp:126,129` via `result.ok()`), which is what the ABI defines `ok()` to mean
(`WasmSandbox.h:48,54`).
Commit: none
Test: `WasmAbiConformanceTest::abiProcessReturnValueIsRecordedNotActedOn` — the existing case that pins the
behaviour (not built or run in this lane; it lives under `if(WANT_WASM)`).
Red/green: none

Leftovers: five real, unfixed defects, all in WASM code that this build tree compiles out — no regression test for
any of them can be built or run here (missing wasmtime C API), so no product code was touched. In rough value
order for a lane with a wasmtime-enabled build: **5438** needs no work (false positive — do not "fix" it, it would
break `abiProcessReturnValueIsRecordedNotActedOn`); **3648** (sample-rate guard) and **3647** (frame bound, and
the unclamped `static_cast<std::uint32_t>(args["frames"].toInt())` at `ControlCommandsWasmRender.cpp:204-206`
that makes a negative `frames` reachable) are small and deterministic to test; **5440** is a 2-line re-acquire
whose test is wasmtime-version-dependent; **5441** needs an extracted, testable helper for the pair selection;
**4562** is real but needs the parent to decide whether the header comment or `docs/WASM-EFFECT-ABI.md` §12 is
normative before it is enforced. A wasmtime-enabled build tree is the prerequisite for all five.
