# Cluster new-p12-281 — VST3 instrument: a failed re-prepare retried for ever (new-p12-281) and load failures not shown (4554)

**Verdict: FIXED** for cluster new-p12-281 - **Verdict: ALREADY-FIXED** for cluster 4554.

new-p12-281: FIXED
4554: ALREADY-FIXED

Both findings are in `plugins/Vst3Instrument/Vst3Instrument.cpp`. They are decided
separately below; the product change touches only the first one.

---

## Bug 1 - cluster new-p12-281: a failed VST3 re-prepare is retried every 100 ms - FIXED

### Finding

`plugins/Vst3Instrument/Vst3Instrument.cpp:294-309` (the finding's line 306 was
`m_needsReprepare.store(false, std::memory_order_relaxed);`): `reprepare()`
cleared the re-prepare request **even when `m_plugin.prepare()` failed**. The
audio thread sets the flag again in the very next block
(`processImpl()`, `plugins/Vst3Instrument/Vst3Instrument.cpp:390-410`), so the
GUI thread's `poll()` (`plugins/Vst3Instrument/Vst3Instrument.cpp:514-521`,
driven by a 100 ms `QTimer`, `plugins/Vst3Instrument/Vst3Instrument.cpp:174-177`)
re-attempted the setup for ever, and warned on every attempt.

### Evidence (file:line)

The first three citations are the ORIGINAL (pre-fix) line numbers; the current
ones are in brackets.

- `plugins/Vst3Instrument/Vst3Instrument.cpp:294-309` (original) [now
  `:305-340`]: the failed `prepare()` only produced a `qWarning`;
  `m_needsReprepare` was cleared unconditionally, so nothing remembered that
  the attempt had failed.
- `plugins/Vst3Instrument/Vst3Instrument.cpp:390-410` (original) [now
  `:418-437`]: the audio thread raises `m_needsReprepare` on **every** block
  while the plug-in is not prepared.
- `plugins/Vst3Instrument/Vst3Instrument.cpp:514-521` (original) [now
  `:542-566`]: `poll()` called `reprepare()` whenever the flag was set - once
  per 100 ms tick.
- `plugins/Vst3Instrument/Vst3Instrument.cpp:174-177`: the poll timer interval
  is 100 ms - the "every 100 ms" of the finding.
- `plugins/Vst3Effect/Vst3Host.cpp:481-567`: `HostedPlugin::prepare()` returns
  false and leaves the plug-in unprepared when `setupProcessing()` fails - the
  "unsupported sample rate" case the finding describes.

### Fix (the cause, not the symptom)

`plugins/Vst3Instrument/Vst3Instrument.cpp` + `plugins/Vst3Instrument/Vst3Instrument.h`:

1. The instrument now remembers the engine configuration (sample rate + block
   size) a re-prepare attempt used, and how many attempts that configuration
   has had (`PrepareConfig m_attemptedConfig`, `int m_attemptCount`).
2. `reprepare()` counts the attempt and, on success, refreshes the budget.
3. `poll()` asks `mayPrepareAgain()` before it retries: a configuration the
   plug-in has not been asked for yet is tried at once (that is the
   sample-rate change the re-prepare exists for); the same configuration is
   tried at most `MaxPrepareAttempts` (3) times. After that the host waits for
   a relevant configuration change - exactly the policy the finding asks for
   ("Add a retry policy or wait for a relevant configuration change before
   trying again").

The re-prepare request itself stays pending (the audio thread keeps raising it),
so nothing about the recovery path changed - only the number of attempts made
for one configuration.

### Regression test

`tests/src/plugins/Vst3InstrumentIntegrationTest.cpp` -
`testAFailedReprepareIsNotRetriedForEver()` (new case in the existing VST3
instrument integration binary):

- The in-tree VST3 fixture gained an opt-in switch
  (`tests/data/vst3-test-instrument/vst3-test-instrument.cpp`): while
  `VST3_TEST_INSTRUMENT_FAIL_PREPARE` is set, `setupProcessing()` refuses the
  setup, so the **host** sees a real failed `prepare()` (not a mock). The
  switch is read per `setupProcessing()` call and is off by default, so every
  other test and the fixture probe are unaffected.
- The test releases the loaded plug-in (so it is unprepared), then drives eight
  cycles of "one audio block + one `poll()`". The observable is the symptom the
  finding describes: the host's own `VST3: re-prepare failed:` warning. A
  `QMessageHandler` counts those warnings for the scope of the test.
- Assertion: the failure is reported (`> 0`) but **not once per poll**
  (`< Cycles`). Without the fix the count is one warning per cycle (8), with
  the fix it is the retry budget (3).

The test is deterministic: no sleep, no timer wait, no wall-clock. `poll()` is
invoked directly through the meta-object
(`QMetaObject::invokeMethod(instrument, "poll", Qt::DirectConnection)`), so the
100 ms timer cannot race the assertions, and the failure injection is an
environment switch read by the fixture, not a timing artefact.

### RED (the product change reverted)

```
$ git diff -- plugins/Vst3Instrument/Vst3Instrument.cpp plugins/Vst3Instrument/Vst3Instrument.h > logs/fix.patch
$ git apply -R logs/fix.patch
$ cmake --build build --target vst3instrument Vst3InstrumentIntegrationTest   # log: build-2-red.log
BUILD_EXIT=0
$ (cd build/tests && ./Vst3InstrumentIntegrationTest)                       # log: test-2-red.log
TEST_EXIT=1
FAIL!  : lmms::Vst3InstrumentIntegrationTest::testAFailedReprepareIsNotRetriedForEver()
   'warnings.count() < Cycles' returned FALSE. (the failed re-prepare was retried on
   every poll: the request is never given up)
Totals: 15 passed, 1 failed, 0 skipped, 0 blacklisted
```

The other 15 cases of that binary still pass, so the new case is the only
difference.

### GREEN (the product change restored)

```
$ git apply logs/fix.patch
$ cmake --build build --target vst3instrument Vst3InstrumentIntegrationTest   # log: build-3-green.log
BUILD_EXIT=0
$ (cd build/tests && ./Vst3InstrumentIntegrationTest)                       # log: test-1b.log (fix in place)
TEST_EXIT=0
PASS   : lmms::Vst3InstrumentIntegrationTest::testAFailedReprepareIsNotRetriedForEver()
Totals: 16 passed, 0 failed, 0 skipped, 0 blacklisted
```

### The affected family

```
$ cmake --build build --target vst3instrument vst3-test-instrument Vst3InstrumentIntegrationTest \
      Vst3InstrumentTest Vst3InstrumentFixtureProbe                          # log: build-1.log, build-4.log
BUILD_EXIT=0
$ (cd build/tests && ctest -R '^Vst3Instrument' --output-on-failure)         # log: test-4-green.log
CTEST_EXIT=0
 1/3 Test #497: Vst3InstrumentFixtureProbe .......   Passed    0.14 sec
 2/3 Test #498: Vst3InstrumentTest ...............   Passed    0.38 sec
 3/3 Test #499: Vst3InstrumentIntegrationTest ....   Passed    6.82 sec
100% tests passed out of 3
```

The wider VST3 / missing-device / latency family (the product `zene` binary
rebuilt first, `build-5-zene.log`, EXIT=0):

```
$ (cd build/tests && ctest -R 'Vst3|MissingDevices|InstrumentLatency' --output-on-failure)   # log: test-6-family.log
CTEST_EXIT=0
100% tests passed out of 10
```

### Callers and siblings checked

- `reprepare()` is called from exactly one place, `poll()`
  (`plugins/Vst3Instrument/Vst3Instrument.cpp:546-550`); no control command, no
  audio-thread path calls it. The new `mayPrepareAgain()` is called from
  `poll()` only, on the GUI thread - the same thread that owns
  `m_attemptedConfig` / `m_attemptCount`.
- The other place that calls `m_plugin.prepare()` is
  `enableOutputBus()` (`plugins/Vst3Instrument/Vst3Instrument.cpp:239-260`); it
  does not raise the re-prepare flag, so it is not part of the retry loop and
  is unchanged.
- The audio thread (`processImpl()`) is untouched: it still only raises the
  atomic `m_needsReprepare` flag, and it never reads the new members.
- The same "clear the flag even when the prepare failed" shape exists in
  `plugins/Vst3Effect/Vst3Effect.cpp:145-161` and
  `plugins/ClapInstrument/ClapInstrument.cpp:203-226`. Those are other
  clusters' files and were deliberately left untouched; the policy added here
  is the model for them.

---

## Bug 2 - cluster 4554: VST3 instrument load failures are not shown in the UI - ALREADY-FIXED

### Why it is not a bug on this branch

The finding reads the constructor
(`plugins/Vst3Instrument/Vst3Instrument.cpp:115-137`) in isolation: the load
failure does only produce a `qWarning` there. The user-visible part of that
failure is raised one layer up, by the 0.5.0 Q050-018 "missing devices" lane,
and it covers VST3 instruments explicitly:

- `plugins/Vst3Instrument/Vst3Instrument.h:92` -
  `unavailableReason()` returns "the VST3 module or class was not found or
  could not be loaded" whenever the module did not load. That is the hook the
  missing-device machinery uses.
- `src/tracks/InstrumentTrack.cpp:1140-1144` - opening a project instantiates
  the instrument and then calls `capturePlaceholder()`.
- `src/tracks/InstrumentTrack.cpp:1189-1197` - `capturePlaceholder()` keeps the
  original XML of an instrument whose `unavailableReason()` is not empty (the
  placeholder), so nothing of the device is lost.
- `src/core/MissingDevices.cpp:208-227` - `missingdevices::collect()` lists
  that instrument as a missing device, with the reason from
  `unavailableReason()`.
- `src/gui/MainWindow.cpp:239-243` - on `projectLoaded` the product raises the
  notice built from exactly that list.
- `src/gui/MissingDevicesNotice.cpp:33-40` - the notice is a visible,
  non-modal warning box ("Some devices could not be loaded") that names the
  device, its format, its vendor and its track.
- `src/core/ControlCommandsProjectFiles.cpp:54` - the agent surface carries the
  same list in the `project.open` reply (`missing_devices`, each with its
  `reason`).

So a project that references a missing or invalid VST3 module does get an
in-app explanation - a warning dialog at project open - and the plug-in's
original data is preserved and written back unchanged.

### The proof that already exists in this tree

`tests/src/core/MissingDevicesPreserveTest.cpp:301-357` -
`hostedFormatsWithAMissingBinaryKeepsTheirXml()` has a `vst3` row whose
project names `/nonexistent/Ghost.vst3` and asserts, end to end:

- the placeholder is kept (`track->preservedInstrument()` is not null),
- `track->instrument()->unavailableReason()` is not empty (line 340),
- the instrument really is the VST3 host's own (not a dummy), so the empty
  parameter view the finding mentions is the designed placeholder state
  (lines 341-345),
- the `project.open` reply lists the missing device (lines 347-352).

That test was run for this change:

```
$ (cd build/tests && ctest -R 'MissingDevices' --output-on-failure)   # log: test-6-family.log
 1/2 Test #141: MissingDevicesPreserveTest .......   Passed    2.86 sec
 2/2 Test #142: MissingDevicesRound2Test .........   Passed    3.75 sec
100% tests passed out of 2
```

### The one gap that is left, stated honestly

The dialog text (`src/core/MissingDevices.cpp:246-259`) names the device and
says that it "could not be loaded", but it does not repeat the per-device
`reason` string; the reason itself is carried in the `project.open` reply's
JSON (`MissingDevice::toJson()`, `src/core/MissingDevices.cpp:52-58`). That is
a wording detail of the shared missing-device notice, not a missing VST3
mechanism, and changing it would touch the notice every format shares - out of
scope for this bug. No product change was made for cluster 4554.

---

## Gates

```
$ python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py \
      /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-281
# log: gates-feature-check.log
GATES_EXIT=0
feature-check zene-qwen-new-p12-281 (base ded355440, 5 changed files)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

No new source file was introduced, so no `tests/fork-sources.txt` /
`tests/all-sources.txt` registration was needed (the fixture and the
integration test are already registered there); no inherited production file was
touched, so no `tests/upstream-modifications.txt` entry was needed either.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with
  it.** Reverted: `git apply -R logs/fix.patch`, rebuild
  (`build-2-red.log`, EXIT=0), run (`test-2-red.log`, **TEST_EXIT=1**,
  `FAIL! ... testAFailedReprepareIsNotRetriedForEver() 'warnings.count() <
  Cycles' returned FALSE`, Totals: 15 passed / 1 failed). Restored: `git apply
  logs/fix.patch`, rebuild (`build-3-green.log`, EXIT=0), run
  (`test-1b.log`, **TEST_EXIT=0**, Totals: 16 passed / 0 failed).
- **The test is deterministic.** No sleep, no timer wait, no wall-clock: the
  test calls the GUI thread's `poll()` directly through the meta-object
  (`QMetaObject::invokeMethod(instrument, "poll", Qt::DirectConnection)`) and
  injects the failure through the fixture's environment switch
  (`VST3_TEST_INSTRUMENT_FAIL_PREPARE`), which is read by the fixture's
  `setupProcessing()` - a value, not a timing. The 100 ms poll timer may fire
  at any moment, and it cannot change the outcome: with the fix the retry
  budget is spent after three attempts, without it every poll adds another
  warning.
- **The fix is at the cause and complete.** The cause is the missing retry
  policy in `reprepare()`/`poll()`; it is fixed there, not by hiding the
  warning. `reprepare()` has exactly one caller (`poll()`), the other
  `prepare()` call site (`enableOutputBus()`) does not raise the re-prepare
  flag, and the audio thread only touches the atomic flag - all checked in the
  "Callers and siblings checked" section. The two sibling hosts with the same
  shape (`plugins/Vst3Effect/Vst3Effect.cpp:145-161`,
  `plugins/ClapInstrument/ClapInstrument.cpp:203-226`) are other clusters and
  are listed here rather than changed silently.
- **No collateral damage.** No saved data, project file or command surface was
  touched: the change is confined to the in-memory re-prepare policy of one
  plug-in host. A successful re-prepare behaves exactly as before (the budget
  is refreshed, `m_needsReprepare` is cleared as before); the new limit is
  three attempts *per configuration*, so a sample-rate or block-size change -
  the case the re-prepare exists for - is still tried immediately. The fixture
  change is opt-in and off by default (the fixture probe, the host test and
  the effect integration test all still pass).
- **Verdict ALREADY-FIXED for cluster 4554, with the code that shows it.**
  `plugins/Vst3Instrument/Vst3Instrument.h:92` (`unavailableReason()`),
  `src/tracks/InstrumentTrack.cpp:1140-1144` and `:1189-1197` (the placeholder),
  `src/core/MissingDevices.cpp:208-227` (the collection),
  `src/gui/MainWindow.cpp:239-243` and `src/gui/MissingDevicesNotice.cpp:33-40`
  (the visible notice), `src/core/ControlCommandsProjectFiles.cpp:54` (the
  agent surface), and the existing proof
  `tests/src/core/MissingDevicesPreserveTest.cpp:301-357` (the `vst3` row).

## Files committed

- `plugins/Vst3Instrument/Vst3Instrument.cpp` - the re-prepare retry policy.
- `plugins/Vst3Instrument/Vst3Instrument.h` - the policy's state and constant.
- `tests/data/vst3-test-instrument/vst3-test-instrument.cpp` - the fixture's
  opt-in "refuse to be prepared" switch (test support).
- `tests/src/plugins/Vst3InstrumentIntegrationTest.cpp` - the regression test.
- `docs/qwen-fixes-2026-10/cluster-new-p12-281.md` - this report.
