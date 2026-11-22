FIXED
new-p18-34: FIXED
Decision: REAL. `playNoteImpl()` now leaves a note without a voice unassigned so it retries on the next render period. The fixed 1024-voice scan is bounded per attempt (`plugins/FreeBoy/FreeBoy.cpp:63,455-457`).

- RED: `ctest -R '^FreeBoy$' --output-on-failure` from `build/tests` — EXIT=8. `freeBoyExhaustedNoteRetriesWhenVoiceFrees()` failed because the waiting note stayed silent; logs `7-build-p18-34-red-proof.log`, `8-red-proof-p18-34.log`.
- GREEN: same CTest command — EXIT=0; logs `9-build-p18-34-green-proof.log`, `10-green-proof-p18-34.log`.
- The same test passed in final focused-family run `ctest -R '^FreeBoy' --output-on-failure` — 3/3, EXIT=0, log `48-ctest-freeboy-family-final.log`.

new-p15-69: FIXED
Decision: REAL. Pools track instrument references and active voices; retired pools are erased only after both counts reach zero. A control-side `std::jthread` reaps retired pools every 100 ms. Note cleanup only releases the voice atomically and never locks; see `plugins/FreeBoy/FreeBoy.cpp:94-104,140-164,626-630`.

- RED: `ctest -R '^FreeBoyPoolReclamation$' --output-on-failure` from `build/tests` — EXIT=8. `freeBoyOldSampleRatePoolsAreReclaimed()` observed retained old-rate pools; logs `21-build-p15-69-red-final.log`, `22-red-proof-p15-69-final.log`.
- GREEN: same CTest command — EXIT=0; logs `23-build-p15-69-green-final-proof.log`, `24-green-proof-p15-69-final.log`.
- The same test passed in final focused-family run `ctest -R '^FreeBoy' --output-on-failure` — 3/3, EXIT=0, log `48-ctest-freeboy-family-final.log`.
- QA reliability fix: the worker rechecks the test pause flag after taking the registry mutex, and the pause hook takes that mutex after setting the flag. Returning from pause therefore waits for any in-flight collection to finish and prevents a later pass until resumed (`plugins/FreeBoy/FreeBoy.cpp:146-150,259-266`).

new-p15-70: FIXED
Decision: REAL. The instrument publishes its pool through a lock-free `std::atomic<FreeBoyVoicePool*>`; sample-rate changes use an atomic exchange, and an atomic reader handoff keeps a retired pool alive until voice acquisition finishes (`plugins/FreeBoy/FreeBoy.h:81`, `plugins/FreeBoy/FreeBoy.cpp:124,176-184,323-330`).

- RED: `ctest -R '^FreeBoySampleRateHandoff$' --output-on-failure` from `build/tests` — EXIT=8. The raw-pointer probe failed `handoffIsRealtimeSafe()`; logs `33-build-p15-70-red-final-proof.log`, `34-red-proof-p15-70-final.log`.
- GREEN: same CTest command — EXIT=0; logs `35-build-p15-70-green-final-proof.log`, `36-green-proof-p15-70-final.log`.
- The same test passed in final focused-family run `ctest -R '^FreeBoy' --output-on-failure` — 3/3, EXIT=0, log `48-ctest-freeboy-family-final.log`.

## Batch verification

- Final build: `cmake --build build --target zene freeboy PluginPortsMigrationTest` — EXIT=0, log `47-build-audio-cleanup.log`.
- Final focused tests: `ctest -R '^FreeBoy' --output-on-failure` from `build/tests` — 3 tests, 3 passed, EXIT=0, log `48-ctest-freeboy-family-final.log`.
- Rebuild after QA pause fix: `cmake --build build --target zene freeboy PluginPortsMigrationTest -j12` — EXIT=0, log `59-build-qa-reaper-pause.log`.
- Focused tests after QA pause fix: `ctest -R '^FreeBoy' --output-on-failure` from `build/tests` — 3 tests, 3 passed, EXIT=0, log `60-ctest-freeboy-after-pause-fix.log`.
- Full migration harness: `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` from `build/tests` — 83 passed, 1 failed, 2 skipped, EXIT=8, log `49-ctest-plugin-migration-full.log`. The sole failure is the existing LADSPA fixture assertion `SWH Decimator test fixture was not loaded`; all FreeBoy slots passed.
- Source manifest repair: regenerated the `tests/all-sources.txt` entry list using its own `Regenerate with` recipe, preserving the documented header and dropping stale vendored `plugins/SfzPlayer/sfizz/...` entries. `bash tests/all-sources-reproduce.sh` — EXIT=0; `bash tests/fork-sources-gate.sh` — EXIT=0. Gate logs: `55-all-sources-reproduce-final.log`, `56-fork-sources-gate-final.log`.
- Gate after the QA pause fix: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-34` — EXIT=0; final table in `61-feature-check-after-qa-fix.log`.

## Self-check

- Regression proof: yes; each targeted test failed with the product patch reversed and passed after reapplying it (red/green logs above). The current final FreeBoy family is 3/3.
- Determinism: yes; exhaustion frees a selected voice directly, pool reclamation pauses the worker through a mutex barrier and calls the collector seam explicitly, and handoff checks the atomic publication type and realtime allocation watch. The worker rechecks pause under the same mutex; the regression uses no sleeps or timing-based assertions.
- Cause and sibling paths: checked construction, sample-rate change, destruction, note acquisition, note cleanup, retired-pool collection, and reader handoff. Both sample-rate callback publication and destructor unpublication use atomic exchange; audio-side cleanup only updates atomics.
- Collateral behavior: project loading, save data, parameters, and user controls are unchanged. The polyphony cap only applies to FreeBoy's existing 1024-voice pool; unassigned notes retry. Pool management and reclamation run off the audio thread.
- Verdict check: all three decisions are REAL, so the ALREADY-FIXED/FALSE-POSITIVE evidence branch does not apply; code locations and failing assertions are given above.
- Files committed for this batch: `plugins/FreeBoy/FreeBoy.cpp`, `plugins/FreeBoy/FreeBoy.h`, `tests/CMakeLists.txt`, `tests/src/plugins/PluginPortsMigrationTest.cpp`, `tests/upstream-modifications.txt`, `tests/all-sources.txt`, and this report.
