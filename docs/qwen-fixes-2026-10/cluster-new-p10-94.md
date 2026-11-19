FIXED

# Cluster new-p10-94: refuse an impulse the resampler cannot convert

Product fix: commit `429eaf572a` (`plugins/ConvolutionReverb/ConvolutionReverb.cpp`).
This cluster adds the regression tests in `tests/src/plugins/ConvolutionFftwPlanScopeTest.cpp` and `tests/src/core/DeviceDspTestParts2.h`, and this report. Round 3 (below) adds a second product change in `src/core/ControlCommandsDeviceTools.cpp`. It is in the commit whose message names this cluster.

## Evidence

### 1. The failure and the fix

- **Where the exception starts.** `AudioResampler.cpp:477-480` throws `std::runtime_error` when `src_process` fails. `resampleBuffer` (`src/core/SampleBufferResample.cpp:58`) has no try/catch in its body (lines 58-80 read), so the exception goes up to its caller.
- **Before (`429eaf572a~1`, `ConvolutionReverb.cpp:103-114`).** `atRate` called `resampleBuffer` with no try. It returned the result only when it was non-null and non-empty, and otherwise returned `source`, the decoded buffer at its original rate. So a failure produced the source at the wrong rate, and an exception thrown by the resampler escaped `atRate`.
- **After (`429eaf572a`).**
  - `atRate` (`ConvolutionReverb.cpp:105`) catches `std::exception` at line 120 around `resampleBuffer` (`std::bad_alloc` included) and returns `nullptr` (line 122).
  - An empty result while frames were expected returns `nullptr` (line 126).
  - An empty result when `expected == 0` keeps the one-frame fallback and returns `source` (line 125).
  - `loadImpulse` returns `false` on `nullptr` (`ConvolutionReverb.cpp:208`), before the channel buffers are built. The previous impulse and kernel stay in place.
- **What the code can actually see.** A null return from `resampleBuffer` is not reachable from `atRate`. `resampleBuffer` returns `input` unchanged in its early return (`SampleBufferResample.cpp:67`) and otherwise returns a new buffer, and `atRate` only passes it a non-null source (`loadImpulse` returns earlier on `decoded == nullptr`). The `resampled != nullptr` check is defensive. The real failure modes are: (a) the exception from the resampler; (b) an empty result when frames were expected; (c) `std::bad_alloc`, which is also a `std::exception`.
- **Known limit.** The catch is `std::exception` only. A non-standard throw would still escape `atRate`.

### 2. Tests

Five slots in `tests/src/plugins/ConvolutionFftwPlanScopeTest.cpp` exercise the impulse-load path. This pass added the fifth, `emptyResampleOfAShortImpulseKeepsTheFallback`. Two helpers: `writeWav` (writes a 16-bit stereo WAV) and `tryLoad` (turns an exception from `loadImpulse` into a failure message).

| Slot | Line | What it checks |
|---|---|---|
| `unresamplableImpulseIsRefused` | 191 | A 1 Hz impulse is refused, not loaded: the resampler rejects the engine-rate-to-1-Hz ratio (outside [1/256, 256]), and the state is unchanged. The WAV has 8 frames; the ratio, not the length, is what fails. This is the bug-catching slot (RED with the fix reversed, EXIT=8). |
| `veryShortImpulseStillLoads` | 217 | Covers the normal short-resample branch: 2 frames at 2x the engine rate resample to 1 frame (expected 1), which loads. It does not pin the one-frame fallback. |
| `emptyResampleOfAShortImpulseKeepsTheFallback` | 235 | 1 frame at 4x the engine rate resamples to 0 frames (expected 0), so the one-frame fallback keeps it and it loads. This slot pins the fallback (section 3). |
| `validImpulseLoads` | 257 | Positive control: a normal response loads at the engine rate and at another valid rate. |
| `undecodableImpulseIsRefused` | 285 | Body read. The name says "undecodable", but the body uses a missing file (none is written): `loadImpulse` returns false without throwing, and `impulsePath()` and `tailFrames()` are unchanged. It does not test a corrupt file. |

**The one-frame fallback is pinned.** `expected == 0` (`ConvolutionReverb.cpp:125`) is intentional, per its comment. `veryShortImpulseStillLoads` does not reach it: 2 frames at 2x give `expected == 1`, and the resampler returns one frame through the normal branch. `emptyResampleOfAShortImpulseKeepsTheFallback` does reach it: a 1-frame WAV at 4x gives an empty result with `expected == 0`, so `atRate` returns `source`. The slot passes with the product as committed and fails with line 125 deleted (section 3).

The new slot is a behaviour pin. Its RED proof is the mutation, not a run before the fix; it was not run against `429eaf572a~1`.

### 3. Commands and results

Exit codes are from the run record for each step. The steps marked "`EXIT=` line" have that line appended to their log; the earlier logs have none.

The earlier run's logs are cited with their folder, `new-p10-94.r1/logs/`. That folder is not on disk here (checked 2026-11-19 under `~/.config/kads/zene/state/runs/`), so those rows were not re-read in this round.

| Step | Log (folder `new-p10-94.r1/logs/`) | Result | Exit |
|---|---|---|---|
| Red: new slots before fix | `test-1-red.log` | `Totals: 8 passed, 1 failed` | 8 |
| Red: second run | `test-2-red.log` | `Totals: 8 passed, 1 failed` | 8 |
| Build: test rebuilt with the fix | `build-3.log` | `Built target ConvolutionFftwPlanScopeTest` | not recorded |
| Green | `test-3-green.log` | `100% tests passed out of 1` | 0 |
| Family run (4 tests) | `new-p10-94.r1/logs/test-4-family.log` | `100% tests passed out of 4` (4/4) | 0 |
| 4b reverse: build | `build-5-rev.log` | built | 0 (`EXIT=0` line in log) |
| 4b reverse: test | `test-5-red-reversed.log` | `Totals: 8 passed, 1 failed` | 8 (`EXIT=8` line in log) |
| 4b reapply: build | `build-6-reapplied.log` | built | 0 (`EXIT=0` line in log) |
| 4b reapply: test | `test-6-green-reapplied.log` | `100% tests passed out of 1` | 0 (`EXIT=0` line in log) |
| Fallback pin: build, product as committed | `build-7.log` | built, after the cast in section "Disclosures" | 0 (`EXIT=0` line in log) |
| Fallback pin: test, product as committed | `test-7-new.log` | `100% tests passed out of 1` | 0 (`EXIT=0` line in log) |
| Mutation: build, fallback line removed | `build-8-mut.log` | built | 0 (`EXIT=0` line in log) |
| Mutation: test | `test-8-mutated.log` | `FAIL! : emptyResampleOfAShortImpulseKeepsTheFallback() 'loaded' returned FALSE`; `Totals: 9 passed, 1 failed` | 8 (`EXIT=8` line in log) |
| Restored: build | `build-9-restored.log` | built | 0 (`EXIT=0` line in log) |
| Restored: test, verbose | `test-9-restored.log` | all 10 QTest entries PASS (8 slots + initTestCase and cleanupTestCase); `Totals: 10 passed, 0 failed` | 0 (`EXIT=0` line in log) |
| Gates at HEAD | `new-p10-94.r1/logs/gates-2.log` | `RESULT: PASS`; no FAIL; complexity and file-length: `no new item in your files` | 0 (`EXIT=0` line in log) |
| Final build, four family targets, at HEAD | `build-10.log` | exit 0 | 0 (`EXIT=0` line in log) |
| Final test, ConvolutionFftwPlanScopeTest, at HEAD | `test-10-final.log` | `100% tests passed out of 1` | 0 (`EXIT=0` line in log) |
| Family test, Convolution, SampleBufferResample, AudioResampler, at HEAD | `new-p10-94.r1/logs/test-11-family.log` | `100% tests passed out of 4` (4/4) | 0 (`EXIT=0` line in log) |

Disclosures:

- The 4b reverse/reapply patch was built from commit `429eaf572a`, because the fix was committed before 4b. It was not built from a working-tree diff.
- The first family run exited 8 because three test executables were not built. Its log was overwritten by the passing rerun, so that failure is not in the logs.
- Complexity (CCN) was counted by hand first and then checked by the gate. The gate result is the record. No gate limit was changed.
- TSan was not run.
- Threads: `loadImpulse` is called from the constructor (`ConvolutionReverb.cpp:173`), from `ConvolutionReverbControls::loadSettings` (`:164`), and from the `sampleRateChanged` lambda (`:176`). `atRate` is called only from `loadImpulse` (`:207`). Nothing on the audio thread calls them. The emitting thread of `sampleRateChanged` was not checked.
- Fallback-pin pass, first build: the new slot's comparison of `sampleRate()` (an unsigned `sample_rate_t`) with a signed value failed `-Werror=sign-compare`. The expected value is now cast to `sample_rate_t`. This changes no behaviour.
- Fallback-pin pass, mutation: deleting the fallback line alone did not compile. `expected` (line 114) became unused, and the build treats warnings as errors (`-Werror=unused-variable`). That first mutated build exited 2, and the ctest run after it used the stale binary and exited 0. That run is not evidence and its logs were overwritten. The mutant therefore replaces the deleted line with `(void)expected;`; nothing else differs. The patch is the run's `new-p10-94.r1/logs/mutate.patch`, not committed. The product file was restored and equals `429eaf572a` (`git diff HEAD` empty).
- Gates were rerun at HEAD for the final pass: `gates-2.log` (the gate row above). The earlier `gates-1.log` row is superseded.
- For the r1 logs `build-10`, `test-10-final`, `test-11-family`, `gates-2` and `submit-1` (folder `new-p10-94.r1/logs/`), the log files first held only the quiet-runner summary. They were replaced with the full output of the same runs, and the recorded `EXIT=` line was kept.

## Round 3: `impulseLoad` reports the refusal

**Cause.** `controlRestoreEffectState` (`src/core/ControlDeviceSupport.cpp:567-572`) calls `effect->restoreState(body)`, which returns `void`, and then returns success whenever the XML is valid. `ConvolutionReverbControls::loadSettings` (`plugins/ConvolutionReverb/ConvolutionReverb.cpp:164`) calls `loadImpulse` and drops its `bool`. A refused impulse left the previous impulse in place, but `impulseLoad` still reported success: it added an undo step and marked the song modified.

**Fix.** In `impulseLoad` (`src/core/ControlCommandsDeviceTools.cpp:363`), after the restore succeeds, `impulseOf` (`:356-362`) reads the `impulse` attribute the plugin holds after the restore. If it is not the requested path, `impulseLoad` restores the previous state (`:390`) and returns `ControlErrorKind::Refused` with a message that names the path (`:391`). The undo step and `setModified()` (`:393-395`) come after this check, so a refusal adds no undo step and marks nothing modified. The helper `convolutionControls` (`:338`) is shared with `withImpulse` (`:348`). No plugin file changed.

**Why at the cause, and why core does not link the plugin.** Core cannot link `ConvolutionReverb`, so it cannot receive `loadImpulse`'s `bool`. The state the plugin keeps is what core can read: `controlEffectStateXml` is what `plugin.state_save` writes. The check reads the outcome the plugin kept, not what it reported. It is an exact string match against the path `impulseLoad` holds. A valid load passes it: the DeviceDsp slot asserts `loaded.ok` for a valid click (`tests/src/core/DeviceDspTestParts2.h:76`).

**GUI confirmation (by reading, not a GUI run).** A failed `ControlResult` reaches the user on both paths. On the direct path, `announce` (`src/gui/CommandRun.cpp:103-112`) shows `errorMessage` for a failed result. On the form path, `src/gui/CommandForm.cpp:354-359` invokes the command, emits `ran(m_result)`, and on failure calls `showError(m_result.errorMessage, nullptr)` and returns. The effect menu calls `runCommand` at `src/gui/EffectDeviceMenu.cpp:167-168`.

**New slot.** `anUnresamplableImpulseIsRefusedThroughTheCommand` (`tests/src/core/DeviceDspTestParts2.h:64-108`, declared at `tests/src/core/DeviceDspTest.cpp:462`) calls `plugin.impulse_load` through `ControlRegistry::instance()->invoke`, the entry the GUI uses. It loads a valid click, asserts `loaded.ok`, and saves the project. It then loads an 8-frame 1 Hz WAV and checks that the result is `Refused` with a message, that the saved impulse path is unchanged, that the audio output is unchanged (within 0.03), that `Engine::projectJournal()->undoDepth()` is unchanged, and that `Engine::getSong()->isModified()` stays false. Finally it reloads the click and asserts `ok`.

**Commands and exit codes.** The logs below are from the round-3 run (`new-p10-94.r3/logs/`). They are not the earlier run's `logs/` files, whose names overlap. Each log ends with an `EXIT=` line; the codes were checked by grep in this session.

| Run | Log | EXIT | Result |
|---|---|---|---|
| Red, before the fix | `test-1-red.log`, `test-2-red.log` | 8 | `'!refused.ok'` returned FALSE |
| With the fix | `build-10.log`, `test-10-green.log` | 0, 0 | - |
| Family, four targets | `test-11-family.log` | 8 | 3 of 4 pass; the failure is the transcript test (see Disclosures) |
| Fix reversed | `test-4-red-reversed.log` | 8 | `'!refused.ok'` returned FALSE |
| Fix reapplied | `test-5-green-reapplied.log` | 0 | - |
| Undo-depth mutant | `test-6-mutated.log` | 8 | `undoDepth() == undoDepthBefore` returned FALSE |
| Modified-only mutant | `test-6b-mutated-modified.log` | 8 | `!Engine::getSong()->isModified()` returned FALSE (`Parts2.h:105`) |
| Restored | `test-7-restored.log`, `test-7b-restored.log` | 0, 0 | - |
| Final build, at HEAD | `build-13.log` | `zene DeviceDspTest ConvolutionFftwPlanScopeTest` built | 0 |
| Final DeviceDsp test, at HEAD | `test-13-final.log` | `100% tests passed out of 1` | 0 |
| Final family test, at HEAD | `test-14-family.log` | `100% tests passed out of 6` (AudioResamplerRatioTest, AudioResamplerPoolMemoryTest, SampleBufferResampleTest, DeviceDspTest, ConvolutionFftwPlanScopeTest, ControlCommandsSnapshot) | 0 |
| Transcript test, at HEAD | `test-14b-transcript.log` | `0% tests passed, 1 tests failed out of 1` (see Disclosures) | 8 |
| New slot, verbose, at HEAD | `test-15-slot.log` | `PASS   : DeviceDspTest::anUnresamplableImpulseIsRefusedThroughTheCommand()` (line 38), no SKIP | 0 |
| Gates, at HEAD | `gates-3.log` | `RESULT: PASS` | 0 |
| Pre-submit check | `submit-1.log` | `RESULT: PASS - submit`; six ASK items printed, answered in Self-check | 0 |

Runs in this session (the same worktree, after the edits):

- `build-7` and `test-8` (fix in place, 8-frame WAV): EXIT 0 and 0, `100% tests passed out of 1`.
- `build-7b-rev` and `test-8b-red` (effect fix reverted): EXIT 0 and 8. `FAIL! : ...unresamplableImpulseIsRefused() 'error.isEmpty()' returned FALSE` (`SRC ratio outside [1/256, 256]`).
- `build-7c` and `test-8c-green` (fix reapplied): EXIT 0 and 0. `git diff 429eaf572a -- plugins/ConvolutionReverb/ConvolutionReverb.cpp` is empty.
- `gates-1.log`: EXIT 0, `RESULT: PASS`, no FAIL line.
- `build-12-dsp` and `test-12-dsp` (DeviceDsp target, with the new slot): EXIT 0 and 0, `100% tests passed out of 1`. The ctest line covers the whole executable; per-slot results are not printed.

Disclosures (round 3):

- The slot calls `project.save` to get a clean modified flag, because `Song::setModified(bool)` is private. The save writes `session.mmp` inside the test's `QTemporaryDir`.
- `ControlReversibilityTranscript` exits 8 with the fix reversed (`test-4b-transcript-reversed.log`), with it reapplied (`test-5b-transcript-reapplied.log`), and in the round-3 family run (`test-11-family.log`). Both states show `0% tests passed, 1 tests failed out of 1`. At HEAD (`test-14b-transcript.log`, `EXIT=8`) the log has `cannot undo 'project.save' (class 'snapshot')` (line 43) and `section E could not run: plugin.list offered no 'amplifier' device` (line 211). The cause is not verified. The failure does not depend on this change: it occurs with the fix reversed and with it applied.
- The earlier table above lists a family run of 4 of 4 (`new-p10-94.r1/logs/test-11-family.log`, EXIT 0). That is from the earlier run. The round-3 family run (`new-p10-94.r3/logs/test-11-family.log`, `EXIT=8`) is 3 of 4, and the difference is the transcript test.
- Five DeviceDspTest slots are skipped in this build, each with the reason `this build has no <module> module`: two de-esser slots, dynamic EQ, transient shaper and vocoder (`new-p10-94.r3/logs/test-1-red.log`). A skip is not a failure. The new slot is not skipped: `test-15-slot.log` reports `PASS` for it (line 38) and no SKIP line.
- `canUndo()` was replaced by `undoDepth()` so that the slot can check the count.
- The CCN of `impulseLoad` was counted by hand. The gate result (`gates-1.log`) is the record.
- TSan was not run. `include/ControlRegistry.h:108` is a comment that says command handlers always run on the UI thread; I did not check the dispatch code against it. The round-3 change is in `src/core/ControlCommandsDeviceTools.cpp` only.
- Portability: standard C++20 and Qt API only, no platform code. `QDomDocument` is already used in this file.

## Other findings

Not changed in this cluster.

- **Callers ignore the `bool` from `loadImpulse`.** The call in `ConvolutionReverbControls::loadSettings` (`ConvolutionReverb.cpp:164`), the constructor (`:173`), and the `sampleRateChanged` lambda (`:176`) are bare statements. A refused impulse is silent at all three. (An earlier note cited line 151 for `loadSettings`; line 151 is in `saveSettings`. The call is at line 164.)
- **Siblings restore the same state without checking the impulse (not changed here).** `plugin.state_load`, preset load, chain preset, device copy, structural/rack and wasm edit each restore device state through the same path. None checks whether `loadImpulse` accepted the impulse, so a refused impulse could be reported as a success there too. I did not run them. Locations, from the p1 matrix in the round-3 run logs:
  - `plugin.state_load`: `src/core/ControlCommandsPluginState.cpp:202`, `:208` (undo), `:287-288`
  - preset load: `src/core/ControlCommandsPluginPreset.cpp:311`, `:319`
  - chain preset: `src/core/ControlChainPresetSupport.cpp:498` (via `controlRestoreDeviceState`)
  - device copy: `src/core/ControlCommandsDeviceCopy.cpp:353` (result ignored), `:365`
  - structural/rack: `src/core/ControlStructuralSupport.cpp:487` (rack wrap not traced)
  - wasm edit: `src/core/ControlCommandsWasmEdit.cpp:336`, `:368`
  - Also in the matrix, not in the packet's list: `src/core/ControlCommandsChainEdit.cpp:125` (not traced)

## Self-check

Answers to the six ASK items that `submit_check.py` printed (`new-p10-94.r3/logs/submit-1.log`, and `submit-2.log` after the report commit). Paths are given where they are not `plugins/ConvolutionReverb/ConvolutionReverb.cpp`.

1. **Fails with the change reverted, passes with it (exit codes).** Round 3 (`new-p10-94.r3/logs/`): with the round-3 fix reversed, `test-4-red-reversed.log:49` reads `FAIL! : DeviceDspTest::anUnresamplableImpulseIsRefusedThroughTheCommand() '!refused.ok' returned FALSE`, and `:183` is `EXIT=8`. With it reapplied, `test-5-green-reapplied.log` is `100% tests passed out of 1` with `EXIT=0` (`:8`). At HEAD, `test-13-final.log` is `100% tests passed out of 1` with `EXIT=0`, and `test-14-family.log` is `100% tests passed out of 6` with `EXIT=0`. The earlier run's evidence for the first fix (`new-p10-94.r1/logs/test-5-red-reversed.log:11` and `:30`, `EXIT=8`) is not on disk here, so those lines were not re-read and are not kept.
2. **Deterministic.** The new slot (`tests/src/core/DeviceDspTestParts2.h:64-108`) has no sleep, timer or thread: a grep of those lines for `sleep|qWait|QTimer|QThread|chrono` finds nothing (grep exit 1). Its inputs are injected: the slot writes an 8-frame 1 Hz WAV, and the state is compared before and after the load. The earlier slots in `tests/src/plugins/ConvolutionFftwPlanScopeTest.cpp` were checked the same way: a grep for `sleep|qWait|QElapsedTimer|std::chrono|QTimer|msleep|usleep|QThread|rand|time(` returns no lines. Repeat runs agree: `new-p10-94.r3/logs/test-1-red.log:49` and `test-2-red.log:49` both show the same FAIL message and `EXIT=8`.
3. **Fix at the cause; callers and siblings: partly.** The changed unit is `atRate` (`ConvolutionReverb.cpp:105-127`). Its only caller is `loadImpulse` (`:207`), which returns false on a null result (`:208`). Two gaps stay open. First, the three callers of `loadImpulse` ignore its bool: `:164`, `:173` and `:176` are bare calls, so a refused impulse is silent there (see Other findings). Second, the other `resampleBuffer` callers, `src/core/ControlStemSource.cpp:130` and `src/core/RenderQueue.cpp:207`, do not pass through `atRate` and were not changed. I did not check whether they catch the resampler's exception. `impulseLoad` now compares the path the plugin kept after the restore with the requested one, and returns `Refused` when they differ (`src/core/ControlCommandsDeviceTools.cpp:388-391`; see Round 3).
4. **No collateral damage: partly.** The normal path is unchanged. A non-empty resample is returned as before (`:124`). `veryShortImpulseStillLoads` (`:217`) and `validImpulseLoads` (`:257`) pass: all 10 QTest entries pass in the earlier run's `new-p10-94.r1/logs/test-9-restored.log` (not re-read here), and at HEAD the ConvolutionFftwPlanScopeTest entry passes in `new-p10-94.r3/logs/test-14-family.log` (`EXIT=0`, 6 of 6). Two changes can reach saved projects, both intended:
   - An impulse that used to throw is now refused, and the previous kernel stays (`unresamplableImpulseIsRefused`, `:191`). Before, the exception escaped `loadImpulse`.
   - An impulse whose resample came back empty while frames were expected used to load as the wrong-rate `source` (`429eaf572a~1`, `ConvolutionReverb.cpp:113`: `return resampled != nullptr && resampled->size() > 0 ? resampled : source;`). Now it is refused (`:126`, `:208`). I did not establish that a real file reaches this case, and no slot tests it.

   The code added is a try/catch and two null returns inside `atRate` (`:120-126`). No limit was added. `loadImpulse` is the only caller of `atRate`.
5. **ALREADY-FIXED or FALSE-POSITIVE.** Not applicable: the verdict is FIXED (line 1 of this report).

6. **Sibling commands in the changed command file.** This cluster changed one command file, `src/core/ControlCommandsDeviceTools.cpp` (`git diff --name-only 429eaf572a~1 HEAD`). It registers three commands: `plugin.impulse_load` (line 403, the changed one), `mixer.console_apply` (line 408) and `rack.split` (line 415). The impulse helpers `impulseOf` and `withImpulse` are used only inside `impulseLoad` (lines 380 and 388 of that file, and none elsewhere in `src`), so `mixer.console_apply` and `rack.split` do not reach the changed code. I did not read their bodies. The `rack.*` names in the submit_check item are not in the changed file. `rack.add_chain` appears only in `ControlCommandsChain.cpp`, `ControlCommandsPlugin.cpp`, `ControlCommandsRack.cpp`, `ControlCommandsRackWrap.cpp` and `ControlCommandsRackZones.cpp`, none of which this cluster changed. The other names were not traced.

**Round 3 checks, new slot** (`tests/src/core/DeviceDspTestParts2.h:64-108`):
- Undo-depth mutant: `new-p10-94.r3/logs/test-6-mutated.log`, `EXIT=8` (line 183). It fails at `undoDepth() == undoDepthBefore` (the refused load added undo steps: depth 13 -> 14), so a refused load that adds an undo step is caught.
- Modified-only mutant: `new-p10-94.r3/logs/test-6b-mutated-modified.log`, `EXIT=8` (line 183). It fails at `!Engine::getSong()->isModified()` (`Parts2.h:105`), so a refused load that marks the song modified is caught.
- Transcript test: independent of this change, as stated under Disclosures (exit 8 in both states).
- Known limit: `impulseLoad` checks the stored `impulse` string with an exact compare against the absolute path it computed. If the plugin stored the same file in another form (relative path, different case, a resolved symlink), a load that succeeded would be reported `Refused` and undone. The command passes an absolute path, and `loadSettings` is described as storing that form; I did not re-check that. No test covers this case.
