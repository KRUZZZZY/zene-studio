# LANE-STATE — 030/out-of-process (feature row 80, board card #670)

**Location.** Written to `docs/reports/` per REPO-4 (`docs/reports/README.md`); a repository-root
`LANE-STATE*.md` is moved here on sight. This is the lane's own report; the feature's document is
`docs/OUT-OF-PROCESS-BEYOND-ZYN.md`.

**Worktree** `zene-030/woop`, **branch** `030/out-of-process`, base `49a40b30c` (`release/0.3.0` as merged
by wave 10). Single-agent lane. **No push.**

---

## 1. What was built

`oop.*` — out-of-process plugin hosting / crash isolation **beyond ZynAddSubFx**. The prior art
(`post-alpha/oop-hosting`, `0e1cdcef6`, `docs/OOP-HOSTING.md`) made ZynAddSubFx's remote path an opt-in
per-instance toggle; every part of that decision was Zyn-specific and the one crash that arrived was
logged and forgotten. This lane makes the property a property of the **build**, adds the **lifecycle
record**, and makes both drivable and observable through the control socket.

| piece | where |
|---|---|
| the family table + `HostTracker` | `include/OutOfProcessHosting.h`, `src/core/OutOfProcessHosting.cpp` (fork-NEW) |
| the `oop.*` group (reads) | `src/core/ControlCommandsOutOfProcess.cpp` (fork-NEW) |
| the `oop.*` group (writers) | `src/core/ControlCommandsOutOfProcessEdit.cpp` (fork-NEW) |
| their helpers + vocabulary | `src/core/ControlCommandsOutOfProcessSupport.cpp`, `…Shared.h` (fork-NEW) |
| the A16 rows (5) | `src/core/ControlReversibilityTableOutOfProcess.cpp` (fork-NEW), joined by ONE entry |
| the proof (2 registered ctests) | `tests/src/core/OutOfProcessHostTest.cpp` + `…ClientLoopTest.cpp`, shared `…Support.h` (fork-NEW) |

**Ids (group `oop`):** `oop.get_state`, `oop.list_families` (reads, `not_mutating`);
`oop.set_mode`, `oop.restart`, `oop.reset_crashes` (writers, `irreversible`, each with its reason and
fallback). Addressing is the `plugin.*` convention: `target` = `trk-<n>`/`ch-<n>`, `plugin` = `fx-<n>`/`inst`.

**The generalization, precisely.** The table classifies 15 families: `zynaddsubfx` is
`client-available` (the only family with BOTH implementations), `vestige`/`vsteffect` are
`always-separate` (VST2's only path IS the client process), and the other twelve are `no-client` **with a
per-family reason** (CLAP/VST3 hosts live in this process; LADSPA/LV2 libraries are dlopen'd into it;
SF2/GIG/Carla are in-tree modules). An unknown key is answered by a fallback that NAMES the key. A device
is drivable when its plugin implements the documented `Q_INVOKABLE` convention
(`hostingState()`, `hostingProcessId()`, `setHostingMode(bool)`, `reloadPlugin()`), which is asked of the
meta-object — so a family that grows the convention is drivable with no change in the control surface.

**Crash isolation / lifecycle discipline.** `RemotePlugin::init()` records a start;
`processFinished()` records the exit (exit code + whether it was `QProcess::CrashExit`); the destructor's
own deliberate shutdown is recorded as a shutdown and NEVER as a crash. The key is the **client
executable**, not the plugin instance (re-instantiating is what a crash loop does, so an instance-keyed
count could be reset by the reload the crash triggers). At **3** deaths in one session `oop.set_mode` and
`oop.restart` refuse that client executable's out-of-process path, typed, quoting the count and the last
exit code; `oop.reset_crashes` is the only thing that lifts it, and the exit history stays readable.
`oop.get_state` resolves a device from four sources in order — a live client pid, then the session's
record (refusal, then `client-exited`), then the plugin's own answer, then the family table — so a plugin
re-instantiated after a crash cannot look healthy. No auto-restart (a decision, in KNOWN-LIMITATIONS).

**Inherited edits (declared in `tests/upstream-modifications.txt`, same commit):**
`include/RemotePlugin.h`, `src/core/RemotePlugin.cpp` (notifications + two read-only accessors),
`plugins/ZynAddSubFx/ZynAddSubFx.{h,cpp}` (`setHostingMode`/`hostingProcessId` as the ONE settle point;
the view's checkbox now calls it), `include/ControlRegistryGroups.h`, `include/ControlReversibility.h`,
`src/core/ControlRegistryRegistrations.cpp`, `src/core/ControlReversibilityTable.cpp`,
`src/core/CMakeLists.txt`, `tests/CMakeLists.txt`.

---

## 2. Evidence

All commands unpiped, run in this worktree; the logs are named beside each one.

**The build (Qt6, RelWithDebInfo, `-j4`, one build dir for this lane):**

```
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWANT_QT6=ON -DUSE_COMPILE_CACHE=ON
CONFIGURE_EXIT=0
$ cmake --build build -j4
FULL_BUILD_EXIT=0                      # whole tree, 0 errors
```

**The registered ctests** (run from the build tree's `tests/`, the workspace rule):

```
$ cd build/tests && ctest -R "OutOfProcessHost" --output-on-failure
    Start 25: OutOfProcessHostTest
1/2 Test #25: OutOfProcessHostTest .............   Passed    1.46 sec
    Start 26: OutOfProcessHostClientLoopTest
2/2 Test #26: OutOfProcessHostClientLoopTest ...   Passed    2.38 sec
100% tests passed, 0 tests failed out of 2
CTEST_EXIT=0
```

**The two binaries' own runs** (`QT_QPA_PLATFORM=offscreen ./<name>`, both EXIT=0):

```
Totals: 9 passed, 0 failed, 0 skipped, 0 blacklisted, 1404ms      # OutOfProcessHostTest
Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 2346ms      # OutOfProcessHostClientLoopTest
```

including the whole loop on a REAL client process — the interesting lines, verbatim:

```
QINFO  : ...aRealClientIsHostedKilledNoticedAndRefused() hosted out of process: client pid 2087373, host pid 2087308
QCRITICAL: ...aRealClientIsHostedKilledNoticedAndRefused() Remote plugin crashed
QINFO  : ...aRealClientIsHostedKilledNoticedAndRefused() round 1: client pid 2087373 killed; the host kept running and counted the death
QINFO  : ...aRealClientIsHostedKilledNoticedAndRefused() round 2: client pid 2087380 killed; the host kept running and counted the death
QINFO  : ...aRealClientIsHostedKilledNoticedAndRefused() round 3: client pid 2087385 killed; the host kept running and counted the death
QINFO  : ...aRealClientIsHostedKilledNoticedAndRefused() bound reached: the client executable 'RemoteZynAddSubFx' has
         died 3 times in this session (the last exit: code 9, a crash), which is this build's crash-loop bound (3); the
         out-of-process path for it is REFUSED until oop.reset_crashes clears the count. The in-process path is unaffected
QINFO  : ...aRealClientIsHostedKilledNoticedAndRefused() after oop.reset_crashes the slot is drivable again: client pid 2087390
```

The typed refusal through the surface, on a family with no client executable in this build:

```
QINFO  : ...aFamilyWithNoClientIsRefusedTyped() refused: oop.set_mode: TripleOscillator (trk-3/inst) cannot run in a
         separate process in this build: 'tripleoscillator' is not in this build's out-of-process family table and ships
         no client executable, so an instance of it runs in this process. The device keeps running in this process -
         nothing was changed
```

**The A16 rows.** `ReversibilityContractTest` (built + run on this tree) — the case that READS the published
histogram out of `docs/RELEASE-NOTES-v0.3.0-alpha.md` and compares it against the live table:

```
PASS   : ReversibilityContractTest::theTableHistogramIsTheDocumentedOne()
```

so the figure this lane moved (335 -> 340 rows; 10 -> 13 `irreversible`, 135 -> 137 `not_mutating`) is the
figure this tree measures. The test's NEXT case then aborts in the **pre-existing** LuaBridge defect the
parent's fix-up list already carries (`FIXUP-LIST-MERGED-TIP-2026-09-15.md` item 1:
`luabridge::LuaException: No writable member 'apiSurface'` at `src/core/ScriptDawBindings.cpp:307`) — not
this lane's, and reproducible without it.

**The two ratchets this lane does NOT inherit.** After the shape the first draft had
(`ControlCommandsOutOfProcess.cpp` 597 lines, `OutOfProcessHostTest.cpp` 623, `allChains` CCN 11) the
files were split along the seam that was already there, and the gates were re-run:

```
$ bash tests/file-length-gate.sh     # EXIT=1: the remaining regressions are inherited
                                     # (ControlRegistryGroups.h 684, ControlRegistry.h 509,
                                     #  ControlReversibility.h 528, ControlRegistryTest.cpp 505 …)
                                     # — NO OutOfProcess file appears
$ bash tests/complexity-gate.sh      # EXIT=1: 57 regressions, all other lanes'
                                     # — NO OutOfProcess function appears
```

**The manifests.** `bash tests/fork-sources-gate.sh` (Gate 9, the whole-tree scope): `GATE_FORK_EXIT=0`,
"REPRODUCES: the entry list in all-sources.txt is the recipe's own output", "PASS: every tracked source in
scope is registered (650 fork-NEW, 1104 inherited, 40 tooling)". `tests/upstream-modifications.txt` carries
this lane's reasons on all ten inherited paths (trailing newline; no note line without a leading `#`).

---

## 3. Open items, limits and hotspots

- **Thirteen of the fifteen classified families cannot be hosted out of process in this build** and are
  refused by name rather than run in place. The generalized *mechanism* is in; the *count* is one family
  that can choose plus VST2, which always is.
- **No plugin was crashed by its own bug** in the proof: the client is SIGKILLed from outside.
- **The A16 histogram figure** in `docs/RELEASE-NOTES-v0.3.0-alpha.md` was moved to the number this lane's
  tree measures (335 → 340 rows; 10 → 13 `irreversible`, 135 → 137 `not_mutating`). The MERGE TIP must
  re-measure it (board card #677 made that block self-checking — `ReversibilityContractTest` reads it).
- **`hotspot: plugins/ZynAddSubFx/ZynAddSubFx.cpp`** — this lane adds two invokables and rewrites three
  lines of the view handler. A sibling lane touching the ZynAddSubFx module will collide here.
- **`hotspot: src/core/RemotePlugin.h` / `src/core/RemotePlugin.cpp`** — additive only (two accessors, one
  flag, three notifications), but they are the shared remote-plugin machinery.
