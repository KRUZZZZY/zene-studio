# tools/crashbot — bot-driven crash testing, v0

`pool.py` + `runner.py` (with `scenario.py`) are P0/T1+T2 of
`verification/AGENT-CRASH-TESTING-PLAN.md` (L1 the instance layer, L2 the driver layer). They
launch the REAL `zene` binary headless, hold N instances in their own temp worlds, drive them
over their control sockets, and REAP them — and the reaping is a tested feature, not a hope.

## Files

| File | What it is |
|---|---|
| `pool.py` | `InstancePool`: boots N instances through the tree's own harness (`tests/control_socket_harness.py`, imported in place — no second launch path), writes the PID ledger `run.json`, files artifacts, and reaps in `finally` + `atexit` + a SIGTERM handler. `python3 pool.py --orphan-check` runs the wave-start check alone. |
| `scenario.py` | The scenario schema (v0): `@id:` / `@work:` / `@fixture:` references, path lookups (`channels[id=@id:cha].sends`), and the per-command budget rule. Pure schema: it launches nothing. |
| `runner.py` | The drive loop: one instance per case (IR-21), per-command declared budgets (IR-15), typed outcomes per step (`ok`/`refusal`/`hang`/`crash`/`mismatch`/`schema_refused`/`cap_exceeded`/`not_run`), raw request/response transcripts per case, a coverage count, and the run ledger. |
| `capture.py` | **T3.** The replayable directory a NON-OK case files: raw transcript, the case's own outcomes, the project/scratch copy taken before the pool's reap, the instance's argv + environment + settings XML, its logs, exit/signal, the box state — plus `capture.json` and a one-command `replay.sh`. Clean runs file nothing. Also arms/reads the product's own reporter (`crash.list_reports` first; `crash.enable` only when the product says it is not armed; `crash.upload_report` is never called). `python3 capture.py --selftest` proves the bounded copy alone. |
| `pilot.py` | **T3.** The pilot driver: runs `runner.py` once per PASS over the whole pack set, one pass directory each, and stops on a declared bound (cases / wall / free disk). Verifies hygiene after every pass by reading each wave's own ledger (every pid in `/proc`, every world on disk, `our_pids_alive_after`), aggregates per-case outcomes, distinct-command coverage and captures into `<run-root>/pilot-ledger.json`. |
| `scenarios/` | 44 packs: the three v0 packs (transport punch gate, structural undo, mixer routing churn) plus 41 packs whose every expectation was MEASURED on the frozen binary before it was written (clip/note editing, plugin chains, chain presets, Lua, crash reports, automation, modulators, VCA/bus/rack/session/groove/scale-chord/warp/clock-link-MIDI/comp/meter/browser/project/settings/track/control/controller/dawproject/interchange/detect/oop/wasm/plugin-scan/transport-play/mixer/undo-hammer/fixture round trip). Five steps carry the render-class 180 s declared budget (IR-15): `render.render`, `freeze.track`, `freeze.region`, `bounce.in_place`, `render.stems`. |
| `selftest_reaper.py` | The reaper's negative controls: `a` raise-mid-run, `a2` SIGTERM mid-run, `b` deliberate SIGKILL (extended by T3: the crash case must file ONE replayable capture), `d` orphan check. Exit 0 only when every proof held. |

## The frozen binary, and why a bare copy is not enough

A crash hunt must run against a build that cannot move under it, so the pilot copies the release
binary to `/tmp/zene-pilot/zene` and runs EVERY instance from that copy. A bare copy is not a
faithful one, measured on 0.2.1-alpha.612+a039d26:

* the launcher resolves the plugin modules from the binary's OWN directory (portable layout,
  `src/core/PluginFactory.cpp:283`), so a copy without `plugins/` reports an EMPTY device
  catalogue (`plugin.list` count 0 instead of 409/334);
* the data directory comes from `LMMS_DATA_DIR`, `<prefix>/share/zene/`, or a dev-tree
  `CMakeCache.txt` (`src/core/ConfigManager.cpp:88-98,760-800`), so a copy without it reports
  `script.list` count 0 instead of 4;
* `libwasmtime.so` is NEEDED with no rpath, and the harness injects it from the binary's own
  path (`tests/control_vendor_libs.py`), so the copy needs `third_party/wasmtime/lib` beside it.

The pilot's frozen tree is therefore `zene` + `third_party/wasmtime/lib` + `plugins/**/*.so` +
`data/` + `wasm-modules/*.wasm`, and its parity was MEASURED against the in-tree binary before the
run (identical `plugin.list` 409/334 dev-0 = Amplifier and identical `script.list` 4-script sha256).
`capture.json` records `LMMS_DATA_DIR` with the rest of the instance's environment.


## Run it

```bash
cd <worktree>                                   # this lane: zene-030/wcrash
PY=/home/kruzzzzy/.hermes/hermes-agent/venv/bin/python3
Z=/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030

# the smoke: 3 packs across 2 instances (one case per instance), ledger + transcripts
$PY tools/crashbot/runner.py --scenarios tools/crashbot/scenarios --instances 2 \
    --run-dir /tmp/crashbot-runs/smoke-001 --binary $Z/build/zene

# one pack, one instance
$PY tools/crashbot/runner.py --scenarios tools/crashbot/scenarios/transport-punch-gate.json \
    --instances 1 --binary $Z/build/zene

# the reaper's own proofs
$PY tools/crashbot/selftest_reaper.py            # all four; --proof b for one

# the wave-start orphan check
$PY tools/crashbot/pool.py --orphan-check

# T3 - the PILOT: passes over the whole pack set until a declared bound stops it
LMMS_DATA_DIR=/tmp/zene-pilot/data \
$PY tools/crashbot/pilot.py --scenarios tools/crashbot/scenarios \
    --binary /tmp/zene-pilot/zene --run-root /tmp/zene-pilot/pilot \
    --instances 4 --target-cases 300 --wall-budget-s 4500 --min-free-gb 20
```

`LMMS_DATA_DIR` is the frozen tree's `data/` copy: without it the instance finds no scripts and no
factory presets, and `plugin.*` measures an empty catalogue (see the section above). The pilot's own
verdict is in `<run-root>/pilot-ledger.json`: per pass, per case, coverage, captures, and the
hygiene check taken after every pass.

`--binary` defaults to `$ZENE_BINARY`, else `<tree>/build/zene`, else this worktree's PARENT
build (this lane's worktree carries no build of its own). `LD_LIBRARY_PATH=
<tree>/third_party/wasmtime/lib` is mandatory for the binary to run at all — the harness injects
it from the binary's own path (`tests/control_vendor_libs.py`), and the ledger records both the
resolved path and the sha256 the run actually used.

Exit codes — runner: `0` every case reached its end · `1` a case recorded a
crash/hang/mismatch/not_run · `2` usage or schema error · `3` aborted (injected fault or an
unhandled worker error). Selftest: `0` every proof held, `1` otherwise.

## What a run leaves behind (all under `/tmp`)

```
<run-dir>/ledger.json                  the run's own ledger: cases, typed outcomes per step,
                                       coverage (distinct scenario ids + distinct command ids +
                                       refusal kinds), wave pids, artifacts, reaping
<run-dir>/waves/w<N>/run.json          the pool's PID ledger for that wave: pids, sockets,
                                       worlds, binary sha256 + version, spawn times, the
                                       orphan check at boot and after the reap, artifacts
<run-dir>/waves/w<N>/artifacts/<n>-<kind>/   ONE directory per recorded event
                                       (kind: instance_died | run_aborted) with artifact.json,
                                       the app's stderr/stdout, and the orphan check taken at
                                       filing time
<run-dir>/cases/<case>/<instance>/outcome.json    every step of that case, typed
<run-dir>/cases/<case>/<instance>/transcript.txt  the raw request/response lines (+ the
                                       fixture sha256 that was copied out of the tree)
<run-dir>/cases/<case>/<instance>/work/           the case's scratch: `@work:` paths and the
                                       `@fixture:` copy — never a repo path
```

Nothing is written into a repo tree, ever: fixtures are copied out before any command can write
to them, and every path a scenario can name lives under the run's own scratch.

## The rules this code enforces (and why)

* **One case per instance** (IR-21): `control.undo` reverses the LAST recorded transaction, so a
  reused instance can "pass" by undoing its predecessor's work. A wave boots one instance per
  scenario and reaps it after the case.
* **Bounds are per-command declared budgets** (IR-15): `render.render`, `bounce.in_place`,
  `freeze.track`, `freeze.region` get 180 s (the list and the bound are copied from
  `tests/freeze_bounce_evidence.py`, which measured a working render being reported as a hang
  under the 30 s socket bound); every other command keeps the tight socket bound, and a scenario
  may declare a TIGHTER budget only — a looser one is refused by the schema, not honoured.
* **No health probes inside a render budget**: while a render child runs, the instance answers
  nothing (`docs/RENDER-CHILD-WAIT.md`); the runner waits out the budget it declared.
* **Hang is never a crash** (IR-17): a hang is "no reply inside the declared budget, process
  alive"; a crash is "process gone, signal and exit code filed". They are separate counters.
* **Ids are bound, never guessed**: `trk-<n>` / `clip-<n>` / `ch-<n>` come off a session-ordinal
  counter (measured: `trk-27`, `clip-28`, `ch-42` on one session, `ch-36` on another), so a pack
  binds them from a reply (`"bind": {"drums": "track"}`) and refers to them as `@id:drums`.
* **Exact PIDs, never patterns**: the only signals sent are `control.quit`, and — as a LAST
  RESORT when `control.quit` does not stop a process inside its bound — `SIGKILL` to one exact
  PID from the ledger. `pkill -f` has already matched and killed a lane's own build on this box
  twice (`tests/control-midi-reconnect.py:131-138`).
* **The orphan check is PID-scoped**: `pgrep -a zene` exit 1 means "no process named zene
  anywhere" — meaningful on an idle box, meaningless while foreign lanes drive the same release
  binary. `orphan_check()` reports the name-scoped answer AND names the foreign PIDs it saw, and
  a leak proof uses the ledger's own PIDs.
* **Ceiling 4 instances** (measured: 4 × ~137 MB ≈ 550 MB, ~32% of one core); the real limit
  today is the bridge state-cache race (plan L3), not the box.

## Selftest proofs, in one line each

* `a` — a run aborted by an injected fault: exit 3, ONE `run_aborted` artifact, every PID dead,
  every world removed, the reaping reason naming the exception.
* `a2` — SIGTERM delivered to the runner mid-case: the pool's handler reaps (reason
  `signal 15`), no hard kill needed, no PID alive, no case outcome written.
* `b` — one instance SIGKILLed by exact PID mid-case: EXACTLY ONE `instance_died` artifact
  (signal 9, the killed PID), the sibling case still reaches its end with `quit_ok` and exit 0.
* `d` — the wave-start check: the name-scoped probe recorded (exit 1 on a clean box), our PID
  listed and attributed while up, and gone after the reap — with foreign lanes' PIDs named
  separately so nothing of theirs is ever claimed as ours.
