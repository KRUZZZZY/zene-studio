FALSE-POSITIVE

3776: FALSE-POSITIVE
The handler's private __transaction payload is not a public success-result field: ControlSchema::finishResult removes it (src/core/ControlSchema.cpp:247). The public reversibility record is still made centrally. clip.remove_silence takes a track undo step (src/core/ControlCommandsArrangeTools.cpp:198) and the contract classifies it as TrueInverse with a Track checkpoint (src/core/ControlReversibilityTableDevices.cpp:180). When a true-inverse command leaves a journal step without a handler descriptor, ControlRegistry::recordTransactionOf marks the fallback record reversible and records it (src/core/ControlRegistry.cpp:607, 616, 619); control.transactions exposes each record's mechanism (src/core/ControlTransactions.cpp:132, 143). Thus the claimed missing record does not occur. Step 2's false-positive branch applies, so no regression test or product change was added for 3776.

Code-evidence commands (each exit 0):
- rg -n 'recordTransactionOf|recorded.isEmpty|honest.reversible = journalled|recordTransaction\(honest\)|finishResult|result->result.remove\(QStringLiteral\("__transaction"\)\)|QJsonArray ControlRegistry::transactions|entry.insert\(QStringLiteral\("mechanism"\)' src/core/ControlRegistry.cpp src/core/ControlSchema.cpp src/core/ControlTransactions.cpp
- rg -n 'R\("clip.remove_silence"' src/core/ControlReversibilityTableDevices.cpp

new-p15-4: FIXED
The threshold is now rounded up at src/core/ControlCommandsArrangeTools.cpp:191. The regression uses a 44,101 Hz clip, whose frames-per-tick value is fractional, and gives it a quiet run exactly equal to the truncated one-tick count. That run now returns NotFound and remains uncut; the existing aQuietStretchIsCutOut test covers normal removal and undo.

RED/GREEN commands (run from build/tests; stdout/stderr captured to the named log):
- Build before fix: cmake --build build --target zene ArrangeToolsTest — exit 0; logs/005-build-red-p15.log.
- RED: ./ArrangeToolsTest aQuietStretchShorterThanOneTickIsNotRemoved — exit 1; logs/006-test-red-p15.log. The assertion observed success (errorKind 0) instead of NotFound (1).
- Build after fix: cmake --build build --target zene ArrangeToolsTest — exit 0; logs/007-build-green-p15.log.
- GREEN: ./ArrangeToolsTest aQuietStretchShorterThanOneTickIsNotRemoved — exit 0; logs/008-test-green-p15.log.

Step 4b proof:
- git diff -- src/core/ControlCommandsArrangeTools.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3776.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3776.r1/logs/fix.patch — exit 0; logs/009-reverse-patch-p15.log.
- cmake --build build --target zene ArrangeToolsTest — exit 0; logs/010-build-4b-red-p15.log.
- ./ArrangeToolsTest aQuietStretchShorterThanOneTickIsNotRemoved — exit 1; logs/011-test-4b-red-p15.log.
- git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3776.r1/logs/fix.patch — exit 0; logs/012-restore-patch-p15.log.
- cmake --build build --target zene ArrangeToolsTest — exit 0; logs/013-build-4b-green-p15.log.
- ./ArrangeToolsTest aQuietStretchShorterThanOneTickIsNotRemoved — exit 0; logs/014-test-4b-green-p15.log.

new-p9-32: FIXED
transientsAround now keeps a mapped transient only when clipStart <= mappedTick < clipEnd (src/core/ControlCommandsArrangeTools.cpp:104, 119). The regression places the playhead at tick 170 inside a 200-tick clip, then offsets its audio by 100 ticks so the next source burst maps beyond the visible end. The command now returns NotFound and leaves the playhead at 170. Both next and previous selection use this same filtered list; the existing in-range forward/backward and no-audio tests cover their normal and exhaustion paths.

RED/GREEN commands (run from build/tests; stdout/stderr captured to the named log):
- Build before fix: cmake --build build --target zene ArrangeToolsTest — exit 0; logs/015-build-red-p9.log.
- RED: ./ArrangeToolsTest nextTransientDoesNotLeaveTheVisibleClip — exit 1; logs/016-test-red-p9.log. The command sought successfully (errorKind 0) instead of returning NotFound (1).
- Build after fix: cmake --build build --target zene ArrangeToolsTest — exit 0; logs/017-build-green-p9.log.
- GREEN: ./ArrangeToolsTest nextTransientDoesNotLeaveTheVisibleClip — exit 0; logs/018-test-green-p9.log.

Step 4b proof:
- git diff -- src/core/ControlCommandsArrangeTools.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3776.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3776.r1/logs/fix.patch — exit 0; logs/019-reverse-patch-p9.log.
- cmake --build build --target zene ArrangeToolsTest — exit 0; logs/020-build-4b-red-p9.log.
- ./ArrangeToolsTest nextTransientDoesNotLeaveTheVisibleClip — exit 1; logs/021-test-4b-red-p9.log.
- git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3776.r1/logs/fix.patch — exit 0; logs/022-restore-patch-p9.log.
- cmake --build build --target zene ArrangeToolsTest — exit 0; logs/023-build-4b-green-p9.log.
- ./ArrangeToolsTest nextTransientDoesNotLeaveTheVisibleClip — exit 0; logs/024-test-4b-green-p9.log.

## Verification

The full affected test family passed: ctest -R ArrangeToolsTest --output-on-failure, run from build/tests, exit 0; 1/1 CTest tests passed (logs/033-ctest-final.log).

| Cluster | RED command and exit | GREEN command and exit | Step 4b | Affected-family count |
| --- | --- | --- | --- | --- |
| 3776 | N/A: step 2 showed the finding is false-positive | N/A | N/A | no added test for this finding |
| new-p15-4 | ./ArrangeToolsTest aQuietStretchShorterThanOneTickIsNotRemoved, exit 1 (log 006) | same command, exit 0 (log 008) | reverted test exit 1 (log 011); restored test exit 0 (log 014) | ArrangeToolsTest 1/1 passed (log 033) |
| new-p9-32 | ./ArrangeToolsTest nextTransientDoesNotLeaveTheVisibleClip, exit 1 (log 016) | same command, exit 0 (log 018) | reverted test exit 1 (log 021); restored test exit 0 (log 024) | ArrangeToolsTest 1/1 passed (log 033) |

The pre-report feature gate run passed (logs/032-feature-check-pre-report-fixed.log, exit 0). Its table:

| Gate | Result |
| --- | --- |
| fork-sources | PASS |
| all-sources-reproduce | PASS |
| no-upstream-regression | PASS |
| check-namespace | PASS |
| no-tautology | PASS |
| doc-figures | PASS |
| complexity | PASS, no new item |
| file-length | PASS, no new item |

| Final check | Command | Exit | Log |
| --- | --- | ---: | --- |
| feature gates | python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3776 | 0 | logs/036-feature-check-final.log |
| completion check | python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3776 3776 | 0 | logs/037-submit-check-final.log |

The initial gate run found that tests/all-sources.txt carried 1,148 vendored sfizz entries excluded by its own recipe. The manifest was regenerated verbatim from that recipe; tests/upstream-modifications.txt records the reason. This gate-only inventory repair changes no product source or ratchet baseline.

Build/test output is in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3776.r1/logs. The fff grep utility was not available in the tool catalog or PATH, so logs were searched with rg for error, FAILED, and Assertion.

## Self-check

1. Revert proof: new-p15-4 and new-p9-32 each failed with only their product hunk reversed (exit 1) and passed after restoration (exit 0); the exact reverse/build/test/restore sequences and logs are above. 3776 is a false-positive at step 2, so it has no regression test or RED/GREEN run.
2. Determinism: the frame test uses the fixed 44,101 Hz sample rate and a run exactly equal to the truncated count. The transient test uses fixed burst data, a fixed tick offset, and an explicitly set playhead; neither test uses time, sleeps, or races.
3. Cause and callers: quietRuns has one caller, removeSilence. transientsAround has one caller, nextTransient; next and previous both select from its output, so the interval check covers both directions. The min-frame change is local to the same validated command argument.
4. Collateral and realtime: these control-command paths run on the control thread. No audio-thread code, allocation, lock, wait, I/O, signal, undo mechanism, or saved project format was changed. The frame check only changes runs below the requested duration; the transient check only excludes ticks outside the visible clip. Existing normal, no-match, no-audio, forward/backward, and undo coverage passed with ArrangeToolsTest. The configured Linux/Qt build was verified; no MSVC or macOS build environment was available. The only new API used is standard C++20 std::ceil from <cmath>.
5. False-positive evidence for 3776: ControlSchema::finishResult strips the private handler payload by design, while ControlRegistry records a reversible fallback for a true-inverse command with a journal step and control.transactions exposes that record. The Track checkpoint contract row and the command's addUndoStep are cited above.
6. Siblings: clip.list is read-only and track.folder_move uses separate position and track-checkpoint logic; neither calls the edited helpers. clip.remove_silence is the only quietRuns caller. arrangement.next_transient is the only transientsAround caller, and its next and previous directions both use the filtered list. The listed arrangement.fit_tracks, arrangement.get_state, arrangement.modes, arrangement.modifier_tools, arrangement.navigator, arrangement.nudge, arrangement.ruler_lanes, arrangement.scroll, arrangement.select_clip, arrangement.set_tool, arrangement.zoom, arrangement.zoom_back, arrangement.zoom_forward, and arrangement.zoom_toggle commands live on other command paths and do not map transient ticks or convert a tick duration to sample frames, so they are unaffected.

## Files committed

- src/core/ControlCommandsArrangeTools.cpp — the two fixes, in commits 2588659a0 and 189e0883b.
- tests/src/core/ArrangeToolsTest.cpp — both regressions, in commits 2588659a0 and 189e0883b.
- tests/all-sources.txt — regenerated whole-tree source inventory.
- tests/upstream-modifications.txt — source-inventory repair reason.
- docs/qwen-fixes-2026-10/cluster-3776.md — this evidence report.
