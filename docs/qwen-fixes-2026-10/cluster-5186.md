Verdict: FIXED

5186: ALREADY-FIXED
new-p12-159: FIXED

Evidence:
- 5186 model lifetime already coordinated by commits 73ecb52ba (pin modulation targets during audio writes) and 97789a3fd (scope modulation teardown gates to target generations). include/ModulationLayer.h:370-396 targetAlive/beginTargetWrite pin the writer count; src/core/ModulationLayerPublisher.cpp:129-158 retireTarget closes the slot and waits for in-flight writers before nulling the token. Existing test ModulationAudioViewTest::aDestroyedTargetIsSkippedByAnOlderView passes on this branch.
- new-p12-159: instrument had no teardown coordination. Added Instrument::aboutToBeDestroyed signal (include/Instrument.h, src/core/Instrument.cpp), emitted before deletion in InstrumentTrack (src/tracks/InstrumentTrack.cpp), and a per-slot m_instrumentWatch in ModulationLayerPublisher (include/ModulationLayer.h, src/core/ModulationLayerPublisher.cpp) connected to retireTarget, so a destroyed instrument retires its slot before derived state is torn down.

Tests:
- ModulationAudioViewTest::anInstrumentTargetIsRetiredWhenDestroyed (new) FAILS without product change, PASSES with it.

Commands:
- RED: git apply -R fix.patch; cmake --build build --target ModulationAudioViewTest -j2 (EXIT=0); cd build/tests && ctest -R '^ModulationAudioViewTest$' (EXIT=8, 11 passed, 1 failed: anInstrumentTargetIsRetiredWhenDestroyed).
- GREEN: git apply fix.patch; cmake --build build --target ModulationAudioViewTest zene -j2 (EXIT=0); cd build/tests && ctest -R '^ModulationAudioViewTest$' (EXIT=0, 12 passed).
- Gates: python3 feature_check.py (EXIT=0); python3 submit_check.py (see below).
" filePath">
/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5186/docs/qwen-fixes-2026-10/cluster-5186.md
## Self-check
- RED: reverted product hunks via git apply -R fix.patch, rebuilt ModulationAudioViewTest (EXIT=0), ran ctest (EXIT=8, 11 passed, 1 failed: anInstrumentTargetIsRetiredWhenDestroyed).
- GREEN: restored product hunks, rebuilt ModulationAudioViewTest and zene (EXIT=0), ran ctest (EXIT=0, 12 passed).
- The test is deterministic: it uses a direct delete of an Instrument and checks publisher.targetAlive() afterwards; no sleeps, timers, or races.
- Cause and completeness: checked all callers of Instrument destruction (InstrumentTrack destructor now emits aboutToBeDestroyed before delete) and publisher watch arrays (m_watch and m_instrumentWatch) disconnected in both destructor and publishView. No new control commands.
- Collateral: Instrument gains Q_OBJECT and a signal; existing instrument plugins build. No saved-data format change. targetAlive gating only retires the slot whose instrument is being destroyed.
- 5186 already fixed by commits 73ecb52ba and 97789a3fd; evidence include/ModulationLayer.h:370-396 and src/core/ModulationLayerPublisher.cpp:129-158.
