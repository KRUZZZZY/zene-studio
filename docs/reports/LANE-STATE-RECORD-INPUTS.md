# LANE STATE — `030/record-inputs` (recording engine surface: rows 64 / 14 / 16, board task 650)

> **Location (2026-09-15).** Moved from the repository root to `docs/reports/LANE-STATE-RECORD-INPUTS.md` by
> REPO-4 ("move lane reports and transcripts out of the repository root"), the second pass —
> the 2026-09-13 pass moved `DOCS-NAMING.md`, `CMDN-REPORT.md` and `CMDN-TRANSCRIPT.md` and
> left the rule in `docs/reports/README.md`. It is a lane report, not `docs/RECORD-INPUTS.md`.
> The lane's own text is unchanged: it is a record of what the lane measured, and editing a
> record is how a record stops being evidence. A citation that names it by bare name still
> resolves to this file; the rule is recorded in `docs/reports/README.md` and
> `docs/CONVENTIONS.md`.

**Worktree:** `zene-030/wrec` · **Branch:** `030/record-inputs` · **Base:** `release/0.3.0` @ `f611c888b`
**Do not merge yet** — see §4. The parent re-runs the build, the suite and the gates on the merged tip before
accepting anything here. Nothing was pushed; nothing was merged; `release/0.3.0` and `zene-030` were not
touched.

## 1. What this lane delivered

| | |
|---|---|
| **Commands** | 9 new ids, all in the `record.*` group: `record.get_state`, `record.arm_track`, `record.disarm_track`, `record.disarm_all`, `record.input_get_state`, `record.input_set`, `record.retro_capture_arm`, `record.retro_capture_status`, `record.retro_capture_to_take`. `track.set_arm` **replaced**: it was a registered refusal stub and is now a real arm verb |
| **Engine** | `src/core/audio/AudioAlsa.cpp`'s **ALSA capture path** (a second PCM, SND_PCM_STREAM_CAPTURE, on its own thread — the first `snd_pcm_readi` this tree has had); `include/AudioInputPath.h` + `src/core/AudioInputPath.cpp` (the plan + the live state); `include/InputChannelRing.h` + `include/AudioWideInputStage.h` + `src/core/AudioWideInputStage.cpp` (the N-channel staging); `MultiTrackRecorder`/`TrackRecorder` N routes × N channels; `include/RetroAudioCapture.h` + `include/RetroAudioRing.h` + `src/core/RetroAudioCapture.cpp` (the retro window) |
| **A16** | `src/core/ControlReversibilityTableRecording.cpp` (10 rows, joined into `reversibilityRowTable()`), plus `track.set_arm`'s row removed from the passive block's refusals |
| **Registered proofs** | `RecordingInputPathTest`, `RetroAudioCaptureTest` (both in `LMMS_TESTS`) and `ControlRecordInputs` (`tests/control-record-inputs.py`, a ctest driving the real binary over `--control-socket`) |
| **Docs** | `docs/RECORD-INPUTS.md`, `docs/RETRO-AUDIO-CAPTURE.md` (design records), the absence lines in `docs/KNOWN-LIMITATIONS.md` and `docs/RELEASE-NOTES-v0.3.0-alpha.md`, the class change noted in `docs/A16-REVERSIBILITY.md` |
| **Fork ledgers** | `tests/fork-sources.txt` (13 new files), `tests/upstream-modifications.txt` (4 inherited files: `include/AudioAlsa.h`, `src/core/audio/AudioAlsa.cpp`, `include/AudioEngine.h`, `src/core/AudioEngine.cpp`) — same commits as the files |
| **Surface snapshot** | `tools/mcp-zene-control/zene_control/commands_snapshot.json` regenerated from a live instance of this build (236 commands, 15 `record.*` ids) |

## 2. Commits

| SHA | What |
|---|---|
| `da8ff314a` | `feat(recording)`: the capture-IN path, the record routes, the retro window, the 10 A16 rows, the two registration points, the ledgers, the docs |
| `68f012a60` | the restart-proof instance started from the config file the command wrote |
| `eda969f1a` | the socket proof's own fixes + the regenerated command snapshot |

## 3. What was RUN here, and what it returned

Every command below was run in this worktree; exit codes are unpiped (`cmd > log 2>&1; echo EXIT=$?`).

```
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWANT_QT6=ON -DWANT_WASM=ON \
      -DWANT_STEM_SPLIT=OFF -DWANT_COVERAGE=OFF > /tmp/wrec-build.log 2>&1; echo EXIT=$?
EXIT=0
$ cmake --build build -j2 --target zene RecordingInputPathTest RetroAudioCaptureTest \
      ReversibilityContractTest ControlEditCommandsTest MultiTrackRecorderTest >> /tmp/wrec-build.log; echo EXIT=$?
EXIT=0                      # 452 objects; every new translation unit compiled
$ cmake --build build -j2 > /tmp/wrec-fullbuild.log 2>&1; echo EXIT=$?
EXIT=0                      # the WHOLE tree, plugins and every test target
$ cd build/tests && ctest -R "RecordingInputPathTest|RetroAudioCaptureTest" --output-on-failure; echo EXIT=$?
EXIT=0                      # 2/2 Passed  (N routes x N channels, sample-exact takes; the retro window)
$ cd build/tests && ctest -R "ReversibilityContractTest|ControlEditCommandsTest|MultiTrackRecorderTest|RecordClipTest|RecordRingBufferTest" --output-on-failure; echo EXIT=$?
EXIT=0                      # 5/5 Passed (the histogram, track.set_arm's new behaviour, the 2-track prototype intact)
$ cd build/tests && ctest -R ControlRecordInputs --output-on-failure; echo EXIT=$?
EXIT=0                      # 1/1 Passed: every check in that file, against the real binary
$ cd build/tests && ctest -R "AudioEngine|Record|Control|MidiRetro|MidiLearn|Reversibility|TakeLane" -j2 --output-on-failure; echo EXIT=$?
EXIT=0                      # 63/63 Passed (AudioEngineTeardownTest, RecordingRealtimeTest, the whole Control* family,
                            #  ControlSocketIntegration, ControlRetroCapture, ControlRecordingRecovery, MidiRetro, TakeLane)
$ cd build/tests && ctest -R ControlCommandsSnapshot --output-on-failure; echo EXIT=$?
EXIT=0                      # 1/1 Passed with the regenerated snapshot (0 missing / 0 extra in all three modes)
$ python3 tools/mcp-zene-control/snapshot_commands.py --socket <own instance> --lane <this worktree>; echo EXIT=$?
EXIT=0                      # wrote 236 commands, 15 of them record.*
$ bash tests/file-length-gate.sh --check; echo EXIT=$?          # EXIT=0
$ bash tests/complexity-gate.sh --check; echo EXIT=$?           # EXIT=0
$ bash tests/fork-sources-gate.sh; echo EXIT=$?                 # EXIT=0  447 fork-NEW files registered
$ bash tests/no-upstream-regression-gate.sh; echo EXIT=$?       # EXIT=0  410 changed paths declared
$ bash tests/duplication-gate.sh; echo EXIT=$?                  # EXIT=0  0.33% duplicated (budget 5%)
$ bash tests/unregistered-tests-gate.sh; echo EXIT=$?           # EXIT=0
$ bash tests/no-tautology-gate.sh; echo EXIT=$?                 # EXIT=0
$ bash tests/evidence-gate.sh; echo EXIT=$?                     # EXIT=0
$ python3 tests/agent-surface-gate.py build/zene tests/data/agent-control-fixture.mmp --check; echo EXIT=$?
EXIT=0                      # 236 commands, 235 swept, 1 allowlisted, 0 compiled out, 0 NEW unregistered actions
```

### The one gate that does not pass locally, and why it is not this lane

```
$ bash tests/release-honesty-gate.sh --dump <this build's `zene --version`>; echo EXIT=$?
EXIT=1                      # 3 of 6 documented features do not match: vst3-hosting, vst3-instrument-hosting, clap-hosting
$ bash tests/release-honesty-gate.sh --dump <the INTEGRATION tree's `zene --version` @ f611c888b>; echo EXIT=$?
EXIT=0                      # RESULT: PASS - all 6 documented feature(s) match this build on linux
```

The difference is the **configure**, not the code: this lane configured with `-DWANT_QT6=ON` (the fork skips
VST with Qt6, `CMakeLists.txt:371`) and without the CI's plugin-hosting provisioning, so CLAP/VST3 are OFF in
*this* build. The same gate passes on the integration tree's build of the same base commit, which is the
control. **The parent's merged-tip run must use the release configuration**, and this lane's build must not be
read as a verdict on the manifest.

## 4. What the parent must run (and what this lane could NOT verify)

**Hardware-bound, unverified on this box, and it is a deliverable sentence rather than a caveat:**
**the real-interface half of this feature is unverified.** This box has no capture device, so the ALSA capture
path's device open, the channel count a driver grants, and the frames it actually delivers have **never been
executed here**. What is proved instead is (a) everything that is engine-side — the arbitrary input count, the
N routes, the sample-exact takes, the retro window's bounds and its non-allocating producer path — and (b) that
the device state is *reported* rather than assumed: `record.input_get_state` carries `capture_capable` /
`capture_open` / `capture_reason` and the granted channels/rate, and the socket proof asserts those fields for
**internal consistency** (a backend with no capture path never claims a reason; a backend whose device refused
always does) instead of for any particular value. `real-device capture` belongs on whatever hardware the
release is verified on; the task-#556 `TwoTrackAlsaCaptureProbe` remains the tool for it.

Not run here, and each needs the parent or CI: `tests/run-all-gates.sh` on the merged tip (expect exit 3),
`tests/coverage-gate.sh` (the 13 new fork files have **no coverage-baseline entries yet**; the gate will either
measure them ≥50% or they need `--reanchor-file` entries with reasons), `tests/run-coverage.sh`,
`tests/mutation-gate.sh`, the release configuration's `release-honesty-gate.sh` (above), and the CI matrix.

## 5. Collisions to expect at integration

`tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/upstream-modifications.txt`,
`docs/KNOWN-LIMITATIONS.md`, `docs/RELEASE-NOTES-v0.3.0-alpha.md`, `src/core/CMakeLists.txt` and
`src/core/ControlRegistryRegistrations.cpp` were all edited additively by hand in this lane; seven sibling
lanes edit the same files the same day. The A16 histogram figure in `docs/RELEASE-NOTES-v0.3.0-alpha.md` is
this lane's own measurement (**236 / 121 / 20 / 7 / 88** in the release configuration, base
**234 / 121 / 20 / 7 / 86**, and `ReversibilityContractTest`'s constant matches the base) and must be re-taken
once, at the merged tip, by whoever integrates — it is a measurement of the whole table, not a per-lane number.
