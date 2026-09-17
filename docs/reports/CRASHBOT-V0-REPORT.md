# CRASHBOT-V0-REPORT — `tools/crashbot/` (P0/T1 + T2)

**Lane:** `030/crashbot`. **Worktree:** `zene-030/wcrash` (branch `030/crashbot`, forked from
`f16baff79`). **Commits:** `d15550b3f` (tools + `tests/tools-sources.txt`), `e5f26bdd9` (hang
control + killed-state marker), plus the commit that carries this file. **No push, no merge, no
board writes.** Nothing was written into the release worktree or the sibling lanes; scratch lives
under `/tmp` only, and every fixture a case touches is copied out before the first command runs.

**Scope, verbatim from `verification/AGENT-CRASH-TESTING-PLAN.md` §6 P0:** T1 instance runner +
reaper (with its negative control), T2 scenario runner v0 + 3 packs (transport, arrange, mixer)
ported from the strongest transcripts. **Not in scope here:** T3 capture/pilot, T4 coverage
tracker, T5 fuzz, T6 triage (see §6).

**And what this does not claim** (plan §0, repeated because it is binding): a green run is not
"the product is good". 3 scripted cases that reach their end are a *declared matrix*, not a rate,
not coverage of the surface (33 of the live 340 ids), and not an absence of crashes.

## 1. What exists

| File | Lines | What it is |
|---|---|---|
| `tools/crashbot/pool.py` | 498 | `InstancePool`: boot N instances through the tree's own harness (**imported in place** — no second launch path), PID ledger `run.json`, artifact filing, and reaping in `finally` + `atexit` + a SIGTERM handler. `python3 pool.py --orphan-check` runs the wave-start check alone. |
| `tools/crashbot/scenario.py` | 378 | Schema v0: `@id:` / `@work:` / `@fixture:` references, path lookups (`channels[id=@id:cha].sends[to=...]`), expectation application, and the per-command budget rule. Pure schema — launches nothing. |
| `tools/crashbot/runner.py` | 491 | The drive loop: one instance per case (IR-21), declared per-command budgets (IR-15), typed outcomes per step (IR-17), raw transcript + outcome per case, coverage count, run ledger, CLI. |
| `tools/crashbot/scenarios/transport-punch-gate.json` | 22 steps | Ported from `tests/control-punch-transcript.py`: region + gate at its boundaries, disarm-keeps-the-range, `punch_clear`, one `control.undo` (SPEC A16), the refusal family, a tempo round trip. |
| `tools/crashbot/scenarios/arrange-undo-structural.json` | 33 steps | Ported from `tests/control-undo-structural.py`: fixture opened **from a copy**, track+clip+3 notes built, `track.remove`, ONE `control.undo` must bring the clip and its notes back **by content**, `control.redo`, `track.move` order + no-op + refusals, save/open round trip. |
| `tools/crashbot/scenarios/mixer-routing-churn.json` | 26 steps | Ported from `tests/control-routing-commands.py` / `control-vca-commands.py`: channels added and **bound** (ids are session-ordinal — measured `ch-42` on one session, `ch-36` on another), fader read-back, `route_to`/`send_to`/`route_remove` with `control.undo`/`redo` on the send list, refusals incl. `irreversible` on `mixer.remove_channel`. |
| `tools/crashbot/selftest_reaper.py` | 352 | The negative controls: `a` raise-mid-run, `a2` SIGTERM mid-run, `b` deliberate SIGKILL, `d` orphan check, `h` hang-vs-crash. Exit 0 only when every proof held. |
| `tools/crashbot/README.md` | 111 | How to run it, what each file is, the rules and why. |

Every measured expectation in the three packs was taken off a live instance of the build named
below **before** it was written (`/tmp/crashbot/probe/probe.json`, `probe2.json`); the packs carry
no guessed ids, no guessed reply shapes, and no render commands (the smoke stays cheap).

## 2. Acceptance, as run

All commands below were run from `zene-030/wcrash` with
`env -u LD_LIBRARY_PATH` — i.e. **without** an exported `LD_LIBRARY_PATH`, which also proves the
harness's `with_vendor_library_path()` injection (from the binary's own path) is what makes the
binary runnable; `binary_identity()` fails the run loudly when it is not.

**(a) NEGATIVE CONTROL for the reaper — a raise mid-run still reaps everything and files ONE
artifact.** The fault is injected by the runner's own documented hook (`--inject-fault-at`), which
is a fault injection for exactly this control and never appears in evidence runs.

```
$ python3 tools/crashbot/selftest_reaper.py --proof a
=== proof a: a: raise mid-run (2.9s) ===
   exit_code_3                              ok
   one_artifact                             ok
   artifact_is_run_aborted                  ok
   artifact_on_disk_with_its_evidence       ok
   fault_recorded                           ok
   no_pid_alive                             ok
   no_world_left                            ok
   reaping_reason_names_the_abort           ok
   faulted_case_has_no_outcome_file         ok
   sibling_case_did_finish                  ok
   verdict: PASS
```

- runner exit **3**; the fault text is in the run ledger: `RuntimeError: fault injected at
  arrange/undo-structural-0001 step 12 (the reaper's negative control)`.
- the two live instances were PIDs `1064253`, `1064254` (ledger); **both** `/proc/<pid>` gone, both
  worlds removed, `our_pids_alive_after: []`.
- the pool's reaping reason names the exception: `context exit (exception: RuntimeError)`.
- the abort wrote **exactly one** artifact:
  `/tmp/crashbot-selftest/a-raise/waves/w1/artifacts/001-run_aborted/` (with `artifact.json`,
  the app's `stderr.log`/`stdout.log`, and the orphan check taken at filing time).
- the sibling instance's case finished (its `outcome.json` exists); the faulted case's does not.

**(a2) SIGTERM mid-run — the handler path.** `selftest_reaper.py --proof a2` starts the runner as
its own child, polls the wave's `run.json` for the PIDs, then sends `SIGTERM` **to its own child,
by exact PID**, while a case is in flight.

```
   died_of_sigterm                          ok     (child returncode == -15)
   pids_found_before_the_signal             ok
   reaping_runs / reaping_reason_is_signal_15  ok  (ledger: "reason": "signal 15")
   no_pid_alive / no_world_left             ok
   no_hard_kill_needed                      ok     (control.quit answered before the signal landed)
   case_never_finished                      ok     (no case outcome.json was written)
   our_pids_alive_after_is_empty            ok
   verdict: PASS
```

**(b) Deliberate SIGKILL of one instance mid-run — EXACTLY ONE filed artifact, siblings continue.**

```
$ python3 tools/crashbot/selftest_reaper.py --proof b
=== proof b: b: deliberate SIGKILL (2.7s) ===
   exit_code_1                              ok      (a case did not reach its end — the killed one)
   one_artifact                             ok
   artifact_is_instance_died                ok
   artifact_pid_is_the_killed_one           ok
   artifact_signal_is_sigkill               ok
   killed_instance_has_no_hard_kill         ok
   sibling_quit_cleanly                     ok
   sibling_case_reached_its_end             ok      (arrange/undo-structural-0001: ok)
   killed_case_terminal_is_crash            ok      (mixer/routing-churn-0001: crash)
   no_pid_alive / no_world_left             ok
   deliberate_kill_recorded_with_pid        ok
   verdict: PASS
```

Facts from the wave ledger (`/tmp/crashbot-selftest/b-sigkill/waves/w1/run.json`):
`deliberate: [{"name":"i1","pid":1067514,"signal":"SIGKILL","reason":"deliberate SIGKILL,
acceptance proof (b): mixer/routing-churn-0001 step 12"}]`; the killed row reads
`state: "killed"`, `exit_code: -9`, `hard_kill: false`; the sibling reads
`quit_ok: true, exit_code: 0, wait_s: 0.121, hard_kill: false`. Artifact:
`artifacts/001-instance_died/artifact.json` → `{kind: instance_died, target: i1, pid: 1067514,
signal: 9, exit_code: -9, detail: "while driving case mixer/routing-churn-0001"}`.
Note the one expected asymmetry: a SIGKILLed process cannot unlink its own socket, so
`socket_gone: false` for that row and the socket file leaves with the world (`world_gone: true`) —
the case's own evidence records both.

**(c) SMOKE — 2 instances x the 3 packs, end to end.**

```
$ python3 tools/crashbot/runner.py --scenarios tools/crashbot/scenarios --instances 2 \
      --run-dir /tmp/crashbot-runs/accept-c-smoke --binary <Z>/build/zene
EXIT=0
=== crashbot run accept-c-smoke ===
  cases: 3 (3 reached their end)
  steps: {'ok': 59, 'refusal': 22}
  coverage: 3 distinct scenario id(s), 33 distinct command id(s), refusal kinds
            {"refused": 5, "invalid_args": 11, "not_found": 5, "irreversible": 1}
  arrange/undo-structural-0001       i0   ok       {"ok": 24, "refusal": 9}
  mixer/routing-churn-0001           i1   ok       {"ok": 20, "refusal": 6}
  transport/punch-gate-0001          i0   ok       {"ok": 15, "refusal": 7}
  ledger -> /tmp/crashbot-runs/accept-c-smoke/ledger.json
```

- wall clock **6 s** (`started_at 17:21:34Z` → `finished_at 17:21:40Z`) for 2 waves / 3 instances —
  the plan's ~10-minute smoke budget is not close to binding.
- **coverage count: 3 distinct scenario ids, 33 distinct command ids** (of the live registry's
  **340** on this build; every case's `surface_ids` field records the registry size it ran
  against). Per-case outcomes are in `ledger.json` (`cases[]`, plus `counts` per step outcome).
- **artifacts: ZERO** for a clean run (the artifact writer is not a catch-all).
- every instance was reaped with `quit_ok: true`, `exit_code 0`, `hard_kill: false` in
  **0.14–0.26 s**; `our_pids_alive_after: []` in both waves.
- per-case evidence under `<run-dir>/cases/<case>/<instance>/`: `outcome.json` (typed outcome per
  step, with the raw reply kept), `transcript.txt` (raw request/response lines), and `work/` (the
  `@fixture:` copy: the arrange case's `work/agent-control-fixture.mmp` carries the fixture's
  sha256 `ce6f4e30…` in its outcome).

**(d) Wave-start orphan check.**

Clean box (measured twice, unpiped):

```
17:10:49Z  $ pgrep -a zene          → exit 1, no output
17:10:55Z  $ python3 tools/crashbot/pool.py --orphan-check    → exit 0
           {"label":"manual wave-start check","probe":{"command":"pgrep -a zene","exit":1,
            "matches":[],"clean_by_name":true},"our_pids":[],"our_pids_alive":[],
            "foreign_alive":[],"foreign_count":0}
17:24:03Z  $ pgrep -a zene          → exit 1, no output      (repeat, after the whole lane's runs)
```

The 17:24:03Z repeat is the one that matters for T1: it is taken **after** every instance this lane
ever started, and it is clean — the name-scoped pass and the PID-scoped audit below agree.

Under foreign load — and this is the point of the helper, not a failure of it. While proof `d` ran,
`pgrep -a zene` listed PIDs that were **not** this run's (a sibling lane driving the same release
binary):

```
   probe_is_consistent                         ok
   our_pid_listed_and_attributed_while_up      ok    (our_pids_alive == [that one pid])
   foreign_procs_are_never_claimed_as_ours     ok    (2 foreign PIDs named separately)
   our_pids_gone_after                         ok
   world_gone_after / app_log_removed_with_the_world  ok
   verdict: PASS
```

So the check reports both answers: `clean_by_name` (the plan's literal `pgrep -a zene` exit-1 pass,
which is a statement about the whole box) and the PID-scoped one a leak proof must use. The same
regime is visible in the smoke's own ledger: wave-1's boot probe was `exit 0` with one foreign PID
named while every instance of that wave was reaped with `our_pids_alive_after: []`.

**(h) Extra — the hang control (IR-17), added because "hang is never a crash" deserves a
measurement rather than an assertion.** One instance, SIGSTOP by exact PID, a step with a 2 s
declared budget, then SIGCONT, then SIGKILL by exact PID; the runner's own classifier is called
each time:

```
   frozen_instance_times_out                ok    (outcome "hang")
   hang_carries_the_budget_as_its_detail    ok    (Blocked: no response line inside 2.0s)
   process_was_alive_during_the_hang        ok
   hang_is_not_a_crash                      ok
   instance_recovered_after_SIGCONT         ok    (control.ping ok again)
   same_step_is_a_crash_once_killed         ok    (outcome "crash", exit_code -9)
   pid_gone_after_the_kill                  ok
   verdict: PASS  (5.5 s, one instance, no signal sent except to exact PIDs)
```

**Leak proof, PID-scoped, over every run this lane made** (32 ledger/outcome JSON files under
`/tmp/crashbot-runs/` and `/tmp/crashbot-selftest/`): **19 distinct instance PIDs recorded, 0 alive
now; 19 distinct harness worlds created, 0 present now; 2 artifacts filed in the whole session**
(one `run_aborted` from proof (a), one `instance_died` from proof (b)) — the clean smoke filed
none. Every claim above cites a PID from a ledger, never a process name. No `pkill`, no
`pgrep`-driven kill, no signal ever sent to a process this lane did not start.

## 3. Repo gates (all unpiped, all after the final commit of the tools)

| Gate | Exit | Last line |
|---|---|---|
| `bash tests/fork-sources-gate.sh` | 0 | `PASS: every tracked source in scope is registered (660 fork-NEW, 1104 inherited, 44 tooling).` |
| `bash tests/no-upstream-regression-gate.sh` | 0 | `(422 changed path(s) declared; the ledger holds 460 entries)` |
| `bash tests/all-sources-reproduce.sh` | 0 | `REPRODUCES: the entry list in all-sources.txt is the recipe's own output.` |
| `bash tests/file-length-gate.sh --check` | 0 | `PASS (check mode: no regressions; baseline not written)` |
| `bash tests/complexity-gate.sh --check` | 0 | `PASS (check mode: no regressions; baseline not written)` |
| `bash tests/unregistered-tests-gate.sh` | 0 | `PASS: every test source under tests/src/ is registered, or declared with a reason` |
| `bash tests/file-length-gate.sh --check --scope tools` | 0 | `PASS` (the four new files are 491/498/378/352 lines vs the 500 cap) |
| `bash tests/complexity-gate.sh --check --scope tools` | 0 | `PASS` (no function over CCN 10; the first draft had seven — they were decomposed) |
| `bash tests/evidence-gate.sh` | 0 | `PASS: no committed evidence file types and nothing over the cap.` |

The `--scope tools` pair is the one that actually measures Python tooling (see §5.2); both were
**red on the first draft** (`runner.py` 521 lines, `pool.py` 502; seven functions CCN 11–29) and are
green after the refactor. `unregistered-tests-gate.sh` did not flag anything (it scans
`tests/src/*.cpp`; the selftest is not a CTest and is not registered as one).

## 4. Provenance (plan §4 rule 4)

- **Binary actually used:** `/…/zene-030/build/zene`, sha256
  `2eed203147678d3eec7e774d67b555eed95f01bcc96124b423c7c65aa53a7cf6`, version
  `Zene Studio 0.2.1-alpha.612+a039d26`, `--version` exit 0. Recorded per wave in `run.json`.
- **Harness:** `tests/control_socket_harness.py`, sha256 `84e9ed8537e5cd08…` (the same file the
  live-proof pass used), imported in place from this worktree; recorded per wave.
- **Box:** 20 cores, loadavg `3.8 3.1 2.6` at the time of the smoke (recorded in both wave
  ledgers) — i.e. these numbers are not best-case, and a verdict taken on this box says nothing
  about the ~34 s engine start the harness documents on the linux-arm64 CI job.
- **Per case:** the fixture sha256 copied out of the tree, the live registry size the case ran
  against (340), the seed, and the scope label.

## 5. Two premises this work corrected, with the measurements

**5.1 The binary the brief names is no longer at that path.** The brief pins
`sha256 26da09c81a088c74…` / `0.2.1-alpha.590+e9a298b`; measured at
`/…/zene-030/build/zene`: `sha256 2eed203147678d3e…` / `0.2.1-alpha.612+a039d26`, and the file's
mtime is `2026-09-17 18:02:56 +0100` — a sibling lane moved the release worktree to
`a039d262e` and rebuilt during this session (`git -C zene-030 log --oneline -1` →
`a039d262e Merge 030/arch2-api: registry core moved into static library zene_api …`). Nothing was
adapted silently: the acceptance runs above were executed **against this binary**, every pack
expectation was measured against it, and each ledger records the sha it used. The report's numbers
are for `2eed2031…` and nothing else; if the release binary moves again, the packs must be
re-measured (the runner's failure mode is a typed `mismatch`, not a silent pass).

**5.2 New files under `tools/` belong in `tests/tools-sources.txt`, not `tests/fork-sources.txt`.**
The brief says to declare new files in `tests/fork-sources.txt`. Measured: Gate 9's own failure
message for these four files reads *"new code this product adds -> add it to
tests/fork-sources.txt … the fork's own tooling under tools/ -> add it to
tests/tools-sources.txt (measured by gates 4, 7 and 8 with `--scope tools`; never put it in
tests/upstream-modifications.txt)"*, and `tests/tools-sources.txt`'s header states the same rule
and why (`tests/fork-sources.txt` is the C/C++ product scope; the fork ratchets would otherwise
measure Python tooling against it — measured there: `tools/mmpz-git/mmpz_git.py` is 1915 lines with
CCN up to 47). The four modules are registered in `tests/tools-sources.txt` with a dated note, and
that manifest's own recipe was run against this tree: it reproduces
(`diff <(entries) <(git diff --diff-filter=A 4e677cb6c6ab --cached -- tools | … | LC_ALL=C sort)` →
`REPRODUCES`). Registering them in `fork-sources.txt` instead would have needed the C/C++ scope's
own recipe edited too, and would have widened the wrong ratchets.

## 6. What T3's pilot needs (and what v0 deliberately does not do)

T3 = "capture module + first pilot ledger" (plan §7). v0 already gives it: the pool/reaper with its
negative controls, the runner with declared budgets and typed outcomes, per-case transcripts and
outcomes, a coverage count, and a ledger that is machine-computable. Still to build:

1. **Replay directory per case** (IR-22): argv/env recorded verbatim, the project state *before the
   failing step* copied, `replay.sh` one-command reproduction. v0 keeps the transcript, the case
   `work/`, the fixture hash and the seed; it does not yet snapshot project state mid-case.
2. **Product crash reporting armed per test instance** (`crash.enable`, read back with
   `crash.list_reports` before close; `upload_report` stays OFF — bots never phone home). v0
   captures the process-exit signal and the app log, which is one of the four planned signals.
3. **Corpus + triage** (T6): run ids and artifact paths are stable and ledgered, so a corpus
   directory (`~/zene-crash-runs/<run-id>/`) can be pointed at them without changing the tools.
   v0 files artifacts but does not signature, retain or prune anything.
4. **Coverage tracker** (T4): the runner emits `distinct_command_ids` + the exercised list; the
   arg-domain half and the delta-vs-previous-run half are T4's.
5. **Fuzz mode** (T5): the schema already refuses an unseeded case, and `@id:` binding exists for
   the bundle mechanic; the generator itself is T5's. Nothing in v0 calls a model per case.
6. **Pilot shape** (plan §5): ≤4 instances (v0 refuses more, by measurement), ≤2 h wall, ≤2 GB
   disk, one committed summary in the program workspace's `verification/crash-runs/`, and **the
   run's own hygiene proof** — the wave ledger's `our_pids_alive_after: []` + orphan check is that
   proof, already written per wave.
7. **Rung the programme still owes the engine backlog:** the deferred-reply fix for the render dead
   window. Until it lands, a hang during a declared render budget is indistinguishable from a slow
   render — v0 respects the window (no probes inside it) instead of pretending to measure it.

## 7. Residuals / honest gaps

- **No product crash was found by v0's three packs** (a crash *was* produced only by proof (b), on
  purpose). The packs are deliberately cheap and scripted; their refusals and read-backs are ported
  from the strongest existing transcripts, not from a fuzz campaign.
- **A hang was never produced by a *scenario*:** proof (h) proves the classifier and the budget
  semantics on a frozen instance, but no pack contains a command that legitimately stalls (the
  packs use no render commands, by design, to keep the smoke cheap).
- **Render-class budgets (180 s) are implemented and schema-enforced but not exercised live** in
  this lane: the quickest live render command would have spent an engine start plus audio and the
  packs are render-free on purpose. The list and the bound are the tree's own
  (`tests/freeze_bounce_evidence.py`), and the runner never probes inside such a budget — that is
  code plus a precedent, not yet a measurement from this lane.
- **Proof (d)'s literal "clean box" criterion cannot be asserted while foreign lanes are live.**
  It was measured on a clean box (§2d, 17:10:49Z) and the proof itself asserts the PID-scoped facts
  under either regime; on a busy box `pgrep -a zene` exit 1 is simply not available to anyone.
- **`pgrep -a zene` can also match a *render child*** (`zene render … -o …`) of a foreign instance —
  the orphan check's matches include it and attribute it as foreign, which is correct but worth
  knowing when reading the output.
- **One instance's `close()` path is the harness's rmtree** (`tests/control_socket_harness.py`):
  worlds are removed, and a killed instance's socket file goes with the world. Nothing this lane
  wrote touches a repo path.
