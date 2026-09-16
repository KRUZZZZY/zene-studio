# CI-PREP lane — board cards #638, #639, #640 (Zene Studio 0.3.0-alpha)

**Branch `030/ci-prep`**, base `49a40b30c`, worktree `zene-030/wcicp`. The raw transcript
(command lines and their verbatim output, plus the two proof scripts in full) is beside this
report: `docs/reports/CI-PREP-030-TRANSCRIPT.md`. Four commits:

| commit | card | what it lands |
|---|---|---|
| `8e16a02c5` | #638 | the two `ControlResult` forward declarations that made msvc-x64's C4099 fatal |
| `e28911edf` | #639 (a) | numpy + scipy provisioned for `ControlMasteringCommands` on the four unix jobs |
| `7afcc4869` | #640 (a) | `control.undo` copies the transaction record instead of reading it through a freed pointer |
| `1f6fa07cc` | #640 (b) | a closed control socket now reports the instance's exit status (the evidence gap) |

Files touched: `include/ControlMeterSupport.h`, `include/ControlMasteringSupport.h`,
`.github/workflows/build.yml`, `src/core/ControlCommandsControl.cpp`,
`tests/control_socket_harness.py`, `tests/control_instance_diagnosis.py`.
No gate, baseline, manifest or workflow step was removed, weakened or re-anchored; no build
tree was created in this lane; nothing was pushed. All six files are fork-authored
(`tests/fork-sources.txt`) or CI config (`.github/**`, classified "allowed, non-runtime" by
Gate 6), so no manifest entry was owed — Gate 6 passes (below).

---

## 1. #638 — msvc-x64 `C4099` on `ControlResult`: FIXED, both directions

The archived log (`$B/.dlog-fire-0915/msvc-x64.log`) fails three TUs on the same
disagreement, and each carries the exact pair of lines it saw:

```
D:\...\include\ControlRegistry.h(70): error C2220: the following warning is treated as an error
D:\...\include\ControlRegistry.h(70): warning C4099: 'lmms::ControlResult': type name first seen
                                    using 'class' now seen using 'struct'
D:\...\include\ControlMasteringSupport.h(69): warning C4099: 'lmms::ControlResult': type name first
                                    seen using 'struct' now seen using 'class'
D:\...\include\ControlEdit.h(44): note: see declaration of 'lmms::ControlResult'
```

Failing TUs, from the log's `FAILED:`/`Building CXX object` lines (1789-1807):
`ControlMasteringSupport.cpp.obj`, `ControlCommandsMastering.cpp.obj` (both reported at
`ControlRegistry.h:70`) and `ControlCommandsMasteringRun.cpp.obj` (reported at
`ControlMasteringSupport.h:69`, and its `note:` names `ControlEdit.h:44` — the `struct`
forward declaration that TU includes at its own line 68, one line before the support header).

The defect is only ever the class-KEY: the definition at `include/ControlRegistry.h:70` and
twelve of the fourteen sightings say `struct`; `include/ControlMasteringSupport.h:69` and
`include/ControlMeterSupport.h:50` said `class`. Both are now `struct`, with the failing TU
pairs recorded in the comment beside each.

**Local proof** (this box has no MSVC toolchain, and `lmms_export.h` is generated, so a
`-fsyntax-only` check needs a configured build tree — deliberately not created):

```bash
$ grep -rn "class ControlResult\|struct ControlResult" include/ src/ plugins/ tests/ tools/
include/ControlEdit.h:44:struct ControlResult;
include/ControlWarpSupport.h:44:struct ControlResult;
include/ControlRegistry.h:70:struct ControlResult          <- the definition
... (14 sightings in total)
$ grep -rho "class ControlResult\|struct ControlResult" include/ src/ plugins/ tests/ tools/ | sort | uniq -c
     14 struct ControlResult          # 0 `class ControlResult` in the tree
```

**Only CI can confirm** that msvc-x64's Build step now compiles those three TUs. If it does
not, the next log will name the remaining pair — the fix makes both include orders legal, so a
second failure would mean a third spelling of the name exists somewhere the grep does not
reach (there is none in `include/ src/ plugins/ tests/ tools/` today).

## 2. #639 — CI runner environment

### 2a. numpy + scipy: FIXED (a step in each of the three test-running job definitions)

The measured failure, identical on all four unix jobs: `linux-x86_64` (1/140 failed),
`linux-arm64` (1/149), `macos-x86_64` and `macos-arm64` (2/149 — the second is the same
test), all with this one line and nothing else:

```
auto-mastering-demo.py needs numpy and scipy for its independent BS.1770-4 measurement:
No module named 'numpy'
```

`tools/auto-mastering-demo.py:29-35` `raise SystemExit(2)`s without them, and
`tests/control-mastering-commands.py` runs it as the independent BS.1770-4 measurement the
proof's verdicts rest on — so this is a missing CI dependency, not a test defect.

What landed: one step before `Run tests` in **linux-x86_64**, **linux-arm64** and **macos**
(the third definition covers both macOS arches — 3 definitions, 4 platforms):

* linux: `apt-get install -y --no-install-recommends python3-numpy python3-scipy`, which
  lands in the system python3 — the interpreter
  `find_program(PYTHON3_EXECUTABLE NAMES python3 ...)` (`tests/CMakeLists.txt:1832`) resolves
  and the one the job logs as the test command `/usr/bin/python3`;
* macos: `python3 -m pip install numpy scipy` (with a documented `--break-system-packages`
  retry for a PEP 668 interpreter) — pip and not brew, because brew installs into *its*
  python, which is later on that runner's PATH than the logged `/usr/local/bin/python3`;
* both end with a line that prints `sys.executable` + both versions, so an install that
  landed in a different interpreter fails **in that step** rather than leaving the same test
  red for the same reason on the next run.

The `msvc` job deliberately gets nothing: the control suite is POSIX-only
(`tests/CMakeLists.txt:1840-1844` sets `CONTROL_SUITE_AVAILABLE FALSE` on `WIN32`, and every
python control test, `ControlMasteringCommands` at :2506 included, is registered inside that
gate), so the dependency would be unused there. A shared deps file was not used either:
`deps-ubuntu-24.04-gcc.txt` is also consumed by the mingw job, whose ctest never runs this
test, and the macOS job has no deps list at all (Brewfile) — three symmetric steps keep the
dependency beside the step that needs it on every platform that runs it.

**Local proof** (the steps themselves cannot run here):

```bash
$ python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/build.yml'))"   # EXIT=0, "YAML OK"
$ yamllint -c .yamllint .github/workflows/build.yml                                    # EXIT=0 (was 0 before the edit too)
```

**Only CI can confirm** the four unix jobs' `ControlMasteringCommands` now goes green.

### 2b. The POSIX `/tmp` assumption in the Windows jobs: ALREADY FIXED in the tree — no change, recorded precisely

The card's two named instances were fixed on 2026-09-14 by `eda364aae` ("the two msvc-x64 test
failures are platform facts, not defects"), whose message names both, and I re-verified them
in this tree rather than trusting the commit:

* `ControlChainPresetTest` — the two assertions that failed on a Windows test host (no
  loadable plugin MODULE) now `QSKIP` on the catalogue measurement
  (`tests/src/core/ControlChainPresetTest.cpp`), the class `ReversibilityUndoTest` already used.
* `ControlCommandsSnapshot` — moved under the `CONTROL_SUITE_AVAILABLE` gate its twenty
  siblings use (`tests/CMakeLists.txt:2994`), because its live half drives an AF_UNIX socket
  that Windows does not have. It is not registered on any Windows job today.

The remaining hardcoded `/tmp` paths, censused mechanically rather than by eye:

* every python test that passes `dir="/tmp"` to `tempfile` is now POSIX-gated
  (`tests/control_socket_harness.py:215`, `control-commands-snapshot.py`, `agent-surface-gate.py`,
  `control-stable-ids*.py`, `render-software-tag.py`, `control-mcp-group-coverage.py`, …) — the
  registration table was derived from `tests/CMakeLists.txt` by walking `if()/endif()` nesting:
  53 python-driven ctests, 47 of them POSIX-only, and the six ALL-PLATFORMS ones
  (`LuaApiSurface`, `GoldenAudioSelfTest`, `RtSafetySweep`, `RtSafetySelfTest`,
  `ControlMcpGroupCoverage`, plus Windows-only `ControlNamedPipeSmoke`) contain no instance-path
  `/tmp` use except `ControlMcpGroupCoverage`, which reaches the harness's mkdtemp **only** after
  `server_python()` finds an interpreter with the `mcp` distribution — none of `build.yml`'s jobs
  provisions one (`tools/mcp-zene-control/tests/harness.py:91` uses the same shape) — so it exits
  `SKIP_CODE` (77, ctest *Skipped*, never *Passed*) before any instance starts.
* the C++ `/tmp` strings that do run on Windows are fixture VALUES, not filesystem paths
  (`BrowserCatalogTest` path classification, `ProjectRecoveryTest`/`ImportDetectionTest` string
  comparisons, `LoudnessReportTest` text formatting) — nothing there creates or opens `/tmp`.
* `tests/control_socket_harness.py`'s `dir="/tmp"` stays as it is, on `eda364aae`'s recorded
  reason: `/tmp` is the short-path choice for an AF_UNIX socket path (108-byte limit) and a
  platform-conditional rewrite of the mkdtemp would only move the failure from
  `FileNotFoundError` to the AF_UNIX connect. If a future job ever provisions `mcp` for a
  Windows runner, the correct fix is the `eda364aae` precedent — gate that test — not a
  `/tmp` rewrite.

**Observation for the parent (not a card, not a red):** `ControlMcpGroupCoverage`
(`tests/control-mcp-group-coverage.py`, added after the archived run by `967cae35a`) is the one
newly registered test whose SKIP it depends on an unprovisioned distribution, so on the next
run it will report *Skipped* on all seven platforms unless a job installs `mcp` (only
`quality-gates.yml:202` does). Its POSIX coverage is unchanged; the next run simply adds no
coverage for the mcp group.

## 3. #640 — macOS `ControlPluginScanCommands`: the failing assertion named, the cause found in the tree, the evidence gap closed

**The failing assertion** (verbatim from `$B/.dlog-fire-0915/macos-arm64.log`, identical on
`macos-x86_64`): test 129 dies at `tests/control-plugin-scan-commands.py:266`,
`check_undo_removes()` → `session.result("control.undo")`:

```
129: -> {"id":11,"cmd":"control.undo","args":{},"proto":1}
129: <- NO REPLY inside 30s (the server closed the connection without answering)
129: Traceback ... check_undo_removes(session, problems, target)
129: control_socket_harness.Blocked: the server closed the connection without answering
```

The engine did not answer — it was gone. Note this is the FIRST `control.undo` in the test and
the first undo over a record whose inverse is a COMMAND (`applies=command`); the ten earlier
commands all replied, including the three typed refusals. Both macOS jobs fail identically;
both Linux jobs pass the same test.

**Cause this lane can prove is in the tree** (committed in `7afcc4869`): `undoLastCommand()`
held `const ControlRegistry::Transaction* top = registry.lastTransaction()` — and
`ControlRegistry::lastTransaction()` is `&m_transactions.last()`
(`src/core/ControlTransactions.cpp:114-117`), a pointer INTO the registry's
`QVector<Transaction>`. The inverse it then dispatches is a mutating command (this test's is
`plugin.scan_cache_quarantine_remove`, `cmd.mutating = true` at
`src/core/ControlCommandsPluginScanEdit.cpp:371`), so the nested `registry.invoke()` runs
`runHandler()` → `recordTransactionOf()` → `recordTransaction()` →
`m_transactions.append(stamped)` (`ControlTransactions.cpp:108`) — and an append may
**reallocate** that vector. The reply then read `top->command` and `top->cls`: a
use-after-free whose visible outcome belongs to the allocator. glibc tends to leave the freed
bytes readable (which is why this branch has been green on Linux for a release); Darwin's
allocator is free to poison or reuse them. The record is now copied once, before the dispatch,
and every later read goes through the copy.

**Stated plainly:** the archived log contains no frame, no exit status and no crash report, so
I cannot prove that this UAF is what killed the macOS instance — and I do not claim it. What I
can prove is that it is real, that it is in exactly the branch that fired, and that it is the
only defect this lane found in the control.undo path. The tree now also records the failing
include-order evidence next to it.

**The evidence gap closed** (committed in `1f6fa07cc`): the harness's EOF branch was the one
failure path that printed no diagnosis — the two timeout branches append
`instance_diagnosis()`, and the branch a DYING instance takes did not. It now raises
`closed_connection_reason()` — the same sentence plus the diagnosis ("the instance EXITED with
-11 - a crash or a refusal, not a hang", and any crash report under the instance's temp dir,
which still exists at that point because `Instance.close()` rmtree's it). That is also where the
product's own crash reporter writes: `src/core/main.cpp:1003` installs it at startup with
`ConfigManager::inst()->workingDir()` as its root, and the harness gives the instance a working
dir inside that temp directory (`<tmp>/workspace`), so a signal death leaves
`<tmp>/workspace/crash-reports/zene-crash-report.txt` there — the file the workflow's
crash-evidence step can no longer find, because `Instance.close()` has removed the whole
directory long before that step runs. The diagnosis reads it at the failure point, while it
still exists. The text is built in
`tests/control_instance_diagnosis.py` because `tests/control_socket_harness.py` is grandfathered
at its exact 511 lines in `tests/file-length-baseline.tsv` and Gate 7's tolerance is 0; the
harness's change is net-zero lines.

**Local proof — real harness code, same wire shape, before and after:**

```bash
$ python3 /tmp/ci-prep-eof-proof2.py /tmp/ci-prep-oldharness   # harness as committed at HEAD~1
the server closed the connection without answering
$ python3 /tmp/ci-prep-eof-proof2.py tests                    # this branch
the server closed the connection without answering
diagnosis: the instance EXITED with -9 - a crash or a refusal, not a hang (its stdout/stderr are the transcript above)
```

(The proof registers a real harness `Instance` whose binary is a sleeper, kills it, and points
the real `Client` at a server that accepts and then closes without answering; both runs exit 0
— it prints the message, it is not a test.) A second stimulus with no instance gives
"diagnosis: no instance was launched by this process", so the helper is never silently empty.

**The macOS verdict can only come from CI.** Two outcomes are both useful next time: if test
129 now passes, the UAF was it; if it still dies, the log names the instance's exit status
(SIGSEGV vs SIGABRT vs a clean code) and any crash report — which is the fact this run lacked.

## 4. What only a CI run can confirm

1. msvc-x64's **Build** step compiles `ControlMasteringSupport.cpp`, `ControlCommandsMastering.cpp`
   and `ControlCommandsMasteringRun.cpp` (#638).
2. The four unix jobs' `ControlMasteringCommands` goes green with numpy/scipy provisioned (#639).
3. macOS test 129 `ControlPluginScanCommands` — pass (the UAF was the cause) or a new failure
   that now names its exit status (#640).
4. That no *other* red appears in the Windows jobs' `Run tests` — the Windows test registrations
   were not changed by this lane (see §2b).

## 5. What I could NOT verify

* No MSVC, no MinGW and no macOS toolchain on this box, and no build tree in this lane (none was
  created): nothing here was compiled. `-DUSE_WERROR=ON` local builds, the msvc Build step and
  every macOS/Linux runner step are unverified by construction.
* The `apt-get install python3-numpy python3-scipy` and `pip install numpy scipy` steps were not
  executed anywhere; their package availability was argued from the runners' OS versions, not
  measured.
* The Darwin allocator behaviour in §3 is the standard mechanism, not a measurement: what is
  proven locally is the dangling-pointer construction and the append that can invalidate it.
* The #639(b) census is static (registration nesting + grep); it does not prove what a Windows
  runner's preinstalled python contains.

## 6. Hotspots / collisions

* `hotspot: .github/workflows/build.yml` — this lane added three installation steps before the
  `Run tests` steps of linux-x86_64/linux-arm64/macos only; the sibling lane `wfixhyg` may add an
  `LD_LIBRARY_PATH` env line to the test steps. The two edits do not touch the same lines (the
  env lines belong to `Run tests`, my steps sit above it), so the merge train's union should apply
  cleanly — but this is the one file two lanes write.
* `hotspot: tests/control_socket_harness.py` — changed at exactly its grandfathered 511 lines
  (import + one call swap). Any lane that adds a line there trips Gate 7; the diagnosis text
  lives in `tests/control_instance_diagnosis.py` instead for that reason.
* `hotspot: src/core/ControlCommandsControl.cpp` — one function (`undoLastCommand`) touched; the
  sibling `wfixeng` lane owns `src/core/` script/modulator/mixer bindings, not this file.

## 7. Gate state of this branch (local, this worktree)

`bash tests/run-all-gates.sh` (default enforcement scope) → **FAIL**, and none of the reds are
this lane's:

* gate 4 (complexity) FAIL, gate 7 (file-length) FAIL — pre-existing reds on other lanes' files
  (`include/ControlRegistry.h` 509, `include/ControlRegistryGroups.h` 684,
  `include/ControlReversibility.h` 528, `src/core/ControlReversibilityTablePassive.cpp` 518,
  `tests/control-session-api-proof.py` 889, `tests/control-stable-ids-slice2.py` 549,
  `tests/src/core/ControlRegistryTest.cpp` 505, `DawProject*.cpp`, `ImportDetection*.cpp`, …).
  Every file this lane touched appears only as *allowed* / baseline-matching
  (`tests/control_socket_harness.py` 511 = its baseline entry, no growth).
* gate 5 (mutation) FAIL is "no `build/` — configure first", i.e. the missing build tree in this
  lane, not a mutation regression.
* gates 3, 6, 9, 10, 11, 12 PASS (gate 6: "every change to upstream-inherited code … is
  declared"); gates 1 and 2 SKIP (no build tree, no `--with-coverage`).

## 8. The single next action

Push `030/ci-prep` (or merge it into the 0.3.0 release line) and read the fresh job logs:
**msvc-x64 Build** for #638, **the four unix jobs' `Run tests`** for #639, and **macOS test 129's
diagnosis block** for #640 — with the UAF fixed, that block now has to say either nothing (test
passes) or the exit status of the instance that dies.
