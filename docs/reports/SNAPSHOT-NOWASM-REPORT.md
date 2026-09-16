# 030/snapshot-nowasm — the `--compiled-in wasm.` question, decided and closed

**Lane:** `030/snapshot-nowasm` @ `zene-030/wwasm` (worktree of `lmms`).
**Base:** `1f5480d43` (the wave-11 train's record commit, `release/0.3.0`).
**Commits:** `059dad221` (the snapshot), `b584044c5` (the flag prose). Nothing pushed.

This is the train's own single next action
(`docs/reports/MERGE-TRAIN-030w11.md` §6.2/§9), and the parent's condition for the
first CI push: `ControlCommandsSnapshot` was the only red a CI run could not
explain by itself.

## 1 · The finding, reproduced

`ControlCommandsSnapshot` compares the committed
`tools/mcp-zene-control/zene_control/commands_snapshot.json` against a live
binary in both directions, and checks each `--compiled-in` prefix in both
directions too. The wave-11 snapshot was captured from `zene-030/build` — a
**wasm-ON** build (`WASMTIME_ROOT` on that box's find path) — so the snapshot
carried the eight `wasm.*` ids.

On such a box `tests/CMakeLists.txt` passes `--compiled-in wasm.`, and the
test's own `admitted_ids()` then refuses the flag. Its exact text, produced by
running that function against the committed file
(`/tmp/wwasm-repro-finding.py`, EXIT=0):

```
--compiled-in wasm. was declared, but the snapshot ALREADY carries ids with that prefix: the flag would excuse real drift
```

`compare()` reports exactly **1 finding** for that configuration (live ==
committed, both carrying the eight ids).

**The same file is red on CI, from the other side.** No CI job ever fetches
wasmtime — `grep -rn wasmtime .github/workflows/` is empty, no `deps-*.txt`
carries it, `scripts/fetch-wasmtime.sh` is called by nothing, and `third_party/`
does not exist in the tree — so every job configures `WANT_WASM` **ON by
default** and then degrades it to **OFF** (`CMakeLists.txt:962-967`,
`Wasmtime_LIBRARY-NOTFOUND`). A no-wasm binary against the old snapshot gives:

```
8 command id(s) the snapshot carries are NOT registered by this binary
  extra: wasm.get_state, wasm.list, wasm.load, wasm.pool, wasm.process,
         wasm.render_offline, wasm.set_param, wasm.unload
```

**Independent third witness:** the bridge's own
`tools/mcp-zene-control/tests/test_declared_surface.py` (EXIT=1 before,
`FAILED (failures=1)`) asserts at line 142-146 that "the committed snapshot
carries no wasm.\* id, so the flag must fire".

## 2 · The configuration decision, and the evidence that settled it

**The committed snapshot must describe the RELEASE configuration: `session.*`
and `telemetry.*` present, no `wasm.*`, no `stem.*`.** Evidence:

1. **What CI runs.** `.github/workflows/build.yml`: `CMAKE_OPTS` = `-DUSE_WERROR=ON
   -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON
   -DWANT_DEBUG_CPACK=ON -DWANT_VST3=ON -DWANT_CLAP=ON -DWANT_VST3_TEST_INSTRUMENT=ON`,
   then `cd build/tests && ctest --output-on-failure -j2`. No `-DWANT_WASM`, no
   `-DWANT_SESSION_VIEW`, no `-DZENE_TELEMETRY`, no `-DWANT_STEM_SPLIT` — the
   defaults, with wasm degrading to OFF for the missing wasmtime C API.
   `tests/CMakeLists.txt` therefore registers the test with **no flags at all**
   on CI: the snapshot must equal the live id set exactly.
   (Registered on POSIX only — `CONTROL_SUITE_AVAILABLE` is FALSE on WIN32 — so
   the three Windows jobs do not run it.)
2. **The test's own contract.** `tests/control-commands-snapshot.py` header and
   the registration comment: "the snapshot was captured from the RELEASE
   configuration (both command-group options ON)"; `WANT_WASM` is the one option
   in the other direction, and `--compiled-in` exists precisely because "the
   snapshot is one file and a configuration is not".
3. **The bridge agrees.** `tests/test_declared_surface.py` test_03 exists to
   assert the no-wasm snapshot (it was red against the file).

The four options that move a whole group are the only ones that do
(`src/core/ControlRegistryRegistrations.cpp`: "the `#ifdef` blocks below are the
only place that decides which groups a configuration gets"); CLAP/VST3 add no
ids, so the local build below turned them off without touching the surface.

## 3 · What was regenerated, and how

`tools/mcp-zene-control/zene_control/commands_snapshot.json` — never hand-edited,
captured from a live instance of a **no-wasm** build of this branch:

```bash
cmake -S . -B build-nowasm -DWANT_QT6=ON -DWANT_WASM=OFF -DWANT_STEM_SPLIT=OFF \
      -DWANT_CLAP=OFF -DWANT_VST3=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O2 -DNDEBUG" \
      -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -DNDEBUG" \
      -DUSE_WERROR=OFF -DUSE_COMPILE_CACHE=ON          # CONFIGURE_EXIT=0
cmake --build build-nowasm -j2 --target zene           # BUILD_EXIT=0, ~14 min, 589 TUs
QT_QPA_PLATFORM=offscreen python3 /tmp/wwasm-capture-snapshot.py build-nowasm/zene
                                                       # CAPTURE_EXIT=0
```

The capture script starts the instance through the shared
`tests/control_socket_harness.py` (offscreen platform, throwaway HOME/XDG, dummy
audio device, `--control-socket`) and then calls the bridge's own generator
`tools/mcp-zene-control/snapshot_commands.py --socket <sock> --lane <this lane>`;
it shuts the instance down through `control.quit` (exited True code=0) and closes
it in a `finally`. No `pkill -f` was used anywhere. The whole driver is this,
kept in `/tmp` rather than the tree (no new manifest entry):

```python
import os, sys
sys.path.insert(0, "<lane>/tests"); sys.path.insert(0, "<lane>/tools/mcp-zene-control")
os.environ["QT_QPA_PLATFORM"] = "offscreen"          # the headless start recipe
os.environ["ZENE_CONTROL_BINARY"] = "<lane>/build-nowasm/zene"   # provenance
import control_socket_harness as H, snapshot_commands
instance = H.start_instance("<lane>/build-nowasm/zene")
client = H.connect(instance); H.wait_ready(instance, client, None)
code = snapshot_commands.main(["--socket", instance.socket_path, "--lane", "<lane>"])
client.call(2, "control.quit"); client.close()
exited, exit_code, _ = instance.wait_for_exit(H.QUIT_TIMEOUT)
instance.close()
sys.exit(0 if code == 0 and exited and exit_code == 0 else 1)
```

```
live instance     332 command id(s), proto 1     live wasm.* []   live stem.* []
wrote …/commands_snapshot.json: 332 commands, proto 1, version 0.2.1-alpha.547+1f5480d,
      lane head 1f5480d431e673f29adabc797e45b3a6a8020b37
surface: 332 id(s) across 52 group(s), ids_sha256 c6a605888aa39366534eaaafe3b3251d7e481caed89e5c5cba0d5086478274ba
```

**Structural diff against the previous file** (`/tmp/wwasm-snapshot-diff.py`,
EXIT=0): `REMOVED` = the eight `wasm.*` ids; `ADDED` = none; `CHANGED entries` =
0; `count` 340→332, `surface.group_count` 53→52, plus `captured_at` and
`instance` (provenance). Every non-wasm command entry is byte-identical, so the
wave-11 regeneration's work (the five `oop.*`, `plugin.host_notes`, the three
description-only fixes) is preserved untouched.

## 4 · The proof — 0 findings, EXIT=0

```
$ cd build-nowasm/tests && ctest -R '^ControlCommandsSnapshot$' --output-on-failure
  1/1 Test #196: ControlCommandsSnapshot ..........  Passed  2.91 sec    FINAL_CTEST_EXIT=0
  100% tests passed, 0 tests failed out of 1

$ QT_QPA_PLATFORM=offscreen python3 tests/control-commands-snapshot.py build-nowasm/zene
  snapshot ids      332 command(s), captured 2026-09-16T14:54:53Z, proto 1
  live ids          332 command(s), proto 1
  bridge live       332 generated tool(s) (334 with the bridge tools), source live
  bridge offline    332 generated tool(s) (334 with the bridge tools), source snapshot
  bridge stale-cache 332 generated tool(s), source snapshot, passed over ["cache"]
  PASS: the committed offline snapshot and this binary register the SAME 332 command id(s)
  PASS: the bridge exposes a tool for EVERY one of them, live, offline, and with a stale cache planted
  FINAL_DIRECT_EXIT=0
```

The bridge's own unit tests, run the same way (`ZENE_CONTROL_PYTHON=python3
python3 -m unittest tests.<name>`): `test_units` EXIT=0, `test_offline_staleness`
EXIT=0, `test_declared_surface` EXIT=0 (3/3 — it was 1 failure before).

Gates re-run after the edits, unpiped: `tests/no-upstream-regression-gate.sh`
EXIT=0 (`422 changed path(s) declared; the ledger holds 460 entries`),
`tests/all-sources-reproduce.sh` EXIT=0 (`REPRODUCES`), `tests/fork-sources-gate.sh`
EXIT=0 (`660 fork-NEW, 1104 inherited, 40 tooling, 0 stale`). `tests/file-length-gate.sh
--check` EXIT=1 — **the same two inherited lines the wave-11 train named**
(`include/ControlRegistryGroups.h` 684→696, `include/ControlReversibility.h`
528→536); the edited test script (490→492 lines) adds no line.

## 5 · The prose, corrected in the same change (`b584044c5`)

Three comments said the wasm.\* group is **six** ids. It registers **eight**:
eight `wasmCommand(...)` calls in `src/core/ControlCommandsWasm*.cpp`, eight
`wasm.*` A16 rows in `src/core/ControlReversibilityTable*.cpp`, and the eight ids
the wave-11 live capture itself recorded. Six was true at `#614`
(`e2737cec6`); the shared lane pool and the deterministic offline render added
two in CODE-5 (`2b8a7ac0a`) and the prose never moved.

* `tests/control-commands-snapshot.py` (docstring — fork-NEW, no ledger entry)
* `tests/CMakeLists.txt:3030` (the `--compiled-in` comment)
* `src/core/ControlRegistryRegistrations.cpp:162` ("guards its six rows")

The two inherited files' reasons are appended to their existing
`tests/upstream-modifications.txt` lines in the same commit. **No new file in
the tree** except this report, which per `docs/reports/README.md` is not a
manifest candidate (the scope lists admit source extensions only) — nothing was
added to `tests/fork-sources.txt`, and declaring one would be wrong.
`tests/CMakeLists.txt` and `tests/control-commands-snapshot.py` are already
registered there / in the ledger.

## 6 · Not verified / open for the parent

* **The wasm-ON path was not re-run live.** No wasm-enabled build was made in
  this lane (`zene-030`'s build is the only one, and that worktree is off
  limits). The flag's wasm-ON behaviour is argued from the test's own functions
  driven with the wasm-ON id set (§1) plus the wave-11 live capture's record
  (§6.2 there). The CI path — the one that gates the push — **is** run live
  (§4), because a CI build is the no-wasm configuration.
* **`tests/control-mcp-group-coverage.py`** (the sibling ctest that drives the
  bridge over a real MCP stdio session) was not run: it needs the `mcp` stdio
  server path and a live socket, and it declares `--compiled-out wasm.` when the
  sandbox is off, so my configuration is the one it expects. It is the next
  nearest red to this change and was not measured.
* **The snapshot's provenance** now names this lane
  (`…/zene-030/wwasm`, `build-nowasm/zene`, `binary_sha256 7ef45a38…`). It is
  descriptive — nothing reads it (`registry.py` names `version`/`proto` only in a
  message string; freshness ranking is on `captured_at`). If the parent wants the
  provenance to name the release tree, it must be re-taken there.
* **`WANT_STEM_SPLIT=ON`+onnxruntime** would add `stem.*` on a box that has it;
  no box here does, and `tests/CMakeLists.txt` already declares that case
  (`--compiled-in stem.`), so it is not this capture's problem.

## 7 · Hotspots

* `hotspot: tools/mcp-zene-control/zene_control/commands_snapshot.json` — derived;
  **re-take only from a no-wasm build.** A re-take from `zene-030/build` (wasm
  ON) re-opens this exact finding; the recipe is §3.
* `hotspot: tests/CMakeLists.txt:3030` and `src/core/ControlRegistryRegistrations.cpp:162`
  — the two inherited comment edits (one word each) plus the ledger line for
  `tests/CMakeLists.txt`, which every lane appends to; the wave-11 train had to
  hand-resolve a conflict on exactly that line.
* `hotspot: tests/control-commands-snapshot.py` — 492 lines and in the fork
  scope, i.e. 8 lines under the file-length cap.
* **Named finding, not fixed:** `tools/mcp-zene-control/tests/test_declared_surface.py`
  derives the `wasm.*` ids from the sources with
  `QStringLiteral\("([a-z0-9_]+\.[a-z0-9_]+)"\)`, which misses the ids built by
  `wasmCommand(QStringLiteral("get_state"), …)` — it sees 6 of the 8. It is a
  stand-in-socket test, so it stays green, but it is where the "six" in the
  prose came from. Whoever next needs the group's real size should count
  `wasmCommand(` calls (8) or the A16 rows (8).

## 8 · The single next action

**Push.** `ControlCommandsSnapshot` reports 0 findings with EXIT=0, and every
other red at this tip is one of the 25 inherited ctests and the four named
ratchet lines the wave-11 train already named. When the merge lands, re-take the
snapshot only if the merge adds or removes an id — and if it does, re-take it
**from a no-wasm build** with the recipe in §3, never from `zene-030/build`.

**Build tree deleted** per the lane brief after the final verification; the
recipe above is the whole of it (`build-nowasm` is gitignored, and no untracked
file was left in the tree — `git status --porcelain` is empty at both commits).
