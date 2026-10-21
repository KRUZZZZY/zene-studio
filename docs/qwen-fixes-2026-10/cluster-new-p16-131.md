VERDICT: FIXED
new-p16-131: FIXED
new-p9-270: ALREADY-FIXED
new-p9-281: FIXED

Evidence:
- src/core/midi/MidiOss.cpp: old sendByte called m_midiDev.putChar on caller thread; now sendByte writes to a non-blocking pipe and a writer thread performs device writes (runWriter).
- include/MidiOss.h: m_quit is std::atomic<bool>; output pipe, OutputWriter, runWriter added.
- src/core/MidiClock.cpp:462-487 emitMessage calls MidiClient::processOutEvent; sequencer client (src/core/midi/MidiAlsaSeq.cpp:298) already queues via MidiOutQueue with no mutex on the audio thread, and raw clients queue in sendByte (OSS after this fix; sndio and Jack already).
- src/core/midi/MidiSndio.cpp:160 sendByte already queues to a writer thread.

Commands:
- Build OSS test: cmake --build build --target MidiOssTest (EXIT=0)
- RED: git apply -R fix.patch; cmake --build build --target MidiOssTest (EXIT=0); ctest MidiOssTest (3 failed) (EXIT=3)
- GREEN: git apply fix.patch; cmake --build build --target MidiOssTest (EXIT=0); ctest MidiOssTest (6 passed) (EXIT=0)
- Affected tests: ctest -R "MidiSndioTest|MidiOssTest|MidiClockTest" (3/3 passed) (EXIT=0)
- feature_check.py (EXIT=0, all PASS)

## Self-check
- Regression test FAILS with product change reverted and PASSES with it: RED exit 3, GREEN exit 0.
- Deterministic: uses a real temp device file, weak device-write/close hooks, QSemaphore; no sleeps or wall-clock timing.
- Fix at cause: device I/O removed from sendByte and caller thread; all raw clients keep access on worker.
- No collateral damage: open failure leaves m_fd=-1 and sendByte no-ops; old projects unaffected.
