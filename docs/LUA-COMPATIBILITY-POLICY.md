# Lua scripting API — version and compatibility policy

> **Status:** in force for the v0 API as shipped in the alpha (`Zene Studio`).
> **Authoritative for:** what a script may rely on, what may change, and how the
> version is produced. Implementation: `include/ScriptApiVersion.h`,
> `src/core/ScriptApiVersion.cpp`, `ScriptEngine::parseVersionHeader` /
> `isCompatibleVersion`, and the `lmms.version` / `lmms.apiVersion` bindings.
> **NOT authoritative for:** what the API contains (that is
> `docs/specs/SPEC-lua-api-v0.md`) or the release schedule.

## 1. Where the version comes from

One place, and nothing else may spell it out:

| Element | Location |
|---|---|
| The numbers | `CMakeLists.txt` — `ZENE_LUA_API_VERSION_MAJOR/MINOR/PATCH` |
| Handed to the engine | `src/CMakeLists.txt` — `TARGET_COMPILE_DEFINITIONS(lmmsobjs …)` |
| Handed to the tests | `tests/CMakeLists.txt` — same variables, same values |
| Read by code | `include/ScriptApiVersion.h` (macros + `lmms::ScriptApi` accessors) |
| Reported to scripts | `lmms.version()`, `lmms.apiVersion()`, `lmms.apiVersionMajor()`, `lmms.apiVersionMinor()`, `lmms.apiStability()` |
| Enforced on a script | `ScriptEngine::isCompatibleVersion()` (the `--! lmms-api` gate) |

A translation unit that is not passed the build definitions still compiles —
`ScriptApiVersion.h` carries fallbacks — but the version tests compare what the
engine reports against what the *test* binary was compiled with (both from the
same CMake variable), so a version hardcoded in either place fails
`ScriptStabilisationTest`.

Current value: **0.2.0**, reported as `0.2` in the major.minor form a script
declares, with stability `v0-unstable`. It moved from 0.1.0 when the DAW-control
binding (mixer channel, effect chain, effect; `zene.mixer()`, `zene.apiSurface()`)
was added: an addition bumps MINOR, which is the whole of section 2's first rule.

## 1a. What enforces this document

Each clause below names the artefact that fails when it is broken. "Described" is
not "enforced", so nothing here is left to review:

| Clause | Enforced by |
|---|---|
| The numbers come from one place | `ScriptStabilisationTest::versionEntryPointReportsTheBuiltVersion` compares what the engine reports against what the test binary was compiled with (both from `CMakeLists.txt`) |
| A script declares what it needs | `ScriptEngine::isCompatibleVersion()` — the `--! zene-api` / `--! lmms-api` header gate (section 3.1) |
| No name is removed within a major version | `tests/lua-api-surface.py`, registered as the ctest **`LuaApiSurface`**: it derives the whole surface (every `zene.*` function and every class member) from the registration sources and fails on drift against the committed `docs/LUA-API-SURFACE.md`, naming each entry as `REMOVED (breaking)` |
| An addition bumps MINOR | the same test: an entry the manifest lacks is reported as `ADDED (additive)` and the recorded `api <major>.<minor>` line is compared with `CMakeLists.txt`, so a surface change without a version change fails |
| A script can ask what it is running against | `zene.apiSurface()` (version, stability, and the live `zene` surface), `zene.version()`, `zene.apiVersion()`, `zene.apiVersionMinor()` |

`docs/LUA-API-SURFACE.md` is a DERIVED file: `python3 tests/lua-api-surface.py
--write` produces it and never hand-edit it. When `LuaApiSurface` fails, the
message says which of the two bumps (MINOR for an addition, MAJOR for a removal)
the change needs.

## 2. Current state: v0, no stability promise

**There is no API stability guarantee at v0.** This is stated first because it is
the only honest summary: the API is implemented, exercised by 21 tests and four
example scripts, and it is *not* yet frozen.

Specifically, today:

* Any part of the v0 surface — a class, a method, its argument order, its
  clamping, its error behaviour — may change.
* A change that could break a working script is a **major** change and must bump
  `ZENE_LUA_API_VERSION_MAJOR`, which makes every existing script fail the
  header gate with a clear message rather than misbehave silently.
* A change that only adds is a **minor** change and must bump
  `ZENE_LUA_API_VERSION_MINOR`.
* Until a v1 exists, a release that adds to the API makes *older builds* reject
  *newer scripts* (the gate refuses `minor > build minor`) while the same
  scripts keep working on the new build. That asymmetry is deliberate: a script
  that declares what it needs is never run by an engine that cannot provide it.

## 3. What a script can rely on today

These are implemented, not aspirational. Each is proved by a test in
`tests/src/core/`.

1. **The version gate.** A script has a required
   `--! lmms-api <major>.<minor>` header (first 4 KB of the file). A missing,
   malformed, newer-major or newer-minor declaration is refused with a message
   naming the version this build implements, and the script body **does not
   run**. (`ScriptEngineTest::testVersionHeader`,
   `ScriptStabilisationTest::versionHeaderGateFollowsTheBuild`.)
2. **The version surface.** `lmms.version()` returns the same major.minor the
   gate enforces; `lmms.apiVersion()` the full build version;
   `lmms.apiVersionMajor()`/`apiVersionMinor()` the numbers;
   `lmms.apiStability()` the stability string.
   (`ScriptStabilisationTest::versionEntryPointReportsTheBuiltVersion`.)
3. **Threading.** A script always runs on the dedicated worker thread
   (`lmms-lua-script-worker`), never on the audio thread or the caller's thread.
   Engine state is mutated only on the apply side.
   (`ScriptEngineTest::testWorkerThreadNotAudioThread`,
   `testApplyRunsOnApplySideThread`.)
4. **Bounded execution.** An instruction budget (default 5,000,000 instructions)
   aborts a runaway script; `debug.sethook(n)` may lower a script's own budget
   but never raise or disable it. The script-to-apply command queue is bounded
   (1024) and drops on overflow instead of blocking, counting the drops.
   (`ScriptEngineTest::testSandboxInstructionBudget`,
   `testSandboxHookCannotRaiseBudget`, `testCommandQueueOverflowDropsWithoutBlocking`.)
5. **The sandbox.** `os`, `io`, `package`, `require`, `dofile`, `loadfile`,
   `load`, `loadstring`, `module` and `collectgarbage` are absent; `debug` is a
   restricted table with only `sethook` and `traceback`; `string`, `table`,
   `math`, `utf8` and the base library are present. File access is limited to the
   project directory and refuses symlink and `..` escapes.
   (`ScriptEngineTest::testSandboxStdlibRemoved`, `testSandboxFileAccess`,
   `ScriptBindingsTest::projectFileWrapper`.)
6. **A failed script does not take the DAW down.** A Lua error (including a call
   to a name that does not exist) is reported as `RunResult::ScriptError`, the
   DAW survives, and the engine is usable for the next run.
   (`ScriptStabilisationTest::luaErrorIsSurvivedAndTheEngineStaysUsable`.)
7. **The console.** `print()` and `lmms.log():info/warn/error/write()` reach the
   DAW's Qt logging path (prefixed `lua:`), as well as the capture buffer
   `ScriptEngine::takeLogMessages()` reads. A script's fatal error is reported
   there too. (`ScriptStabilisationTest::consoleOutputReachesTheLoggingPath`.)
8. **The DAW-control surface** (added 2026-09-15, API 0.2). `zene.mixer()` returns
   the live mixer; a channel's gain, mute, solo and name, its effect chain, and a
   device's parameters and enabled state are readable and writable from a script,
   under the `ch-<n>` / `fx-<n>` ids the control surface uses. Every write is
   queued to the apply side and journalled where the engine journals the manual
   path, so one `control.undo` reverses it. (`ScriptDawBindingTest`; what is
   deliberately NOT bound is listed in `docs/LUA-API-STABILISATION.md` §5.)

## 4. What is explicitly NOT promised

* **No stability of the surface.** See §2. What §1a adds is that a change is
  RECORDED and caught, not that the surface is frozen.
* **No script-defined devices.** A script cannot yet define a device or effect
  the engine runs. Design and touch points:
  `docs/LUA-SCRIPT-DEVICES-DESIGN.md` (not implemented).
* **No audio-thread access and no audio-buffer DSP in Lua.** v0 is a command
  queue; audio-rate DSP is the WASM track (spec §1).
* **No event- or idle-triggered scripts.** A script runs when a host runs it —
  the menu action, `--run-script`, or the embedding application. Auto-run on
  transport events is deferred (spec §11, OQ-1).
* **No GUI scripting** and no arbitrary C bindings (spec §1, §5).
* **No package format or discovery yet.** A script is a file you run by path.
* **No in-GUI console pane.** Output goes to the process log; a dock widget is
  future work.
* **No on-disk deprecation ledger.** The spec's intent — v0 fields are not
  removed within v0 — is intent, not implementation; §2 governs.

## 5. Bumping the version

1. Change `ZENE_LUA_API_VERSION_{MAJOR,MINOR,PATCH}` in `CMakeLists.txt`, and
   nothing else. Never edit a version string at a call site.
2. Additive change → bump MINOR. Breaking change → bump MAJOR.
3. Update this document's "Current value" line and any example script that
   declares a version, and re-run:
   `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R Script`

The tests fail if the build and the reported version disagree, so a forgotten
bump is caught by `ctest`, not by a user.

## 6. How a script should behave today

* Declare the *lowest* version that provides what you need
  (`--! lmms-api 0.1`), not the newest you have seen.
* Feature-detect with `lmms.apiVersionMajor()`/`apiVersionMinor()` before
  calling anything that may not exist, and fail loudly:
  `assert(lmms.apiVersionMinor() >= 1, "this script needs API 0.1")`.
* Expect to re-test on every minor release until a v1 exists, and state the API
  version you tested against in the script header comment.
