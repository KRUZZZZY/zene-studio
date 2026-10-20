NEEDS-CONTEXT - finding is REAL; fix applied with a compile-time guard; no runtime regression test can finish in test time, so RED/GREEN proof and the test change need an owner decision

## Finding

`BUG 6: LADSPA control counts wrap at 65,536` (cluster new-p15-95).

## Evidence (file:line, base b61e5f965)

- `plugins/LadspaEffect/LadspaControls.h:76` (base) declared `ch_cnt_t m_controlCount;`
- `include/LmmsTypes.h:44` defines `using ch_cnt_t = std::uint16_t;`, so the member was 16 bits.
- `plugins/LadspaEffect/LadspaControls.cpp:48` (base) assigned `m_controlCount = controls.count();`. `controls` is a `QVector`, so its count is wider than 16 bits.
- `plugins/LadspaEffect/LadspaControls.h:50-52`: `controlCount()` returns the member as `int`. `src/gui/EffectView.cpp:82` reads it with `> 0`, so a wrapped 0 hides the effect view.
- `plugins/LadspaEffect/LadspaControls.cpp:171` and `:178` loop `port < m_controlCount / m_processors` in the global stereo-link path, so a wrapped count skips every processor-0 link.
- `plugins/LadspaEffect/LadspaControlDialog.cpp:82-83` computes the column count from `m_controlCount / m_processors`.
- The manager accepts wider counts: `src/core/LadspaManager.cpp:348-351` returns `uint32_t`, and `tests/src/core/LadspaManagerTest.cpp:118` already checks a 65536-port plugin.

The defect is real: 65,536 controls wrap to 0.

## Fix applied (this round)

- `plugins/LadspaEffect/LadspaControls.h:76`: `int m_controlCount;`, matching the `int controlCount()` interface and the `int port` loops.
- `plugins/LadspaEffect/LadspaControls.cpp:52-54`: a `static_assert` that `decltype(m_controlCount)` can hold 65536, then `m_controlCount = static_cast<int>( controls.count() );`. `#include <limits>` added.
- `tests/upstream-modifications.txt`: both inherited product files registered with reasons.

Sibling check: `controlCount()` (`LadspaControls.h:50-52`, `int`), the loops at `LadspaControls.cpp:171,178` (`int port`), and `LadspaControlDialog.cpp:82-83` (`int / ch_cnt_t`) all stay consistent with an `int` member. `processorCount` stays `ch_cnt_t`, which is correct: it is a processor count, not a control count.

## Verification

| # | Command (abridged) | EXIT | Log (`new-p15-95.r2/logs/`) |
|---|---|---|---|
| G1 | `cmake --build build --target ladspaeffect -j4` with the fix | 0 | build-green-1.log |
| R1 | `git diff -- <2 product files> > fix.patch`, `git apply -R fix.patch`, rebuild `ladspaeffect` | 0 (reverted tree still compiles) | build-red-1.log |
| G2 | `git apply fix.patch`, rebuild `ladspaeffect` | 0 | build-green-2.log |
| 11 | `python3 feature_check.py <worktree>` | 1 (PermissionError: writes `<worktree>-feature-check`, outside the write-jail) | gates-2.log |
| 12 | `python3 submit_check.py <worktree> new-p15-95` | 1 (3 FAILs: uncommitted fix, no test change, gates) | submit-2.log |

**R1 is not a RED proof.** The static guard is part of the fix, so reverting the fix also removes the guard, and the old code compiles. A compile-time check cannot fail on the old code unless it lives outside the fix. So the RED step is not provable this way.

Earlier runtime attempts (from the previous round, logs in `new-p15-95.r1/logs/`): `test-red-1.log` (ctest timeout, EXIT 8), `direct-timed-2.log` (QTest 300 s watchdog, EXIT 134), `red-long-1.log` (3600 s watchdog, EXIT 134, no assertion reached). The construction of 65,536 `LadspaControl` objects is too slow in this tree.

## Why the test change is not landed

I wrote a runtime regression (fixture module plus `LadspaControlCountTest`). It builds but cannot finish in test time, so I did not commit it: a ctest entry that times out would break the suite. The work is saved outside the repo at `.../new-p15-95.r1/wip/` (`tracked-wip.patch`, `LadspaControlCountTest.cpp`, `LadspaControlCountFixture.cpp`). The "tests added or changed" item therefore remains FAIL.

Cause (from the code and sampled stacks, not a timing series): each control creates several `AutomatableModel`/`JournallingObject` instances. `ProjectJournal::allocID` (`src/core/ProjectJournal.cpp:269-275`) probes linearly from a random start. `fastRand()` returns 15 bits (`include/lmms_math.h:89-94`), and IDs sit in a 2^23 window (`ProjectJournal.cpp:43`).

## Options (owner decision needed)

- A. Fix `ProjectJournal::allocID` scaling (separate cluster). This changes saved journal IDs, so it needs review. Afterwards the 65,536-control test becomes practical.
- B. Add a test seam so `LadspaControls` can be built from a count instead of real ports. This is a product change and needs approval.
- C. Accept the compile-time guard plus a manual repro with a generated plugin, outside ctest.

## Siblings found, NOT changed

- `include/LadspaBase.h:57` `uint16_t port_id;` and `:58` `uint16_t control_id;` are 16-bit. `control_id` takes `m_portControls.count()` (`LadspaEffect.cpp:460`), so it wraps at the 65,537th control. `port_id` wraps at plugin port 65,536, and `saveSettings` builds element names from it (`LadspaControls.cpp:113`), so names could collide. Neither is observable in the current link path, and neither is covered by the 65,536 boundary. They need their own decision.

## Self-check

- **Test FAILS with the fix reverted and PASSES with it:** NOT ACHIEVED. The only compile-time proof (R1/G2) is not a RED test. Runtime RED/GREEN never completed (rows in the earlier log list).
- **Test is deterministic:** the designed runtime test uses no sleeps or timing assertions and an injected LADSPA descriptor, but it cannot run in test time. The committed guard is compile-time only and deterministic.
- **Fix is at the cause and complete:** the type change and the three readers are consistent (see Sibling check). The cause is the 16-bit member; `control_id`/`port_id` are reported above, not changed.
- **No collateral damage:** the change widens storage and changes no behaviour for counts below 65,536. `controlCount()` already returned `int`. The saved `ports` attribute is written from the un-narrowed `controls.count()` (`LadspaControls.cpp` `saveSettings`), not from `m_controlCount`, so it is unchanged and old projects load as before. The `static_assert` only requires the member to hold 65,536, so it cannot reject a legitimate project.
- **ALREADY-FIXED / FALSE-POSITIVE:** not claimed.
- **Gates:** `feature_check.py` could not run from this lane (write-jail). The harness check's gate PASS from the previous round predates this fix; this round's gate result is NOT known. Row 11.
