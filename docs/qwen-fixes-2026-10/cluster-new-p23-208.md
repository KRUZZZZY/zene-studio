FIXED
new-p23-208: FIXED

Evidence: [PerformView.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-208/src/gui/PerformView.cpp:382) checks `transport.get_state` before mutating the cached transport fields or widgets and stores the returned error for the status line. The regression at [PerformViewTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-208/tests/src/gui/PerformViewTest.cpp:286) verifies the last valid tempo, position and play state remain visible on a registry refusal. Its original RED showed actual `0 BPM` versus expected `120 BPM` (`005-red-new-p23-208.log`).

Commands (all outputs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-208.r1/logs`):

| Purpose | Exact command | Exit |
|---|---|---:|
| Baseline build | `cmake --build build --target zene PerformViewTest` (`001-baseline-build.log`) | 0 |
| Baseline family | `ctest -R '^PerformViewTest$' --output-on-failure` from `build/tests`, 1/1 (`002-baseline-test.log`) | 0 |
| RED | `./PerformViewTest failedTransportStateReadKeepsLastKnownState` from `build/tests` (`005-red-new-p23-208.log`) | 1 |
| GREEN build | `cmake --build build --target zene PerformViewTest` (`009-green-build-new-p23-208.log`) | 0 |
| GREEN | `./PerformViewTest failedTransportStateReadKeepsLastKnownState` from `build/tests` (`010-green-new-p23-208.log`) | 0 |
| 4b remove fix | `git diff -- src/gui/PerformView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-208.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-208.r1/logs/fix.patch` | 0 |
| 4b rebuild without fix | `cmake --build build --target zene PerformViewTest` (`011-red-rebuild-new-p23-208.log`) | 0 |
| 4b RED | `./PerformViewTest failedTransportStateReadKeepsLastKnownState` from `build/tests` (`012-red-proof-new-p23-208.log`) | 1 |
| 4b reapply fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-208.r1/logs/fix.patch` | 0 |
| 4b rebuild with fix | `cmake --build build --target zene PerformViewTest` (`013-green-rebuild-new-p23-208.log`) | 0 |
| 4b GREEN | `./PerformViewTest failedTransportStateReadKeepsLastKnownState` from `build/tests` (`014-green-proof-new-p23-208.log`) | 0 |

The exact RED/GREEN command logs and the full matrix are stored in the supplied log folder.

new-p17-121: FIXED

Evidence: [PerformView.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-208/src/gui/PerformView.cpp:237) records a failed `session.get_state` read for the status line while leaving the last grid untouched; [PerformView.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-208/src/gui/PerformView.cpp:400) checks `session.setlist` before reading or changing labels and button availability. `m_readError` takes status priority over ordinary notices during a refresh and clears at the next full refresh. Tests at [PerformViewTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-208/tests/src/gui/PerformViewTest.cpp:331) inject a failure into each command independently. RED logs show the missing state error and the setlist changing from `Setlist: 2 song(s), not started` to `No setlist` (`016` and `017`).

Commands (all output logs share `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-208.r1/logs`):

| Purpose | Exact command | Exit |
|---|---|---:|
| RED build | `cmake --build build --target zene PerformViewTest` (`015-red-build-new-p17-121.log`) | 0 |
| RED state read | `./PerformViewTest failedSessionStateReadKeepsGridAndShowsError` from `build/tests` (`016-red-session-state-new-p17-121.log`) | 1 |
| RED setlist read | `./PerformViewTest failedSetlistReadKeepsLastKnownSetlist` from `build/tests` (`017-red-setlist-new-p17-121.log`) | 1 |
| GREEN build | `cmake --build build --target zene PerformViewTest` (`018-green-build-new-p17-121.log`) | 0 |
| GREEN state read | `./PerformViewTest failedSessionStateReadKeepsGridAndShowsError` from `build/tests` (`019-green-session-state-new-p17-121.log`) | 0 |
| GREEN setlist read | `./PerformViewTest failedSetlistReadKeepsLastKnownSetlist` from `build/tests` (`020-green-setlist-new-p17-121.log`) | 0 |
| 4b remove fix | `git diff -- include/PerformView.h src/gui/PerformView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-208.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-208.r1/logs/fix.patch` | 0 |
| 4b rebuild without fix | `cmake --build build --target zene PerformViewTest` (`022-red-rebuild-new-p17-121.log`) | 0 |
| 4b RED state read | `./PerformViewTest failedSessionStateReadKeepsGridAndShowsError` from `build/tests` (`023-red-proof-session-state-new-p17-121.log`) | 1 |
| 4b RED setlist read | `./PerformViewTest failedSetlistReadKeepsLastKnownSetlist` from `build/tests` (`024-red-proof-setlist-new-p17-121.log`) | 1 |
| 4b reapply fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-208.r1/logs/fix.patch` | 0 |
| 4b rebuild with fix | `cmake --build build --target zene PerformViewTest` (`025-green-rebuild-new-p17-121.log`) | 0 |
| 4b GREEN state read | `./PerformViewTest failedSessionStateReadKeepsGridAndShowsError` from `build/tests` (`026-green-proof-session-state-new-p17-121.log`) | 0 |
| 4b GREEN setlist read | `./PerformViewTest failedSetlistReadKeepsLastKnownSetlist` from `build/tests` (`027-green-proof-setlist-new-p17-121.log`) | 0 |
