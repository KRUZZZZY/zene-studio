# WPLAT-MAC — the macOS platform deltas of run 35126160372

**Lane:** PLATFORM-DELTA (MACOS). **Branch `030/wplat-mac`**, worktree `zene-030/wplat-mac`
(a worktree of the `lmms` fork). **Base:** `eba78f8ff` — the head of run `35126160372`
(attempt 1; mac jobs started 2026-09-16T17:07Z). **Tip:** `33fb40cf9` plus this report's own
commit. **Date:** 2026-09-17. Nothing in this lane is pushed, nothing is merged, and no gate,
baseline, manifest or workflow step was removed or weakened.

The lane exists because six tests are red on macOS that are not red on the same commit
elsewhere. **Four of them are platform deltas in the test or CI layer** — a GNU tool that does
not exist on Darwin, a bundle layout hardcoded to Linux, a Python provisioning step that
installed into the wrong interpreter, a forked child whose death arrived as a default-action
signal — and each is fixed on the evidence its log carries. **One (item 5) is an unexplained
render difference** that this lane could not diagnose from the log and therefore *instrumented*
instead of papering over, and **one (item 6) is not a platform delta at all** (it fails on
linux-x86_64 too): it is left to the parent merge, deliberately.

| # | item (ctest) | red on | root cause | commit | residual owed |
|---|---|---|---|---|---|
| 1 | `ControlMasteringCommands` (186) | arm64 only | the numpy/scipy step provisioned Homebrew's `python3.14`; ctest runs `/usr/local/bin/python3` | `dea96ef65` | CI-proof: 186 green on arm64 |
| 2 | `MmpzGitDepthTest` (205) + the mac backtrace step | both mac arches | GNU `timeout` is absent on macOS: the load probe shells out to it, and the backtrace step is a silent no-op without it | `e021de5dd` | CI-proof: 205 green both arches; the step prints lldb |
| 3 | `Vst3ChunkProbeTest` (152) | both mac arches + msvc-x64 | the chunk-probe fixture hardcoded the Linux bundle layout and entry point on every platform | `c1a2f9dcb` | CI-proof: 152 green on both mac arches and msvc-x64 |
| 4 | `SafeStartTest` (48) | both mac arches | the forked child died as a *default-action* signal death, not the disposition the test assumed (proven on linux only — no macOS here) | `85103e377` | CI-proof: 48 green both arches (not compiled here) |
| 5 | `ControlGoldenAudio` (189) | arm64 only | **not diagnosed** — one window's peak differs by 1748.015 LSB against a 1 LSB limit, with every other term inside 5e-5 dB | `33fb40cf9` | the next arm64 run names the window and the direction; then decide product defect vs re-record |
| 6 | `ControlShutdownHookTest` (39) | arm64 (`-j3`) + linux-x86_64 | `ControlRegistry::destroy()` may hand the re-created singleton the same address, so the raw-pointer comparison in the test is unsound on any allocator | **none — by design** | owed to the parent merge with the linux-instance lane (same file, same line) |

---

## Evidence base

The mac CI logs were gone from this box after the reboot, so they were re-fetched for this
report with the lane's own recipe (the token command is the one `RESUME-NOTES.md` records):

```
zene-gh-token gh api "repos/KRUZZZZY/zene-studio/actions/jobs/<ID>/logs" --allow-escape-sequences
```

| job | arch | result | where its log is now |
|---|---|---|---|
| `104895806458` | `macos-arm64` | **6 failed / 206** | `/tmp/ga_probe/mac-arm64.clean.log` (8200 lines) |
| `104895806868` | `macos-x86_64` | **3 failed / 206** | `/tmp/ga_probe/x86.clean.log` (7893 lines) |
| `104895806780` | `linux-x86_64` | **5 failed / 209** | `/tmp/ga_probe/linux.clean.log` (10915 lines) |
| `104895806855` | `msvc-x64` | 5 failed (build + tests) | `/tmp/ga_probe/msvc.clean.log` (5862 lines) |

Every number quoted below is from those four files (line numbers are the *cleaned* copies:
timestamps and ANSI stripped — `sed 's/^[0-9T:.-]*Z //; s/\x1b\[[0-9;]*[a-zA-Z]//g'`). The job
API confirms the mac jobs belong to run `35126160372`, attempt 1, head `eba78f8ff91cc…`.
**Ctest numbers are per job** — the mac jobs register 206 tests, linux-x86_64 209 — so the same
test can carry a different number on another platform (on linux `ControlGoldenAudio` is #192 and
`ControlMasteringCommands` is #189); every `NN` in this report's headings is the **mac** number,
and a cross-platform claim is always made by test name.
*Correction:* the `SafeStartTest` commit message (`85103e377`) prints the run id as
`26126160372`; the API and the job logs say `35126160372`. The commit is not rewritten — this
report uses the API's value.

Failed sets, verbatim from each job's summary:

```
macos-arm64   (mac-arm64.clean.log:7633)          macos-x86_64  (x86.clean.log:7560)
The following tests FAILED:                       The following tests FAILED:
     39 - ControlShutdownHookTest                     48 - SafeStartTest
     48 - SafeStartTest                              152 - Vst3ChunkProbeTest
    152 - Vst3ChunkProbeTest                         205 - MmpzGitDepthTest
    186 - ControlMasteringCommands
    189 - ControlGoldenAudio
    205 - MmpzGitDepthTest

linux-x86_64  (linux.clean.log:10574)
The following tests FAILED:
     16 - ClipLinkTest (Subprocess aborted)
     17 - ClipLinkPersistenceTest (Subprocess aborted)
     39 - ControlShutdownHookTest (Failed)
     48 - SafeStartTest (Failed)
     57 - ImportDetectionTest (Subprocess aborted)
```

The difference between the two mac lists is itself evidence: **186 and 189 are arm64-only**
(both pass on x86_64 — 186 in 12.49 s, 189 in 44.73 s — and on linux), and **39 is not
arm64-specific**: it failed there in the `-j3` run, passes in isolation, and on linux-x86_64 it
fails in the `-j3` run *and* its rerun (§6). The mac jobs publish no ctest artefact (only msvc
does), so the per-test output quoted here comes from the runner's stdout log.

---

## 1 · `ControlMasteringCommands` (186) — arm64 only · `dea96ef65`

**Root cause (proven from the log).** The ctest line names its interpreter:

```
186: Test command: /usr/local/bin/python3 ".../tests/control-mastering-commands.py" ".../build/zene"
                                        (mac-arm64.clean.log:7708)
186: auto-mastering-demo.py needs numpy and scipy for its independent BS.1770-4 measurement:
     No module named 'numpy'                                                       (…:7713)
```

but the provisioning step's own `python3` was Homebrew's versioned interpreter
(`/opt/homebrew/opt/python@3.14/bin/python3.14 2.5.3 1.18.1`, `…:6832`), reached through the
PEP 668 refusal and its retry (`error: externally-managed-environment`, `…:6786` →
`Successfully installed numpy-2.5.3 scipy-1.18.1`, `…:6830`). The install therefore landed in
**Homebrew's** python, and the test's own interpreter — `/usr/local/bin/python3`, the
`PYTHON3_EXECUTABLE` that `find_program()` at `tests/CMakeLists.txt:1896` feeds to every python
ctest — never saw the modules. On macos-x86_64 Homebrew *is* `/usr/local`, so the two names
collapse into one interpreter and `186 … Passed 12.49 sec` (`x86.clean.log:7381`).

`tools/auto-mastering-demo.py` is loaded **by path in the test's own interpreter**
(`tests/mastering_probe_lib.py:shipped_fixture()`), so only that interpreter matters; installing
for any other python is a no-op.

**Fix.** The mac step reads `PYTHON3_EXECUTABLE` out of `build/CMakeCache.txt` — the exact value
the build handed the tests — and provisions that interpreter, keeping the PEP 668 retry, and
fails loudly if the cache names no usable interpreter.

**Evidence, proven here.** YAML parses and `yamllint` exits 0; the cache extraction was run
against a fixture `CMakeCache.txt`; the guard fires on an empty value. **Residual (CI-proof
owed):** the next macos-arm64 run must print `/usr/local/bin/python3 2.5.3 1.18.1` from the
provisioning step and 186 must be green; x86_64 must stay green.

---

## 2 · `MmpzGitDepthTest` (205) + the mac backtrace step · `e021de5dd`

**Root cause — one absence, two faces.** `timeout` is GNU coreutils and does not exist on
macOS. The mmpz-git load probe shells out to it (`tests/test_mmpz_git.py:482`, `_loads()`):

```
205:   File ".../tools/mmpz-git/tests/test_mmpz_git.py", line 482, in _loads
205:     r = subprocess.run(["timeout", "180", self.BIN, "render", path, ...
205: FileNotFoundError: [Errno 2] No such file or directory: 'timeout'
205: FAILED (errors=2, skipped=1)          — Ran 45 tests in 107.649s
                                        (mac-arm64.clean.log:8014-8048; the same two errors, same line of the probe, on x86.clean.log:7509/7538)
```

and the same absence turns the workflow's "Signal-death backtrace" step into a no-op on every
mac job:

```
=== lldb backtrace of failed test SafeStartTest ===
/Users/runner/work/_temp/…sh: line 19: timeout: command not found
=== lldb backtrace of failed test Vst3ChunkProbeTest ===
… line 19: timeout: command not found
                                        (mac-arm64.clean.log:8099-8103)
```

That second face is why **no mac red in this run has a stack frame** — the step that exists to
produce one silently printed nothing.

**Fix.** `_loads()` bounds the DAW call itself (`subprocess.run(..., timeout=180)`) and turns
`TimeoutExpired` into an explicit hang failure, with no dependency on a coreutils binary; the
workflow step resolves `timeout`/`gtimeout`/neither once and uses what it found (all three job
copies of the step stay byte-identical). The file is kept at its ratchet value: 966 lines.

**Evidence, proven here.** `BinarySafety` → `OK` 2 tests, `EXIT=0`, against a stand-in
`build/zene`; the hang path with the bound temporarily set to 1 s → `AssertionError: … the DAW
hung loading … (180 s)`, `EXIT=1` after 1.086 s; all three timeout-resolution branches run
`rc=0`; YAML, `yamllint` and `bash -n` clean. **Residual (CI-proof owed):** 205 green on both
mac arches, and the mac backtrace step must print real lldb output the next time a test dies
by signal.

---

## 3 · `Vst3ChunkProbeTest` (152) — both mac arches + msvc-x64 · `c1a2f9dcb`

**Root cause (proven).** The fixture hardcoded the Linux bundle layout and entry point on every
platform, so every non-Linux loader looked for a file that was never built:

```
152: FAIL! : lmms::vst3::Vst3ChunkProbeTest::initTestCase() 'm_plugin.load(path, …)' returned
     FALSE. (The bundle "vst3-chunk-probe.vst3" couldn't be loaded because its executable
     couldn't be located.)                       (mac-arm64.clean.log:7181, Loc …Test.cpp(132))
[ 28%] Building CXX … vst3-chunk-probe … /public.sdk/source/main/linuxmain.cpp.o   (…:2618)
LoadLibraryW failed for path D:/a/…/vst3-chunk-probe.vst3\Contents\x86_64-win\vst3-chunk-probe.vst3:
     The specified module could not be found.   (msvc-x64 job 104895806855, msvc.clean.log:5320;
                                                that job's FAILED list carries 152 at :5402)
```

`vst3-test-effect` — the fixture that passes on macOS — already carries the correct
per-platform contract, which is why this one defect is visible on three platforms at once.

**Fix.** Per-platform subdir / executable name / entry `main` (+ `Info.plist.in` for APPLE, +
two configure-time contract assertions), mirroring `tests/data/vst3-test-effect/CMakeLists.txt`.

**Evidence, proven here.** Three configures against a stubbed SDK (linux; `-DAPPLE=TRUE`;
`-DWIN32=TRUE`), all `EXIT=0`, and the *generated* `build.make` rules were read back:
`Contents/MacOS/vst3-chunk-probe` + `Contents/Info.plist` + `macmain.cpp`;
`Contents/x86_64-win/vst3-chunk-probe.vst3` + `dllmain.cpp`; `Contents/x86_64-linux/…so` +
`linuxmain.cpp`; generated plist carries `CFBundleExecutable = vst3-chunk-probe`.
**Residual (CI-proof owed):** 152 green on both mac arches and msvc-x64.

---

## 4 · `SafeStartTest` (48) — both mac arches · `85103e377` (NOT compiled here)

**What the log says, and all it says.** Both signal slots failed on both mac arches, silently —
no `QFATAL`, no child output, no crash report:

```
48: FAIL!  : SafeStartTest::realSignalLeavesTheMarkerAndTheNextLaunchStartsSafe()
     'WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV' returned FALSE.
     (the child must still die by SIGSEGV, as it would without any of this)  …cpp(187)
48: FAIL!  : SafeStartTest::sigkillAlsoLeavesTheMarker()
     'WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL' returned FALSE.
     (the child must die by SIGKILL)                                         …cpp(265)
                                        (mac-arm64.clean.log:6961-6964; the same two failures on x86_64 at x86.clean.log:7586-7588)
```

The SIGKILL slot matters: its child sends itself an **uncatchable** signal, so if that slot fails
the child never reached that line as intended. The isolated rerun repeats both failures
(`48: Totals: 7 passed, 2 failed`, `…:7688`).

**Measured mechanism, from linux-x86_64** (the only platform provable here: job
`104895806780`, `linux.clean.log:10208/10226`, repeated in that job's rerun at `:10647/10665`).
That child ran qtestlib's inherited `FatalSignalHandler`

```
=== Received signal at function time: 3ms, total time: 4ms, dumping stack ===
QFATAL : SafeStartTest::realSignalLeavesTheMarkerAndTheNextLaunchStartsSafe() Received signal 11
```

and died by the **SIGABRT** that `qFatal()` raises, not by SIGSEGV — an inherited Qt handler was
converting the crash the test demands.

**Fix (assertions unchanged).** The SIGSEGV child resets the fatal signals to `SIG_DFL` before
its deliberate fault, so no inherited disposition can convert it ("as it would without any of
this" *is* the default-action death — the product's own crash path re-raises with `SIG_DFL` for
the same reason, `src/core/CrashReporter.cpp:130-134`); `waitBounded()` continues a merely
*stopped* child instead of reading Darwin's stopped-child status as a wrong-signal death; and
both failure messages decode the wait status in words. A useful corroboration in the same run:
`CrashReporterTest` (47) passes on mac because its child *arms the product handler*, which
re-raises with `SIG_DFL` — the exact shape this fix gives the SafeStart child.

**Evidence, honestly bounded.** This box has no macOS and no Qt5 headers, so the file was
**not compiled or run here**; the two mechanisms above are argued from the CI logs and the Qt
sources. `tests/src/core/SafeStartTest.cpp` is exactly 500 lines (the whole-tree limit), so the
long reasoning lives in the commit message and here. **Residual (CI-proof owed):** 48 green on
both mac arches — and if macOS's mechanism turns out to be a third one (e.g. the child dying
inside `install()/beginSession()`), the decoded status will name it; fix *that*, do not widen
the assertion.

---

## 5 · `ControlGoldenAudio` (189) — arm64 only · `33fb40cf9` (diagnostic, not a fix)

**What failed, exactly** (this is the whole of the evidence the run carries):

```
=== socket-1track-2clips / render (render.render): 3 runs, floor over 3 pairs ===
  floor max |delta|    : 0.000 LSB (-inf dBFS)
  byte-identical pairs : True of 3
  run levels           : -11.042, -11.042, -11.042 dBFS
  floor vs record      : measured 0.000 LSB (-inf dBFS), recorded 0.000000 LSB on build fd10f16021277708
  golden vs record:
    frames               226560 vs 226560         limit 0            ok
    window count         103 vs 103               limit 0            ok
    envelope delta       0.000049 dB              limit 0.01         ok
    peak envelope delta  1748.015 LSB             limit 1            FAIL
    level delta          +0.000011 dB             limit 0.01         ok
                                        (mac-arm64.clean.log:7294-7311)
```

Three facts that shape the reading:

* **It is deterministic, not noise.** The three renders of the same fixture are byte-identical
  (`floor max |delta| 0.000 LSB`, `byte-identical pairs: True of 3`), so the 1748.015 LSB is a
  difference between this build's render and the committed golden, reproduced every run.
* **All three headline paths fail with the same number** — `render`, `stems` and `bounce` each
  report `peak envelope delta 1748.015 LSB limit 1 FAIL` (`…:7310`, `…:7329`, `…:7348`), i.e.
  the same one-window difference survives three different export paths.
* **The tolerance is the strictest the programme has.** That row records `floor_max_lsb
  0.000000` and `floor_identical_bytes yes` (`tests/golden-audio-record.tsv`), i.e. its renders
  were byte-identical when the golden was measured, so the peak term is judged at the 1 LSB
  programme floor — any peak that moved at all fails. The demo fixture in the same run passes
  comfortably at `5121.010 LSB limit 26550 ok` (`…:7419`) — the defect is specific to the socket
  fixture's row.

**Not a fixture defect, and not the numpy failure.** `186`'s message ("No module named numpy")
belongs to a different test; the same golden rows pass on **macos-x86_64**
(`195/206 Test #189: ControlGoldenAudio … Passed 44.73 sec`, `x86.clean.log:7399`) and on linux
(`193/209 Test #192: ControlGoldenAudio … Passed 19.47 sec`, `linux.clean.log:10531`). What is
left is a real difference in the arm64 render's *peak envelope*: one window's peak moves
1748.015 LSB ≈ 0.0533 in normalised amplitude, while the **worst** window of the RMS envelope
term moves 0.000049 dB. A transient that moves and a window energy that does not.

**Reading of the term (`tests/golden_audio_record.py:compare_fingerprints`).** The per-window
PEAK term is compared in LSB and is **not** skipped for windows that are digital silence in the
record (only the `-inf` dBFS envelope term is). On this row 67 of the 103 record windows are
`0.000000`, so a small blip in one of them is consistent with everything the log says — but the
log could not say *which* window, nor whether the render rose above the record or fell below it.

**What this lane committed.** Not a fix and not a re-record: the diagnostic. The peak term now
prints the window that reached the worst value, with the record's value, the measured value and
the signed delta, as one more line on the same `lines` list both callers already echo
(`judge_measurement` and `control-golden-audio.py`'s `negative_control`):

```
    peak envelope delta  1748.015 LSB             limit 1            FAIL
    worst peak window    window 18 of 103: record 0.000000, measured 0.053345, delta +1748.015 LSB (measured - record)
```

(rendered here on the `socket-1track-2clips / render` row with one of its 67 silence windows
moved by the logged 1748.015 LSB; the next arm64 run prints the real one).

It is additive by construction and measured, not asserted:

* the index and both values ride in the `max()` that already picked the worst window (a tuple:
  delta, index, the pair), so no second pass and **no new branch**;
* lizard CCN for `compare_fingerprints` is **17 before and after** (`tests/complexity-gate.sh
  --check` → `EXIT=0`; the 17 is grandfathered in `tests/complexity-baseline.tsv`);
* the verdict is untouched: a side-by-side run of HEAD's function and the new one over 10 inputs
  × all 4 committed record rows (40 cases — the 1748.015 LSB case, the same window moved down, a
  20 LSB blip, the loudest window moved, identical, a tie, frames drift, window-count drift,
  empty peak envelopes, an all-silent render) shows `new_lines[:-1] == old_lines` and
  `old_passed == new_passed` on **every** one;
* `tests/golden_audio_selftest.py` (`GoldenAudioSelfTest`, the instrument's own control) still
  passes; `tests/file-length-gate.sh --check` → `EXIT=0` (the file is 380 lines; it stayed under
  the 500 limit). **`tests/control-golden-audio.py` was NOT touched** — it is exactly 500 lines.

**Residual.** The window and the direction are still unknown until macos-arm64 runs again — that
is the point of the change. Then the decision the lane deliberately did not pre-empt: an arm64
render delta at 0.0533 amplitude in one window, at 1e-5 dB in every envelope term, is either a
real product difference on arm64 (a defect: find the codegen/FP path) or a re-record of the
golden row (a recorded decision, with the reason in the record's provenance — "NEVER rewrite a
row to make a lane green" is the file's own rule). Nothing here authorises a re-record.

---

## 6 · `ControlShutdownHookTest` (39) — evidence only, deliberately not touched

**Evidence.** In the arm64 `-j3` run:

```
39: FAIL!  : ControlShutdownHookTest::aHookSurvivesTheRegistryInstanceItWasRegisteredOn()
     'recreated != registry' returned FALSE. (destroy() did not build a new instance for
     instance())                              (mac-arm64.clean.log:6928, …Test.cpp:86)
```

In isolation the same binary passes 6/6 (`39: Totals: 6 passed, 0 failed`, `…:7664`). macos-x86_64
passed it in the `-j3` run (`x86.clean.log:7059`). linux-x86_64 fails it in **both** the `-j3` run
(`linux.clean.log:10162`, in a 5-of-209 list) **and** its rerun (`linux.clean.log:10696-10705`,
5 failed of 5 — that job's rerun does not clear it, unlike the mac job's, where 39 passes 6/6).

**Root cause.** `ControlRegistry::destroy()` deletes and nulls the singleton
(`src/core/ControlRegistry.cpp:141-145`); the object re-created by the next `instance()` call
*may* land on the same address, so `recreated != registry` is an unsound way to say "a new
instance was built" — on any allocator, on any platform. It is a genuine defect in the test,
not a platform delta.

**Why no fix here.** The file is shared with the linux-instance lane (a sibling lane is on the
same lines), and per this lane's dispatch a duplicate edit on the same line is the risk to
avoid: **the parent resolves it.** A sound re-derivation, for whoever lands it: the test
already asserts the re-created instance's own observable state on the same object —
`recreated->runShutdownHooks(); QCOMPARE(ran, 1); QCOMPARE(recreated->shutdownHookCount(), 0)`
(`tests/src/core/ControlShutdownHookTest.cpp:90-92`) — so the unsound `recreated != registry`
pointer comparison at `:86` can simply go, or be replaced by a monotonic instance serial if one
is ever added (note `include/ControlRegistry.h` is grandfathered over the 500-line limit at 509
lines, so it cannot grow — a member cannot be added there without a deliberate ratchet
re-anchor).

---

## Gates and checks — as run on this tree

| check | mode | result |
|---|---|---|
| `python3 -m py_compile tests/golden_audio_record.py` | — | `EXIT=0` |
| `python3 tests/golden_audio_selftest.py` | GoldenAudioSelfTest, no build needed | `EXIT=0` (every check PASS) |
| additivity probe (HEAD's function vs the new one) | 4 record rows × 10 inputs | `EXIT=0`, 40 ok / 0 fail |
| `bash tests/complexity-gate.sh --check` | ratchet, fork scope | `EXIT=0`, no regressions |
| `bash tests/file-length-gate.sh --check` | ratchet, fork scope | `EXIT=0`, no regressions |
| `bash tests/fork-sources-gate.sh` | namespace/manifest | `EXIT=0`, PASS (1732 sources; 660 fork, 1104 inherited, 40 tooling, 0 stale) |
| `bash tests/unregistered-tests-gate.sh` | every test source registered | `EXIT=0`, PASS (171 scanned; 169 registered, 2 declared-not-built) |
| `bash tests/all-sources-reproduce.sh` | whole-tree manifest | `EXIT=0`, REPRODUCES |
| `bash tests/no-upstream-regression-gate.sh eba78f8ff` | every upstream-code change declared | `EXIT=0`, PASS (0 changed paths declared; ledger 460 entries) |

`file-length-gate.sh --check --scope all` still fails, but **not on this lane's files** — the
three entries are pre-existing from other lanes (ScriptBindingsTest 741→752, ScriptEngineTest
627→651, ClapHostTest 705 new); `SafeStartTest.cpp` is no longer flagged.

## What could NOT be verified here (stated plainly)

* **No macOS, no Qt5 headers, no build tree** in this worktree: nothing on this lane was
  compiled or executed against the engine. Items 1–4 are proved only by log arithmetic, source
  reading, and the fixtures/`build.make`/cache reproductions listed above; item 4's file was not
  even compiled. Item 5's diagnostic is real but unexercised against a mac render.
* **CI-proof is owed on all six items** — one macos-arm64 run (and one macos-x86_64 run) after
  the merge train picks this branch up. Item 5 additionally needs the *decision* recorded.
* The mac jobs publish no ctest artefact, so post-fix runs will be judged from the runner's
  stdout log again; the "Signal-death backtrace" step must now print frames — if it does not,
  that is item 2's residual, not a new defect.

## Merge / hotspot notes

* **`build.yml` is shared with sibling branch `030/wplat-win`** (the mingw job). This lane's only
  edit to it is `e021de5dd` — the mac backtrace step's `timeout` resolution. The mingw region was
  not touched, and no other lane's region should be resolved by this branch.
* **`tests/src/core/SafeStartTest.cpp` and `tests/src/core/ControlShutdownHookTest.cpp` are
  both shared with the linux-instance lane**: expect merge work in exactly those two files, and
  item 6 is *left* to that merge by design (see §6).
* `tests/control-golden-audio.py` is exactly at the 500-line cap — the diagnostic went into
  `tests/golden_audio_record.py` (380 lines) for that reason, and any future golden-audio work
  has to keep respecting both caps.

---

## Follow-up (2026-09-17): item 4's fix was incomplete — refuted by run `35212797698`, re-fixed on `030/mac-forkfix`

**Where this stands.** The item-4 row above says the SIG_DFL reset is "CI-proof: 48 green both
arches". Run `35212797698` (head `8edfe30d5`, the merge tip) refutes that: both mac jobs still fail
*only* `SafeStartTest` (1 failed of 206, the same two slots, each "died by signal 6") on
`macos-arm64` (job `105174080436`) and `macos-x86_64` (job `105174080471`):
`realSignalLeavesTheMarkerAndTheNextLaunchStartsSafe` at `:229` and `sigkillAlsoLeavesTheMarker`
at `:312`. The reset is right for
the failure this lane diagnosed — a QtTest handler inherited by a fork-only child — and cannot help
here, because the child dies before it reaches the reset.

**The mechanism, from the jobs' own lldb step** (both jobs, one line per slot, verbatim):

```
SafeStartTest[58692:137404] Process 58685 was forked to 58692 without calling exec().
This is not supported by FileManager. Aborting.
SafeStartTest[58693:137406] Process 58685 was forked to 58693 without calling exec().
This is not supported by FileManager. Aborting.
```

`58685` is the test process (it logs `[qt.test.enter] realSignal…` a millisecond earlier);
`58692`/`58693` are its two fork children, one per slot. macOS CoreFoundation's fork-safety guard
aborts a forked child that never exec'd, on the child's first call into the framework — and the
first thing the child does is the engine's `install()` + `beginSession()`, which is such a call. So
the child dies by SIGABRT, which is the "died by signal 6" the run reports. (The plain ctest run has
no child output — os_log, not stderr — which is why run `35126160372` showed these slots as bare
FALSE.)

**The fix (`15ed9753f`, `tests/src/core/SafeStartTest.cpp`, +114/−115, 499 lines).** The child is no
longer a fork-only clone: both slots **fork AND exec** this binary in child mode
(`--safe-start-crash-child <workdir> segv|kill`, intercepted by a hand-written `main()` in place of
`QTEST_GUILESS_MAIN`, before `QCoreApplication` exists). The child runs the same engine calls in a
process image of its own — so it still writes the marker with its own pid, the record the next
launch reads back — and then dies by the signal the slot names, with the kernel's disposition. A
fork-only child no longer exists to be aborted, whatever a platform's runtime does to one. Every
`QVERIFY`/`QCOMPARE`/`QSKIP` line is byte-identical to `8edfe30d5`.

**Proofs** (linux, Qt 6.4.2 RelWithDebInfo, worktree `zene-030/wmacfork`):

- `env -u DISPLAY ./SafeStartTest` → `EXIT=0`, `Totals: 9 passed, 0 failed`.
- `strace -f -e trace=execve,wait4` on each slot — the child really re-execs and really dies by its
  signal:

  ```
  447467 execve(".../build/tests/SafeStartTest", ["...", "--safe-start-crash-child",
         "/tmp/SafeStartTest-MHzGlY", "segv"], ...) = 0
  447465 wait4(447467, [{WIFSIGNALED(s) && WTERMSIG(s) == SIGSEGV && WCOREDUMP(s)}], WNOHANG, NULL) = 447467
  447523 execve(".../build/tests/SafeStartTest", ["...", "--safe-start-crash-child",
         "/tmp/SafeStartTest-pGhGRH", "kill"], ...) = 0
  447522 wait4(447523, [{WIFSIGNALED(s) && WTERMSIG(s) == SIGKILL}], WNOHANG, NULL) = 447523
  ```

- The linux lane's Qt5-handler kit (`~/.cache/wplat-lin/forkhook.so`): the *unfixed* binary is red
  under it **in this environment** (`EXIT=1`, "FORKHOOK: the child's SIGSEGV ran the inherited
  harness-style (Qt5) handler" — the hook is live, so the green below is not vacuous); the new
  binary is green under it, whole suite and each slot alone (`EXIT=0`, 9 passed / 3 passed each).
  With exec, the hook's fork-return handler cannot outlive the child's own process image — which is
  the point of the change.
- `ctest -R 'SafeStartTest|ControlShutdownHookTest'` → `CTEST_EXIT=0`, 2/2 passed.
- Gates after the commit: file-length `--check` 0, complexity `--check` 0, fork-sources 0,
  no-upstream-regression 0, all-sources-reproduce 0.

**What CI must show.** Run #5 (the next mac run): `SafeStartTest` (48) PASS on both mac arches, the
two slots green because each child died by its intended signal, and 9/9 on the other platforms
unchanged. **Not verified locally:** there is no macOS host in this environment, so the CF abort is
refuted from the job logs and designed out — not reproduced here.
