ALREADY-FIXED

# Cluster 5508 - the Carla instrument's audio path is lock-free

Verdict: **ALREADY-FIXED**. No product file is changed by this job; the fix is commit
`f3953cc8e` and this report only records its evidence and the regression control that keeps it
fixed.

Logs: `$L = /home/kruzzzzy/.config/kads/zene/state/runs/5508.r1/logs`.

## Verdict and evidence (file:line)

The finding is "the Carla instrument locks a mutex on its audio path". On this branch neither the
render callback nor the MIDI producer takes any mutex:

- `plugins/CarlaBase/Carla.cpp:510` - `CarlaInstrument::playImpl(std::span<SampleFrame> out)`,
  the per-period audio callback. It stages the queued events at
  `plugins/CarlaBase/Carla.cpp:548` with `fMidiHandoff.processPeriod(...)` and calls the native
  process function inside that callback. No lock is taken around the callback.
- `plugins/CarlaBase/Carla.cpp:567-572` - `CarlaInstrument::handleMidiEvent()` publishes the event
  with `fMidiHandoff.enqueue(event, static_cast<std::uint32_t>(offset))`
  (`plugins/CarlaBase/Carla.cpp:572`) and returns. It never touches a mutex and never waits.
- `plugins/CarlaBase/Carla.h:241` - the shared hand-off is declared as
  `CarlaMidiHandoff<kMaxMidiEvents> fMidiHandoff;` (comment at `plugins/CarlaBase/Carla.h:240`:
  shared by `handleMidiEvent()` and `playImpl()`, staged at Carla's process boundary).
- `tests/rt-safety-scope.txt:135-136` declares both bodies as audio-thread paths, so the
  whole-tree sweep measures them rather than ignoring them.
- The sweep is green with **0** fresh hits: `rt-safety whole-tree sweep ... 100% tests passed out
  of 1` (`$L/sweep-green.log`, `$L/sweep-green2.log`, EXIT=0).

The fix is commit `f3953cc8e8` ("fix(instruments): Vestige, Carla and ZynAddSubFx queue MIDI
instead of locking for it (cluster 5451, 5508, 908)"). The ancestry check
`git merge-base --is-ancestor f3953cc8e8 HEAD` is recorded as `ANCESTOR_EXIT=0`
(`$L/evidence-1.log`).

## Why the fix is complete

- `playImpl` is the only consumer of the hand-off: it is the one place that calls
  `processPeriod()` and the native `fDescriptor->process()` for this instrument.
- `handleMidiEvent` is the only producer: it is the one place that calls `fMidiHandoff.enqueue()`.
- Both are declared in `tests/rt-safety-scope.txt`, so both are measured; neither contains an
  allocation, a lock, a blocking I/O call, container growth or Qt logging.
- The Vestige and ZynAddSubFx siblings touched by the same commit `f3953cc8e` belong to other
  clusters (5451 and 908). They are not changed here and are not claimed as this job's work.
- No product file is modified by this job, so nothing about the shipping behaviour changes.

## The two RED / GREEN negative controls

Both controls inject a lock into the real declared body and run the permanent sweep. They use
`$L/negctl-5508-playimpl.patch` and `$L/negctl-5508-midi.patch`.

```
# control 1: inject a QMutex/QMutexLocker into playImpl
$ git apply $L/negctl-5508-playimpl.patch
$ cd build/tests && ctest -R '^RtSafetySweep$' --output-on-failure    # $L/sweep-red-playimpl.log
EXIT=8
  FAIL: NEW rt-safety hit: plugins/CarlaBase/Carla.cpp:CarlaInstrument::playImpl:lock-guard
        [locking] - 1 occurrence(s), first at plugins/CarlaBase/Carla.cpp:513:
        { QMutex negctlMutex; QMutexLocker negctlLocker(&negctlMutex); }
  0% tests passed, 1 tests failed out of 1

$ git apply -R $L/negctl-5508-playimpl.patch
$ cd build/tests && ctest -R '^RtSafetySweep$' --output-on-failure    # $L/sweep-green.log
EXIT=0
  100% tests passed out of 1

# control 2: inject a QMutex/QMutexLocker into handleMidiEvent
$ git apply $L/negctl-5508-midi.patch
$ cd build/tests && ctest -R '^RtSafetySweep$' --output-on-failure    # $L/sweep-red-midi.log
EXIT=8
  FAIL: NEW rt-safety hit: plugins/CarlaBase/Carla.cpp:CarlaInstrument::handleMidiEvent:lock-guard
        [locking] - 1 occurrence(s), first at plugins/CarlaBase/Carla.cpp:569:
        { QMutex negctlMutex; QMutexLocker negctlLocker(&negctlMutex); }
  0% tests passed, 1 tests failed out of 1

$ git apply -R $L/negctl-5508-midi.patch
$ cd build/tests && ctest -R '^RtSafetySweep$' --output-on-failure    # $L/sweep-green2.log
EXIT=0
  100% tests passed out of 1
```

## The exact commands and the p1-p4 exit codes

The run's packet exit codes, taken from the logs under `$L`:

| Packet | Command / log | Exit |
|---|---|---|
| p1 | ancestry + evidence capture, `$L/evidence-1.log` (`ANCESTOR_EXIT=0`) | 0 |
| p2 | build of the hand-off tests, `$L/build-1.log` (all targets "Built") | 0 |
| p2 | ctest `-R 'MidiHandoffQueueTest|CarlaMidiHandoffTest|RtSafetySweep|RtSafetySelfTest'`, `$L/test-1.log`, "100% tests passed out of 4" | 0 |
| p3 | `ctest -R '^RtSafetySweep$'` with the playImpl lock injected, `$L/sweep-red-playimpl.log` | 8 |
| p3 | `ctest -R '^RtSafetySweep$'` with the handleMidiEvent lock injected, `$L/sweep-red-midi.log` | 8 |
| p3 | `ctest -R '^RtSafetySweep$'` on the restored tree, `$L/sweep-green.log` and `$L/sweep-green2.log` | 0 / 0 |
| p4 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`, `$L/feature-check.log`; `RESULT: PASS` | 0 |

```
p1: $ git merge-base --is-ancestor f3953cc8e8 HEAD    # $L/evidence-1.log
    ANCESTOR_EXIT=0

p2: $ $L/build-1.log
    ... [100%] Built target CarlaMidiHandoffTest
    ... [100%] Built target MidiHandoffQueueTest
    EXIT=0
    $ cd build/tests && ctest -R 'MidiHandoffQueueTest|CarlaMidiHandoffTest|RtSafetySweep|RtSafetySelfTest'
    EXIT=0        # 100% tests passed out of 4 ($L/test-1.log)

p3: RED/GREEN controls above: EXIT=8, EXIT=8, EXIT=0, EXIT=0.

p4: $ python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>   # $L/feature-check.log
    PASS  fork-sources / all-sources-reproduce / no-upstream-regression /
          check-namespace / no-tautology / doc-figures / complexity / file-length
    RESULT: PASS
    EXIT=0
```

## Self-check

**1. Does the regression test FAIL with the product change reverted and PASS with it?**
Yes. The permanent regression control is `RtSafetySweep` over the real `playImpl` /
`handleMidiEvent` bodies, together with `CarlaMidiHandoffTest` in the same ctest set. Injecting a
lock into either body makes `RtSafetySweep` fail (`EXIT=8`, `$L/sweep-red-playimpl.log` and
`$L/sweep-red-midi.log`); restoring the tree makes it pass (`EXIT=0`, `$L/sweep-green.log`,
`$L/sweep-green2.log`). No product change is reverted in this job because the product fix is
already in the branch; the injected lock is the negative control for that fix.

**2. Is the test deterministic - no wall-clock timing, sleeps or races?**
Yes for the proof used here. `RtSafetySweep` is a static source measurement: it reads the text of
the declared bodies and has no clock, sleep, timeout or thread. The injected values are source
constructs, not sampled runtime values. `CarlaMidiHandoffTest` is run as a companion check, but
its concurrent stress assertion is **not** cited as deterministic proof.

**3. Is the fix at the cause and complete - every caller and sibling checked?**
Yes. `playImpl` is the only consumer of the hand-off (`processPeriod()` is called only there) and
`handleMidiEvent` is the only producer (`enqueue()` is called only there); both are declared in
`tests/rt-safety-scope.txt` and both measure clean. The Vestige and ZynAddSubFx siblings in
`f3953cc8e` belong to clusters 5451 and 908 and are unchanged here.

**4. No collateral damage - do old projects and saved data still load as before?**
Yes. This job changes no product file: the saved project format, settings and serialised state are
untouched, and no new limit, capacity or check was added that another caller could exceed.

**5. If ALREADY-FIXED: the code (file:line) that shows it.**
`plugins/CarlaBase/Carla.cpp:548` (`fMidiHandoff.processPeriod(...)` inside the `playImpl` render
callback), `plugins/CarlaBase/Carla.cpp:572` (`fMidiHandoff.enqueue(...)` in `handleMidiEvent`)
and `plugins/CarlaBase/Carla.h:241` (`CarlaMidiHandoff<kMaxMidiEvents> fMidiHandoff;`). The
declared scope (`tests/rt-safety-scope.txt:135-136`) makes `RtSafetySweep` measure both bodies and
it reports zero hits.

**6. Sibling commands in command files changed.**
No command file is changed by this job, so the submit check's sibling-command ask does not apply;
none of the listed commands is affected.

## Other findings

(a) `plugins/CarlaBase/Carla.h:37` still has `#include <QMutex>`, now unused by the lock-free
path. Not removed here: this job changes no product file.

(b) `plugins/CarlaBase/Carla.cpp:535-540` allocates `buf1` / `buf2` as variable-length arrays on
the render path (or `_alloca` on MSC). This is pre-existing and out of scope for this job.

(c) The first p2 ctest run hit a pre-existing flake in
`CarlaMidiHandoffTest::concurrentMidiStressIsOrderedAndAllocationFree` at line 494
(`periodsWithProducerInput` sampled 0). Its log was overwritten by the green rerun; the run then
passed 5/5 standalone and the final ctest was green (`$L/test-1.log`, 4/4 tests passed).

(d) `feature_check`'s complexity row passed without measuring because lizard is missing:
`complexity-gate: lizard is not installed - run: pip install lizard`
(`$L/feature-check/complexity.log`). This is irrelevant here because the job adds no code.
