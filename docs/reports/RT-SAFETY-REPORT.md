# Lane report — `030/rt-safety`, board card #678, feature row 52

**The real-time-safety whole-tree verification programme.** Branch `030/rt-safety`,
cut from `d7a402041` (the wave-9 integration train's record on `release/0.3.0`).

Shape of the report: what was added and where it is registered; what was **proved**
with real commands and exit codes; what does not work and what could not be
verified; the hotspots; one next action. The programme's own record — the four
files, the measurement, the bound, and how to work with the two ledgers — is
`docs/RT-SAFETY-SWEEP.md`, and the gate's entry is `tests/QA-GATES.md` Gate 12.

---

## 1. Branch and commits

| commit | what it is |
|---|---|
| `4fe7e9bec` | the programme: the sweep, its two ledgers, its controls, and the four Python files registered in `tests/fork-sources.txt` |
| `fff336487` | the two ctests registered in `tests/CMakeLists.txt`, recorded on the existing `tests/CMakeLists.txt` line of `tests/upstream-modifications.txt` |
| `116653b9d` | Gate 12 in `tests/run-all-gates.sh`, the step in CI's `static-gates` job, `tests/QA-GATES.md` Gate 12, `docs/CONVENTIONS.md` row 9, `docs/RT-SAFETY-SWEEP.md` |

Branch head: `116653b9d7d7ba880a8183ca444c35a5313179f2`. Nothing else was committed
in this lane; no push, no merge, no rebase.

## 2. What was added, and where it is registered

**The programme is a SWEEP, not another probe.** One declared scope, one allowlist,
one verdict over the whole tree:

| file | role |
|---|---|
| `tests/rt-safety-scope.txt` (new) | the **declared** audio-thread path set: `<path><TAB><symbol><TAB><reason>` — **27 pairs**, each with a reason (the render callback `AudioEngine::renderNextPeriod`, its four stages, `swapBuffers` + the two staging drains, `Song::processNextBuffer`, the `Mixer` pair, the four `SessionScheduler` entries, `MidiClock::processAudioPeriod`, `EffectChain::processAudioBuffer` / `processThroughGraph`, the in-class `AutomatableModel::incrementPeriodCounter`, `Controller::triggerFrameCounter`, `LfoInstances::trigger`, `ScriptEngine::audioThreadTick`, and the fork's own realtime entries: `SampleRecordAccumulator::append`, `RetroAudioRing::push`, `RetroMidiRing::push`, `MasterLoudnessTap::feed`, `LufsMeter::processBlock`). A blank reason is exit 2; a symbol that no longer resolves is exit 1; a file that is not in the tree is exit 2; an **empty scope is exit 2** (0 entries is an error, never a pass) |
| `tests/rt-safety-allowlist.txt` (new) | the **explicit allowlist with reasons**: `<path><TAB><symbol><TAB><rule><TAB><count><TAB><reason>` — **3 lines, 4 hits**, each saying which construct, on which line, why it is tolerated today and that the count may only fall |
| `tests/rt_safety_source.py` (new) | the rules — `alloc-new`, `alloc-c`, `alloc-smart`, `alloc-string`, `lock-guard`, `lock-call`, `grow-container`, `grow-script`, each in one of the rule's three categories (allocation / locking / growth) — and the source reader: comments and literals blanked with line structure preserved, a declared symbol resolved by brace-matched body extraction (with an in-class fallback for `static` members defined inside their class) |
| `tests/rt_safety_lib.py` (new) | both ledgers, the judgement (new hit / growth past a line / stale line / undeclared symbol) and the report, which prints the programme's **BOUND** on every run |
| `tests/rt-safety-sweep.py` (new) | the gate: `--check`, `--list-scope`, `--list-rules`, `--json`, `--reanchor "reason"`; exit **0** pass / **1** violation / **2** setup error |
| `tests/rt_safety_selftest.py` (new) | the controls (18 checks) |

**Registration (all four of the acceptance contract's applicable points):**

* **ctests** — `RtSafetySweep` (the sweep over the tree, `--check`, TIMEOUT 120) and
  `RtSafetySelfTest` (TIMEOUT 300), in the `PYTHON3_EXECUTABLE` block of
  `tests/CMakeLists.txt` beside `GoldenAudioSelfTest`. Neither needs a binary: the
  sweep reads sources, which is what lets a build with no audio device measure the
  rule at all.
* **gate** — Gate 12 in `tests/run-all-gates.sh` (banner, sweep unpiped, `SKIP` —
  never a pass — when no `python3` is on PATH, skip hint added).
* **CI** — a step in `.github/workflows/quality-gates.yml`'s `static-gates` job,
  which is enforced on every push and pull request; the job name now reads
  `static gates (3, 4, 6, 7, 8, 9, 11, 12)`.
* **manifests** — the four Python files are registered in `tests/fork-sources.txt`
  with a new pathspec line in both blocks of that file's own recipe, and the file's
  "Verify it" recipe prints REPRODUCES (measured, see §3). `tests/upstream-modifications.txt`
  records the two `add_test` entries on the file's existing `tests/CMakeLists.txt`
  line. Measured before registering: every function in the four files is CCN ≤ 10
  (lizard) and every file is under 500 lines, so gates 4 and 7 measure them at the
  existing thresholds rather than moving a baseline.
* **No command group, deliberately** — this row's own schema is "n/a (a programme,
  not a command)", so there is no control-surface id, no A16 row and no UI absence
  line to write. **The absence item this row carried is therefore moot**: the
  release documents' missing rt-safety line (`docs/KNOWN-LIMITATIONS.md`,
  `docs/RELEASE-NOTES-v0.3.0-alpha.md`) has nothing to declare, and
  `docs/FEATURE-LIST-0.3.0.md` rows 52 and 15 now record the programme as in the tree.

## 3. What was PROVED (command, exit code, verbatim)

**The positive control — the sweep bites, on the real tree.** A deliberate
allocation injected into a declared audio-thread path (`AudioEngine::renderStageMix`):

```cpp
	// rt-safety POSITIVE CONTROL (temporary, reverted in this same run): a deliberate
	// allocation on a declared audio-thread path. The sweep MUST fail on this line.
	{ float* rtProbe = new float[4]; rtProbe[0] = 0.0f; delete[] rtProbe; }
```

```
$ python3 tests/rt-safety-sweep.py --check ; echo POSITIVE_CONTROL_EXIT=$?
  measured  : 922 region line(s); 5 hit(s) in 4 key(s)
FAIL: NEW rt-safety hit: src/core/AudioEngine.cpp:AudioEngine::renderStageMix:alloc-new
      [allocation] - 1 occurrence(s), first at src/core/AudioEngine.cpp:446:
      { float* rtProbe = new float[4]; rtProbe[0] = 0.0f; delete[] rtProbe; }
POSITIVE_CONTROL_EXIT=1
```

Reverted (`git checkout -- src/core/AudioEngine.cpp`; the file byte-identical to its
pre-injection copy — `diff -q` → IDENTICAL):

```
$ python3 tests/rt-safety-sweep.py --check ; echo CLEAN_TREE_EXIT=$?
  declared  : 27 path:symbol pair(s)
  resolved  : 4 in-class form, 0 whole-file
  measured  : 918 region line(s); 4 hit(s) in 3 key(s)
  by rule   : allocation 0, locking 2, growth 2
  allowlist : 3 line(s); 3 key(s) allowlisted, 0 key(s) fresh
  ...
  PASS: every hit on a declared audio-thread path is allowlisted with a reason and at
        its allowed count.
CLEAN_TREE_EXIT=0
```

**The negative case and the bound control, in the same binary** —

```
$ python3 tests/rt_safety_selftest.py ; echo SELFTEST_EXIT=$?
  18 checks, all PASS, incl.:
  PASS POSITIVE CONTROL: a deliberate `new` / QMutexLocker / container growth … FAILS
  PASS POSITIVE CONTROL: a hit that grows past its allowlist count FAILS
  PASS ... and the CLI's OWN exit code is 1 (the number a gate records)
  PASS NEGATIVE CONTROL: a declared path with no forbidden construct PASSES
  PASS NEGATIVE CONTROL: the same allocation WITH an allowlist line PASSES
  PASS BOUND: the same allocation OUTSIDE the declared scope is NOT seen
  PASS BOUND: `new` in a comment and in a string literal is not a hit
  PASS (5 ledger refusals: empty scope 2, blank reason 2, unknown rule 2,
        unresolved symbol 1, missing file 2, stale line 1, undeclared symbol 1)
RESULT: PASS
SELFTEST_EXIT=0
```

**The ctests, from the build tree's `tests/` directory** (one configure, no build
needed; 196 tests total in the directory, so 0 tests was never a risk):

```
$ cmake -B wrtsafe/build -S . -DCMAKE_BUILD_TYPE=Debug -DWANT_QT6=ON   # CONFIGURE_EXIT=0 (61.5 s)
$ cd wrtsafe/build/tests && QT_QPA_PLATFORM=offscreen ctest -R RtSafety --output-on-failure
1/2 Test #148: RtSafetySweep ....................   Passed    0.28 sec
2/2 Test #149: RtSafetySelfTest .................   Passed    0.16 sec
100% tests passed, 0 tests failed out of 2
CTEST_EXIT=0
```

**The suite and the manifests** —

```
$ bash tests/run-all-gates.sh --no-mutation      # gate 12 of 12
================ Gate 12: real-time safety (whole-tree sweep) ================
  PASS: every hit on a declared audio-thread path is allowlisted …
…
12     rt-safety                PASS
SUITE_EXIT=1   (gates 4, 6, 7 and 11 are red for OTHER lanes' work — see §4)
```

```
$ bash tests/fork-sources-gate.sh ; echo EXIT=$?          # Gate 9, includes the manifest recipe
  633 fork-sources entry(ies), 1103 all-sources, 40 tools-sources, 0 stale
PASS: every tracked source in scope is registered
EXIT=0
$ bash tests/all-sources-reproduce.sh                     # REPRODUCES
$ <fork-sources.txt's own "Verify it" recipe> ; diff …    # DIFF_EXIT=0 (empty → REPRODUCES)
```

**Ratchet compliance of the new files** (so registering them moved no baseline):

```
$ lizard -l python -C 10 tests/rt_safety_source.py tests/rt_safety_lib.py \
      tests/rt-safety-sweep.py tests/rt_safety_selftest.py
No thresholds exceeded (cyclomatic_complexity > 10 …)
$ wc -l  →  340, 415, 207, 392   (all < 500)
$ bash tests/duplication-gate.sh → PASS: duplicated lines 0.51% (budget 5%)   EXIT=0
```

## 4. What does not compile / what does not work yet

* **Nothing in this lane**: no C++ was written or changed — the programme is
  Python plus two ledgers plus registrations — so there is no compile error to
  report. The full tree was **configured** (`CONFIGURE_EXIT=0`), not built.
* **Not run: the full ctest suite** (gate 1). It needs a built `zene` binary; only
  the two new ctests were executed, by name, from `wrtsafe/build/tests`.
* **Pre-existing reds on this tree, none of them mine** (measured before and after
  this lane's commits; no red line names a file this lane touched):
  * Gate 4 (fork-scope complexity): **57 regression lines** from other lanes'
    sources — e.g. `src/core/DawProjectWrite.cpp:dawProjectXmlFromModel` CCN 42,
    `DawProjectRead.cpp:parseDocument` CCN 41, `tests/stem_commands_lib.py:check_result`
    CCN 31.
  * Gate 7 (fork-scope file length): **21 files over 500 lines** (also not mine).
  * Gate 6 and Gate 11: one committed file, `docs/reports/VST3-INSTRUMENT-ROW78-EVIDENCE.log`
    (the row-78 lane) — a `.log` in a `git ls-files` scan is both committed evidence
    (Gate 11) and an undeclared divergence (Gate 6). It is the only violation either
    gate reports.
  * Consequently `run-all-gates.sh --no-mutation` exits **1** on this tree, not the
    expected 3 — inherited, and unchanged by this lane (Gate 12 itself is PASS).

## 5. What could NOT be verified, and the programme's stated bound

**Could not verify**: the two new ctests in any *built* configuration beyond a
configure (the tests are Python and need no build, so this is a statement about the
suite as a whole, not about these two); Windows/macOS ctest registration (the CMake
guard is `PYTHON3_EXECUTABLE`, the same one `GoldenAudioSelfTest` uses — CI's
`build.yml` matrix will exercise it); and CI itself (`gh` is not authenticated, and
this lane does not push).

**The programme's stated bound** — printed by every run, and written out in
`docs/RT-SAFETY-SWEEP.md` and `tests/QA-GATES.md`:

* **Static only.** The engine is not run. The **runtime** half of the rule stays
  with the `AllocationProbe` tests (`tests/src/core/AllocationProbe.h` and the
  ~20 test sources that use it); this programme is their static counterpart.
* **Declared scope, not a call-graph.** Only the 27 declared `path:symbol` pairs are
  measured; the sweep never follows a call. **A new audio-thread path that nobody
  declares is measured by nothing here** — the self-test's two BOUND controls assert
  exactly that silence, so the limit is a proof rather than a caveat. The frontier is
  the **render thread**; the capture thread's own paths keep their runtime probes.
* **No virtual dispatch, function pointers, macros or includes** — an implementation
  reached through an interface is checked only if the scope names it too.
* **No cost, no syscall, no I/O, and no lock-free correctness** (memory ordering,
  ABA) — only the three words of the `AGENTS.md` rule.
* **No aliasing analysis** — a hit is a *mention* of a construct, not proof that it
  runs on the audio thread; that is what an allowlist line with a reason is for.
* **The 4 accepted hits are debt with an address, not endorsements**: all four are
  upstream-inherited (`AudioEngine::renderNextPeriod`'s `std::lock_guard{m_changeMutex}`,
  `LfoInstances::trigger`'s `QMutexLocker`, `Song::processNextBuffer`'s two
  `TrackList push_back`s). Removing them is an engine change with its own concurrency
  proof; the ratchet keeps the count visible and can only move down.

## 6. Hotspots

`hotspot: tests/fork-sources.txt — every lane that adds a Python test driver edits
the same two recipe blocks; my change adds one pathspec line to each and four sorted
entries, so a merge conflict here is textual and resolvable by re-running the
"Verify it" recipe (which must print REPRODUCES).
hotspot: tests/CMakeLists.txt — appended one `add_test` block; three lanes appended
to the same locality today (GoldenAudio, VST3 instrument, this one).
hotspot: tests/run-all-gates.sh — the Gate 12 row is appended after Gate 11 and the
header's gate list is edited; the summary now has 12 rows, so any lane that
hard-codes 11 rows in a script or a doc must update.
hotspot: tests/upstream-modifications.txt — one line (`tests/CMakeLists.txt`) carries
every lane's registration reason appended in place, and it is getting very long;
whoever merges should keep appending rather than split it.
hotspot: docs/FEATURE-LIST-0.3.0.md rows 52 and 15 — both rewritten in place.
hotspot: docs/reports/VST3-INSTRUMENT-ROW78-EVIDENCE.log — a committed `.log` that
keeps Gates 6 and 11 red; it is another lane's file and this lane did not touch it.
hotspot: .github/workflows/quality-gates.yml — the `static-gates` job name enumerates
its gates, so the string changed; the repo has no required_status_checks binding it
yet (the file's own comment says so).`

## 7. The single next action

**Widen the declared frontier by one line and let the sweep report what it finds**:
add the next audio-thread path (the obvious candidates are the play-handle /
instrument render path — `PlayHandle::doProcessing`, `InstrumentTrack::processAudioBuffer` —
and the mixer-channel render path behind `MixerChannel::processAudioBuffer`) to
`tests/rt-safety-scope.txt`, run `python3 tests/rt-safety-sweep.py --check`, and for
each `NEW rt-safety hit` either fix it or allowlist it with a reason and its measured
count. That is the whole act: one line, one run, one reason per hit.

---

### Lane checklist

* [x] Engine work in the tree (the sweep, the rules, the two ledgers) — `4fe7e9bec`
* [x] Proof registered: ctests `RtSafetySweep` + `RtSafetySelfTest`, Gate 12, CI step
* [x] Exit codes recorded unpiped, positive control on the real tree (§3)
* [x] Explicit allowlist with reasons and a one-way ratchet (`--reanchor "reason"`)
* [x] Stated bound printed by every run and documented (§5)
* [x] Manifests: fork-sources (entries + recipe line, REPRODUCES) and
      upstream-modifications (the `tests/CMakeLists.txt` reason)
* [x] Nothing weakened: no gate, baseline, manifest or workflow step was relaxed or
      re-anchored by this lane
* [x] Build dir `wrtsafe/build` deleted after the ctest run; no push

### Absence item

The release-documents absence line this item was flagged for is **moot**: the
programme is in the tree, and there is no UI absence to declare (a programme, not a
command). `docs/FEATURE-LIST-0.3.0.md` row 52 now reads "in the tree" with the
evidence, and its absence table's row 15 is struck through accordingly. If the
release-document owner wants a presence line instead, the one-liner is:

> *Real-time safety: the whole-tree sweep (`tests/rt-safety-sweep.py`, Gate 12)
> enforces "no allocation, no locking, no unbounded growth on audio-thread paths"
> over the 27 declared paths; 4 hits, all inherited from upstream, are allowlisted
> with reasons and a ratchet that may only fall.*
