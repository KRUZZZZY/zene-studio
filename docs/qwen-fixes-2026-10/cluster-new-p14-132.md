# Cluster new-p14-132: invalid MIDI channels become wildcard bindings

**Verdict: FIXED**

A hand-edited launcher binding whose `channel` is not `-1` and not a MIDI channel (0..15) was read as an any-channel binding. `bindingFor` read the channel with `QJsonValue::toInt(-1)`, which returns `-1` for any non-number (a word, `null`) and truncates fractions, and then treated every negative value as "any channel". So key 60 with channel `-2` or `"bad"` launched on every channel.

## Evidence

Original code (before this commit), `src/core/LaunchBindings.cpp`: the channel read at `:55` (`toInt(-1)`) and the wildcard test at `:57` (`if (channel < 0) { any = binding; }`), in `bindingFor`.

Fix, `src/core/LaunchBindings.cpp`:

- `:48-57` `storedChannel` accepts only an explicit `-1` or a whole number 0..15. A missing channel is invalid; `session.bind` still stores an explicit `-1` when its optional channel argument is omitted. Any other value (a fraction, a word, `-2`, `null`) returns false.
- `:68-71` `bindingFor` skips an entry whose channel fails `storedChannel`, so it is never a wildcard. Its exact-channel and any-channel matches are unchanged.
- `:175` is the only caller (`applyNote`), on the UI thread (the header calls `applyNote` "the UI thread's half").

Regression, `tests/src/core/RecordingPassTest.cpp:346-397` (`aBindingWithAnInvalidChannelIsSkipped`). It binds six keys in a 1×6 grid, all on track 0 where a launch stops the cell it replaces, and checks each note right after it lands:

- key 60, channel `-2` (`:375`, assertion `:377`): expects `Idle`;
- key 61, channel `"bad"` (`:378`, assertion `:380`): expects `Idle`;
- key 62, channel `3` (`:381`, assertion `:383`): expects `Playing` (an exact match still works);
- key 63, channel `-1.5` (`:384`, assertion `:386`): expects `Idle`, and cell 2 keeps playing;
- key 64, channel `-1` on note channel 5 (`:388`, assertion `:390`): expects `Playing` (any channel still works);
- key 65 with no channel field (`:391`, assertion `:393`): expects `Idle` (a missing channel is malformed, not a wildcard).

No clock, sleep or race. The test uses `setBindings`, fixed `LaunchNote` values, and `processAudio` with a fixed `SessionClockContext` (4 frames per tick, 16 frames per period).

## Evidence table

ccache note: the first build (`build-1.log`, EXIT=2) failed on ccache's temp file under `/run/user/1000`, which this sandbox cannot write. It is not a compile error. Every later build ran with `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14`. All ctest runs were from `build/tests`. Logs are in the run folder's `logs/`.

| Check | Exact command | EXIT | Result |
| --- | --- | --- | --- |
| Build before fix (ccache retry) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14 cmake --build build --target zene RecordingPassTest -j4 > build-2.log 2>&1` | 0 | built |
| RED before fix | `ctest -R '^RecordingPassTest$' --output-on-failure > test-red-2.log 2>&1` | **8** | `aBindingWithAnInvalidChannelIsSkipped` fails: `RecordingPassTest.cpp:375`, actual `2` (`Playing`), expected `0` (`Idle`). Key 60 with channel `-2` launched on channel 3. |
| Build with fix | `CCACHE_TEMPDIR=… cmake --build build --target zene RecordingPassTest PerformViewTest -j4 > build-5-green.log 2>&1` | 0 | built |
| GREEN family | `ctest -R '^(RecordingPassTest\|PerformViewTest)$' --output-on-failure > test-green-1.log 2>&1` | **0** | 2/2 CTest targets passed (`RecordingPassTest` 1 target, `PerformViewTest` 1 target) |
| Direct run, all cases | `./RecordingPassTest > direct-green-1.log 2>&1` (from `build/tests`) | **0** | `Totals: 13 passed, 0 failed`; `PASS: aBindingWithAnInvalidChannelIsSkipped()` |
| Proof: revert fix | `git diff -- src/core/LaunchBindings.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 | fix removed (the product diff is only that file) |
| Proof: rebuild reverted | `CCACHE_TEMPDIR=… cmake --build build --target RecordingPassTest -j4 > build-6-proof-revert.log 2>&1` | 0 | built |
| Proof: test on reverted | `ctest -R '^RecordingPassTest$' --output-on-failure > test-proof-revert-1.log 2>&1` | **8** | fails at `RecordingPassTest.cpp:375`, actual `2`, expected `0`: the same assertion as the first RED run |
| Proof: re-apply fix | `git apply logs/fix.patch` | 0 | fix restored |
| Proof: rebuild restored | `CCACHE_TEMPDIR=… cmake --build build --target RecordingPassTest -j4 > build-7-proof-reapply.log 2>&1` | 0 | built |
| Proof: test restored | `ctest -R '^RecordingPassTest$' --output-on-failure > test-proof-reapply-1.log 2>&1` | **0** | 1/1 passed |
| Affected family | `RecordingPassTest` (13 cases in the binary) and `PerformViewTest` (`ctest -R`) | — | 2 CTest targets run, 2 passed, 0 failed |
| Feature gate table (direct) | `python3 …/feature_check.py <worktree> > gate-feature-check-1.log 2>&1` | **1** | Not a gate result: it failed at `logs.mkdir` for `<worktree>-feature-check`, a folder outside the write-jail (`PermissionError`). |
| Feature gate table (same logic, logs in run folder) | `python3 -I /tmp/p14-gate/run_gate_p14.py <worktree> <logs>/gate-check-1 > gate-feature-check-2.log 2>&1` | **0** | `RESULT: PASS`: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures (all exit 0); complexity and file-length: no new item in the changed files |

The wrapper in `/tmp/p14-gate/` imports `feature_check.py` and repeats its `main()` with the log folder moved. It changes no gate, limit or baseline; it reuses the cached base listing for `2da2ed356`, which already existed.

## What I did not separately prove

The test checks its cases in order and stops at the first failure. The RED and reverted runs stop at key 60 (`:375`), so the later malformed cases are not individually reached. In the old code, a string or missing value returns the `toInt(-1)` default, while a negative fraction converts to a negative integer or the same negative default; all of those pass the old `channel < 0` wildcard check. The regression assertions cover the cases separately after the first one.

## Sibling reads left unchanged (deliberately)

- `src/core/LaunchBindings.cpp:98` (`learnNote`) and `src/core/ControlCommandsLaunchBindings.cpp:84` (`without`, used by `session.bind`/`session.unbind`) also read `channel` with `toInt(-1)`. They compare against a concrete channel 0..15 and only decide which stored entry is replaced or removed. They cannot produce a wildcard match, so the reported symptom is gone without touching them.
- `src/core/LaunchBindings.cpp:68` reads `key` with `toInt(-1)`, so a fractional key such as `60.5` still matches key 60. The finding does not mention keys and no legitimate writer produces one, so I left it for a reviewer to decide.

## Self-check

1. **Regression fails without the fix and passes with it (exit codes).** Yes. RED on the unfixed code: `test-red-2.log`, **EXIT=8**, `RecordingPassTest.cpp:375`, actual `2`, expected `0`. GREEN with the fix: `test-green-1.log` **EXIT=0** and `direct-green-1.log` **0 failed**. Reverting and restoring the product diff gave the same result (`test-proof-revert-1.log` **EXIT=8**, `test-proof-reapply-1.log` **EXIT=0**). See the caveat above about the later cases.
2. **Deterministic.** Yes. No wall clock, sleep or race. Bindings come from `setBindings`, notes from fixed `LaunchNote` values, and time from a fixed `SessionClockContext` passed to `processAudio`. It uses the same `stopProcessing()`/`clearProject()` set-up as the neighbouring learn-and-release test.
3. **Cause and siblings.** The cause is the channel conversion in `bindingFor` (`toInt(-1)` plus the `< 0` wildcard). `storedChannel` is its only new use. Its only caller is `applyNote` (`:175`). The remaining sibling reads are listed above with the reason they are unchanged.
4. **No collateral damage.** Valid stored entries behave as before: channel `0..15` matches its channel and `-1` matches any channel. `session.bind` stores the explicit `-1` default when its optional `channel` argument is omitted. The command schema already limits channel to `-1..15` (`ControlCommandsLaunchBindings.cpp:157-158`), so no legitimate writer produces anything else. No limit or check is added to a shared path. The saved-data format is unchanged. No new work on the audio thread: `applyNote` runs on the UI thread, and the helper adds no allocation beyond the existing `QStringLiteral` and `QJsonObject` use in the same function.
5. **Verdict is FIXED,** not ALREADY-FIXED or FALSE-POSITIVE. The before-code is at `LaunchBindings.cpp:55-57` (the historical line 55 was the channel read) and the fix is at `:48-72`.

Files committed: `src/core/LaunchBindings.cpp`, `tests/src/core/RecordingPassTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p14-132.md`. No registration is needed: `src/core/LaunchBindings.cpp` is already in `tests/fork-sources.txt:1701`, and no file is new. The `submit_check.py` gate row fails in this sandbox for the same write-jail reason as the direct gate run; the same gate logic passes, as shown above.

## Independent QA review (2026-10-20)

The reviewer additionally rebuilt `zene`, `RecordingPassTest`, and `PerformViewTest` with `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14 cmake --build build --target zene RecordingPassTest PerformViewTest -j4` (EXIT=0; log `qa-review-build-2.log`), ran `ctest -R '^(RecordingPassTest|PerformViewTest)$' --output-on-failure` from `build/tests` (EXIT=0; 2/2 passed; log `qa-review-ctest-2.log`), and ran the required direct feature check (EXIT=0, RESULT: PASS; log `qa-review-feature-check-2.log`). The existing loop negative-control evidence is recorded above.
