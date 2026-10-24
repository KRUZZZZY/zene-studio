FIXED

# Cluster new-p17-202-port: Watsyn oscillator parameter caches race with playback

## Decision and code evidence

**Verdict: REAL.** The finding was present in the parent of the existing fix commit `aff8b3383`.

| Path | Evidence |
| --- | --- |
| `plugins/Watsyn/Watsyn.h:170-174` in `HEAD^` | `m_lvol`, `m_rvol`, `m_lfreq`, and `m_rfreq` were plain `float` arrays. |
| `plugins/Watsyn/Watsyn.cpp:417-438` | Model `dataChanged()` signals called `updateVolumes()` and `updateFreqA*()`. |
| `plugins/Watsyn/Watsyn.cpp:130-200` in `HEAD^`; phase updates later in `renderOutput()` | The audio render read those arrays while producing each frame. |
| `plugins/Watsyn/Watsyn.cpp:112, 627-717`; `plugins/Watsyn/Watsyn.h:176,184-186` | The fix takes one immutable `WatsynParamHandoff` set per render call. Every volume/pan and frequency update slot recomputes and publishes that set. |

The unsynchronised reads and writes made a knob change during playback a C++ data race. The four caches have no remaining product callers. Save/load fields and model ranges are unchanged, so existing projects retain their prior parameter values and serialization. The product change added no command or undo step.

In production, `WatsynParamHandoff::read()` uses only atomic operations and a fixed-size value copy: it does not allocate, free, lock, wait, or log. Publication and reclamation run on the instrument's model-change path. The claim-window seam used by the threaded regression is guarded by `LMMS_TESTING`.

## Regression and proof

The regression is `PluginPortsMigrationTest::watsynPublishesItsOscillatorParametersThroughARealTimeSafeHandoff()` (`tests/src/plugins/PluginPortsMigrationTest.cpp:3329`). With the product patch removed, it fails deterministically because `Watsyn.h` declares `m_lvol`; with the fix restored, it passes. `watsynAppliesOscillatorParameterChangesToASoundingNote()` (`:3448`) additionally confirms a volume change reaches a note already playing. `WatsynParamHandoffTest::anIndependentReaderPausedInTheClaimWindowKeepsItsSetAlive()` (`tests/src/plugins/WatsynParamHandoffTest.cpp:233`) scripts the reclamation interleaving with condition-variable handshakes and no sleep or timing assumption.

The existing fix was already committed at `aff8b3383`, so the proof patch was extracted from that commit against its parent, then reversed and restored in the worktree. The patch is retained at `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-202-port.r3/logs/fix.patch`.

| Stage | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Extract product patch | `git diff HEAD^ HEAD -- plugins/Watsyn/Watsyn.h plugins/Watsyn/Watsyn.cpp plugins/Watsyn/WatsynParamHandoff.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-202-port.r3/logs/fix.patch` | 0 | Captured only product files. |
| Remove fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-202-port.r3/logs/fix.patch` | 0 | Restored the original unsynchronised arrays. |
| RED rebuild | `cmake --build build --target zene watsyn PluginPortsMigrationTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-202-port.r3/logs/08-proof-red-build.log 2>&1` | 0 | Rebuilt `zene`, Watsyn and the regression runner against the unfixed source. |
| RED regression | `cd build/tests && ./PluginPortsMigrationTest watsynPublishesItsOscillatorParametersThroughARealTimeSafeHandoff > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-202-port.r3/logs/09-proof-red-test.log 2>&1` | 1 | Failed at `Watsyn.h still declares the plain parameter cache m_lvol`. QtTest: 2 passed, 1 failed. |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-202-port.r3/logs/fix.patch` | 0 | Restored the committed product patch. |
| GREEN rebuild | `cmake --build build --target zene watsyn WatsynParamHandoffTest WatsynSelectedGraphTest PluginPortsMigrationTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-202-port.r3/logs/10-proof-green-build.log 2>&1` | 0 | Rebuilt the app, plugin, and every affected registered test target. |
| GREEN regression | `cd build/tests && ./PluginPortsMigrationTest watsynPublishesItsOscillatorParametersThroughARealTimeSafeHandoff > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-202-port.r3/logs/11-proof-green-test.log 2>&1` | 0 | Same regression passed. QtTest: 3 passed, 0 failed. |
| Sounding-note path | `cd build/tests && ./PluginPortsMigrationTest watsynAppliesOscillatorParameterChangesToASoundingNote > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-202-port.r3/logs/05-sounding-note.log 2>&1` | 0 | A live note silenced and resumed after volume changes. |
| Watsyn CTest family | From `build/tests`: `ctest -R '^(WatsynSelectedGraphTest|WatsynParamHandoffTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-202-port.r3/logs/07-watsyn-family.log 2>&1` | 0 | 2/2 registered Watsyn tests passed. |

A broader run of `ctest -R '^(Watsyn|WatsynSelectedGraphTest|WatsynParamHandoffTest|PluginPortsMigrationTest)$' --output-on-failure` from `build/tests` returned **8** (log `06-ctest-family.log`): both registered Watsyn tests passed, as did the Watsyn rows in `PluginPortsMigrationTest`; that full executable had one unrelated failure, `nonRealtimeLadspaEffectsAreInvalid()`, because its SWH Decimator fixture was not loaded. This does not affect either focused Watsyn regression.

## Gates and completion check

The required feature gate passed. Its recorded table is in `12-feature-check.log`:

| Gate | Result |
| --- | --- |
| fork-sources | PASS, exit 0 |
| all-sources-reproduce | PASS, exit 0 |
| no-upstream-regression | PASS, exit 0 |
| check-namespace | PASS, exit 0 |
| no-tautology | PASS, exit 0 |
| doc-figures | PASS, exit 0 |
| complexity | PASS, no new item in changed files |
| file-length | PASS, no new item in changed files |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-202-port` | **EXIT=0** |

The existing fix commit `aff8b3383` explicitly committed: `plugins/Watsyn/Watsyn.cpp`, `plugins/Watsyn/Watsyn.h`, `plugins/Watsyn/WatsynParamHandoff.h`, `tests/CMakeLists.txt`, `tests/all-sources.txt`, `tests/fork-sources.txt`, `tests/rt-safety-scope.txt`, `tests/src/plugins/PluginPortsMigrationTest.cpp`, `tests/src/plugins/WatsynParamHandoffTest.cpp`, `tests/upstream-modifications.txt`, and this report. The all-sources regeneration in that commit matches the manifest's published recipe; it was required because the base manifest contained vendored paths excluded by that recipe.

## Self-check

- **Was the finding real?** Yes. `HEAD^` had plain float arrays in `Watsyn.h:170-174`, model callbacks wrote them in `Watsyn.cpp:618-645`, and the render path read them at `Watsyn.cpp:130-200`.
- **Did the deterministic regression fail without and pass with the fix?** Yes. The exact RED/GREEN commands, assertions and exit codes are recorded in the proof table above and in numbered logs `09-proof-red-test.log` and `11-proof-green-test.log`.
- **Were all writers and readers checked?** Yes. All five update slots publish the complete set; the render takes one set; the old caches have no remaining product references.
- **Did the Watsyn test family pass?** Yes, 2/2 registered CTest tests passed. The separate full plugin-migration ctest failure is documented above and is caused by its unavailable LADSPA fixture.
- **Did the feature gate pass?** Yes, `feature_check.py` returned 0; its full PASS table is preserved in the numbered log.
- **Was the fix committed?** Yes, in `aff8b3383` with the explicit paths listed above. This report is being refreshed in the follow-up commit after the review rerun.
- **Completion check:** Run after committing this refreshed report; record its exact exit code and answer any `?` items here before finalizing.
