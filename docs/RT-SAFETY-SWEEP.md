# The real-time-safety whole-tree sweep (feature row 52, board card #678)

**What it is.** The sweeping enforcement of `AGENTS.md`'s realtime rule — *"no
allocation, no locking, no unbounded growth on audio-thread paths"* — over the
audio-thread paths this tree **declares**, with an explicit allowlist that carries
a reason and a **count** per accepted hit. The count is a ratchet: it may only
fall.

Until 2026-09-16 the rule was held **per feature**. `tests/src/core/AllocationProbe.h`
proved one path at a time, on the paths somebody wrote such a test for, and
nothing measured the rule across the tree. `docs/CONVENTIONS.md` row 9 said so in
as many words: *"**partially enforced** — a rule held by tests where they exist,
not by a sweeping gate"*. That row, and the "Realtime safety" line in
`docs/KNOWN-LIMITATIONS.md` / `docs/RELEASE-NOTES-v0.3.0-alpha.md` that did not
exist, are what this programme closes.

```sh
python3 tests/rt-safety-sweep.py                        # the gate (repo root), exit 0
python3 tests/rt-safety-sweep.py --list-scope           # the declared path set
python3 tests/rt-safety-sweep.py --list-rules           # the rules and why each bites
python3 tests/rt-safety-sweep.py --check --json out.json
python3 tests/rt_safety_selftest.py                     # the controls, exit 0
cd build/tests && ctest -R 'RtSafety' --output-on-failure   # both, as ctests
bash tests/run-all-gates.sh                             # Gate 12 runs the sweep
```

Exit codes: **0** = every hit on a declared path is allowlisted at its allowed
count; **1** = a new hit, growth past a line, a stale line, or a declared symbol
that no longer resolves; **2** = setup error (a ledger that does not parse, an
empty scope, a declared path that is not in the tree) — no verdict was reached.

## The four files

| file | what it is |
|---|---|
| `tests/rt-safety-scope.txt` | the **declared** audio-thread path set: `<path><TAB><symbol><TAB><reason>`. 27 pairs today, each with a reason. A blank reason is refused (exit 2); a symbol that resolves to nothing is a failure (exit 1); a path that is not in the tree is a setup error (exit 2); an **empty scope is an error, never a pass**. |
| `tests/rt-safety-allowlist.txt` | the **accepted** hits: `<path><TAB><symbol><TAB><rule><TAB><count><TAB><reason>`. Three lines today (4 hits). A blank reason or an unknown rule id is refused (exit 2). |
| `tests/rt_safety_source.py` | the rules (allocation / locking / growth) and the source reader: comments and literals are blanked with the line structure preserved, and a declared symbol is resolved by brace-matching its body. |
| `tests/rt_safety_lib.py` | the two ledgers, the judgement and the report — which prints the programme's **BOUND** on every run. |
| `tests/rt_safety_selftest.py` | the controls (ctest `RtSafetySelfTest`). |
| `tests/rt-safety-sweep.py` | the gate (ctest `RtSafetySweep`, Gate 12, CI `static-gates`). |

Registered: `tests/CMakeLists.txt` (both ctests, in the `PYTHON3_EXECUTABLE`
block beside `GoldenAudioSelfTest` — neither needs a binary), `tests/run-all-gates.sh`
(Gate 12), `.github/workflows/quality-gates.yml` (`static-gates`). The four Python
files are registered in `tests/fork-sources.txt`, whose own recipe derives them.

## What it measured on this tree (2026-09-16)

```sh
python3 tests/rt-safety-sweep.py --check ; echo EXIT=$?
  declared  : 27 path:symbol pair(s)
  resolved  : 4 in-class form, 0 whole-file
  measured  : 918 region line(s); 4 hit(s) in 3 key(s)
  by rule   : allocation 0, locking 2, growth 2
  allowlist : 3 line(s); 3 key(s) allowlisted, 0 key(s) fresh
  ...
  PASS: every hit on a declared audio-thread path is allowlisted with a reason and at
        its allowed count.
EXIT=0
```

The three allowlisted keys, all of them **upstream-inherited** code — the fork's
own declared realtime paths (record demux, retro audio/MIDI rings, the loudness
tap, the session scheduler, the MIDI clock, the Lua audio tick) are **clean**:

| key | hit | class |
|---|---|---|
| `src/core/AudioEngine.cpp:AudioEngine::renderNextPeriod:lock-guard` | `std::lock_guard{m_changeMutex}` at the top of the render callback | inherited LMMS |
| `src/core/EnvelopeAndLfoParameters.cpp:EnvelopeAndLfoParameters::LfoInstances::trigger:lock-guard` | `QMutexLocker` in the per-period LFO trigger called from STAGE 3 | inherited LMMS |
| `src/core/Song.cpp:Song::processNextBuffer:grow-container` | two `trackList.push_back` calls per period in the Pattern/MidiClip play modes | inherited LMMS |

They are **debt, not endorsement**: each line names the construct, the line it is
on and why it is tolerated today, and the ratchet fails when the tree measures
**fewer** than the line allows (the debt was paid — lower it or delete the line)
as well as when it measures **more** (a new hit, or growth). The only way a count
rises is `--reanchor "reason"`, which refuses a blank reason and refuses to run
while a real problem is outstanding. Removing the render-thread lock is an engine
change with its own concurrency proof; it is named here so the fix-up pass can
find it rather than rediscover it.

## The positive control — the sweep bites, on the real tree

A sweep that cannot fail proves nothing, so the claim is recorded with its
command and its exit code. A deliberate allocation was injected into a **declared**
audio-thread path (`AudioEngine::renderStageMix`, `src/core/AudioEngine.cpp`):

```cpp
	// rt-safety POSITIVE CONTROL (temporary, reverted in this same run): a deliberate
	// allocation on a declared audio-thread path. The sweep MUST fail on this line.
	{ float* rtProbe = new float[4]; rtProbe[0] = 0.0f; delete[] rtProbe; }
```

```sh
python3 tests/rt-safety-sweep.py --check ; echo POSITIVE_CONTROL_EXIT=$?
  measured  : 922 region line(s); 5 hit(s) in 4 key(s)
FAIL: NEW rt-safety hit: src/core/AudioEngine.cpp:AudioEngine::renderStageMix:alloc-new
      [allocation] - 1 occurrence(s), first at src/core/AudioEngine.cpp:446:
      { float* rtProbe = new float[4]; rtProbe[0] = 0.0f; delete[] rtProbe; }
POSITIVE_CONTROL_EXIT=1
```

Reverted (`git checkout -- src/core/AudioEngine.cpp`, the file byte-identical to
its pre-injection copy), the same command:

```sh
python3 tests/rt-safety-sweep.py --check ; echo CLEAN_TREE_EXIT=$?
  measured  : 918 region line(s); 4 hit(s) in 3 key(s)
  PASS: every hit on a declared audio-thread path is allowlisted with a reason and at
        its allowed count.
CLEAN_TREE_EXIT=0
```

The permanent form of that control is `tests/rt_safety_selftest.py` (ctest
`RtSafetySelfTest`, no build, no engine, no compiler — it synthesises its fixture
sources in a temp directory), **18 checks, exit 0**:

```
  PASS POSITIVE CONTROL: a deliberate `new` on an audio-thread path FAILS
  PASS POSITIVE CONTROL: a deliberate QMutexLocker on an audio-thread path FAILS
  PASS POSITIVE CONTROL: a deliberate container growth on an audio-thread path FAILS
  PASS POSITIVE CONTROL: a hit that grows past its allowlist count FAILS
  PASS ... and the CLI's OWN exit code is 1 (the number a gate records)
  PASS NEGATIVE CONTROL: a declared path with no forbidden construct PASSES
  PASS NEGATIVE CONTROL: the same allocation WITH an allowlist line PASSES
  PASS the in-class definition form resolves (declared Class::method, written unqualified)
  PASS BOUND: the same allocation OUTSIDE the declared scope is NOT seen
  PASS BOUND: `new` in a comment and in a string literal is not a hit
  PASS an EMPTY scope is exit 2 (0 entries is an error, never a pass)
  PASS an allowlist line with a BLANK reason is exit 2
  PASS an allowlist line naming an unknown rule id is exit 2
  PASS a scope entry whose symbol resolves to nothing is exit 1
  PASS a scope entry whose FILE is not in the tree is exit 2
  PASS an allowlist line that no longer covers anything is exit 1
  PASS an allowlist line for a symbol the scope does not declare is exit 1
  PASS the rule set covers the AGENTS.md rule's three words
RESULT: PASS
```

## The stated bound — what this programme does NOT cover

Every run prints this block, so no reader can mistake the sweep for a proof that
the audio thread is real-time safe:

* **STATIC ONLY.** Source text is read; the engine is not run. The **runtime**
  half of the rule stays with the `AllocationProbe` tests
  (`tests/src/core/AllocationProbe.h`: `RecordingRealtimeTest`,
  `RecordRingBufferTest`, `RetroMidiRingTest`, `SessionSchedulerTest`,
  `LufsMeterTest`, `MeterTapTest`, `ScriptMemoryBudgetTest`, and the two-track
  capture harness) — this programme is their static counterpart, not their
  replacement.
* **DECLARED SCOPE, NOT A CALL-GRAPH.** Only the `path:symbol` pairs in
  `tests/rt-safety-scope.txt` are measured. It never follows a call, so a new
  audio-thread path that nobody declares is measured by **nothing here** — the
  self-test asserts exactly that silence (`BOUND: the same allocation OUTSIDE the
  declared scope is NOT seen`). Widening the frontier is a deliberate act: add a
  line with a reason (the 27 pairs today are the render callback, its four stages,
  the per-period drains, and the fork's own realtime entries).
* **NO VIRTUAL DISPATCH, FUNCTION POINTERS, MACROS OR INCLUDES.** An
  implementation reached through an interface is checked only if the scope names
  it too; a macro that expands to an allocation is invisible.
* **NO COST, NO SYSCALL AND NO I/O CHECK.** Only the three words of the
  `AGENTS.md` rule. Lock-free *correctness* (memory ordering, ABA, torn reads) is
  not measured; the mixer-concurrency TSAN job and the per-feature probes are
  where that lives.
* **NO ALIASING ANALYSIS.** A hit is a *mention* of a construct, not proof that
  it runs on the audio thread — a `std::make_unique` member in a constructor
  initialiser list is the shape of a mention that is not a hit, and an
  allowlist line with a reason is how a tolerated mention is said.
* **C++ SYNTAX IS NOT PARSED.** Regions are brace-matched on comment/literal-blanked
  text; a pathological macro or a body-less declaration is reported rather than
  guessed. A declared symbol that stops resolving **fails** the sweep instead of
  being skipped.
* **NOT the capture thread.** The declared frontier is the render (audio) thread's
  path. The capture thread's own realtime paths have their runtime probes
  (`TwoTrackAlsaCaptureProbe`, `RecordRingBufferTest`) and are not declared here.

## Working with it

* **A new audio-thread path**: add `path<TAB>symbol<TAB>reason` to
  `tests/rt-safety-scope.txt`. The sweep then measures it, and reports every hit
  it finds as a `NEW rt-safety hit` (exit 1) until each is fixed or allowlisted
  with a reason.
* **A hit you accept**: add a line to `tests/rt-safety-allowlist.txt` with the
  measured count and a reason that names the class (`inherited`, bounded by a
  declared capacity, or a mention that cannot run on the audio thread).
* **A hit you fixed**: delete (or lower the count on) the line. Leaving it is a
  **failure**, not a no-op — the ratchet only moves down.
* **Re-anchoring**: `--reanchor "reason"` rewrites the counts to the measured
  state, refuses a blank reason, and refuses to run while a real problem is
  outstanding (so it cannot bury a new hit). It prints every line it drops.
* **Widening the rules**: a rule is a `Rule(id, category, pattern, note)` in
  `tests/rt_safety_source.py`. A rule that stops biting is caught by the
  self-test's `rule_controls`.
