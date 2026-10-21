FIXED - the LADSPA control total is no longer 16-bit, and a regression now drives the real `LadspaControls` global stereo link over a 65,536-control plugin: it fails on the 16-bit type (RED, EXIT 8) and passes on the wide type (GREEN, EXIT 0).

## Finding

`BUG 6: LADSPA control counts wrap at 65,536` (cluster new-p15-95, base `b61e5f965`).

Luna's round-4 review (REWORK) accepted the production change but rejected the regression: round 2's criterion - a regression that exercises `LadspaControls` (or its global-link path) with 65,536 controls and asserts an observable affected behaviour - was still unmet. The round-4 test only checked `numeric_limits<control_count_t>` and `controlCountOf()`, i.e. the type, not the behaviour.

## Verdict

**REAL, and FIXED.** The defect was real at the base commit and the behaviour-level regression now exists, fails without the fix and passes with it.

## Evidence (file:line, this tree)

- `include/LadspaBase.h:53` `using control_count_t = int;` - the wide control-count type, replacing the 16-bit `ch_cnt_t` (`include/LmmsTypes.h:44`, `std::uint16_t`).
- `include/LadspaBase.h:56-59` `controlCountOf( qsizetype )` - the one place a port count becomes a control count.
- `plugins/LadspaEffect/LadspaControls.h:98` `control_count_t m_controlCount;` (was `ch_cnt_t` at base `b61e5f965:LadspaControls.h:76`).
- `plugins/LadspaEffect/LadspaControls.cpp:57` `m_controlCount = controlCountOf( controls.count() );`
- The global stereo-link path, the behaviour the finding names: `plugins/LadspaEffect/LadspaControls.cpp:211-229` walks `m_controlCount / m_processors` ports and applies the link state through `applyLinkToPort()` (`:235-246`).
- Readers of the count, all on the wide type: `LadspaControls.h:51-54` (`int controlCount()`), `LadspaControlDialog.cpp:80-83` (column count), `src/gui/EffectView.cpp:82` (`controlCount() > 0`).
- The manager already accepts wide counts: `src/core/LadspaManager.cpp:348-351` returns `uint32_t`, and `tests/src/core/LadspaManagerTest.cpp:118` checks a 65536-port plugin. So the host accepted a count the control model could not hold.

## What changed this round (on top of `287d39b2d`)

- **The regression now drives the real class.** `tests/src/plugins/LadspaGlobalLinkTest.cpp` (new) compiles the LADSPA plug-in's own control model into the test target (the SlicerT/Rnnoise plug-in tests' shape, `tests/CMakeLists.txt`) and drives a real `LadspaControls` through the global stereo link with 65,536 controls over two processors.
- **A test seam, so the walk is observable without building 65,536 port controls.** `plugins/LadspaEffect/LadspaControls.h:68-84` declares, under `#ifdef LMMS_TESTING` only: a constructor that builds the controls model of a plugin with a given processor/control total (no LADSPA effect, no port controls), a setter for the global link state, and a `LinkPortVisitor` the link reports every port it walks to. `plugins/LadspaEffect/LadspaControls.cpp:235-246` applies the link state through `applyLinkToPort()`, which the visitor intercepts. The shipping plug-in is built without `LMMS_TESTING` and keeps the plain path (`m_controls[0][port]->setLink(...)`).
- The earlier rounds' fix is unchanged: the count type is `control_count_t` (int), the product diff is `include/LadspaBase.h` only.
- `tests/all-sources.txt` and `tests/fork-sources.txt` register the new test source; `tests/upstream-modifications.txt` records the three product files with the reasons of this round.

## The regression

`tests/src/plugins/LadspaGlobalLinkTest.cpp`, three cases:

1. `theControlTotalOfASixtyFiveThousandControlPluginIsNotTruncated` - a `LadspaControls` with 2 processors and 65,536 controls reports `controlCount() == 65536` (on the 16-bit type it reads back **0**).
2. `globalStereoLinkCoversEveryPortOfASixtyFiveThousandControlPlugin` - flipping the global link state makes the link walk **32,768** ports of the first processor (65,536 / 2), the last one being port 32,767. On the 16-bit type the walk covers **0** ports - "changing the global link state skips its controls", exactly as the finding describes.
3. `globalStereoLinkCoversThePortsOfAPluginBelowTheSixteenBitLimit` - a plugin with 100 controls keeps its count and links 50 ports, so the fix changes nothing below the limit.

No clock, no sleep, no thread, no fixture plugin: the counts are integer literals and the link walk is observed through the seam.

## Regression RED/GREEN

The product change under proof is the count type in `include/LadspaBase.h`. The fix was committed in an earlier round, so the patch that carries it is generated against the current tree (`git diff -- include/LadspaBase.h > logs/fix.patch`, one line: `using control_count_t = ch_cnt_t;` -> `using control_count_t = int;`) and toggled with `git apply -R` / `git apply`, exactly as step 4b prescribes. The test seam is new in this round and is deliberately NOT part of that patch, so the test stays compilable in the reversed state (the RED build below exits 0 - it builds and links).

| Step | Command | EXIT | Log (`qwen/runs/new-p15-95.r5/logs/`) | Result |
|---|---|---|---|---|
| B1 | `env -u MAKEFLAGS cmake --build build --target cmake_check_build_system` | 0 | `build-1-regen.log` | the new test target needs the build tree regenerated |
| B2 | `CCACHE_TEMPDIR=/tmp/ccache-zene cmake --build build --target LadspaGlobalLinkTest -j6` | 0 | `build-2-test.log` | builds and links |
| T1 | `ctest -R '^LadspaGlobalLinkTest$' --output-on-failure` (from `build/tests`) | 8 | `test-1-green.log` | first run: SIGSEGV in `~LadspaControls` - the seam constructor had not given `m_controls` one list per processor; fixed in the product (below) |
| B3 | `cmake --build build --target LadspaGlobalLinkTest -j6` (after the fix) | 0 | `build-3-test.log` | builds |
| T2 | `ctest -R '^LadspaGlobalLinkTest$' --output-on-failure` | 0 | `test-2-green.log` | passes |
| B4 | `cmake --build build --target LadspaGlobalLinkTest -j6` | 0 | `build-5-green.log` | builds (final test file) |
| T3 | `ctest -R '^LadspaGlobalLinkTest$' --output-on-failure` | 0 | `test-4-green.log` | passes |
| **RED** | `git apply -R logs/fix.patch` (the product change reversed: 16-bit count) | 0 | - | `include/LadspaBase.h:53` back to `using control_count_t = ch_cnt_t;` |
| BR | `cmake --build build --target LadspaGlobalLinkTest -j6` | **0** | `build-12-red-final.log` | the test still COMPILES and links with the product change reversed |
| **RED** | `ctest -R '^LadspaGlobalLinkTest$' --output-on-failure` | **8** | `test-11-red-final.log` | `FAIL! theControlTotalOfASixtyFiveThousandControlPluginIsNotTruncated() Actual (controls.controlCount()): 0, Expected (65536): 65536` (`:63`) and `FAIL! globalStereoLinkCoversEveryPortOfASixtyFiveThousandControlPlugin() Actual (walk.unlinkedCalls): 0, Expected (32768): 32768` (`:77`); the sub-limit case PASSES |
| **GREEN** | `git apply logs/fix.patch` (the product change restored) | 0 | - | `include/LadspaBase.h:53` back to `using control_count_t = int;` |
| BG | `cmake --build build --target LadspaGlobalLinkTest -j6` | 0 | `build-13-green-final.log` | builds |
| **GREEN** | `ctest -R '^LadspaGlobalLinkTest$' --output-on-failure` | **0** | `test-12-green-final.log` | `100% tests passed out of 1` |
| BP | `cmake --build build --target ladspaeffect zene -j6` | 0 | `build-11-final.log` | the shipping plug-in and the app rebuild (the plug-in is built WITHOUT `LMMS_TESTING`) |
| BF | `cmake --build build --target LadspaChannelCountsTest LadspaManagerTest LadspaPortDialogTest -j6` | 0 | `build-10-family.log` | the other LADSPA test binaries had never been built in this tree (ctest showed them as `Not Run`) |
| FAM | `ctest -R 'Ladspa' --output-on-failure` (from `build/tests`) | 0 | `test-10-family-final.log` | **4/4 passed**: LadspaChannelCountsTest, LadspaManagerTest, LadspaPortDialogTest, LadspaGlobalLinkTest |

Affected-family count: **4 tests executed, 4 passed** (`test-10-family-final.log`).

The RED run is a behaviour failure, not a build failure: the reversed product still compiles the test (BR exit 0) and the test fails on the two 65,536-control assertions.

## Gates

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exits **0** (`gates-2-feature-check.log`):

```
feature-check zene-qwen-new-p15-95 (base b61e5f965, 11 changed files; logs in ...-feature-check)
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

- The first gate run of this round (`gates-1-feature-check.log`) exited **1** with `check-namespace` red: the script requires every `#if <MACRO>` block of more than 30 lines to close with `#endif // <MACRO>`. Fixed by closing the three new blocks with `#endif // LMMS_TESTING` (`plugins/LadspaEffect/LadspaControls.cpp:41,131,243`, `plugins/LadspaEffect/LadspaControls.h:84`); `tests/scripted/check-namespace` then reports `0 errors`.

## Product change, in full

Only one line of production behaviour is in this round's proof patch (`logs/fix.patch`):

```diff
--- a/include/LadspaBase.h
+++ b/include/LadspaBase.h
-using control_count_t = ch_cnt_t;
+using control_count_t = int;
```

The rest of this round's product diff is the `LMMS_TESTING` seam (invisible in the shipping build) and the extraction of the link application into `applyLinkToPort()`, which the shipping path calls with the same arguments as before.

## Siblings found, NOT changed

- `include/LadspaBase.h:67-68` `uint16_t port_id;` and `uint16_t control_id;` are still 16-bit. `control_id` takes `m_portControls.count()` at `plugins/LadspaEffect/LadspaEffect.cpp:460` and is read only at `LadspaControls.cpp:92` for processor-0 controls (numbered first, so below 65,536 even on a bigger plugin); the global link path uses `m_controlCount`, not `control_id`. `port_id` wraps at plugin port 65,536, which `saveSettings` (`LadspaControls.cpp:156`) uses for element names. These need their own decision and are out of this finding.

## Limits

- The regression drives the control model through its test seam, not through a loaded LADSPA plug-in: the earlier attempts (`new-p15-95.r1/logs/red-long-1.log`, 3600 s watchdog, EXIT 134) showed that building 65,536 real `LadspaControl` objects cannot finish in test time - `ProjectJournal::allocID` (`src/core/ProjectJournal.cpp:269-275`) probes linearly from a 15-bit `fastRand()` start, so each object walks the occupied run. That is a separate cluster (it would change saved IDs) and it is the owner's decision. The seam keeps the *link path* of the shipping class - the constructor's port loop is the only part not exercised.
- The seam lives in the product headers under `#ifdef LMMS_TESTING`, the pattern this tree already uses (`include/EffectControls.h:83`). The shipping plug-in module is compiled without the macro, so the shipped code path is the one from before this round.

## Self-check

Completion check: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p15-95` exits **0** (`submit-1.log`): commits on the branch, working tree clean, this report, tests added or changed, the new test file registered in `tests/CMakeLists.txt` and the gates all PASS. Its five ASK items are answered below.

- **Test FAILS with the product change reverted and PASSES with it:** yes. RED `ctest -R '^LadspaGlobalLinkTest$'` EXIT 8 (`test-11-red-final.log`): the count reads back 0 and the global link covers 0 of 32,768 ports; the RED build itself exits 0, so the test really compiles with the product change reversed. GREEN EXIT 0 (`test-12-green-final.log`).
- **Test is deterministic:** no clock, sleep, thread or race. The injected values are the integer literals 2, 65536 and 100; the seam is the `LMMS_TESTING`-only `LadspaControls::LinkPortVisitor` (`plugins/LadspaEffect/LadspaControls.h:70-83`), which the link reports the ports it walks to. The link state is set explicitly through `setGlobalLinkState()`; the walk is then counted, not timed. The only engine interaction is `Engine::init(true)` (the audio engine the models need), the pattern of the other model tests.
- **Fix is at the cause and complete:** the cause is the 16-bit `m_controlCount`. Every reader was checked: `controlCount()` (`LadspaControls.h:51-54`), the global-link loops (`LadspaControls.cpp:215,222`), `LadspaControlDialog.cpp:80-83`, `EffectView.cpp:82`. The processor count `m_processors` stays `ch_cnt_t` (correct: it counts processors, not controls). The 16-bit `port_id`/`control_id` siblings are reported above, not changed.
- **No collateral damage:** `saveSettings` writes `ports` from `controls.count()`, not from `m_controlCount`, so saved files are unchanged and old projects load as before. Counts below 65,536 are the same values in `int`. The seam is compiled out of the shipping plug-in, so no shipped behaviour changes; the only shipped change is the widened type, which changes nothing below 65,536 (case 3 of the regression covers that). The LADSPA family passes 4/4.
- **Realtime, undo, commands:** the changed code is not on the audio thread; the link walk runs on the GUI thread exactly as before. No allocation, lock or logging was added. No control command was added.
- **ALREADY-FIXED / FALSE-POSITIVE:** not claimed. The verdict is FIXED. The finding was real at base `b61e5f965`: `LadspaControls.h:76` (`ch_cnt_t m_controlCount;`) and `LadspaControls.cpp:48` (`m_controlCount = controls.count();`).
- **Files committed this round:** `plugins/LadspaEffect/LadspaControls.h`, `plugins/LadspaEffect/LadspaControls.cpp`, `tests/src/plugins/LadspaGlobalLinkTest.cpp` (new), `tests/CMakeLists.txt`, `tests/all-sources.txt`, `tests/fork-sources.txt`, `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p15-95.md`.
