# The WASM effect ABI (v0) — what a module must export and import

**Status: derived from the host source in this tree, on 2026-09-13, at `5e8335855`.** Every statement
below names the file and symbol it was read from. Anything this document could not determine from the
source is marked **UNKNOWN** in §12 rather than filled in from what a wasm audio ABI usually looks like.
The upstream specification this code was written against (`docs/specs/SPEC-wasm-sandbox.md`, §3) is a
*pinned snapshot of a draft*; where the code and the draft differ, **this document follows the code** and
names the difference (§11).

**The limit, stated first, because a reader must not mistake this for a working feature.**
The sandbox is **compiled out of these builds** — `WANT_WASM` defaults `ON` (`CMakeLists.txt:140`) but
degrades to `OFF` when the wasmtime C API is absent from the find path (`CMakeLists.txt:957-963`), and no
build on the reference box has it. **No module described here has been executed.** The host source is
present and reviewed; its runtime behaviour is described from the code, not observed. The same limit
applies to the conformance suite and the example effect committed beside this document — both are
*source*, checked against this document, never run. See §13 and `docs/KNOWN-LIMITATIONS.md`.

**Licence.** No new dependency and no vendored code is added by this document, the conformance suite or
the example module: they are first-party GPL-2.0-or-later like the rest of the tree (AGENTS.md rule 8).

---

## 1. The surface at a glance

| Kind | Name | Wasm type | Required? | Source |
|---|---|---|---|---|
| export | `process` | `(i32 i32 i32 f32) -> i32` | **required** for a DSP module | `WasmAbi.h:41`, `WasmSandbox.cpp:386-392` |
| export | `memory` | linear memory | **required** whenever `process` is exported | `WasmAbi.h:44`, `WasmSandbox.cpp:393-401` |
| export | `channels` | `i32` **global** | optional (default 1) | `WasmAbi.h:48`, `WasmSandbox.cpp:254-268`, `:403-414` |
| export | `latency` | `i32` **global** | optional (default 0) | `WasmAbi.h:51`, `WasmSandbox.cpp:254-268`, `:404-418` |
| export | `abi` | `i32` global | **declared only — the host never reads it** (§4.3) | `WasmAbi.h:54` |
| import | `env.host_get_param` | `(i32) -> f32` | optional | `WasmAbi.h:57`, `WasmSandbox.cpp:148-154` |
| import | `env.host_log` | `(i32 i32) -> ()` | optional | `WasmAbi.h:58`, `WasmSandbox.cpp:156-161` |
| import | `env.host_get_transport_state` | `() -> i32` | optional | `WasmAbi.h:59`, `WasmSandbox.cpp:163-169` |

The import module namespace is the literal string `"env"` (`WasmAbi.h:37`, used at
`WasmSandbox.cpp:150/158/165`). The host defines all three imports on every linker
(`WasmSandbox.cpp:146-170`), so a module may import any subset; importing none is legal.

A module with **no `process` export is loadable** and is not an effect: `loadModuleBytes` succeeds and
`hasProcess()` is `false` (`WasmSandbox.cpp:386-392`), while `WasmWorker` then refuses to start
(`WasmWorker.cpp:99-105`, error `module does not export process()`). That path exists for probe modules
(the `hello`/`probe` fixtures).

## 2. Required export: `memory`

- The host looks up an export named exactly `memory` and requires its **kind to be memory**
  (`WasmSandbox.cpp:377-379`, `abi::memoryExport`=`"memory"` at `WasmAbi.h:44`).
- If the module exports `process` **and** has no `memory`, the load **fails** with the message
  `module does not export linear memory 'memory'` and `process` is un-set (`WasmSandbox.cpp:393-401`).
- The host **captures the exported memory handle** at load (`WasmSandbox.cpp:377-384`) and resolves its
  data pointer and byte size on each call through `memoryData()` / `memorySize()`
  (`WasmSandbox.cpp:505-516`), which `processSlot` invokes once per block (`WasmWorker.cpp:168`, `:171`).
  A module that exports `memory` but no `process` is allowed (probe modules; `hello.wat` exports
  neither).

## 3. Required export: `process` — and it is called **once per channel plane**

`WasmSandbox::callProcess` builds exactly four arguments and reads exactly one result
(`WasmSandbox.cpp:450-461`):

```
process(in_offset: i32, out_offset: i32, frames: i32, sample_rate: f32) -> i32
```

- `in_offset` / `out_offset` are **byte offsets into the module's own linear memory**
  (`WasmSandbox.h:83-87`, `WasmSandbox.cpp:451-454`). **No host pointer ever crosses the boundary.**
- `frames` is a count of `f32` samples **in one plane**, not a count of frames for the whole block.
- `sample_rate` is a `f32` (Hz).
- The result is read as an `i32` and stored in `CallResult::returnValue`
  (`WasmSandbox.cpp:462-465`) — see §8: nothing reads it back.

**The single most important property, and the one most easily got wrong:**
`WasmWorker::processSlot` calls `process()` **once per channel**, in a loop, each call with the *same*
`frames` and `sample_rate` and a *different* offset pair (`WasmWorker.cpp:201-212`):

```
for channel in [0, channels):
    in_offset  = 0                       + channel * planeBytes
    out_offset = channels * planeBytes   + channel * planeBytes
    process(in_offset, out_offset, frames, sampleRate)
```

with `planeBytes = frames * sizeof(float)` (`WasmWorker.cpp:164`), `inBase = 0`
(`WasmWorker.cpp:165`) and `outBase = channels * planeBytes` (`WasmWorker.cpp:166`). `channels` here is
the module's own declared channel count (§4.1). So a module declaring 2 channels is entered **twice per
block**, once for the left plane and once for the right; a module declaring 1 channel is entered once.

The loop **stops at the first call whose status is not Ok** (`WasmWorker.cpp:208-211`), and that one
failure quarantines the module for the rest of the block and afterwards (§9).

The signature itself is not declared anywhere in the host: it is enforced implicitly, by wasmtime
rejecting a call whose function type does not match. A module exporting `process` with a different
signature therefore fails at *call* time (`CallResult::Status::Error`, `message` from wasmtime —
`WasmSandbox.cpp:281-287`), not at load time. Exporting a correctly named but wrong-typed `process` is
not detected by `hasProcess()`.

## 4. Optional exports

### 4.1 `channels` — an **i32 global**, not a function

Read by `Impl::readGlobalI32` (`WasmSandbox.cpp:254-268`): the host looks up the export, requires
`kind == WASMTIME_EXTERN_GLOBAL`, reads it with `wasmtime_global_get`, and takes the value only if its
kind is `WASMTIME_I32`. **A function named `channels` is ignored** — no error, no warning, the default
applies.

Then (`WasmSandbox.cpp:403-414`):

| Declared value | Effective channel count |
|---|---|
| absent | `1` (default, `WasmSandbox.cpp:403`) |
| `< 1` (including 0 and negative) | `1` |
| `1` or `2` | as declared |
| `> 2` | `2` (clamped to `abi::maxChannels`, `WasmAbi.h:62`) |

The clamp is silent. A module declaring 6 channels is run as stereo, with no error surfaced.

`WasmWorker` copies the effective value out of the sandbox once, at load
(`WasmWorker.cpp:106`) and on every re-instantiation (`WasmWorker.cpp:254`), and exposes it as
`declaredChannels()` (`WasmWorker.h:89`).

### 4.2 `latency` — an **i32 global**, same mechanism

Read the same way (`WasmSandbox.cpp:254-268`, `:404`). Default `0` (`WasmSandbox.cpp:404`); **negative
values are clamped to `0`** (`WasmSandbox.cpp:415-418`). There is no upper clamp in the sandbox; the
consumer clamps: `WasmEffect::latencyFrames()` clamps to `[0, maxBlockFrames]` = `[0, 8192]` before
reporting PDC latency (`WasmEffect.cpp:132-150`, `WasmEffect.h:102`), and `processImpl` clamps the
compensated value to the same bound (`WasmEffect.cpp:166-168`).

`latency` is the module declaring **how many frames of delay it introduces**. The host compensates its
dry path by that amount plus **one engine period** (the pipeline is deliberately one block deep):
`moduleFrames + framesPerPeriod` (`WasmEffect.cpp:132-150`). A module that delays by N samples must
declare N, or the effect will be out of alignment with the rest of the graph.

### 4.3 `abi` — declared in the header, **never read by the host**

`WasmAbi.h:32-34` states the contract in a comment ("ABI version exported by every module as the `abi`
global. The host refuses to load modules that export a different value.") and `WasmAbi.h:54` defines the
name constant `abiVersionExport = "abi"` with `version = 1` (`WasmAbi.h:34`).

**No host code reads it.** A `grep` for `abiVersionExport` / `abi::version` across `src/`, `include/`,
`plugins/`, `tests/` and `tools/` finds the declaration in `WasmAbi.h` and nothing else — the symbol is
not referenced by `WasmSandbox`, `WasmWorker`, `WasmEffect` or the plugin. So:

- **an ABI-version mismatch is not detected, refused, or reported**; and
- a module may export `abi` = 0, 999, or omit it entirely, with identical behaviour.

This is a documented-but-unimplemented part of the frozen contract. It is stated here rather than
implied, because a module author who trusts the header comment will believe the host is checking
something it is not. (Whether `abi` is *intended* to be enforced later is **UNKNOWN** — §12.)

## 5. The memory model and how buffers are passed

**Planar, `f32`, in the module's own linear memory.** For a block of `frames` samples:

```
offset 0                                  : input  plane 0   (frames * 4 bytes)
offset 1*planeBytes                       : input  plane 1
...
offset channels*planeBytes                : output plane 0
offset (channels+1)*planeBytes            : output plane 1
...
offset 2*channels*planeBytes              : free — the module's own scratch (latency.wat uses 65536)
```

- `planeBytes = frames * sizeof(float)` (`WasmWorker.cpp:164`).
- Input planes start at byte `0` (`WasmWorker.cpp:165`); output planes start at
  `channels * planeBytes` (`WasmWorker.cpp:166`).
- The host **deinterleaves** the engine's interleaved stereo `SampleFrame` into the input planes before
  the call (`WasmWorker.cpp:190-199`) and **re-interleaves** the output planes back afterwards
  (`WasmWorker.cpp:225-233`).
- The required memory size for one block is therefore
  `2 * channels * frames * sizeof(float)` bytes; the host refuses the block (dry passthrough, counted
  as a drop) if `memorySize() < needed` (`WasmWorker.cpp:168-176`). At the maximum block
  (`frames = 8192`, `WasmWorker.h:56`) and stereo that is 131072 bytes; the store memory limit is
  16 MiB (`WasmAbi.h:72`), so a module also has room for state beside the planes.
- **A module must keep its own state above `2 * channels * maxBlockFrames * sizeof(float) = 131072`
  bytes** (stereo; 65536 mono) or a later, larger block will overwrite it. `modules/wasm/latency.wat`
  puts its delay line at 65536 and is mono, which is exactly at the boundary of the mono case; this
  document records the general rule rather than copying that layout.
- The host does **not** zero the output planes. A module that does not write a sample leaves whatever
  was in memory there. (The host's own dry-passthrough fallback is a separate `Slot::out` buffer, not
  this memory — `WasmWorker.cpp:186-188`, `:236-240`.)
- Memory may grow as usual, but the ABI does **not** settle what happens if it grows *during* a call. The
  `memory` handle is captured once at load (`WasmSandbox.cpp:377-384`) and the data pointer and size are
  re-resolved from that handle on each use (`WasmSandbox.cpp:505-516`, called per block at
  `WasmWorker.cpp:168`/`:171`). Whether `wasmtime_memory_data` returns a fresh pointer after a
  `memory.grow` is a wasmtime-C-API question, not answerable from this tree — so it is **UNKNOWN** (§12), and
  a module must not rely on the host noticing a growth for the next block.

There is no shared memory, no WASI, no host pointer, no `fd`, no filesystem and no network surface: the
linker defines exactly the three functions in §1 (`WasmSandbox.cpp:146-170`) and nothing else.

## 6. How parameters reach the module

- The host offers **16** parameter slots, `abi::maxParams = 16` (`WasmAbi.h:63`). The worker copies
  **all 16** into the sandbox before every block (`WasmWorker.cpp:178-182`), reading them from
  `WasmWorker`'s own array (`WasmWorker.h:163`).
- The module reads a slot by calling `env.host_get_param(index: i32) -> f32`
  (`WasmSandbox.cpp:148-154`, callback `:172-190`).
- **Out-of-range index returns `0.0f`, silently** — the callback returns 0 unless
  `0 <= index < 16` (`WasmSandbox.cpp:183-186`). It never traps.
- **The plugin exposes 8 of the 16.** `WasmEffectControls::paramCount = 8`
  (`WasmEffectControls.h:50-51`); the remaining 8 are always `0.0f` unless something else writes
  `WasmWorker::setParam`. This is a plugin-surface limit, not an ABI limit.
- Parameter values are persisted in the project under the controls node `wasmeffectcontrols`
  (`WasmEffectControls.h:80-83`, `WasmEffectControls.cpp:128-160`), beside the module path.
- **There is no parameter ABI beyond the index.** A module cannot declare a parameter's name, range,
  default, unit or automation behaviour to the host; the host has 16 anonymous floats. Everything a
  module wants beyond that must ride in its own module state.
- **The parameter slots ARE drivable through the control surface, with one qualification that matters.**
  `wasm.set_param {index, value}` writes any of the 16 slots in the **host's own** sandbox and reports the
  slot's previous value; `wasm.get_state` reads all 16 back. What it does not do is reach the **effect
  instance's** sandbox: a `wasm_effect` device's 8 parameter models are project state, and those are reached
  with `plugin.param_get` / `plugin.param_set` on the device's `fx-<n>` id. So the ABI's "16 anonymous
  floats" are addressable, and the plugin's 8-model surface is addressable, but they are two different
  objects and neither command crosses over. See §13.

## 7. How sample rate and block size reach the module

- Both arrive **as arguments to `process()`**, per call: `frames` (i32) and `sample_rate` (f32)
  (`WasmSandbox.cpp:450-458`). There is no global, no import and no setter for either.
- `sample_rate` comes from `Engine::audioEngine()->outputSampleRate()` as a `float`, and is `44100.0f`
  when no engine is present (`WasmEffect.cpp:188-191`; the worker's own default is `44100.0f`,
  `WasmWorker.h:151`).
- The engine's block size is the engine's period; the ABI does not fix it. **`frames` may change
  between calls.** The only bound is `WasmWorker::maxBlockFrames = 8192` (`WasmWorker.h:56`): a block
  larger than that is dropped by `submit()` (`WasmWorker.cpp:264-269`) and the effect leaves the audio
  untouched above that size (`WasmEffect.cpp:181-186`).
- **A sample-rate change re-instantiates the module.** When the rate on a block differs from the rate
  the current instance was created for, the worker reloads the module file and instantiates it afresh
  (`WasmWorker.cpp:120-129`, `:242-259`) — meaning **all module state is lost**, and if the reload fails
  the module goes to `Failed` and the effect stays dry. A module must therefore be able to start
  correctly from a cold memory image at any rate; it cannot rely on continuity across a rate change.
  The count is exposed as `WasmWorker::reinstantiatedModules()` (`WasmWorker.h:93-97`).

## 8. Return and error conventions

- **The `i32` returned by `process()` is recorded and then ignored.** `callProcess` stores it in
  `CallResult::returnValue` (`WasmSandbox.cpp:462-465`); the only readers of `returnValue` in the tree
  are the test sources. `WasmWorker::processSlot` branches on `result.ok()` — the *wasmtime call
  status* — not on the value (`WasmWorker.cpp:208-211`). **A module cannot signal an error by returning
  a non-zero value**: the block is still treated as good and the output planes are still read back.
  `WasmAbi.h:40` documents the convention `0 = ok`; the host does not implement a check for it.
- The three outcomes the host actually distinguishes are `CallResult::Status` (`WasmSandbox.h:41-56`):
  - `Ok` — returned normally;
  - `Trap` — the module trapped (`unreachable`, out-of-bounds memory, out of fuel, …); `trapCode`
    holds the wasmtime `wasmtime_trap_code_t` value (`WasmSandbox.cpp:288-292`, `takeTrap` `:57-76`);
  - `Error` — the embedder rejected the call: no `process` export, a signature mismatch, or a
    fuel-metering failure (`WasmSandbox.cpp:274-287`).
- `CallResult` also carries `fuelConsumed` for the call (`WasmSandbox.cpp:297-308`) and a `message`
  string from wasmtime.
- A failed call is **quarantined**, never retried, never propagated as an exception:
  `WasmWorker` counts it, copies the message to `lastError()`, passes the block through dry and moves the
  worker to `State::Corrupted` (`WasmWorker.cpp:214-223`). `isCorrupted()` then reports it
  (`WasmWorker.h:84`). The host process stays alive; that is the sandbox's whole purpose
  (`docs/specs/SPEC-wasm-sandbox.md` §1).
- **There is no channel for the module to report a typed error, a warning or a status to the host**
  other than `host_log` (§10) and a trap. In particular there is no error string passed back from
  `process`, and no "bypass me" return code.

## 9. Fuel and traps

- Every call is metered. The engine is created with fuel consumption enabled
  (`WasmSandbox.cpp:104`) and `callWithFuel` sets a **fresh budget before each call**
  (`WasmSandbox.cpp:274-279`). The default budget is `abi::defaultFuelBudget = 1_000_000`
  (`WasmAbi.h:66-69`), configurable through `setFuelBudget()` (`WasmSandbox.h:112`, `:587-595`).
- Exhausting it traps with `WASMTIME_TRAP_CODE_OUT_OF_FUEL` (11) — the `spin` fixture asserts exactly
  that (`modules/wasm/spin.wat`). Out-of-bounds access traps with
  `WASMTIME_TRAP_CODE_MEMORY_OUT_OF_BOUNDS` (1) (`modules/wasm/oob.wat`). Both are *contained*
  outcomes: the block goes dry and the module is quarantined for the rest of the session.
- **The budget is per `process()` call, not per block.** With `channels == 2` a block costs up to two
  budgets, because `process` is entered twice (§3). A module whose cost is near the limit may pass at
  mono and trap at stereo.

## 10. Host imports in detail

### `env.host_get_param(index: i32) -> f32`
`WasmSandbox.cpp:148-154` (type `wasm_functype_new_1_1(i32, f32)`), callback `:172-190`.
Out-of-range → `0.0f`. Never traps. Pure, allocation-free.

### `env.host_log(ptr: i32, len: i32) -> ()`
`WasmSandbox.cpp:156-161` (type `wasm_functype_new_2_0(i32, i32)`), callback `:192-232`.
`ptr`/`len` are **offsets/lengths into the module's own linear memory**. Behaviour, all source-derived:

- `len <= 0` or `ptr < 0` → **no-op**, no trap (`WasmSandbox.cpp:203-206`);
- `ptr + len` beyond the memory → **no-op**, no trap (`WasmSandbox.cpp:222-225`);
- otherwise copies `min(len, maxLogBytes - 1)` = **at most 4095 bytes** into the host's buffer and
  NUL-terminates (`WasmSandbox.cpp:226-230`, `abi::maxLogBytes = 4096` at `WasmAbi.h:64`);
- the text is retrieved once by `takeLog()`, which then clears it ("text passed by the most recent
  call", `WasmSandbox.h:104-105`, `WasmSandbox.cpp:561-570`).

A module cannot flood the host through this import, by construction.

### `env.host_get_transport_state() -> i32`
`WasmSandbox.cpp:163-169` (type `wasm_functype_new_0_1(i32)`), callback `:234-246`. Returns the host's
current transport state: `0` = stopped, `1` = playing (`WasmAbi.h:78-80`). Set by the host through
`setTransportState()` (`WasmSandbox.h:102`); the worker mirrors `WasmWorker`'s own atomic before each
block (`WasmWorker.cpp:183-184`).

## 11. Store limits, and where the code differs from the draft spec

Enforced at store creation (`WasmSandbox.cpp:128-131`) from `WasmAbi.h:71-76`:

| Limit | Value |
|---|---|
| linear memory | 16 MiB (`storeMemoryLimitBytes`) |
| table elements | 10 000 (`storeTableElementLimit`) |
| instances | 1 (`storeInstanceLimit`) |
| tables | 1 (`storeTableLimit`) |
| memories | 1 (`storeMemoryCountLimit`) |

The instance limit of 1 is why a module reload deletes and rebuilds the whole store rather than
instantiating a second time (`WasmSandbox.cpp:111-133`).

**Differences from `docs/specs/SPEC-wasm-sandbox.md` §3** (the draft is a pinned snapshot; the code is
what ships):

1. The draft says "the module declares its I/O channel count and latency at instantiation via exported
   globals" — the code does read **globals** (`WasmSandbox.cpp:254-268`), matching. It adds the 1..2
   clamp and the `i32`-only rule, which the draft does not state.
2. The draft's §5 mentions `wasi.h` being "linked"; **the tree does not include or link WASI at all**
   (`grep` finds no `wasi` reference in `src/wasm/`), and no WASI surface is wired. The non-goal
   ("no WASI filesystem/network") holds; the parenthetical is stale.
3. The draft's §3 return convention ("0 = ok") is **not enforced** (§8).
4. The draft does not mention `host_log`'s 4095-byte truncation or its no-trap range behaviour.

## 12. UNKNOWN — not determinable from this tree

Stated rather than guessed. Each is a thing a module author might reasonably expect to be defined:

1. **Whether `abi` is meant to be enforced.** The constant exists and nothing reads it (§4.3). There is
   no issue, comment or test in this tree stating the intent.
2. **Whether `wasmtime_memory_data` returns a valid fresh pointer after a module calls `memory.grow`
   inside `process()`.** The host captures the `memory` handle at load and re-queries the pointer per
   call; whether wasmtime keeps that handle valid across growth is a wasmtime-C-API question, not
   answerable from this tree.
3. **What a module should do when it cannot process a block** (e.g. an unsupported sample rate). There
   is no bypass return code and no error channel (§8); the only tool is a trap, which quarantines the
   module permanently. No source states a recommended practice.
4. **Whether `channels` and `latency` may be declared as mutable globals and changed at runtime.** The
   host reads them once at load and on re-instantiation only
   (`WasmSandbox.cpp:403-406`, `WasmWorker.cpp:106`, `:254`); a module that mutates them later changes
   nothing, but the source does not say whether that is forbidden or merely ineffective.
5. **Any ABI for module-declared parameter metadata** (name, range, default, unit). None exists in the
   source; whether one is planned is not stated.
6. **The behaviour of a module exporting a second function named `process` under another index**,
   multiple memories, or shared memory: not exercised anywhere and not constrained by the source beyond
   the store limits in §11.
7. **Whether the F32 result of `host_get_param` is required to be finite.** No validation exists
   (`WasmSandbox.cpp:187-188`); NaN/Inf are passed through.

## 13. The example, the conformance suite, and the recorded limit

- **The example module** is `tests/data/wasm-effect-abi/softclip.wat`. Its header states it was written
  from *this document and nothing else*. It implements §3's `process`, §2's `memory`, §4.1's `channels`
  and §6's `host_get_param` — the four parts of the ABI an effect must get right — and no other export.
- **The conformance suite** is `tests/src/wasm/WasmAbiConformanceTest.cpp`, registered in
  `tests/CMakeLists.txt` inside the same `if(WANT_WASM)` block that registers `WasmSandboxTest`. It
  asserts each documented property in §1–§10 against modules assembled at run time from `.wat` source,
  including the negative cases (missing `memory`, out-of-range params, a trap, fuel exhaustion, the
  ignored return value).
- **Neither has been executed.** `WANT_WASM` is `OFF` in every build made on the reference box because
  the wasmtime C API is absent, so the test binary is not built and the `.wat` is not assembled. The
  conformance suite is *source a build with wasmtime can run*; it is not evidence that the ABI works.
  What would convert it into evidence: provision the wasmtime C API and configure with `WANT_WASM=ON` —
  `scripts/fetch-wasmtime.sh` fetches and checksums the pinned prebuilt C API (v48.0.1),
  `cmake/modules/FindWasmtime.cmake` finds it via `-DWASMTIME_ROOT=<prefix>` or
  `$ENV{WASMTIME_ROOT}` or `<source>/third_party/wasmtime`, and
  `cmake -DWANT_WASM=ON -DWASMTIME_ROOT=…` then builds `wasm-wat2wasm` and the test.
- **The scope contract consequence.** Under the 0.3.0 four-part contract (`V0.3-ALPHA-PLAN.md` §1), this
  item satisfies the *documentation* and *proof-source* legs only. It has **no control-surface command
  group**, so it is **not drivable through `--control-socket` in any build**, and it is absent from the
  binaries. `docs/KNOWN-LIMITATIONS.md` carries the one-line statement of both; the release notes carry
  the section.
