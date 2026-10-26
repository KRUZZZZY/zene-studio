FIXED
new-p23-208: FIXED

Evidence: [PerformView.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-208/src/gui/PerformView.cpp:385) checks `transport.get_state` before mutating the cached transport fields or widgets and stores the returned error for the status line. The regression at [PerformViewTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-208/tests/src/gui/PerformViewTest.cpp:308) verifies the last valid tempo, position and play state remain visible on a command-specific refusal. Its original RED showed actual `0 BPM` versus expected `120 BPM` (`005-red-new-p23-208.log`).

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
| Current RED rebuild | `cmake --build build --target zene PerformViewTest` (`045-current-transport-red-build.log`) | 0 |
| Current command-specific RED | `./PerformViewTest failedTransportStateReadKeepsLastKnownState` from `build/tests`, with the unguarded result restored temporarily (`046-current-transport-red.log`) | 1 |
| Current GREEN rebuild | `cmake --build build --target zene PerformViewTest` (`047-current-transport-green-build.log`) | 0 |
| Current command-specific GREEN | `./PerformViewTest failedTransportStateReadKeepsLastKnownState` from `build/tests`, after restoring the check (`048-current-transport-green.log`) | 0 |

The exact RED/GREEN command logs and the full matrix are stored in the supplied log folder.

new-p17-121: FIXED

Evidence: [PerformView.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-208/src/gui/PerformView.cpp:237) records a failed `session.get_state` read for the status line while leaving the last grid untouched; [PerformView.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-208/src/gui/PerformView.cpp:402) checks `session.setlist` before reading or changing labels and button availability. `m_readError` takes status priority over ordinary notices during a refresh and clears at the next full refresh. Tests at [PerformViewTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-208/tests/src/gui/PerformViewTest.cpp:331) inject a failure into each command independently. RED logs show the missing state error and the setlist changing from `Setlist: 2 song(s), not started` to `No setlist` (`016` and `017`).

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

new-p16-163: FIXED

Evidence: [PerformView.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-208/src/gui/PerformView.cpp:396) casts the tick position to `std::int64_t` before dividing and adding one, then formats the widened value. The regression at [PerformViewTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-208/tests/src/gui/PerformViewTest.cpp:386) seeks to `INT_MAX` and changes only the `session.get_state` response to a one-tick bar. RED displayed `Bar -2147483648 · Beat 1`; GREEN displays `Bar 2147483648 · Beat 1`.

## Regression matrix

| Cluster | Normal path | Failure / exhaustion / reconfiguration | Audio-thread constraints | Supported-platform build requirement |
|---|---|---|---|---|
| new-p23-208 | Existing transport test reads tempo and position after a successful `transport.get_state`. | A command-specific refused result keeps the prior tempo, position and play state; the earlier readiness-refusal RED also reproduced default `0 BPM`. No state-read resource pool is exhausted by this UI-only path. | GUI-thread read and widgets only; no audio-thread work changed. | `zene` and `PerformViewTest` build under configured Linux Qt 6; same targets should build on other supported Qt platforms. No platform-specific types or branches were added. |
| new-p17-121 | Existing scene and setlist tests cover successful session reads. | Each of `session.get_state` and `session.setlist` fails independently while sibling reads succeed; last valid UI stays intact. RAII restores handlers and the setlist after injected failures. | GUI-thread read and widgets only; no audio-thread work changed. | Same `zene` and `PerformViewTest` targets; Qt offscreen test environment. |
| new-p16-163 | Existing transport test covers ordinary bar/beat rendering. | A real seek to `INT_MAX` plus an isolated `session.get_state` response with one tick per bar exercises overflow; the test resets transport to tick zero and restores the original handler. | Widened formatting runs only in GUI transport rendering. | Same `zene` and `PerformViewTest` targets; the boundary uses `std::int64_t` and Qt integer formatting. |

Commands (all output logs share `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-208.r1/logs`):

| Purpose | Exact command | Exit |
|---|---|---:|
| 4b remove fix | `git diff -- src/gui/PerformView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-208.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-208.r1/logs/fix.patch` | 0 |
| RED rebuild without fix | `cmake --build build --target zene PerformViewTest` (`030-red-rebuild-new-p16-163.log`) | 0 |
| RED | `./PerformViewTest displayedBarDoesNotOverflowAtMaxTicks` from `build/tests` (`031-red-proof-new-p16-163.log`) | 1 |
| 4b reapply fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-208.r1/logs/fix.patch` | 0 |
| GREEN rebuild with fix | `cmake --build build --target zene PerformViewTest` (`032-green-rebuild-new-p16-163.log`) | 0 |
| GREEN | `./PerformViewTest displayedBarDoesNotOverflowAtMaxTicks` from `build/tests` (`033-green-proof-new-p16-163.log`) | 0 |

## Final verification

| Check | Evidence | Exit |
|---|---|---:|
| PerformView CTest family | `ctest -R '^PerformViewTest$' --output-on-failure`, 1/1 (`049-final-test-after-redproof.log`) | 0 |
| Qt test cases | `./PerformViewTest -silent`, 14 passed, 0 failed (`037-final-suite-direct.log`) | 0 |
| Feature gates | `feature_check.py`; all eight rows PASS (`050-feature-check-final.log`) | 0 |
| Source inventory recipe | `bash tests/all-sources-reproduce.sh`, `REPRODUCES` (`041-all-sources-reproduce.log`) | 0 |
| Completion check | `submit_check.py`, clean tree and report PASS (`051-submit-check-final.log`) | 0 |

The source inventory gate prerequisite is committed separately: the branch base listed 1,148 `plugins/SfzPlayer/sfizz` vendored paths excluded by the manifest's published recipe. Re-deriving the manifest removed only those recipe-excluded paths; no gate or ratchet changed.

## Commit manifest

| Commit | Files |
|---|---|
| `Fix PerformView transport read failures (new-p23-208)` | `src/gui/PerformView.cpp`, `tests/src/gui/PerformViewTest.cpp`, this report |
| `Report PerformView state read failures (new-p17-121)` | `include/PerformView.h`, `src/gui/PerformView.cpp`, `tests/src/gui/PerformViewTest.cpp`, this report |
| `Repair source scope gate for new-p23-208` | `tests/all-sources.txt`, `tests/upstream-modifications.txt` |
| `Fix PerformView bar overflow (new-p16-163)` | `src/gui/PerformView.cpp`, `tests/src/gui/PerformViewTest.cpp`, this report |
| `Record final completion evidence (new-p23-208)` | this report |

## Self-check

1. **Each regression goes RED without its fix and GREEN with it.** Transport: RED `012-red-proof-new-p23-208.log`, exit 1; GREEN `014-green-proof-new-p23-208.log`, exit 0. After narrowing that test to a command-specific refusal, the current test also fails with an unguarded result (`046-current-transport-red.log`, exit 1) and passes with the guard (`048-current-transport-green.log`, exit 0). Session/setlist: RED `023-red-proof-session-state-new-p17-121.log` and `024-red-proof-setlist-new-p17-121.log`, each exit 1; GREEN `026-green-proof-session-state-new-p17-121.log` and `027-green-proof-setlist-new-p17-121.log`, each exit 0. Bar overflow: RED `031-red-proof-new-p16-163.log`, exit 1; GREEN `033-green-proof-new-p16-163.log`, exit 0. All are in the supplied log folder.
2. **The regressions are deterministic.** `CommandHandlerOverride` fails one command at a time; the overflow case overrides `session.get_state` to return `ticks_per_bar = 1` and injects `transport.seek(INT_MAX)`. RAII restores handlers, setlist and transport. No sleeps, timers or races were added to these regression methods.
3. **All read callers and siblings were checked.** `readTransport()` has one caller, `refresh()` at `PerformView.cpp:244`; `refresh()` checks `session.get_state`; `refreshSetlist()` checks `session.setlist` and is called on show (`:225`), periodic refresh (`:245`) and after a song step (`:517`). Each failed read returns before changing its cached state or widgets. The bar value is widened before division and addition.
4. **No collateral data changes.** These changes affect only PerformView's GUI readout and status line. They add no saved-data fields, schema limits or audio-thread work. Existing successful-read and undo behavior remains covered by the 14-pass PerformView suite (`037-final-suite-direct.log`).
5. **Not-real verdict evidence:** not applicable; all three verdicts are FIXED.

Final gates: `feature_check.py` exit 0 (`050-feature-check-final.log`); `submit_check.py` exit 0 (`051-submit-check-final.log`).
