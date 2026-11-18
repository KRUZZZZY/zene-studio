FIXED

new-p18-54: FIXED
Evidence: `src/gui/clips/MidiClipView.cpp:431-450` copies each source lane's visible window, rebases it into the merged clip's timeline, and merges it with any existing lane. `setControllerPoints()` collapses duplicate ticks in stable order, so the later clip in the input vector wins. The regression at `tests/src/gui/MidiClipStepMenuTest.cpp:136-195` covers CC, pitch bend, channel pressure, program change, a `-24` source offset, and the duplicate CC tick at 96 (value 40 from the later clip).

RED/GREEN proof:

- Reverse patch: `git diff -- src/gui/clips/MidiClipView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-54.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-54.r1/logs/fix.patch; status=$?; echo EXIT=$status` — EXIT=0.
- RED build: `cmake --build build --target zene MidiClipStepMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-54.r1/logs/023-red-merge-final-build.log 2>&1; status=$?; echo EXIT=$status` — EXIT=0.
- RED test, from `build/tests`: `ctest -R '^MidiClipStepMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-54.r1/logs/024-red-merge-final-test.log 2>&1; status=$?; echo EXIT=$status` — EXIT=8; `mergingClipsKeepsAndRebasesControllerLanes` fails because the CC lane is absent.
- Restore patch: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-54.r1/logs/fix.patch; status=$?; echo EXIT=$status` — EXIT=0.
- GREEN build: `cmake --build build --target zene MidiClipStepMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-54.r1/logs/025-green-merge-final-build.log 2>&1; status=$?; echo EXIT=$status` — EXIT=0.
- GREEN test, from `build/tests`: `ctest -R '^MidiClipStepMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-54.r1/logs/026-green-merge-final-test.log 2>&1; status=$?; echo EXIT=$status` — EXIT=0.

new-p15-116: FALSE-POSITIVE
Evidence: `src/gui/clips/MidiClipView.cpp:1086-1087` calls `remove()` and then restores journalling through `m_clip`. `src/gui/clips/ClipView.cpp:272-298` removes the clip from its track and schedules model deletion with `m_clip->deleteLater()` at line 294; `src/core/Track.cpp:1038-1052` only erases the clip pointer from the track vector. `arrangement::rippleAfterDelete()` at `src/core/ArrangementEdits.cpp:337-341` shifts following clips synchronously and does not run an event loop. The clip therefore remains alive until the current call stack returns and Qt processes deferred deletion. The `m_clip` dereference at line 1087 is valid under this removal contract, so there is no production change or regression test for this finding.

Code inspection command: `rg -n 'deleteLater|void ClipView::remove|void Track::removeClip|restoreJournallingState' src/gui/clips/ClipView.cpp src/core/Track.cpp src/gui/clips/MidiClipView.cpp` — EXIT=0.

new-p20-70: FIXED
Evidence: `src/gui/clips/MidiClipView.cpp:671-675` returns zero steps when integer button width is zero; `paintEvent()` uses that count at lines 767-785. Thus a clip with more steps than drawable pixels skips button drawing and `noteAtStep()` calls, while normal-width buttons retain their full loop. The deterministic test at `tests/src/gui/MidiClipStepMenuTest.cpp:124-134` checks 16 steps at widths 64 and 8, 64 steps at widths 32 and 128, 5,000,000 steps at width 1000, and width 0.

RED/GREEN proof:

- Reverse patch: `git diff -- src/gui/clips/MidiClipView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-54.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-54.r1/logs/fix.patch; status=$?; echo EXIT=$status` — EXIT=0.
- RED build: `cmake --build build --target zene MidiClipStepMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-54.r1/logs/034-red-paint-final-build.log 2>&1; status=$?; echo EXIT=$status` — EXIT=0.
- RED test, from `build/tests`: `ctest -R '^MidiClipStepMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-54.r1/logs/035-red-paint-final-test.log 2>&1; status=$?; echo EXIT=$status` — EXIT=8; `beatPaintingSkipsStepsWithoutAWholePixel` observes 16 instead of 0 for 16 steps in 8 pixels.
- Restore patch: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-54.r1/logs/fix.patch; status=$?; echo EXIT=$status` — EXIT=0.
- GREEN build: `cmake --build build --target zene MidiClipStepMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-54.r1/logs/036-green-paint-final-build.log 2>&1; status=$?; echo EXIT=$status` — EXIT=0.
- GREEN test, from `build/tests`: `ctest -R '^MidiClipStepMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-54.r1/logs/037-green-paint-final-test.log 2>&1; status=$?; echo EXIT=$status` — EXIT=0.

## Test matrix

- Merge normal path: four lane kinds are preserved; source points are cropped and rebased through the nonzero clip offset; a same-tick overlap uses the later input clip's value. The existing `clip.join` sibling also combines lanes point-by-point (`src/core/ControlCommandsClipArrange.cpp:278-293`); this UI merge follows that duplicate-tick policy. Other `mergeClips()` calls are limited to the context-menu action in `MidiClipView.cpp:280-285`.
- Beat paint normal path: button loops remain for steps with nonzero pixel width. Exhaustion/narrow-width path: five million steps at 1000 pixels and counts greater than the drawable width return zero. Reconfiguration path: the pure helper is checked at narrow and wide widths, with no timer, sleep, or race.
- Split lifetime: the finding is false-positive under `deleteLater()`; no test that fails on the original code can honestly be supplied.
- Audio-thread constraints: merge and repaint run on the GUI thread. No audio-thread function changed; no audio-thread allocation, lock, wait, I/O, logging, or signal was added. The merge continues to use the existing track journal checkpoint, so the edit remains one undo step; painting is read-only.
- Platform/build requirements: rebuilt `zene`, `MidiClipLinkedEditTest`, and `MidiClipStepMenuTest` with the configured C++20/Qt build. The new geometry helper uses standard integer arithmetic; no platform-specific or new Qt API was introduced. Windows and macOS were not separately built in this worktree.

## Final checks

- Final build: `cmake --build build --target zene MidiClipLinkedEditTest MidiClipStepMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-54.r1/logs/038-final-build.log 2>&1; status=$?; echo EXIT=$status` — EXIT=0.
- Affected family: from `build/tests`, `ctest -R '^(MidiClipLinkedEditTest|MidiClipStepMenuTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-54.r1/logs/039-final-gui-family.log 2>&1; status=$?; echo EXIT=$status` — EXIT=0, 2/2 CTest targets passed.
- Feature gate: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-54 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-54.r1/logs/041-feature-check-final.log 2>&1; status=$?; echo EXIT=$status` — EXIT=0.
- Completion gate: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-54 new-p18-54 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-54.r1/logs/042-submit-check-post-commit.log 2>&1; status=$?; echo EXIT=$status` — EXIT=0.
- Explicit commit files: `include/MidiClipView.h`, `src/gui/clips/MidiClipView.cpp`, `tests/src/gui/MidiClipStepMenuTest.cpp`, `tests/all-sources.txt`, `tests/upstream-modifications.txt`, and this report.

## Self-check

1. RED/GREEN: merge regression `MidiClipStepMenuTest::mergingClipsKeepsAndRebasesControllerLanes` exited 8 with the merge patch reversed and 0 restored (logs 024/026). Paint regression `MidiClipStepMenuTest::beatPaintingSkipsStepsWithoutAWholePixel` exited 8 with the paint cap reversed and 0 restored (logs 035/037). The split finding is a false-positive and has no applicable failing regression; its deferred-deletion contract is cited above.
2. Determinism: both tests use literal lane points, clip offsets, step counts, and widths. The paint regression calls the same pure helper used by `paintEvent()`; it has no wall-clock assertion, wait, sleep, or race.
3. Cause and siblings: UI merge is invoked by the context menu and now handles every controller kind through `controllerSegment()` and `setControllerPoints()`. The existing `clip.join` helper merges same-lane points with stable duplicate-tick resolution. The other `noteAtStep()` calls in `MidiClipView.cpp` serve individual mouse/menu events, not a full-clip paint loop.
4. Collateral and undo: no load/save format, stored step count, supported MIDI data, or control command changed. Paint skips only images whose integer width is zero. Merge remains within the existing single track-journal transaction. The split code remains unchanged because `deleteLater()` defers model destruction.
5. False-positive evidence: `ClipView::remove()` uses `m_clip->deleteLater()` (line 294), `Track::removeClip()` only erases the vector entry (lines 1038-1052), and the synchronous split restores journalling at `MidiClipView.cpp:1087` before deferred deletion is processed.
