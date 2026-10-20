BLOCKED - finding is REAL (16-bit control count); no product fix committed; regression test cannot run inside test time

## Finding

`BUG 6: LADSPA control counts wrap at 65,536` (cluster new-p15-95).

## Evidence (file:line, current worktree HEAD b61e5f965)

- `plugins/LadspaEffect/LadspaControls.h:76` declares `ch_cnt_t m_controlCount;`
- `include/LmmsTypes.h:44` defines `using ch_cnt_t = std::uint16_t;`, so the member is 16 bits.
- `plugins/LadspaEffect/LadspaControls.cpp:48` assigns `m_controlCount = controls.count();`. `controls` is a `QVector` count, which is wider than 16 bits.
- `plugins/LadspaEffect/LadspaControls.h:50-52`: `controlCount()` returns this member as `int`. `EffectView.cpp:82` reads it with `> 0`, so a wrapped 0 hides the effect view.
- `plugins/LadspaEffect/LadspaControls.cpp:171` and `:178` loop `port < m_controlCount / m_processors` in the global stereo-link path, so a wrapped count skips every processor-0 link.
- `plugins/LadspaEffect/LadspaControlDialog.cpp:82-83` computes the column count from `m_controlCount / m_processors`.
- The manager accepts wider counts: `src/core/LadspaManager.cpp:348-351` returns `uint32_t`, and `tests/src/core/LadspaManagerTest.cpp:118` already checks a 65536-port plugin.

So the defect is real. 65,536 controls wrap to 0, which breaks `controlCount()`, the link loop, and the column count.

## Intended fix (NOT applied)

`LadspaControls.h:76`: `int m_controlCount;`, matching the `int controlCount()` interface and the `int port` loops.
`LadspaControls.cpp:48`: `m_controlCount = static_cast<int>( controls.count() );`.

Nothing was committed. A type change without a passing regression proof would be a guess, so the product files are unchanged.

## Why no RED/GREEN proof exists

I wrote a regression (fixture module plus `LadspaControlCountTest`, wired through `tests/CMakeLists.txt`). It builds, but it cannot finish inside test time:

1. The test needs 2 x 32768 control ports on a mono plugin, so `LadspaEffect` builds 65,536 `LadspaControl` objects.
2. Each control creates several `AutomatableModel`/`JournallingObject` instances. `ProjectJournal::allocID` (`src/core/ProjectJournal.cpp:269-275`) probes linearly from a random start. `fastRand()` returns only 15 bits (`include/lmms_math.h:89-94`), and IDs sit in a 2^23 window (`ProjectJournal.cpp:43`). Sampled stacks show construction inside `LadspaControls::LadspaControls` -> `LadspaControl::LadspaControl` -> `AutomatableModel` -> `JournallingObject::JournallingObject` -> `ProjectJournal::allocID`, so per-object cost rises as the journal fills. I did not measure the scaling curve; the quadratic reading is from the code and samples, not a timing series.
3. QTest's per-function watchdog aborted the case at 300 s (exit 134). A one-hour run was started (`red-long-1.log`) but had not finished when I stopped waiting. Its result is NOT known.

So the RED step (fails without the fix) and the GREEN step were never observed. Reverting the fix to prove RED is not possible in this lane.

## Commands run and exit codes

| # | Command (abridged) | EXIT | Log |
|---|---|---|---|
| 1 | `cmake --build build --target LadspaControlCountTest -j4` | 2 (no rule: build tree not yet regenerated) | build-1.log |
| 2 | `env -u MAKEFLAGS cmake -S . -B build` | 97 (bunny-guard refused: configuring is the parent's job) | configure-1.log |
| 3 | `cmake --build build --target cmake_check_build_system` | 0 (regenerated) | regen-1.log |
| 4 | `cmake --build build --target LadspaControlCountTest -j4` | 2 (ccache could not write under /run/user) | build-3.log |
| 5 | same with `CCACHE_TEMPDIR=/tmp/...` | 2 (compile error: `loadSettings` protected) | build-4.log |
| 6 | same after switching to `restoreState` | 0 (test target built) | build-5.log |
| 7 | `ctest -R '^LadspaControlCountTest$' --output-on-failure` (from build/tests) | 8 (Timeout at 120 s) | test-red-1.log |
| 8 | direct run, one case, `timeout 60` | 124 | direct-1.log |
| 9 | direct run, one case, timed | 134 (QTest 300 s watchdog) ELAPSED=305 s | direct-timed-2.log |
| 10 | direct run, one case, `QTEST_FUNCTION_TIMEOUT=3600000` | not finished when I stopped waiting | red-long-1.log |
| 11 | `python3 feature_check.py <worktree>` | 1 (PermissionError: script writes logs to `<worktree>-feature-check`, outside the write-jail) | gates-1.log |
| 12 | `python3 submit_check.py <worktree> new-p15-95` | 1 (report missing, nothing committed, self-check missing) | submit-1.log |

Both gates were NOT run successfully. Gate 11 is an environment block, not a code result.

## Test design (kept for the next lane)

The unsaved work is in the run folder: `.../new-p15-95.r1/wip/` (`tracked-wip.patch`, `LadspaControlCountTest.cpp`, `LadspaControlCountFixture.cpp`). The worktree was restored to clean. The test design is deterministic (no sleeps or timing assertions, injected LADSPA descriptor, `QTest` assertions), but it needs a faster journal or a seam for the control count to run in time.

## Options for a follow-up lane

- A. Fix `ProjectJournal::allocID` scaling (separate cluster; it affects any project with many journalling objects). Then the 65,536-control test becomes practical.
- B. Add a test seam that lets `LadspaControls` be built from a count instead of real ports. This is a product change and needs owner approval.
- C. Accept a manual repro with a generated plugin, outside ctest.

## Siblings found, NOT changed

- `include/LadspaBase.h:57` `uint16_t port_id;` and `:58` `uint16_t control_id;` are 16-bit. `control_id` takes `m_portControls.count()` (`LadspaEffect.cpp:460`), so it wraps at the 65,537th control. `port_id` wraps at plugin port 65,536, and `saveSettings` builds element names from it (`LadspaControls.cpp:113`), so names could collide. Neither is observable in the current link path, and neither is covered by the 65,536 boundary test. They are outside this finding's explicit fix and need their own decision.

## Self-check

- Test FAILS without the fix and PASSES with it: NOT ACHIEVED. No RED or GREEN run completed (rows 7-10).
- Test is deterministic: the designed test uses no sleeps or timing assertions and an injected descriptor, but it is not runnable in test time (row 9).
- Fix is at the cause and complete: the fix is designed (`int m_controlCount`, all three reads checked above), but it is NOT applied.
- No collateral damage: nothing changed in product code, so none.
- Not ALREADY-FIXED or FALSE-POSITIVE, so no code citation for those verdicts is needed.
- Worktree: clean. Nothing committed except this report.
