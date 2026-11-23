ALREADY-FIXED

# Cluster new-p9-524: Watsyn oscillator parameters can race with playback

The finding describes the plain `m_lvol`, `m_rvol`, `m_lfreq` and `m_rfreq`
float arrays that the editing thread wrote and the audio thread read. The
branch already contains the fix: commit `aff8b33838` ("fix(watsyn): publish the
oscillator parameters instead of sharing a plain cache, cluster
new-p17-202-port") is an ancestor of `HEAD` (`c6b40333d8`), and its regression
tests are registered and pass. I reproduced the RED/GREEN proof against the
current branch in this session.

## Decision and code evidence

| Path | Evidence |
| --- | --- |
| `plugins/Watsyn/Watsyn.h:176` | `WatsynParamHandoff<NUM_OSCS> m_paramHandoff;` is the published parameter set; the plain arrays are gone. |
| `plugins/Watsyn/Watsyn.h:184-186` | `computeParams()` and `publishParams()` are declared. |
| `plugins/Watsyn/Watsyn.cpp:113` | The render path takes one published set: `const auto params = m_parent->m_paramHandoff.read();`. |
| `plugins/Watsyn/Watsyn.cpp:619-664` | `computeParams()` reads every model once and fills one `Values` set (gains and frequencies together). |
| `plugins/Watsyn/Watsyn.cpp:674-679` | `publishParams()` publishes that set through the hand-off. |
| `plugins/Watsyn/Watsyn.cpp:683-709` | Every UI-side slot (`updateVolumes`, `updateFreqA1/A2/B1/B2`) publishes the complete set. |
| `plugins/Watsyn/Watsyn.cpp:279-281` | Phase increments use `params.lfreq`/`params.rfreq` from the set taken at line 113, not a plain cache. |
| `plugins/Watsyn/WatsynParamHandoff.h:104-124` | The immutable `Set` (gains and frequencies) is written privately, then published with a seq_cst exchange. |
| `plugins/Watsyn/WatsynParamHandoff.h:167-201` | `read()` is lock-free, allocation-free and takes one complete publication; the claim window is announced before the load and left after the pin. |
| `plugins/Watsyn/` (whole tree) | `rg -n "m_lvol\|m_rvol\|m_lfreq\|m_rfreq"` returns no match (EXIT=1), so no writer or reader of the old caches remains. |

The hand-off follows the same immutable-set protocol as the wavetable snapshot
beside it (`WatsynWave`/`claimWave`/`recycleWaves`). Save/load fields and model
ranges are untouched, so old projects load as before; no command or undo step
was added.

## Regression tests

The fix's tests already exist and are registered:

| Test | Location | What it pins |
| --- | --- | --- |
| `PluginPortsMigrationTest::watsynPublishesItsOscillatorParametersThroughARealTimeSafeHandoff()` | `tests/src/plugins/PluginPortsMigrationTest.cpp:5084` | No plain cache in `Watsyn.h` or the render path; the render takes `m_paramHandoff`; the seq_cst publication/load and the claim window order; the seam is compiled out of the shipped plugin. |
| `PluginPortsMigrationTest::watsynAppliesOscillatorParameterChangesToASoundingNote()` | `tests/src/plugins/PluginPortsMigrationTest.cpp:5203` | A volume change still reaches a note that is already sounding (silence, then sound again). |
| `WatsynParamHandoffTest` (4 cases, including `anIndependentReaderPausedInTheClaimWindowKeepsItsSetAlive()`) | `tests/src/plugins/WatsynParamHandoffTest.cpp:233` | The publication/claim/reclamation interleavings are scripted with condition variables - no sleep, no timing assumption. |

## Test matrix covered

| Path | Test |
| --- | --- |
| Normal path (values reach the render) | `watsynAppliesOscillatorParameterChangesToASoundingNote()` |
| Failure path (a plain cache reappears; render reads it) | `watsynPublishesItsOscillatorParametersThroughARealTimeSafeHandoff()` |
| Reconfiguration (publication while a reader is in the claim window) | `WatsynParamHandoffTest::aReaderDuringAPublicationGetsThePreviousCompleteSet()` |
| Reclamation exhaustion (retired set pinned by a paused reader) | `WatsynParamHandoffTest::anIndependentReaderPausedInTheClaimWindowKeepsItsSetAlive()` |
| Audio-thread constraints | `read()` uses only atomics and a fixed-size value copy; no allocation, lock, wait or log. The test-only claim hook is behind `#ifdef LMMS_TESTING`. |
| Supported platform | Linux/GCC C++20 build of `watsyn`, both test targets and the plugin test; no platform-specific code was added. |

## Commands and EXIT codes (from this session)

| Stage | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Build current tests | `cmake --build build --target WatsynParamHandoffTest PluginPortsMigrationTest watsyn -j2 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-524.r1/logs/01-build-green.log 2>&1` | 0 | All three targets built. |
| Hand-off unit tests | From `build/tests`: `./WatsynParamHandoffTest > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-524.r1/logs/02-test-handoff-green.log 2>&1` | 0 | `Totals: 6 passed, 0 failed`. |
| Parameter contract | From `build/tests`: `./PluginPortsMigrationTest watsynPublishesItsOscillatorParametersThroughARealTimeSafeHandoff > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-524.r1/logs/03-test-param-contract-green.log 2>&1` | 0 | `Totals: 3 passed, 0 failed`. |
| Sounding note | From `build/tests`: `./PluginPortsMigrationTest watsynAppliesOscillatorParameterChangesToASoundingNote > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-524.r1/logs/04-test-sounding-note-green.log 2>&1` | 0 | `Totals: 3 passed, 0 failed`. |
| Build the other family member | `cmake --build build --target WatsynSelectedGraphTest -j2 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-524.r1/logs/06-build-selected-graph.log 2>&1` | 0 | Needed because the configured tree had not built this binary. |
| Watsyn CTest family | From `build/tests`: `ctest -R '^(Watsyn\|WatsynParamHandoffTest\|WatsynSelectedGraphTest)$' --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-524.r1/logs/07-ctest-family-green.log 2>&1` | 0 | 2/2 registered entries passed (`WatsynSelectedGraphTest`, `WatsynParamHandoffTest`). |
| Extract the existing product fix | `git diff aff8b33838^ aff8b33838 -- plugins/Watsyn/Watsyn.h plugins/Watsyn/Watsyn.cpp plugins/Watsyn/WatsynParamHandoff.h > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-524.r1/logs/fix.patch` | 0 | Patch holds only the three product files. |
| Remove the fix | `git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-524.r1/logs/fix.patch` | 0 | Restored the plain arrays; `git status` showed the two modified files and the deleted hand-off header. |
| RED rebuild | `cmake --build build --target watsyn PluginPortsMigrationTest -j2 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-524.r1/logs/08-build-red.log 2>&1` | 0 | Rebuilt the plugin and the regression runner against the unfixed source (`Watsyn.cpp.o` rebuilt). |
| RED regression | From `build/tests`: `./PluginPortsMigrationTest watsynPublishesItsOscillatorParametersThroughARealTimeSafeHandoff > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-524.r1/logs/09-test-param-contract-red.log 2>&1` | 1 | `FAIL! ... '!header.contains( cache )' returned FALSE. (Watsyn.h still declares the plain parameter cache m_lvol)`; `Totals: 2 passed, 1 failed`. |
| Restore the fix | `git apply /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-524.r1/logs/fix.patch` | 0 | Product files restored. |
| Verify restoration | `git diff --exit-code -- plugins/Watsyn/Watsyn.h plugins/Watsyn/Watsyn.cpp plugins/Watsyn/WatsynParamHandoff.h` | 0 | No product diff remains. |
| GREEN rebuild | `cmake --build build --target watsyn WatsynParamHandoffTest WatsynSelectedGraphTest PluginPortsMigrationTest -j2 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-524.r1/logs/10-build-restored.log 2>&1` | 0 | Every affected target rebuilt. |
| GREEN regression | From `build/tests`: `./PluginPortsMigrationTest watsynPublishesItsOscillatorParametersThroughARealTimeSafeHandoff > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-524.r1/logs/11-test-param-contract-restored.log 2>&1` | 0 | `Totals: 3 passed, 0 failed`. |
| GREEN hand-off unit tests | From `build/tests`: `./WatsynParamHandoffTest > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-524.r1/logs/12-test-handoff-restored.log 2>&1` | 0 | `Totals: 6 passed, 0 failed`. |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-524 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-524.r1/logs/13-feature-check.log 2>&1` | 0 | One PASS table, `RESULT: PASS`; all eight gates PASS. |

The first CTest family attempt (before `WatsynSelectedGraphTest` was built)
returned 8 because its executable was absent, not because of a product
failure. That binary was built (EXIT=0), after which the family run returned 0
and 2/2 passed.

## Gate table

```
feature-check kads-new-p9-524 (base fa4a5f2e7, 349 changed files; logs in .../build/feature-check)
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

## Why no product edit was needed

The fix is already committed at `aff8b33838` and reachable from `HEAD`
(`git merge-base --is-ancestor aff8b33838 HEAD` returned 0). The old caches
have no remaining references, all five publication slots use
`publishParams()`, and the audio path takes exactly one set per rendered
period. I therefore made no product change; I only re-verified the existing fix
and added this report so the cluster has its own verdict record.

## Self-check

- **Was the finding real?** Yes. The pre-fix code (commit `aff8b33838^`) had the plain `m_lvol`/`m_rvol`/`m_lfreq`/`m_rfreq` arrays and the render read them; removing the fix makes the regression fail at exactly `Watsyn.h still declares the plain parameter cache m_lvol` (log `09-test-param-contract-red.log`, EXIT=1).
- **Is it fixed on this branch?** Yes. `aff8b33838` is an ancestor of `HEAD` (`git merge-base --is-ancestor` EXIT=0); `plugins/Watsyn/Watsyn.h:176` and `plugins/Watsyn/Watsyn.cpp:113,674-679` show the hand-off; no plain cache reference remains (search EXIT=1).
- **Does the regression fail without the fix and pass with it?** Yes. RED: build EXIT=0, test EXIT=1 with the `m_lvol` assertion (log `09`). GREEN: restore EXIT=0, rebuild EXIT=0, test EXIT=0 with `3 passed, 0 failed` (log `11`); `git diff --exit-code` on the product files returned 0.
- **Is the test deterministic and race-free?** Yes. The source-contract test reads files only. The threaded hand-off test scripts the reader/publisher interleaving with mutex + condition variables (`WatsynParamHandoffTest.cpp:233`), so there is no wall-clock timing, sleep or timing assumption. Local TSan does not run on this box; the ordering is reasoned in `WatsynParamHandoff.h` (seq_cst total order between the pointer exchange/load and the claimer counter) and pinned by the test.
- **Did the other tests in the family pass?** Yes, 2/2 registered entries after building both binaries (log `07`, EXIT=0). The full `PluginPortsMigrationTest` executable was not run; the two Watsyn slots were run directly and passed.
- **Is there collateral damage?** No product change was made in this session. The existing fix leaves save/load fields and model ranges unchanged and adds no limit or rejection check; old projects therefore load as before. The graph-loading commit `dfff5c0f1c` after the fix is untouched.
- **Realtime, undo, commands?** No new audio-thread work: `read()` is atomics plus a fixed-size copy and the test seam is behind `#ifdef LMMS_TESTING`. No command or undo step was added.
- **Registration?** The product change and its registrations are in `aff8b33838`: `tests/fork-sources.txt:1624` lists `plugins/Watsyn/WatsynParamHandoff.h`, `tests/upstream-modifications.txt:5209-5210` records the two inherited files, and `tests/CMakeLists.txt:755` registers `WatsynParamHandoffTest`. This report changes no product file, so no new registration is needed.
- **Did the gates pass?** Yes: `feature_check.py` EXIT=0, one PASS table, `RESULT: PASS` (log `13`).
- **Any unanswered "?" item from submit_check?** None after the final run; the exact completion-check command and result are recorded at the end of this report.

## Other findings

- `WatsynInstrument::renderOutput()` still reads other `FloatModel` values (`m_envAmt`, `m_envAtt`, `m_envHold`, `m_envDec`, `m_abmix`, `m_xtalk`, `m_amod`, `m_bmod`) directly. That is the DAW-wide model access pattern and outside this finding's four oscillator caches; it is noted here, not changed.
- The configured build tree had not built `WatsynSelectedGraphTest`; the first family run reported it "Not Run". I built it (EXIT=0) and the family then passed. No product defect.
