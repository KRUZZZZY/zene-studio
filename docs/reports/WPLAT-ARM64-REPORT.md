# WPLAT-ARM64 — the two linux-arm64-only reds no lane owned

**Lane:** platform work, branch **`030/wplat-arm64`**, worktree `zene-030/warm64`
(worktree of `lmms/.git`). **Base:** `eba78f8ff` (the merged tip). **Fix commits:**
`7ec118017` (ControlMeterCommands) and `8fc8cb044` (MmpzGitDepthTest). **Date:** 2026-09-17.
Nothing is pushed and nothing is merged: this lane holds the branch only.

The run judged is **35126160372**, job **104895806952** (`linux-arm64`, 206 tests, 7 failed).
Five of those seven are owned and fixed on the sibling branches (`030/wplat-lin`,
`030/wplat-mac`): ClipLinkTest, ClipLinkPersistenceTest, ImportDetectionTest,
ControlShutdownHookTest, SafeStartTest. This lane owns the other two, and the contrast that
makes them bounds and not platform mysteries is the same run's `linux-x86_64` job
(104895806780):

| # | test | linux-arm64 (job 104895806952) | linux-x86_64 (job 104895806780) |
|---|---|---|---|
| 188 | `ControlMeterCommands` | **Failed** 78.17 s, rerun 77.19 s | **Passed** 13.20 s |
| 205 | `MmpzGitDepthTest` | **Timeout** 300.03 s | **Passed** 139.41 s |

---

## 1 · `ControlMeterCommands` (188) — one socket read is not a render's bound

**What the log says** (read with greps; the file is 893 KB):

```
188: Test command: /usr/bin/python3 ".../tests/control-meter-commands.py" ".../build/zene"
188: Test timeout computed to be: 600
188: note: discarded a stale reply to an earlier request: {"id":0,"ok":true,...}
188: control_socket_harness.Blocked: no response line inside 30.0s (socket timed out)
188: diagnosis: the instance is STILL RUNNING (pid 47884) - a HANG, not a crash
188: children: 1 live
188:   child 47913: state=R ... zene render /tmp/zene-render-47884-*.mmp -o .../armed.wav -f wav -s 44100
188: cpu: burned 2% of one core over 1.0s (3563 -> 3565 ticks)
```

The call that expired is the first `render.render` of
`check_render_is_byte_identical_with_the_tap_attached` (`tests/control-meter-commands.py:412`).
**Nothing is hung**: the dispatch thread (main, `poll_schedule_timeout`) is waiting for the
render **child**, the child is in state `R` and the parent burns no CPU. That is the documented
shape of this command — `render.render` does not render in-process; it runs the product's own
CLI as a child, the child pays a **full `Engine::init`** (measured ~34 s on this runner,
`docs/RENDER-CHILD-WAIT.md`), and the socket answers **nothing** until it exits.

**The `stale reply` notes are not the failure.** They are `wait_ready`'s readiness pings
(request id 0) that were answered after their own 10 s budget; `Client.call` discards any reply
whose id is not the request's, by design, and prints the note. Benign.

**Diagnosis: a bound too small for this class of command.** The bound that expired is the
harness's `SOCKET_TIMEOUT = 30.0`, written for a socket round trip. The tree already answered
this class elsewhere — `tests/freeze_bounce_evidence.py:57` declares `RENDER_TIMEOUT = 180.0`
for the commands whose cost includes a whole engine start, and
`tests/control-render-presets.py:44` / `tests/control-stem-export-verb.py:71` adopted the same
number. `control-meter-commands.py` was the render-driving test that never got it.

**Fix** (`7ec118017`, tests only): `RENDER_TIMEOUT = 180.0` + `RENDER_COMMANDS = ("render.render",)`
declared at the top with its reason, applied in `Session.reply`. Every command **not** in the
list keeps `SOCKET_TIMEOUT` (30 s), so a genuine hang still costs seconds — this is a scoped
budget, not a raised timeout.

**Local proof** (this box is x86_64, so the arm64 child cost is *injected*, and stated: a
`/proc`-verified `SIGSTOP` of **35 s** on the render child, the technique
`docs/RENDER-CHILD-WAIT.md` uses). Exit codes unpiped:

| run | injection | result |
|---|---|---|
| BEFORE — HEAD's `tests/control-meter-commands.py`, run as a temp copy inside `tests/` | SIGSTOP pid 241441, `state=T`, held 35 s (10:38:16 → 10:38:51) | **EXIT=1** — `control_socket_harness.Blocked: no response line inside 30.0s (socket timed out)`, `diagnosis: the instance is STILL RUNNING (pid 241045) - a HANG, not a crash` — the CI's failure, reproduced |
| AFTER — this commit | SIGSTOP pid 242110, `state=T`, held 35 s (10:39:11 → 10:39:46) | **EXIT=0** — 25 checks ok, `PASS: meter.* control-surface transcript (every check held)` |
| control — no injection, same build | — | `ctest -R ControlMeterCommands` → **Passed 13.40 s (EXIT=0)** |

The binary both halves ran is **this worktree's own build of `eba78f8ff`** (`Zene Studio
0.2.1-alpha.563+eba78f8`, Qt6, RelWithDebInfo, `WANT_VST3=OFF WANT_CLAP=OFF` — neither test's
path needs them; the plugin modules these two tests do load, `libaudiofileprocessor` and
`libtripleoscillator`, are built and were what the first local runs were missing, which is worth
knowing for anyone re-running this: a `zene`-target-only build renders silence and the meter
test's *live* half fails for that reason, not for the one diagnosed here), not the main clone's.

**Deliberately NOT fixed here:** the product defect `docs/RENDER-CHILD-WAIT.md` records — the
control surface is dead for the whole lifetime of the render child, `control.ping` included, so
a slow render and a hung instance look identical from outside. That is a deferred-reply change
to `ControlServer::dispatchLine` with a lifetime to get right, and it belongs to its own lane.

---

## 2 · `MmpzGitDepthTest` (205) — 300 s was an x86 number

**What the log says:**

```
205: Test timeout computed to be: 300
205/206 Test #205: MmpzGitDepthTest ***Timeout 300.03 sec
```

and the *rerun* (`-V`) timestamps say slowness, not a hang: the five DAW-driving tests took
**93 / 90 / 45 / 45 s**, and the **fifth** (`test_a_conflicted_merge_output_loads`) was still
running when the bound hit — the same shape this lane's local injection reproduces below.

**The suite drives the DAW seven times**, which is what the 300 s was never sized for:

| call site | DAW invocations |
|---|---|
| `BinarySafety._loads` × 2 (`timeout 180 <bin> render`) | 2 |
| `AudibleDiffBinary.test_identical_project_renders_identically` (`render_mix` per project) | 2 |
| `AudibleDiffBinary.test_a_real_edit_is_localised_to_track_and_bar` (`render_tracks` per project) | 2 |
| `AudibleDiffBinary.test_render_recipe_script_runs` (the recipe's own render) | 1 |

On the arm64 runner each one costs ~45 s (a full `Engine::init` plus the audio), so the suite
needs ~400 s there; on x86 it is 139.41 s.

**This is NOT the macOS mechanism, and the difference is stated because it decides the fix.**
`e021de5dd` (`030/wplat-mac`) fixed a *different* cause on macOS: GNU `timeout` is absent there,
so `_loads` raised `FileNotFoundError`. **On the linux-arm64 job `timeout` exists and runs** —
`test_a_clean_merge_output_loads ... ok` in that rerun **is** a `timeout 180 <bin> render`
exiting 0, and the log carries no `FileNotFoundError`. So `_loads`'s coreutils bound fires
nowhere, and this lane **leaves that line untouched**: no duplicate of the mac lane's rewrite,
no textual conflict with it.

**Fix — two bounds, neither weakening a proof (`8fc8cb044`):**

1. `tools/mmpz-git/tests/test_mmpz_git.py` — every DAW-driving call the suite makes now carries
   the tree's declared per-render budget (`RENDER_TIMEOUT = 180.0`, the
   `tests/freeze_bounce_evidence.py:57` number) through a small `run_bounded(argv, renders)`
   helper: the two `audible-diff` invocations (2 renders each → 2 × 180 s) and the render recipe
   (1 render → 180 s). A hang is now a **named failure at the call that hung**. Line-neutral:
   the file was at its 966-line ratchet and is at **964** (four docstrings tightened);
   `tests/file-length-baseline-tools.tsv` records the shrink in the same commit.
2. `tests/CMakeLists.txt` — `MmpzGitDepthTest` `TIMEOUT 300` → **`TIMEOUT 1800`**, sized from
   the measurement: 7 bounded DAW calls × 180 s = 1260 s, plus the suite's non-DAW work
   (measured **85.3 s** on this box; allow 3× on the slow runner) = ~1520 s. The backstop is
   deliberately larger than the sum of the internal bounds, so a hang fails at **its** bound
   with a message rather than as a bare ctest `Timeout`. 1800 is also the tree's figure for a
   render-heavy ctest (`ControlGoldenAudio`).

**Local proof**, with the arm64 per-render cost injected as a **45 s sleep in front of every DAW
invocation**. The injection is an ELF shim at `build/zene` that execs `build/zene-real` — an ELF
and not a shell script **because** `render.render` spawns its child from
`QCoreApplication::applicationFilePath()` (i.e. `/proc/self/exe`), which a shell wrapper does not
reach (measured: a script wrapper injected the instance start only, and the suite stayed at its
un-injected 95 s). Exit codes unpiped:

| run | constraint | injected | result |
|---|---|---|---|
| control | `ctest -R MmpzGitDepthTest -V` (property 1800) | none | **Passed 95.14 s (EXIT=0)** |
| OLD | 300 s wall clock | 45 s × 7 | **killed at 300.00 s, EXIT=124** — 4 tests ok and the **fifth still running**: the CI's shape reproduced |
| NEW | `ctest -R MmpzGitDepthTest -V` (property 1800) | 45 s × 7 | **Passed 408.89 s (EXIT=0)**, `Test timeout computed to be: 1800` |
| HANG CONTROL | the suite's own 2 × 180 s `audible-diff` bound | 400 s × 2 (> 360) | **EXIT=1 at 360.2 s** — `AssertionError: .../mmpz_git.py audible-diff did not finish 2 DAW render(s) inside 360 s` |

Injection evidence (the shim's stderr is captured into the tool's own render logs):
`/tmp/mmpz_ad_id/A.render.log` and `B.render.log` each carry
`zene-latency-shim: sleeping 45s before exec <worktree>/build/zene-real`.

Note on method: **`ctest --timeout 300` does NOT override a test's own `TIMEOUT` property**
(observed: the run passed at 408.89 s under `--timeout 300`), so the OLD half imposes the same
300 s wall clock with coreutils `timeout 300` instead. The hang control is the half that says the
new bounds are not vacuous: a call that never finishes still fails, at 360 s, named.

---

## Gates

Every gate the tree carries, run in this worktree against this tree, exit codes unpiped:

```
bash tests/file-length-gate.sh --check                    EXIT=0
bash tests/file-length-gate.sh --check --scope tools      EXIT=0
bash tests/complexity-gate.sh --check                     EXIT=0
bash tests/complexity-gate.sh --check --scope tools       EXIT=0
bash tests/duplication-gate.sh --scope tools              EXIT=0
bash tests/fork-sources-gate.sh                           EXIT=0
bash tests/no-upstream-regression-gate.sh                 EXIT=0
bash tests/all-sources-reproduce.sh                       EXIT=0
bash tests/unregistered-tests-gate.sh                     EXIT=0
```

No gate, baseline or manifest was weakened; the one baseline this lane writes moves the ratchet
**down** (`test_mmpz_git.py` 966 → 964, recorded in `8fc8cb044`).

## Files

| file | change |
|---|---|
| `tests/control-meter-commands.py` | declared `RENDER_TIMEOUT` / `RENDER_COMMANDS`, applied in `Session.reply` |
| `tools/mmpz-git/tests/test_mmpz_git.py` | `RENDER_TIMEOUT` + `run_bounded()`; the 3 DAW-driving call sites bounded; four docstrings tightened (966 → 964 lines) |
| `tests/CMakeLists.txt` | `MmpzGitDepthTest` `TIMEOUT 300` → `1800` + the measurement comment (minimal, additive: one value and its reason) |
| `tests/file-length-baseline-tools.tsv` | the ratchet's own shrink record (966 → 964) |

## What is NOT verified here, and what the next CI run must show

* **arm64 truth.** This box is x86_64. Both fixes are *bounds*, so the injection (45 s per DAW
  invocation; SIGSTOP 35 s for the meter test) is a stated stand-in for the measured arm64 cost,
  not a measurement of it. The proof they are right is the next `linux-arm64` job:
  **ControlMeterCommands green**, **MmpzGitDepthTest green**, and `Test timeout computed to be:
  1800` in its `-V` output.
* **The meter test's second render** (the disarm half) is bounded by the same mechanism; it was
  not the one that expired in CI.
* **`render.render`'s product defect is untouched** (see item 1) — a slow render still makes the
  control surface unanswerable; only the *test's* bound is now honest about it.
* **Residual margin, stated.** `ControlMeterCommands` carries ctest `TIMEOUT 600` and needs, on
  arm64, one engine start (~34 s) plus two renders (~45 s each) plus the checks — passing, with
  less margin than x86 (13.20 s). If that ever tightens, the number to raise is that test's
  registered `TIMEOUT`, with the same measurement discipline used here.
