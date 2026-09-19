# CRASHBOT-T3-PILOT-REPORT — the first pilot run (plan §6 P0 / §7 T3)

**Lane:** `030/crashbot`. **Worktree:** `zene-030/wcrash`. **Branch:** `030/crashbot`.
**Commits:** `5ab260400` (tools: capture + pilot + the pool fix), `6923dad98` (the 41 measured
packs), plus the commit that carries this file. **No push, no merge, no board writes** — crashbot
rides a later train.
**Nothing was written into the release worktree or a sibling lane:** the pilot's scratch is under
`/tmp/zene-pilot/`, the tree's harness is imported in place, fixtures are copied out before any
command can write to them, and every product instance ran from the frozen copy.

**Scope, verbatim (plan §7):** T3 = *capture module + first pilot ledger* → "≥ the declared case
count run; every case has replay dir; coverage counted; hygiene proof attached".

**What this report does NOT claim** (plan §0 and §5, repeated because it is binding): a green
pilot is not "the product is good". The crash-free matrix below is a **declared sample**, not a
rate; the coverage figure is a **count against one build**, not the surface; and no product defect
was found — which is a statement about this pack set, this build, and these bounds.

---

## 1. The binary under test — frozen, and proven to be the same product

| | |
|---|---|
| Frozen path | `/tmp/zene-pilot/zene` |
| sha256 | `2eed203147678d3eec7e774d67b555eed95f01bcc96124b423c7c65aa53a7cf6` |
| Version string | `Zene Studio 0.2.1-alpha.612+a039d26` (Linux x86_64, Qt 6.4.2, GCC 13.3.0), `--version` exit 0 |
| Source of the copy | `zene-030/build/zene`, same sha256 at copy time (verified with `sha256sum` on both) |
| Re-checked | the same sha256 before the first pass and after the last one, in every pilot ledger |

The sibling lane `030/version-bump` was rebuilding the release worktree during this run; every
instance in this pilot ran from the frozen copy, so that rebuild could not change what was tested.
The frozen sha is identical at the start and the end of every segment (`binary_at_start` /
`binary_at_end` in each pilot ledger).

### 1.1 A bare copy is NOT the same product — measured, then fixed

The first measurement pass ran against a bare copy and produced nonsense that would have been
reported as product behaviour if it had not been run down. All three findings are in the tree's own
source, and the frozen tree was completed before the pilot:

| What | Why (source) | Bare copy | Completed freeze |
|---|---|---|---|
| Plugin catalogue | `PluginFactory::setupSearchPaths` resolves `plugins/` from the binary's OWN dir (portable layout), `src/core/PluginFactory.cpp:283-307` | `plugin.list` **0 devices** | **409 devices / 334 loadable**, `dev-0` = Amplifier |
| Data dir (scripts, factory presets) | `ConfigManager` uses `LMMS_DATA_DIR`, `<prefix>/share/zene/`, or a dev-tree `CMakeCache.txt` (`src/core/ConfigManager.cpp:88-98,760-800`) | `script.list` **0 scripts** | **4 scripts**, `create-pattern.lua` sha256 `642111ca…` (identical to in-tree) |
| `libwasmtime.so` | NEEDED, no rpath; the harness injects `LD_LIBRARY_PATH` from the binary's own path (`tests/control_vendor_libs.py`) | exits **127** | runs (`--version` exit 0) |

The frozen tree is `zene` + `third_party/wasmtime/lib` + `plugins/**/*.so` (186 modules) + `data/`
+ `wasm-modules/*.wasm`; parity was **measured** against the in-tree binary (`/tmp/zene-pilot/probe_env.py`,
both binaries booted through the tree's harness) before the pilot started. `LMMS_DATA_DIR=/tmp/zene-pilot/data`
is exported for every instance and recorded in each capture's `env`.

---

## 2. The widened pack set

| | v0 (committed 2026-09-17) | T3 (this report) |
|---|---|---|
| Packs | 3 | **44** (the 3 v0 packs + 41 new) |
| Steps | 81 | **540** |
| Distinct command ids | 33 | **284** |
| Render-class declared budgets exercised | 0 | **5 steps** (`render.render`, `freeze.track`, `freeze.region`, `bounce.in_place`, `render.stems`) |
| Declared refusals | 25 | **53** |

**Every expectation in the 41 new packs is a reply the frozen binary actually gave.** The packs
were generated from a measurement pass (`/tmp/zene-pilot/probe/measure2.json`) in which each
prospective pack ran once on its own fresh instance; the generator rewrote the ids the replies
created back into `@id:` references (bound by the step that created them, never guessed), rewrote
the measuring session's paths back into `@work:` references, and carried over only structural
scalars (counts, indices, booleans, list lengths) as expectations. Refusals measured on the frozen
build became `expect_error` steps with the kind and the message in the step's note.

Two measurement rounds were needed, and the corrections are worth recording because both were
**my** errors, not the product's:

1. Round 1's argument errors (a `scope` value outside the live schema's enum, `bit_depth` as an
   int, a mixer channel targeted at the master) were fixed by reading the *live*
   `control.commands_list` schema, which publishes every enum (`scope`, `mode`, `progression`,
   `quantisation`, `type`). The pack schema's own refusal (`budget_s` inside `args`) was the same
   class of error and was fixed the same way.
2. Round 1's `plugin.list` = 0 devices was the *frozen-copy* defect of §1.1 — which is why the
   freeze was completed and the whole set re-measured before a single pilot case ran.

The render-class steps declare the 180 s class budget explicitly (IR-15); each one is recorded in
the run ledger with its declared budget. Measured on the frozen build, the small renders these
packs build actually take **0.59–1.53 s** — the budget is the class bound, and the runner never
probes inside it.

---

## 3. The pilot

**Shape (declared before the run, in each pilot ledger):** ≤4 instances; passes over the whole
pack set, one instance per case (IR-21); stop at 300 cases or 4500 s wall or free disk < 20 GB.
Two segments were run — the second is the same declared shape, to double the sample inside the
measured wall budget.

| Segment | Run root | Passes | Cases | Wall | Stop reason | Coverage | Captures | PIDs checked | Alive after | Worlds left | Hard kills |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 (the T3 pilot) | `/tmp/zene-pilot/final-pilot-1` | 7 | **308** | 258.2 s | target cases reached | **284/340** | 0 | 308 | **0** | **0** | 0 |
| 2 (soak extension, same declared shape) | `/tmp/zene-pilot/final-pilot-2` | 7 | **308** | 258.4 s | target cases reached | **284/340** | 0 | 308 | **0** | **0** | 0 |
| **total** | | **14** | **616** | **516.5 s** (8.6 min) | | **284/340 = 0.835** | **0** | **616** | **0** | **0** | **0** |

Every pass ran 44 cases in ~36.9 s with the same 4-instance shape; the declared 4500 s wall budget
was never the binding bound (the case target was), and free disk never came near the 20 GB floor.
Each segment's ledger re-verifies the binary sha256 at start and end: `2eed2031…` both times, while
the sibling `030/version-bump` lane was rebuilding the release worktree — which is exactly what the
freeze is for.

**Per-case outcomes** (both segments' cases together): every case reached its end — see the
matrix below. Nothing was SIGKILLed by the pilot: the only deliberate kills in this session were
the selftest's two acceptance controls, by exact PID.

| Step outcome | Count | Of 7 560 steps | Of 616 cases |
|---|---|---|---|
| `ok` | 6 790 | 89.8% | — |
| `refusal` (typed, declared by the pack) | 770 | 10.2% | — |
| `crash` | **0** | 0 | **0** |
| `hang` | **0** | 0 | **0** |
| `mismatch` | **0** | 0 | **0** |
| `cap_exceeded` | **0** | 0 | **0** |
| `schema_refused` | **0** | 0 | **0** |
| `not_run` | **0** | 0 | **0** |
| cases that reached their end | **616 / 616** | | |

The 770 refusals are the build's own typed answers, in four kinds — `invalid_args` 294, `refused`
266, `not_found` 196, `irreversible` 14 — and every one of them is a refusal the pack declared in
advance (`expect_error`), with the kind measured when the pack was written. No expectation drifted
across 14 passes.

### 3.1 The coverage number — the first honest one

**284 distinct command ids exercised of the live registry's 340 → 284/340 = 0.835.**

Definition and edges, so the number cannot be read as more than it is:

* an id counts when a step carrying it **reached the engine and answered** — a typed refusal
  counts (the command was exercised; 53 refusals are declared by the packs themselves), while
  `schema_refused` (an id absent from this build's registry) and `not_run` do not;
* the denominator is the **live registry of the frozen binary as each case measured it**
  (`surface_ids` per case in the run ledger; 340 in every case), not the inventory document's
  number and not the MCP bridge's tool count;
* no argument-domain coverage, no depth: 284 ids each exercised once per pass over 44 packs, some
  in several packs, many with a single argument set (T4 owns the arg-domain half and the delta).

### 3.2 The crash-free sample, as a declared matrix (never a rate)

**Declared sample** (repeat this sentence with the next run, not this table):

| | |
|---|---|
| Builds | 1 (`2eed2031…`, `0.2.1-alpha.612+a039d26`) |
| Instances | 4 per wave, 616 instance boots, all reaped (`quit_ok: true`, exit 0, no hard kill) |
| Packs | 44 (3 v0 + 41 T3) |
| Passes | 14 |
| Cases | 616 (44 per pass), 616 reached their end |
| Crashes / hangs / mismatches / caps / schema refusals | 0 / 0 / 0 / 0 / 0 |
| Anomalies filed | 0 (clean runs file nothing — 0 capture directories exist under either segment; the 5 captures that exist in the whole session are the two negative controls, one pre-fix smoke and the selftest's deliberate kill) |

**No product crash, no hang, no mismatch, no cap_exceeded, no not_run and no schema_refused was
observed.** The packs' 53 declared refusals are the build's own typed answers and match the kinds
measured when the packs were written — i.e. no expectation drifted in 28 passes over the set.

---

## 4. Capture — T3's deliverable, and the two negative controls that prove it fires

`tools/crashbot/capture.py` files **one replayable directory per non-ok case** under
`<run-dir>/captures/<NNN-case-instance>/`:

| File | What it is |
|---|---|
| `capture.json` | case summary (per-step outcome, first bad step, seed), instance facts (pid, socket, world, alive-at-capture, exit code, signal), binary identity, harness path + sha256, **argv**, **environment**, **settings XML**, the reporter's state and files, the copy manifests with per-file sha256, the box state, the orphan check taken while filing, and — after the reap — the instance's own reaping row |
| `transcript.txt` | the raw request/response lines of the whole case |
| `outcome.json` | the case's typed per-step outcomes, exactly as the runner filed them |
| `work/` | the case's own scratch (the `@fixture:` copy, projects it saved, anything it rendered) — copied **before** the pool's reap removes the world |
| `world/` | the instance's whole world (`lmmsrc.xml`, `workspace/`, logs) minus the socket |
| `crash-report/` | the product's own reporting evidence — the armed directory's files and the session/safe-start markers the reporter writes |
| `replay.sh` | one-command reproduction (IR-22): the exact pack, seed, binary and a fresh run dir |

The product's reporter is handled the way the product actually works, measured: a test instance is
**already armed at startup** and writes **one** bounded report file
(`<workspace>/crash-reports/zene-crash-report.txt`), so `crash.enable` is *refused* with "the crash
reporter is ALREADY armed in this instance" when it has nothing to change. The runner therefore
reads `crash.list_reports` first, calls `crash.enable` only when the product says it is not armed,
and reads `crash.list_reports` again before close; **`crash.upload_report` is never called** — this
build has no upload code, and bots never phone home (plan §4 rule 1).

**Clean runs file NOTHING, and that is measured too:** 616 cases across 14 passes produced **0 capture directories and 0 artifact rows**; the
capture writer and the artifact writer are not catch-alls. The negative controls below are what
prove they fire at all..

**Negative control 1 — a crash outcome files ONE complete capture** (`selftest_reaper.py --proof b`,
extended by T3): a deliberately SIGKILLed instance's case filed exactly one capture directory; the
six T3 checks hold — one capture for the killed case, the capture names the killed pid, the
directory holds transcript + replay.sh + outcome.json + capture.json, `capture.json` carries
`exit_code -9` / `signal 9`, the reap row is bound into the capture, and the binary sha256 is
recorded. All five reaper proofs (a, a2, b, d, h) PASS after the change.

**Negative control 2 — the capture is replayable** (run after the committed tooling): a copy of
`clip-links-0001` with one deliberately wrong expectation ran to a `mismatch` and filed a capture;
the capture's own `replay.sh` was then executed **verbatim** and produced the same case with the
same `ok 9 / mismatch 1`, the same first-bad step and the same mismatches. One command, same
outcome — the directory is a reproduction, not a transcript-shaped souvenir.

**What a capture cannot hold, stated in `capture.json` itself:** a dead instance's in-memory state
cannot be copied. For a crash, the transcript plus the case's on-disk work is the reproduction; for
a hang, the instance was still alive at capture time and `capture.json` says so.

---

## 5. Hygiene — a tested feature, proven over every pass

| Check | Segment 1 | Segment 2 |
|---|---|---|
| Instance pids checked against `/proc` after every pass | 308 | 308 |
| PIDs still alive at the end of the segment | **0** | **0** |
| Harness worlds still present at the end | **0** | **0** |
| `our_pids_alive_after` after every wave's reap | `[]` in all 7 passes | `[]` in all 7 passes |
| Hard kills needed (last resort, exact PID) | 0 | 0 |
| Orphan check at pilot start | exit 0 — one FOREIGN pid (the `030/version-bump` lane's `wvbump/build/zene`, named separately, never claimed as ours) | exit 1 — no `zene` process anywhere |
| Orphan check at pilot end | exit 1 — box clean, 0 foreign | exit 1 — box clean, 0 foreign |

The wave ledgers carry the same proof per wave: every instance's row has its exact pid, socket,
world, spawn time, quit reply (`quit_ok: true`), exit code (0) and `world_gone: true`.

### 5.1 The whole-session audit, and the worlds that are not this lane's

Every ledger this lane wrote was audited after the last run: **1 335 instance pid/world rows**
across the pilot segments, the selftests, the acceptance runs and the replay proofs — **0 pids
alive** (`/proc/<pid>` gone for every one), **0 worlds present** (every recorded harness world
removed), and `pgrep -a zene` exits **1**: no process named `zene` exists anywhere on this box.

Five capture directories exist in the whole session, and none of them is a pilot case: one from the
pre-fix smoke that found the generator's leaked-path defect, one from each of the two runs of the
replay proof, and one from the selftest's deliberate SIGKILL. The 616 pilot cases filed **0**.

Fourteen directories remain under `/tmp/zctl-run-*` that **no ledger of this lane records**, and
they were left alone rather than tidied: eight of them carry a sibling lane's working-directory
metadata (`/tmp/zctl-rec-*`, `/tmp/zctl-rec-inputs-*`, the scratch of `tests/control-record-inputs.py`
and `tests/control-recording-recovery.py`, which pass an explicit `workingdir`), and the remaining
six have the shape of a default harness world — the same shape a live sibling instance was observed
in during this run (pid 136166, `zene-030/wvbump/build/zene`). Deleting another lane's scratch is not
this lane's business; attributing our own leak would be, and there is none: every world this lane's
ledgers record is gone.

Nothing was signalled out of a pattern: the only process signals this lane ever sent were
`control.quit` and two deliberate `SIGKILL`s **by exact PID** inside the selftest (recorded with
their reason in the wave ledger). No `pkill -f`, no `pgrep`-driven kill, no signal to a process this
lane did not start. The orphan check reports the name-scoped answer *and* the PID-scoped one, and
never claims a foreign lane's process as ours.

---

## 6. What T3 forced (tooling fixes, each with the measurement that forced it)

1. **`pool.collect_deaths` used to be pool-wide, and one worker's call stamped its own case.**
   Forced by the selftest's proof (b) after the capture's in-case work changed the timing: the
   surviving case was marked `crash` for a *sibling's* death, and two threads raced the same
   artifact filing (two ledger rows, one directory). The call is now scoped to the caller's own
   instance (`names=`), and the proof passes with 18/18 checks.
2. **The selftest's wave composition was implicit.** With three packs, `--scenarios <dir>` put the
   arrange/mixer pair in wave 1; with 44 it does not, so proofs (a) and (a2) would have asserted
   facts about whichever cases happened to land there. Both proofs now name their two packs
   explicitly.
3. **The capture writer's report-file hint was too loose** (any path containing "crash" matched a
   preset file), and `replay.sh` inlined the multi-line `--version` output into a shell comment —
   the replay script failed with "Linux: not found" until the first line only was used. Both found
   by running the capture and its replay end to end, not by reading it.
4. **Five new functions were over the tools complexity target** (capture's `file_capture` CCN 18,
   pilot's `hygiene_of` 17 and `main` 14, capture's `copy_tree` 13, runner's `run_wave` 12). They
   were **decomposed**, not baselined: `--scope tools` complexity, file-length and duplication gates
   all PASS on the committed tree.
5. **The README gained the frozen-tree section**: the three environment facts of §1.1 are the
   difference between a crash hunt and a measurement of an empty product.

---

## 7. What the pilot did NOT exercise (the honest gaps)

* **No fuzz, no randomness** (T5) and **no argument-domain coverage or delta** (T4): 284 ids were
  exercised with the argument sets the packs name, mostly one per id.
* **No plugin format beyond the built-in modules.** The catalogue lists 409 devices (334 loadable:
  builtin + LADSPA/LV2/VST3/CLAP); the pilot loaded only built-in effect instances (`dev-0` and
  `dev-1` classes of path). Third-party or format-specific hosting is T8/plugin-matrix work, and
  loading a hostile plugin is a sibling programme's fixture.
* **`warp.*` only up to its typed refusals**: warp markers pin positions on a *sample* clip, and
  every clip this pilot builds is MIDI, so the group was exercised to the boundary the build draws
  and no further.
* **WASM modules were listed and queried, never loaded** (`wasm.list` over the frozen tree's eight
  demo modules, `wasm.get_state`, `wasm.pool`, `wasm.set_param`); the sandbox itself (fuel,
  memory budget, adversarial modules — the tree ships `spin.wasm`, `latency_trap.wasm`, `oob.wasm`)
  is untouched, and `script.run` was exercised with trivial inline sources only.
* **No MIDI hardware, no audio device, no GUI**: every instance is headless/offscreen with the
  Dummy backend, so `midi.*`, audio-backend failure paths and anything the GUI owns are outside
  this sample.
* **No session crash-recovery story**: nothing was SIGKILLed *as a scenario* (S9/session-bot is
  T8); recovery, safestart and retro-capture were exercised as commands, not as a crash narrative.
* **No multi-instance interaction** (Link session sync between two instances), no arm64 (this box
  only — the harness documents ~34 s engine start there, which is budget truth no number here
  transfers to), and no `project.diff`/`audible_diff`/`conflicts` (they refuse without
  `MMPZ_GIT_TOOL` pointing at the tool; the pilot recorded the refusal rather than re-pointing the
  environment mid-run).
* **The render dead window is respected, not measured**: while a render child runs the instance
  answers nothing, so a hang inside a declared render budget is still indistinguishable from a slow
  render until the deferred-reply engine fix lands (plan §8.4). The pilot never probes inside it.

---

## 8. What the next pilot should widen (in the plan's own order)

1. **T4 coverage tracker** — per-id argument domains and the delta against this run's 284-id list,
   machine-computed from the ledgers (this report's number becomes the baseline, not the goal).
2. **T5 fuzz mode** — seeded sequences from live schemas with the bundle mechanic (`@id:` binding
   already exists), so the surface behind the 284 ids gets weight, not just presence.
3. **T6 triage/corpus** — signature rules as data, M=5 replay of every capture, retained outside
   the repos; the capture format this report files is the corpus unit.
4. **T8 packs** — session/freeze-under-kill, plugin-format matrix, adversarial Lua, render
   matrices; and the S9 crash-recovery narrative the pilot deliberately did not fake.
5. **Scale** — >4 instances only after the bridge state-cache isolation (plan L3), and a second
   box (or the CI runner) for budget truth.

---

## 9. Evidence index (all paths under `/tmp`, outside both repos)

| What | Path |
|---|---|
| Pilot ledger, segment 1 (the T3 pilot) | `/tmp/zene-pilot/final-pilot-1/pilot-ledger.json` |
| Pilot ledger, segment 2 (the soak extension) | `/tmp/zene-pilot/final-pilot-2/pilot-ledger.json` |
| Per-pass run ledgers | `/tmp/zene-pilot/final-pilot-{1,2}/pass-NNN/ledger.json` |
| Per-wave instance ledgers (pids, worlds, reaping) | `/tmp/zene-pilot/final-pilot-{1,2}/pass-NNN/waves/wNN/run.json` |
| Reaper + capture selftest summary | `/tmp/crashbot-selftest/summary.json` |
| Capture/replay negative control | `/tmp/zene-pilot/replay-proof/` |
| Measurement passes (round 1, round 2) that authored the packs | `/tmp/zene-pilot/probe/measure{,2}.json` |
| Frozen tree (binary + libs + plugins + data + wasm) | `/tmp/zene-pilot/` |
