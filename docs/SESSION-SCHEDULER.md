# Session View: launch scheduler + quantisation engine (task #595)

**Branch:** `post-alpha/session-scheduler` (base `post-alpha/pr594`, which carries the merged
Session View **data layer**, task #594).
**Worktree:** `projects/lmms-fl-research/zene-pa-session`.
**Feature gate:** everything here is compiled only with `WANT_SESSION_VIEW=ON`
(`LMMS_HAVE_SESSION_VIEW`, `CMakeLists.txt:110` / `:147-149`). The default is OFF.

Task #595's one-line scope was *"Session View: launch scheduler + quantisation engine
(audio-thread safe)"*. This document reports exactly that, and says plainly what it is not.

---

## 1. What the merged data layer already gave us

Read against the tree, not from memory:

| What | Where |
| --- | --- |
| `enum class LaunchMode { Trigger, Gate, Toggle, Repeat }` | `include/SessionModel.h:59`, values at `:61-64` |
| `LaunchQuantisation { Global = -1, None = 0, Bar = 1, TwoBars = 2, FourBars = 4 }` | `include/SessionModel.h:70-77` |
| `struct FollowAction` (NoAction/Stop/PlayAgain/Previous/Next/First/Last/Any/Other/Jump, chance, linked, timeBars, jumpTo) | `include/SessionModel.h:80-114` |
| `class ClipSlot` — a MIDI pattern id **or** an audio source, never both; launch mode, launch quantisation, legato, loop region, gain/transpose/detune/ram, Follow Action chain | `include/SessionModel.h:118-192`; launch settings getters/setters at `:145-159`, `m_launchMode` at `:182`, `m_launchQuantisation` at `:183` |
| `class Scene` — optional per-scene tempo / time signature | `include/SessionModel.h:195-229` |
| `class SessionModel` — the grid (`track * sceneCount + scene`), `globalLaunchQuantisation()` at `:262`, `DefaultLaunchQuantisation = Bar` at `:238`, `CurrentVersion = 1` at `:236` | `include/SessionModel.h:231-303` |
| ClipSlot / Scene XML (`type`, `pattern`, `src`, `launchmode`, `quantisation`, `legato`, `loopstart`, `looplength`, `gain`, `transpose`, `detune`, `ram`, `followactions`) | `src/core/SessionClip.cpp:66-161` (slot), `:169-208` (scene) |
| Versioned `<session version="1">` block read/write, unknown-version preservation | `src/core/SessionModel.cpp`, called from `src/core/Song.cpp:920` (clear), `:1140` (load), `:1256` (save) |
| `Song::sessionModel()` accessors | `include/Song.h:334-336`; member at `:474-476` |

**The gap #595 closes:** when #594 merged, `sessionModel()` had **zero callers** even with the
option ON (`POST-ALPHA-PLAN.md:575`, "DISARMED-DOCUMENTED"). The model was a well-tested
persistence layer nothing drove. This change gives it an engine; the GUI that makes it reachable
by a user is **#598** and is still not done.

---

## 2. Scope, and the shape chosen

In order, as the task laid it out:

1. the launch decision as **pure, testable logic** — no audio-thread state, exhaustively testable;
2. the **audio-thread hand-off** — model thread to audio thread with no lock it can block on and
   no allocation;
3. **LaunchMode semantics actually implemented**, each with a test that fails if it behaves like
   another mode;
4. **tempo/position authority** named with file:line rather than assumed.

New files:

* `include/SessionScheduler.h` — the pure decision + state machine, and the engine (`SessionScheduler`).
* `src/core/SessionScheduler.cpp` — the implementation.
* `tests/src/core/SessionSchedulerTest.cpp` — the pure decision, the four modes, the hand-off, the
  allocation probe.
* `tests/src/core/SessionSchedulerRenderTest.cpp` — the behaviour-preservation proof and its control.

Touched upstream files: `include/Song.h` (accessor + member, both behind the gate),
`src/core/Song.cpp` (the audio-thread hook), and the two `CMakeLists.txt` source lists. All four
are declared in `tests/upstream-modifications.txt` with the reason read off the diff.

---

## 3. The quantisation set — and why this one

The engine implements **None / 1 bar / 2 bars / 4 bars**, plus `Global` which defers to the
session default. It does not invent a set:

* it is the set SPEC-zene-studio §4.1 names ("per-clip + global launch quantisation
  (None/1/2/4 bars)");
* it is the set the data layer already **persists** — `LaunchQuantisation` at
  `include/SessionModel.h:70-77`, serialised as the `quantisation` attribute
  (`src/core/SessionClip.cpp:87` / `:137`), so a second vocabulary would need a translation
  table and would drift;
* it is Live's default set, and SPEC A1 explicitly chooses Live's paradigm.

The length of one step is `bars * ticksPerBar`, with `ticksPerBar` taken from the project's time
signature rather than hard-coded 4/4: `Song::ticksPerBar()` (`include/Song.h:132`) →
`TimePos::ticksPerBar(sig)` (`include/TimePos.h:128`), against
`DefaultTicksPerBar = 192` (`include/TimePos.h:38`). A 7/8 bar is 168 ticks and the grid follows.

---

## 4. The launch decision (pure)

`include/SessionScheduler.h`:

```cpp
struct SessionClockContext { tick_t positionTicks; tick_t ticksPerBar;
                             float framesPerTick; bool transportRunning; };
tick_t quantisationTicks( LaunchQuantisation, tick_t ticksPerBar ) noexcept;
LaunchQuantisation resolveQuantisation( LaunchQuantisation perClip, LaunchQuantisation sessionDefault ) noexcept;
tick_t launchTickAt( LaunchQuantisation, const SessionClockContext& ) noexcept;
```

`launchTickAt` is the whole decision: **the next grid line at or after the current tick**, with a
position already exactly on a line taking effect at that line (Live's behaviour, and the only rule
that makes "launch exactly on the downbeat" mean *now* rather than *in four bars*). `None` is not a
grid of length zero — it means "no musical boundary to wait for", so it returns the current tick.

`SessionClockContext::framesPerTick` is carried so a caller can convert the resulting tick to a
frame; the engine works in ticks because that is the domain the transport, the loops and
`Engine::framesPerTick()` all already speak, and it keeps the decision an integer function with no
floating-point rounding to argue about.

**Tests** (`SessionSchedulerTest::launchTickRoundsUpToTheGrid`,
`launchTickFollowsTheTimeSignature`, `quantisationTicksCoversExactlyThePersistedSet`,
`resolveQuantisationUsesTheSessionDefault`) pin the rounding direction at every boundary
(`191→192`, `192→192`, `193→384`; the same for 2 and 4 bars) and the 7/8 case.

## 5. LaunchMode semantics

The four modes are a state machine over `SlotPhase { Idle, LaunchPending, Playing, StopPending }`
plus a `held` flag, driven by `applyLaunchCommand()` (plan) and `advanceLaunchState()` (fire).
`launchTickAt` decides *when*; these decide *what*.

| Mode | Press | Release | Notes |
| --- | --- | --- | --- |
| **Trigger** | schedule a start at the next grid line; pressing again while playing re-launches at the next grid line | **ignored** | plays until an explicit stop; `startCount` increments per launch |
| **Gate** | schedule a start; `held = true` | released **before** the grid line → the launch is cancelled and never plays; released **while playing** → stop at the next grid line | plays only while held; never retriggers on its own |
| **Toggle** | Idle → schedule a start; LaunchPending → **cancel** the pending start; Playing → schedule a stop; StopPending → cancel the stop | ignored | second press decides the opposite of what is scheduled |
| **Repeat** | schedule a start; `held = true` | same as Gate | while held it **starts over at every grid line**; releasing stops it at the next line |

**Each mode has a test that fails if it behaved like the mode it is closest to:**

* `toggleSecondPressStopsWhileTriggerKeepsPlaying` — same command sequence into a Toggle and a
  Trigger slot; Toggle ends `Idle` with one `Stopped`, Trigger ends `Playing` with none. This fails
  if Toggle behaves like Trigger.
* `toggleSecondPressCancelsAPendingLaunch` — same input; Toggle ends with **zero** starts (it did
  not treat the second press as a re-launch), Trigger has one. This fails if Toggle behaves like
  Trigger *before anything has played*.
* `triggerPlaysUntilStoppedAndIgnoresRelease` — the release a Gate would stop on leaves a Trigger
  slot `Playing`; only the explicit `Stop` command ends it. This fails if Trigger behaves like Gate.
* `gatePlaysOnlyWhileHeld` — a release before the boundary yields **zero** starts (a Trigger would
  have started), and a held Gate produces exactly one start with zero retriggers.
* `repeatRetriggersOnEveryBoundaryWhileHeld` — the *same command stream* into a Repeat and a Gate
  slot: Repeat gets one `Started` plus three `Retriggered` (startCount 4) over four bars; Gate gets
  one start and zero retriggers. This fails if Repeat behaves like either Gate or Trigger.
* `repeatWithoutAGridDoesNotRetrigger` — a documented edge: with `None` quantisation there is no
  grid to restart on, so Repeat launches once and holds.

`FollowAction` evaluation is **not** implemented — the data layer models the chain
(`include/SessionModel.h:80-114`), and SPEC A3 hands its evaluation to the scheduler, but that is
task **#596**.

## 6. The threading story

**Which thread owns the launch state:** the **audio thread**, exclusively. A slot's phase, pending
tick, hold state and start count live in `SessionScheduler::m_active`, a fixed
`std::array<ActiveSlot, 64>`. No other thread reads or writes it, and it is never indexed by a
caller-supplied value, so there is nothing to bounds-check and no structural coupling to the grid.

**How a request crosses over:** a lock-free SPSC ring buffer of POD commands,
`SessionScheduler::CommandQueue` (capacity 256). The model/GUI thread is the only producer
(`requestLaunch` / `requestRelease` / `requestStop`: one relaxed load, one release store); the audio
thread is the only consumer (`processAudio`: one relaxed load, one acquire load). Same invariant
class as the in-tree WASM ring (`src/wasm/WasmSpscRingBuffer.h`), and no `QMutex`, no
`QWaitCondition`, no `std::mutex`, no syscall on either side.

**Why the request carries no time:** the GUI cannot know the sample-accurate transport position, so
the command says only *what* was asked (`track`, `scene`, `type`, `mode`, `quantisation`). *When* it
takes effect is computed on the audio thread from the audio thread's own clock, in
`drainCommands()`. That also means the mode/quantisation recorded at *press* time are what a later
release or stop applies, so a model edit mid-launch cannot half-apply.

**A project change** is a single atomic increment (`reset()`), observed by the audio thread at the
top of `processAudio()`, which then drops every active slot. Nothing is queued, so it is safe to
call from anywhere; `Song::clearProject()` calls it (`src/core/Song.cpp:957`).

**Failure modes are bounded, not unbounded:** a full queue refuses the request and counts it
(`droppedCommands()`); a press with no free active-slot entry is dropped and counted the same way.
Nothing grows with launch traffic — a slot's entry is returned as soon as it reaches `Idle`.

**Measured, not asserted:** `audioThreadPathDoesNotAllocate` runs `processAudio()` 20 000 times with
eight slots live and the command path hot, with `AllocationProbe.h`'s counting `operator new`
installed — **0** allocations. `controlThreadPathDoesNotAllocate` does the same for 3 000 model-side
requests — **0**. `commandsCrossThreadsWithoutLoss` runs a real producer thread against a real
consumer thread for 4 000 launches: none lost, none dropped, and the consumer still allocates
nothing while it drains.

## 7. Tempo / transport / position authority

Named against the tree, not assumed:

| Quantity | Symbol | Where |
| --- | --- | --- |
| Transport position (ticks) | `Song::getPlayPos(PlayMode::Song)` → `Timeline::pos()` | `include/Song.h:213-216`, `:231-234` |
| Ticks in one bar (time signature) | `Song::ticksPerBar()` → `TimePos::ticksPerBar(sig)` | `include/Song.h:132-135`, `include/TimePos.h:128`, `DefaultTicksPerBar` at `include/TimePos.h:38` |
| Frames per tick (tempo × sample rate) | `Engine::framesPerTick()` | `include/Engine.h:96-101`, computed in `src/core/Engine.cpp:129-141` from `s_song->getTempo()` and `outputSampleRate()` |
| Sample rate | `AudioEngine::baseSampleRate()` (not `sampleRate`) | `include/AudioEngine.h:195`, `outputSampleRate()` at `:198` |
| Period length | `AudioEngine::framesPerPeriod()` | `include/AudioEngine.h:259` |
| Play state | `Song::m_playing` and `Song::m_playMode` | `src/core/Song.cpp:207` gate, used in the new hook |

**The session clock is its own domain** (SPEC A2). While the song transport runs, the session clock
*follows* the song position, so launches land on the arrangement's grid lines. While it is stopped,
the session clock **free-runs**, advancing by the frames the audio engine reports each period, so a
launch can still be scheduled with no transport at all. Tested by
`sessionClockFollowsTheTransport` and `sessionClockFreeRunsWhileTheTransportIsStopped`.

## 8. The audio-path hook

One block at the top of `Song::processNextBuffer()` (`src/core/Song.cpp:207`), i.e. on the audio
thread every period via `AudioEngine::renderStageNoteSetup()` (`src/core/AudioEngine.cpp:241`),
**before** the `if (!m_playing) return;` gate so that session-only playback is not gated on the song
transport. It fills a `SessionClockContext` from the symbols in §7 and calls `processAudio()`.

One change inside the per-tick track loop (`src/core/Song.cpp:353-378`): while a session clip on a
track column is `Playing`, that track's `play()` is skipped for the period —
**SPEC-zene-studio A1's mutual exclusivity**, "a track's session and arrangement content stay
mutually exclusive", enforced at track level. The index is the track's position in the song's own
track list, and the test only applies in `PlayMode::Song` (the only mode where that list is the
track list).

**Honest limit:** the launched clip's *content* is not rendered yet. A launched clip currently takes
the track over and the arrangement stops; the notes of a referenced pattern or the samples of a
referenced audio clip are **#597** (`ClipSlot::patternId()` / `audioSource()` are the inputs it
needs). What #595 delivers is the schedule and the takeover, which is what makes the audio-path
effect *observable* at all.

## 9. Behaviour preservation — the proofs

All three renders below are the **real** export path: `ProjectRenderer` with an `AudioFileWave`
device — the code the `lmms render` CLI uses — driving `AudioEngine::renderNextPeriod()` through
the full note-setup / instruments / effects / mix stages. The fixture is a sample track whose clip
carries a deterministic in-memory stereo buffer, so no instrument plugin is involved. The harness
prints `AB_EVIDENCE <label> bytes=<n> sha256=<hash>` for every render, in the same format as
`MixerAbRegressionTest`, so the numbers can be pasted anywhere.

1. **Nothing launched, twice** → byte-identical, non-silent (`renderWithNothingLaunchedIsByteIdenticalAcrossRuns`).
   This is also the comparison against *before this change*: `WANT_SESSION_VIEW=OFF` compiles none
   of this code, and the OFF build produces the same render (see §10).
2. **A Trigger launch on track 0** → the render **must differ**
   (`launchedClipChangesTheRender`). This is the sensitivity control: if the engine were not wired
   into the audio path, this test fails.
3. **A Gate press + release, released before its grid line** → the render is **byte-identical** to
   (1) (`gateReleasedBeforeTheBoundaryLeavesTheRenderUnchanged`), because a cancelled launch never
   starts. This is the control on the control: it shows (2) is caused by the launch *firing*, not by
   the mere fact that a request was made.

## 10. Configurations and totals

Both configurations were built in the **same** build directory with an explicit reconfigure, and
`ctest` was run from `build/tests` (a 0-test run is an error, never a pass). Every exit code below
was measured unpiped.

| Configuration | Build | `ctest` from `build/tests` | Session tests present |
| --- | --- | --- | --- |
| `WANT_SESSION_VIEW=OFF` | exit 0 | **100% tests passed, 0 failed out of 25**, exit 0 | none (`ctest -N` lists no session test) |
| `WANT_SESSION_VIEW=ON` | exit 0 | **100% tests passed, 0 failed out of 28**, exit 0 | `SessionModelTest`, `SessionSchedulerTest`, `SessionSchedulerRenderTest` |

25 is the base count on this branch; 28 is base + the #594 model test + the two added here. The
whole CI-reproducing run (`JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4`, which
configures with `-DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo`, builds and runs ctest) also
exits 0 on the OFF configuration.

The OFF build is the "before this change" binary in the strong sense: it contains no session code
at all —

```
$ nm -C build/lmms | grep -c -i SessionScheduler      # ON  -> 12
$ nm -C build/lmms | grep -c -i SessionScheduler      # OFF ->  0
$ nm -C build/lmms | grep -c -i SessionModel          # OFF ->  0
```

so with the flag off the audio path is literally the pre-change audio path.

**Render evidence** (all three from `SessionSchedulerRenderTest`, printed by the test itself):

```
AB_EVIDENCE no-launch-run1  bytes=302168 sha256=85a7e748ae185d9223abca003b1a442ca97e9b0ec0cbe0fb4cd58eaf8cc22c01
AB_EVIDENCE no-launch-run2  bytes=302168 sha256=85a7e748ae185d9223abca003b1a442ca97e9b0ec0cbe0fb4cd58eaf8cc22c01
AB_EVIDENCE launched-trigger bytes=302168 sha256=0fafce50d8185c151e43da703a2ef86a0d70d9614c732207730c17667628bc75
AB_EVIDENCE gate-cancelled  bytes=302168 sha256=85a7e748ae185d9223abca003b1a442ca97e9b0ec0cbe0fb4cd58eaf8cc22c01
```

`SessionSchedulerTest` = 21 passing cases, 0 failing. `SessionSchedulerRenderTest` = 5 passing,
0 failing.

A note on flakiness, because it is the failure mode that costs the most trust: the first version of
the two-thread hand-off case asserted two things that depend on how the producer and the consumer
interleave — that the bounded queue had *ever* been full, and that a pending launch had *already*
fired by the time the queue drained. It failed roughly one run in three and was caught by running it
repeatedly rather than once. Both assertions were removed (the drop counting is pinned
deterministically by `aFullCommandQueueRefusesInsteadOfGrowing` instead, and the clock is now
advanced past the grid line before the launch count is checked). The case then ran **50 consecutive
times with 0 failures**, and the full suite twice at 28/28. Nothing about the engine changed; the
test was measuring the scheduler's *timing* rather than its *behaviour*.

The queue-full and slot-table-full bounds are still counted and readable
(`droppedCommands()`), they are just not asserted from a two-thread race.


## 11. Gates

Measured on the branch tip, exit codes unpiped:

| Gate | Command | Exit |
| --- | --- | --- |
| Gate 6 — no upstream behavioural regressions | `bash tests/no-upstream-regression-gate.sh` | **0** (34 files in the ledger) |
| Gate 9 — every tracked source in a scope manifest | `bash tests/fork-sources-gate.sh` | **0** (1100 scanned, 106 fork-NEW, 995 inherited, 0 unregistered, 0 stale) |
| Gate 4 — per-method complexity | `bash tests/complexity-gate.sh --check` | **0** |
| Gate 7 — per-file length | `bash tests/file-length-gate.sh --check` | **0** |
| All gates | `bash tests/run-all-gates.sh` | see below |

**Gate 9 does not exist on this branch.** `tests/fork-sources-gate.sh` was added on
`post-alpha/midi-race` (`test(gates): Gate 9 - every tracked source must be in a scope manifest`,
`e4fc8cd7e`), which is not an ancestor of `post-alpha/session-scheduler`; `run-all-gates.sh` here runs
gates 1, 3, 4, 5, 6, 7, 8 and has no gate 9 step. Rather than report a pass for a gate that cannot
run, the **identical script** was taken from the sibling lane's worktree, run against this tree, and
deleted again (it is not committed here — it belongs to that lane's change). It exits 0 as above.

`tests/run-all-gates.sh` (gate 1 `ctest` with 28 tests, gate 5 mutation, and the static gates):

```
================ SUMMARY ================
gate   name                     result
1      ctest                    PASS
2      coverage                 SKIP
3      no-tautology             PASS
4      complexity               PASS
5      mutation                 PASS
6      upstream-regression      PASS
7      file-length              PASS
8      duplication              PASS

RESULT: PASS — every executed gate passed          # exit 0
```

All three exit codes measured on the tip, unpiped: **0, 0, 0**.

Gate 4 was **red** on the first version of this branch and is the reason the tip has two commits:
`applyLaunchCommand` measured CCN 16 and `SessionScheduler::processAudio` CCN 11, both over the
target of 10 and both new. The resolution was the project's rule — *code is never trimmed to satisfy
a metric* and the baseline is never re-anchored to make a gate pass — so the decision was extracted
into `pressSlot` / `releaseSlot` / `stopSlot` over a shared `scheduleAt`, and the audio period into
`consumeResetRequest` / `advanceClock` / `advanceSlots`. Behaviour is unchanged; the mode tests are
what says so. `bash tests/complexity-gate.sh --check` now exits 0.


## 12. Not done (explicitly out of scope)

* **The Session View grid UI — #598.** Nothing a user can click launches a clip yet. The engine API
  is headless and has no GUI dependency, so it is drivable today from a test or an agent script, but
  the `session.*` command surface required by SPEC A14/A15 and `AGENT-TOOLING.md` is **not**
  registered — there is no command registry in this tree yet.
* **Follow Actions — #596.** The chain is modelled and persisted; nothing evaluates it.
* **Clip content rendering — #597.** See §8: the takeover is real, the notes/samples are not played.
* **Scene launch and per-scene tempo/time-signature application** (SPEC §4.1). One-clip launches
  only; `Scene` is modelled but nothing fires a row.
* **Arrangement record / Back-to-Arrangement** (SPEC §4.1).
* Not tested here: what a launch does when the transport *loops* mid-period or jumps; the engine's
  rule is "fire at the first period whose position has reached the pending tick", which is correct
  but deliberately not boundary-split inside a period — with no content rendering there is nothing
  to split *for* yet. #597 will need that.
