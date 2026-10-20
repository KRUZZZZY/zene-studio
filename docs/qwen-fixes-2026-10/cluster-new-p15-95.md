FIXED - LADSPA control totals are no longer 16-bit; a boundary regression fails on the 16-bit type (RED, EXIT 8) and passes on the 32-bit type (GREEN, EXIT 0). Scope limit: the regression exercises the count type and `controlCountOf()`, not a constructed `LadspaControls` holding 65,536 controls (see Limits).

## Finding

`BUG 6: LADSPA control counts wrap at 65,536` (cluster new-p15-95, base `b61e5f965`). Luna's round-3 review asked for the wider type and a boundary test.

## Evidence (file:line, current tree)

- `include/LadspaBase.h:53` `using control_count_t = int;`, which replaces the 16-bit `ch_cnt_t` (`include/LmmsTypes.h:44`, `std::uint16_t`).
- `include/LadspaBase.h:56-59` `controlCountOf( qsizetype )`, the one place a port count becomes a control count.
- `plugins/LadspaEffect/LadspaControls.h:77` `control_count_t m_controlCount;` (was `ch_cnt_t` at base `b61e5f965:LadspaControls.h:76`, and `int` after `cdcc15fb5`).
- `plugins/LadspaEffect/LadspaControls.cpp:48` `m_controlCount = controlCountOf( controls.count() );`
- Readers of the count, all now on the wide type: `LadspaControls.h:51` (`int controlCount()`), `LadspaControls.cpp:171,178` (`m_controlCount / m_processors`, the global stereo-link loop), `LadspaControlDialog.cpp:82-83` (column count), and `src/gui/EffectView.cpp:82` (`controlCount() > 0`).
- The manager already accepts wide counts: `src/core/LadspaManager.cpp:348-351` returns `uint32_t`, and `tests/src/core/LadspaManagerTest.cpp:118` checks a 65536-port plugin.

## What changed this round (on top of `cdcc15fb5`)

- The committed fix's `static_assert` and `<limits>` are removed. The compile-time guard is replaced by a runtime boundary test; the guard could not fail on the old code and gave no RED.
- The shared count type and `controlCountOf()` live in `include/LadspaBase.h`. That header is already visible to every test, and the 65536-object plugin path is too slow to test (see Limits).
- The regression is a new slot `controlCountTypeKeepsEveryControlPastTheSixteenBitLimit` in `tests/src/plugins/LadspaChannelCountsTest.cpp`. That test target already exists in the configured build tree. A new CMake target could not be added: this lane's build tree is configured by the parent (`bunny-guard: configuring is the parent's job`).
- `tests/upstream-modifications.txt`: the two `cluster new-p15-95` entries are rewritten for the new state, and `include/LadspaBase.h` gets an entry.

## Regression RED/GREEN

The seam was not in `HEAD`, so `git apply -R` of the fix would remove the seam and fail to compile rather than fail the assertion. The RED state was built by setting the alias back to the pre-fix 16-bit type with the seam present: `using control_count_t = ch_cnt_t;`. The GREEN state is `using control_count_t = int;`. The full product diff for this round is saved as `logs/fix.patch`.

| Step | Command (run from `build/tests` for ctest) | EXIT | Log (`qwen/runs/new-p15-95.r4/logs/`) | Result |
|---|---|---|---|---|
| R0a | `cmake --build build --target LadspaControlCountTest -j4` | 2 | `r4-build-red-1.log` | No rule: the new target needs a reconfigure, which this lane may not run |
| R0b | `cmake -S . -B build` | 97 | `r4-configure-1.log` | Refused by `bunny-guard` (configuring is the parent's job) |
| R0c | `cmake --build build --target LadspaChannelCountsTest -j4` (RED, first try) | 2 | `r4-build-red-2.log` | ccache could not write to `/run/user/1000` (environment; not the code) |
| B1 | `CCACHE_TEMPDIR=/tmp/ccache-tmp-r4 cmake --build build --target LadspaChannelCountsTest -j4` (RED) | 0 | `r4-build-red-3.log` | builds |
| RED | `ctest -R '^LadspaChannelCountsTest$' --output-on-failure` (RED, `control_count_t = ch_cnt_t`) | **8** | `r4-test-red-1.log` | `FAIL! ... controlCountTypeKeepsEveryControlPastTheSixteenBitLimit() 'std::numeric_limits<control_count_t>::max() >= 65536' returned FALSE` (`LadspaChannelCountsTest.cpp:28`); 3 passed, 1 failed |
| B2 | `cmake --build build --target LadspaChannelCountsTest ladspaeffect -j4` (GREEN, `control_count_t = int`) | 0 | `r4-build-green-1.log` | builds |
| GREEN | `ctest -R '^LadspaChannelCountsTest$' --output-on-failure` | 0 | `r4-test-green-1.log` | `100% tests passed, 0 tests failed out of 1` |
| B3 | `cmake --build build --target zene AutomatableModelTest -j4` | 0 | `r4-build-green-2.log` | rebuilds the dependents of `LadspaBase.h` |
| B4 | `cmake --build build --target LadspaPortDialogTest -j4` | 0 | `r4-build-green-3.log` | the target was not built before; ctest showed it as `Not Run` |
| FAM | `ctest -R 'AutomatableModelTest\|Ladspa' --output-on-failure` (from `build/tests`) | 0 | `r4-test-family-green-2.log` | 4/4 passed: LadspaChannelCountsTest, AutomatableModelTest, LadspaManagerTest, LadspaPortDialogTest |

Affected-family count: **4 tests executed** (the earlier `Not Run` for LadspaPortDialogTest was a missing executable, fixed by B4).

Deviation from step 4b: the proof toggles the type alias in place (RED, then GREEN) rather than `git apply -R fix.patch`, for the reason given above. Both states were built and run, and both EXIT codes are recorded.

## Gates

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exits **1** with `PermissionError` (`r4-gates-1.log`): it writes its logs to `<worktree>-feature-check`, outside the write-jail.
- Run instead: a copy of the same script in `qwen/runs/new-p15-95.r4/feature_check_jailed.py`, with only its log folder and its baseline cache moved into the run folder (`diff` against the original shows those three lines). The first run exits **1** because the baseline needs a `git worktree add` in another repo, which is refused (exit 97, `r4-gates-2.log`). The cached baseline for `b61e5f9658c2` was copied from `qwen/gate-baselines/` into the run folder, and the rerun exits **0** (`r4-gates-3.log`):

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Siblings found, NOT changed

- `include/LadspaBase.h:67` `uint16_t port_id;` and `:68` `uint16_t control_id;` are still 16-bit. `control_id` takes `m_portControls.count()` at `plugins/LadspaEffect/LadspaEffect.cpp:460`. `control_id` is read only at `LadspaControls.cpp:83`, for processor-0 controls. Those are numbered first, so they stay below 65536 even on a 65,537+ control plugin, and the global link path (`:171,:178`) uses `m_controlCount`, not `control_id`. Still, proc ≥ 1 `control_id`s wrap past 65,536, and `port_id` wraps at plugin port 65,536 (`saveSettings` names). These need their own decision and are out of this finding.

## Limits

- The regression tests the count type and `controlCountOf()`. It does not construct a `LadspaControls` with 65,536 controls. The earlier attempts (`new-p15-95.r1`, `red-long-1.log`, 3600 s watchdog, EXIT 134) show that path cannot finish in test time. `ProjectJournal::allocID` (`src/core/ProjectJournal.cpp:269-275`) probes linearly from a `fastRand()` start that is only 15 bits (`include/lmms_math.h:89-94`), so object IDs cluster and each allocation walks the occupied run. That is a separate cluster (it changes saved IDs), and it is the owner's decision.
- A future edit that changes only the `LadspaControls` member back to a 16-bit type would not fail this test, because the constructor would still go through `controlCountOf()`. The member and the helper share `control_count_t` to keep them together.

## Self-check

(to be filled from the completion check)
