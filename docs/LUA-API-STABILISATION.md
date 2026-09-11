# Lua scripting API stabilisation — task #613

> **Branch:** `post-alpha/lua-api` (base `0c23587d2`), worktree
> `projects/lmms-fl-research/zene-pa-lua`.
> **Scope as briefed:** version + compatibility policy, script-defined devices
> "if achievable", a console, package format + generated docs "if time allows" —
> without breaking the existing scripts.
> **Verdict:** items 1 (versioning + policy), 3 (console) and part of 4 (package
> format) are landed and proved. Item 2 (script-defined devices) is **not
> attempted**, with the design and its touch points written down
> (`docs/LUA-SCRIPT-DEVICES-DESIGN.md`); the rest of item 4 is not attempted
> either (§6). No existing script or test changed behaviour.

---

## 1. What existed before this work (established before designing anything)

*(Cited line numbers are as of this branch's HEAD; the symbol name is the stable
reference if the tree moves.)*

The Lua API was real and working: Lua 5.4 vendored, LuaBridge 2.10, a sandbox, a
bounded command queue and an instruction budget, four example scripts, two test
binaries.

| Piece | Where | What it was |
|---|---|---|
| Engine + worker thread | `include/ScriptEngine.h:140-262`, `src/core/ScriptEngine.cpp:85-132` | A `ScriptEngine` singleton runs a script on a dedicated worker thread; a **fresh `lua_State` per invocation** (`src/core/ScriptEngine.cpp:96` `luaL_newstate`, `:132` `lua_close`), so nothing survives a run |
| Instruction budget | `src/core/ScriptEngine.cpp:111-132` (`instructionHook`), `:96-110`; header default `include/ScriptEngine.h:254` | Count hook, default 5,000,000 instructions; a script may only *lower* its budget (`restrictedSetHook`) |
| Command enum + queue | `include/ScriptEngine.h:62-92` (`ScriptCommand`), `:51` (capacity 1024), `src/core/ScriptEngine.cpp:438` (`applyCommand`) | The single engine-mutation path; failed pushes are dropped and counted |
| Bindings surface | `include/ScriptBindings.h` (406 lines), `src/core/ScriptBindings.cpp:276` (`registerAll`) | 14 Lua classes + the `lmms` namespace |
| Tests (the contract that must not break) | `tests/src/core/ScriptEngineTest.cpp` (592), `tests/src/core/ScriptBindingsTest.cpp` (741) | Header gate, sandbox, budget, file sandbox, apply-side threading, queue overflow, all bindings |
| Spec | `specs/SPEC-lua-api-v0.md` | §6 promises the `--! lmms-api` header gate; §1 excludes audio-thread scripting; §11 defers idle-triggered scripts (OQ-1) |

**The three gaps, with evidence:**

1. **No version constant.** `lmms.version()` existed but returned a hardcoded
   `std::string("0.1")` (`src/core/ScriptBindings.cpp:334` before this change);
   the gate hardcoded `major != 0` / `minor > 1`
   (`src/core/ScriptEngine.cpp`, `isCompatibleVersion`). Nothing connected the
   two, and neither came from the build. A policy could not be written honestly
   against a version that has three spellings.
2. **No console.** `print()` and `lmms.log():*` went to `logMessage()`
   (`src/core/ScriptEngine.cpp:687`), which appends to `m_logMessages` and
   `emit logged`. The buffer is only read by `takeLogMessages()`
   — called by the harness and by `MainWindow::runScript()`, which printed
   *after* the script had finished. Nothing streamed, and a headless run only
   saw output because `main.cpp` printed the buffer at the end.
3. **No way to define a device**, and (as shown in the design doc) no honest
   small way to add one: there is no persistent Lua state and no trigger —
   `ScriptEngine::audioThreadTick()` (`src/core/ScriptEngine.cpp:571`) **has no
   caller in the tree** (`grep -rn audioThreadTick src include` → definition and
   declaration only), and the only production caller of `processCommands()` is
   the headless action (`src/core/main.cpp:799`).

## 2. What landed

### Scope 1 — Version and compatibility policy (landed)

* `include/ScriptApiVersion.h` + `src/core/ScriptApiVersion.cpp`: the version is
  read from the build (`ZENE_LUA_API_VERSION_MAJOR/MINOR/PATCH`, set once in
  `CMakeLists.txt`, passed to `lmmsobjs` by `src/CMakeLists.txt`), never spelled
  out at a call site.
* Advisory functions: `lmms.version()` (major.minor, unchanged value `0.1`),
  `lmms.apiVersion()` (`0.1.0`), `lmms.apiVersionMajor()`,
  `lmms.apiVersionMinor()`, `lmms.apiStability()` (`v0-unstable`).
* The gate derives from the same numbers: `ScriptEngine::isCompatibleVersion()`
  compares against `ScriptApi::major()`/`minor()` and names the engine's version
  in its rejection message.
* The policy is written down in **`docs/LUA-COMPATIBILITY-POLICY.md`** (§3 below).

### Scope 2 — Script-defined devices (**not attempted**; design written)

`docs/LUA-SCRIPT-DEVICES-DESIGN.md` explains why, with the file:line evidence:
a device needs a persistent `lua_State` (§2.1), a trigger the architecture does
not have (§2.2), and it inherits the deferred event-system decision OQ-1; the
one half that *is* already built is the output path (`ScriptCommand` →
`applyCommand`). The doc proposes the smallest shape that keeps the realtime rule
and the single sandbox, and lists the exact touch points. **No device was
half-wired and no placeholder Lua API was added** — the v0 surface is
deliberately unchanged by this work.

### Scope 3 — Console (landed)

* `include/ScriptConsole.h` + `src/core/ScriptConsole.cpp`: every captured line
  is streamed onto the DAW's existing Qt message-log path, prefixed `lua:`, so
  it is visible while the script runs instead of only afterwards.
  `ScriptConsole::setEnabled()` exists for a host that owns stdout itself.
* `ScriptEngine::logMessage()` (worker thread, or the apply side for the MIDI-out
  mirror) now streams as well as captures; a **fatal script error is streamed
  too** (`ScriptEngine::reportRunResult()`), because a console that goes silent
  on failure is worse than no console.
* `src/gui/MainWindow.cpp` (`runScript`) no longer prints the captured lines a
  second time; `src/core/main.cpp` (`--run-script`) turns the console off so the
  documented stdout contract is preserved exactly.
* `include/ScriptLuaQtTypes.h`, `src/core/ScriptCommandQueue.cpp`: the LuaBridge
  Qt `Stack` specialisations and the SPSC queue moved out of
  `ScriptBindings.cpp` / `ScriptEngine.cpp`, which are grandfathered at their
  current size under the per-file length ratchet and therefore must not grow
  (they end at 1169 and 873 lines against 1217 and 910).

### Scope 4 — Package format (landed, host-side) and generated docs (not done)

* `include/ScriptPackage.h` + `src/core/ScriptPackage.cpp`:
  `ScriptPackages::isValidName()`, `parseManifest()`, `scan()`.
  Convention: a package is `<scripts root>/<name>/package.lua`, name
  `^[a-z][a-z0-9_-]*$`, optional `--! lmms-package <name> <version>` manifest;
  the entry is an ordinary v0 script and cannot bypass the gate.
* `docs/LUA-PACKAGE-FORMAT.md` documents the format and, in its §5, what is
  missing (no user scripts directory, no UI/CLI listing, no install tooling).
* **Host-side only, on purpose:** no Lua binding was added for packages, because
  the point of this task is to *freeze* the v0 surface, not to grow it while it
  is being pinned down.
* **Generated reference of the exposed bindings: not delivered.** The namespace
  half *is* enumerable from Lua and I proved it by running the built binary
  (every `lmms.*` function shows up in `pairs(lmms)`), but the **class** half is
  not enumerable: `lmms.Note` is `nil` (class tables are not in the namespace)
  and `getmetatable(lmms.song())` returns a **boolean** — LuaBridge protects the
  metatable, so a script cannot walk the methods. A generated class reference
  therefore needs a registration-time registry in C++ threaded through
  `ScriptBindings::registerAll` (`src/core/ScriptBindings.cpp:276`), which is a
  real change to the binding layer and was out of the remaining budget. Shipping
  a hand-written "reference" and calling it generated would be a lie, so it was
  not shipped.

## 3. The policy text, and where it lives

`docs/LUA-COMPATIBILITY-POLICY.md`. Its substance, honest about the current
state:

* **There is no API stability guarantee at v0.** "Any part of the v0 surface —
  a class, a method, its argument order, its clamping, its error behaviour — may
  change." A breaking change must bump MAJOR (every existing script then fails
  the header gate loudly instead of misbehaving); an additive change must bump
  MINOR.
* **What a script can rely on today** (each item named with its test): the
  version gate, the version surface, the worker-thread/apply-side threading
  split, the bounded instruction budget and queue, the sandbox and file
  restrictions, that a failing script does not take the DAW down, and the
  console.
* **What is explicitly not promised:** surface stability, script-defined
  devices, audio-thread access or audio-buffer DSP, event/idle triggers, GUI
  scripting, package discovery, an in-GUI console pane, and an on-disk
  deprecation ledger (the spec's "v0 fields are never removed" is intent, not
  implementation — the policy says so).
* **How to bump:** one file (`CMakeLists.txt`), one edit; the tests fail if the
  build and the reported version disagree.

## 4. Proofs

All commands run in the worktree; exit codes measured unpiped.

**A. Build (CI's `linux-x86_64` flags, `-DUSE_WERROR=ON`, plus `-DWANT_QT6=ON`
as the documented deviation for a box with no Qt5 dev files):**

```sh
JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4
# configure EXIT=0   (log: build/configure.log)
# build     EXIT=0   (log: build/build.log)
# ctest     EXIT=0   (log: build/ctest.log)
# ctest totals: 100% tests passed, 0 tests failed out of 26
# linux-x86_64: REPRODUCED - run: configure OK, build OK, ctest OK
# deviation: Qt5 development files not found: added -DWANT_QT6=ON (CI's runner
#            installs qtbase5-dev; this box has Qt6 only)
# local-ci: overall exit=0
```

**B. The two existing Lua test binaries still run, and the new one passes:**

```sh
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R "Script" --output-on-failure
# 1/3 Test #18: ScriptBindingsTest ............... Passed
# 2/3 Test #19: ScriptEngineTest ................. Passed
# 3/3 Test #20: ScriptStabilisationTest .......... Passed
# 100% tests passed, 0 tests failed out of 3     (CTEST EXIT=0)
```

**C. The four example scripts still run headless, with exit 0 and unchanged
output — and no duplicated lines, because `--run-script` owns stdout:**

```sh
QT_QPA_PLATFORM=offscreen ./build/lmms --run-script data/scripts/hello.lua
# [info] Hello from Lua Lua 5.4
# [info] LMMS Lua API 0.1
# hello.lua finished
# exit 0; stderr contains 0 lines matching "lua:" (no double printing)
QT_QPA_PLATFORM=offscreen ./build/lmms --run-script data/scripts/create-pattern.lua
# [info] create-pattern: 16 notes in 'Hi-Hat 16' on 'Hi-Hat'     exit 0
# generative-bass.lua exit 0; midi-router.lua exit 0
```

**D. The version really is a build-time define in both compilation units**
(this is what makes the version test able to fail):

```sh
grep -o '\-DZENE_LUA_API_VERSION_[A-Z]*=[0-9]*' build/src/CMakeFiles/lmmsobjs.dir/flags.make
# -DZENE_LUA_API_VERSION_MAJOR=0 / MINOR=1 / PATCH=0
grep -o '\-DZENE_LUA_API_VERSION_[A-Z]*=[0-9]*' build/tests/CMakeFiles/ScriptStabilisationTest.dir/flags.make
# same three defines, from the same CMake variables
```

**E. New behaviour, proved headlessly** (`tests/src/core/ScriptStabilisationTest.cpp`):

| Slot | Proves | Shape of the proof |
|---|---|---|
| `versionEntryPointReportsTheBuiltVersion` | the version entry point returns the built value | C++ side compares `lmms::ScriptApi::*` against the test TU's own `ZENE_LUA_API_*`; Lua side asserts `lmms.version()`, `lmms.apiVersion()`, `apiVersionMajor/Minor` and that the value is `major.minor` |
| `versionHeaderGateFollowsTheBuild` | the gate is built from the same numbers | runs a script declaring the build's version (Ok); one minor ahead → `VersionError` naming the engine's version, body **not** run; one major ahead → `VersionError` |
| `consoleOutputReachesTheLoggingPath` | output reaches the **logging path**, not merely a function call | installs a `qInstallMessageHandler` capture, runs a script with `print()`/`lmms.log():info|warn`, asserts the handler saw `lua: console-proof-line` etc. exactly once, and that the capture buffer still holds the raw lines |
| `consoleStreamingIsSwitchable` | the stream is a real sink | with the console off the handler sees nothing while the buffer still has the line |
| `luaErrorIsSurvivedAndTheEngineStaysUsable` | the negative control | `error()`, an undefined namespace entry point and an undefined class method each return `ScriptError` with the name in the message; the failures appear on the console; then a fresh script runs Ok and a queued command is still applied by the apply thread |
| `failedScriptOutputStopsAtTheFailure` | output before a failure is kept, and the run is reported failed | |
| `packageNamesAreValidated`, `packagesAreDiscoveredAndTheirEntryScriptsRun`, `manifestParsingIsStrict` | the package format | a temp root with two valid packages, an invalid name, an entry-less directory and a loose script: exactly two discovered, sorted; the entry runs through `runFile`; a `future` package is refused by the gate; `..`, `Arp`, `2arp`, spaces and >64 chars are rejected |

**F. The new test is load-bearing (red/green, measured).** Removing
`ScriptConsole::streamLine(message)` from `ScriptEngine::logMessage()`,
rebuilding and re-running the binary:

```
FAIL!  : ScriptStabilisationTest::consoleOutputReachesTheLoggingPath()
         'streamed.filter("lua: console-proof-line").size() == 1' returned FALSE
FAIL!  : ScriptStabilisationTest::luaErrorIsSurvivedAndTheEngineStaysUsable()
         '!streamed.filter("deliberate failure").isEmpty()' returned FALSE
Totals: 9 passed, 2 failed, 0 skipped        (ctest exit 8)
```

Exactly the two console-dependent slots go red and every other slot stays green,
so the failure is caused by the removed line and not by collateral. Restoring the
call (`git checkout -- src/core/ScriptEngine.cpp`), rebuilding and re-running
gives `3/3 Passed, 100% tests passed, 0 failed out of 3` with ctest exit 0. The
version test is load-bearing the same way: it compares two translation units,
so it fails if either spells the version out instead of using the build define —
which is the drift it exists to catch.

**G. The repository's quality gates, run on the branch** (all read-only
`--check`/ratchet invocations; no baseline was rewritten):

```sh
bash tests/no-upstream-regression-gate.sh   # Gate 6 -> EXIT=0
# PASS: every change to upstream-inherited code since 01148947e is declared
#       (32 file(s) in the ledger)
bash tests/file-length-gate.sh --check      # Gate 7 -> EXIT=0
# PASS (check mode: no regressions) - ScriptEngine.cpp 880 (< 910 baseline),
#       ScriptBindings.cpp 1169 (< 1217 baseline)
bash tests/no-tautology-gate.sh             # Gate 3 -> EXIT=0
bash tests/complexity-gate.sh --check       # Gate 4 -> EXIT=0
# PASS (check mode: no regressions)
```

Gate 4 earned its keep: the first version of the console change put the
"report a failed run" check inline in `ScriptEngine::runOnWorker()` and the gate
caught the result — `REGRESSION: new function over target:
lmms::ScriptEngine::runOnWorker@… (CCN 11)` against a target of 10. The fix is
the honest one rather than a baseline re-anchor: the check moved into
`ScriptEngine::reportRunResult(RunResult, const QString&)`, a private helper with
CCN 3, and the run path is back under the target (`--check` now exits 0).

## 5. Threading statement for the new entry points

| New entry point | Thread | Realtime safety |
|---|---|---|
| `ScriptConsole::streamLine()` | script worker thread (`print`, `lmms.log()`) and the apply side (fatal-error log, MIDI-out mirror) | one relaxed-atomic read; no lock, no allocation that the audio thread can observe; never called from an audio-thread path |
| `ScriptConsole::setEnabled()` / `enabled()` | any (called once before a run by the host) | relaxed atomic |
| `ScriptEngine::reportRunResult()` | the run's calling thread (the apply side) | pure bookkeeping plus `logMessage()` |
| `ScriptApi::version()/fullVersion()/major()/minor()/stability()` | any | pure, no state |
| `ScriptPackages::scan()/parseManifest()/isValidName()` | host side, outside any run | file I/O, so deliberately **not** reachable from a script (no new Lua binding) |
| `ScriptEngine::isCompatibleVersion()` change | unchanged (caller of `runFile`) | no new work on any hot path |

No new path was added around the instruction budget or the command queue, and
the audio thread's only touch point is still the lock-free
`ScriptEngine::audioThreadTick()`.

## 6. What was NOT done (explicit)

1. **Script-defined devices** — not attempted; design + touch points in
   `docs/LUA-SCRIPT-DEVICES-DESIGN.md` (§2 of this report). This is scope item 2.
2. **A generated reference of the exposed bindings** — not delivered; the class
   surface is not enumerable from Lua (`lmms.Note` is `nil`; `getmetatable()`
   returns a protected boolean), and a C++ registration-time registry was out of
   budget. The namespace half is enumerable — verified — but shipping only that
   would be a reference that omits every method, so it was left out rather than
   shipped half.
3. **The rest of scope 4**: no user scripts directory (`ConfigManager` has none),
   no UI or CLI package listing, no install/upgrade tooling, no signature or
   trust story (`docs/LUA-PACKAGE-FORMAT.md` §5).
4. **No new Lua binding at all.** The v0 surface is unchanged except for the four
   additive version functions; that is what "stabilise" means here.
5. **No in-GUI console pane** (no log dock). Output goes to the process log.
6. **No `zene.version()` alias namespace.** The brief suggested a
   `zene.version()`-style call; the tree's Lua surface is `lmms.*` and adding a
   second name for the same thing is exactly the ambiguity a stabilisation pass
   should remove, so the version entry points stayed under `lmms.*`.
7. **Not verified on Windows/macOS/mingw.** Only the Linux job is reproducible on
   this box (no Qt5 dev files, no cross toolchains); `tools/local-ci.sh` prints
   the other six jobs as `NOT-REPRODUCIBLE-HERE`. The changes are portable C++
   and Qt, but that is an expectation, not a measurement.
8. **No push, no PR, no issue, no remote touched** — local commits only.

## 7. Files

**New:** `include/ScriptApiVersion.h`, `include/ScriptConsole.h`,
`include/ScriptLuaQtTypes.h`, `include/ScriptPackage.h`,
`src/core/ScriptApiVersion.cpp`, `src/core/ScriptConsole.cpp`,
`src/core/ScriptCommandQueue.cpp`, `src/core/ScriptPackage.cpp`,
`tests/src/core/ScriptStabilisationTest.cpp`,
`docs/LUA-COMPATIBILITY-POLICY.md`, `docs/LUA-PACKAGE-FORMAT.md`,
`docs/LUA-SCRIPT-DEVICES-DESIGN.md`, this report.

**Changed:** `CMakeLists.txt` (API version variables), `src/CMakeLists.txt`
(compile definitions), `src/core/CMakeLists.txt` (new sources),
`include/ScriptEngine.h` (the `reportRunResult()` declaration),
`src/core/ScriptEngine.cpp`, `src/core/ScriptBindings.cpp`,
`src/core/main.cpp` (declared in the divergence ledger),
`src/gui/MainWindow.cpp` (ledger reason extended), `tests/CMakeLists.txt`
(new test + version defines), `tests/fork-sources.txt` (8 new sources),
`tests/upstream-modifications.txt`, `.gitignore` (`/logs/`).

All eight new sources (four headers, four translation units) are registered in
`tests/fork-sources.txt` and the new test in `tests/CMakeLists.txt`; both
inherited-file edits carry a ledger entry with a reason, and every new entry
point states the thread it runs on (§5).
