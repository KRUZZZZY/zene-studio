FIXED

# Cluster new-p15-169: VST channel counts are narrowed without validation

## Decision: REAL, fixed

The finding (`plugins/VstBase/RemoteVstPlugin.cpp:286`, now the `inputCount()` / `outputCount()` overrides at
`RemoteVstPlugin.cpp:298-313`) is real on the client side. The base tree (`HEAD` before this commit) cast the plugin's
signed counts straight to `ch_cnt_t`:

- `ch_cnt_t` is `std::uint16_t` (`include/LmmsTypes.h:44`). `numInputs = -1` became `65535`; `numInputs = 65538`
  became `2` - a count the plugin does not agree with, accepted as if it were its own.
- The wrapped value was sent to the host (`setInputOutputCount`, `RemotePluginClient.h:97-103`), and the client
  stored it locally even when the host refused it, then sized its own `m_inputs` / `m_outputs` from it
  (`RemoteVstPlugin.cpp`, `updateInOutCount()`, pre-fix lines 1813-1814).
- The host side already refuses out-of-range counts (`src/core/RemotePlugin.cpp:1137-1160`, `outOfRange()` on
  `0..MaxChannelsPerAudioBuffer`). The unvalidated conversion was on the client, which is the side the finding names.

## The fix

- `plugins/VstBase/VstChannelCount.h` (new): `vst::toChannelCount(int) -> std::optional<ch_cnt_t>` returns `nullopt`
  for anything outside `0..MaxChannelsPerAudioBuffer`, the same range the host enforces.
- `RemoteVstPlugin.cpp:301,311`: the overrides convert through it; an invalid live count falls back to the count the
  host last acknowledged.
- `RemoteVstPlugin.cpp:1788-1793`: `updateInOutCount()` refuses an invalid count before anything is resized (returns
  0, silently - it can run on the processing thread through `audioMasterIOChanged`, so no logging there).
- `RemoteVstPlugin.cpp:751-756`: `init()` now fails the load (`IdVstFailedLoadingPlugin`, with a debug message) when
  the plugin's counts are invalid. `init()` runs on the message thread (`RemoteVstPlugin.cpp:542`, `:629`), not the
  processing thread, so the processing-thread denial cannot trigger it (the processing thread only handles
  `IdStartProcessing`, `IdMidiEvent`, `IdVstSetParameter`, `IdVstSetTempo`, `RemoteVstPlugin.cpp:2345-2348`).

**Behaviour change, limited to malformed plugins:** a plugin that reports a negative or over-128 channel count at load
now fails to load. Before, it loaded with a wrapped count. Valid counts take the same path as before.

## Regression test

`tests/src/core/VstChannelCountTest.cpp` (new), registered in `tests/CMakeLists.txt` and `tests/fork-sources.txt`.
The client is an out-of-process executable with a `main`, so it cannot be linked into a test; the conversion it applies
is pinned on the header it includes, as `VstMidiEventBatchTest` does for `VstMidiEventBatch.h`.

Test matrix:

| Path | Case | Function |
| --- | --- | --- |
| normal | 0, 1, 2 and 128 (the host's inclusive bound) are accepted and unchanged | `acceptsCountsTheHostCanHold` |
| negative | -1 and INT_MIN are refused | `refusesNegativeCounts` |
| above limit | 129 and INT_MAX are refused | `refusesCountsAboveTheBufferLimit` |
| wrap | 65535, 65536 and 65538 are refused (65538 used to become 2) | `refusesCountsThatWrapIntoRange` |
| audio thread | `toChannelCount` is a pure inline function: no allocation, no lock, no log | by reading the header |
| build | `NativeLinuxRemoteVstPlugin64` (which compiles `RemoteVstPlugin.cpp`) builds with the fix | build log below |

Not covered by a test: the `init()` load failure and the `updateInOutCount()` refusal in `RemoteVstPlugin.cpp`. Those
need a real VST plugin binary and the out-of-process client, which this lane does not run. They are verified by
reading and by the compile above, not by a runtime test. Undo: no control command is added, so nothing to undo.

## Evidence

| Item | Command | Log | EXIT |
| --- | --- | --- | --- |
| RED (pre-fix seam, assertions fail) | `ctest -R VstChannelCountTest --output-on-failure` in `build/tests` | `test-red-1.log` | 8 (ctest, failed) |
| RED, direct binary: 3 failed, 3 passed | `./VstChannelCountTest` in `build/tests` | `run-4b-red-1.log` | 3 (QTest: 3 failed functions) |
| GREEN, direct binary: 6 passed | `./VstChannelCountTest` in `build/tests` | `run-green-direct-1.log` | 0 |
| GREEN, ctest: 1/1 passed | `ctest -R VstChannelCountTest --output-on-failure` in `build/tests` | `test-green-1.log` | 0 |
| 4b: fix reversed, test FAILS | `git diff -- plugins/VstBase/VstChannelCount.h plugins/VstBase/RemoteVstPlugin.cpp > fix.patch` / `git apply -R fix.patch` / build / run | `fix.patch`, `build-4b-red-1.log`, `run-4b-red-1.log` | 0 / 0 / 0 / 3 |
| 4b: fix reapplied, test PASSES | `git apply fix.patch` / build / run | `build-4b-green-1.log`, `run-4b-green-1.log` | 0 / 0 / 0 |
| Client compile | `cmake --build build --target NativeLinuxRemoteVstPlugin64 -j2` | `build-client-1.log` | 0 |
| Test target build (RED state) | `CCACHE_DISABLE=1 cmake --build build --target VstChannelCountTest -j2` | `build-red-3.log` | 0 |

The RED assertions (`run`: `test-red-1.log`):
- `refusesNegativeCounts`: `'!toChannelCount(-1)' returned FALSE` (`VstChannelCountTest.cpp:60`)
- `refusesCountsAboveTheBufferLimit`: `'!toChannelCount(static_cast<int>(MaxChannelsPerAudioBuffer) + 1)' returned FALSE` (`VstChannelCountTest.cpp:66`)
- `refusesCountsThatWrapIntoRange`: `'!toChannelCount(65536)' returned FALSE` (`VstChannelCountTest.cpp:74`)

Note on the 4b procedure: the test header was staged in the RED state (`git add`, not a commit), so the fix is the
unstaged diff and `git apply -R` restores exactly the RED seam. The fix patch is `logs/fix.patch`.

## Gates

`feature_check.py` could not run in this lane: it writes its per-gate logs to
`<worktree>/../zene-qwen-new-p15-169-feature-check/`, outside the write-jail (`PermissionError`,
`logs/feature-check-1.log`). I ran the same gate commands it runs, in the worktree, and logged each one:

| Gate | Command | EXIT (final) | Log |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources-3.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-reproduce-3.log` |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-regression-3.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | `gate-check-namespace-3.log` |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology-3.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures-3.log` |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity-3.log` |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | `gate-file-length-3.log` |

- The complexity and file-length gates are red on the train by design. They report 7 and 22 `REGRESSION` lines in
  files this branch does not touch; none names `RemoteVstPlugin.cpp`, `VstChannelCount.h` or the test
  (`grep -c` of those names in both logs: 0). Under `feature_check`'s rule (fail only on items in changed files), both
  ratchets pass for this change. I did not change any gate limit or baseline.
- The `all-sources.txt` manifest needed its two new paths. I regenerated the entry list with the recipe in its header
  (the header comments kept); the diff was exactly the two added lines (`logs/all-sources.diff`).
- The `upstream-modifications.txt` reason for `RemoteVstPlugin.cpp` now also names this change.

## Build notes

- `cmake` configuring is blocked by the lane shim, and a new test target needs the build system regenerated. Running
  `cmake --build build --target zene` regenerated it (the `VstChannelCountTest` target then existed), but that target then
  failed on a ccache temp-file permission error (`build-regen-1.log`). `CCACHE_DISABLE=1` fixed it for every build here.
- `zene` itself was not rebuilt after the fix. No `zene` source includes the changed files (`rg` shows only
  `RemoteVstPlugin.cpp` and the test), so its binary is unaffected.

## Self-check

- **Regression fails with the change reverted and passes with it (exit codes):** yes. Reverted (`git apply -R`):
  3 of 6 functions fail, QTest exit 3 (`run-4b-red-1.log`). Reapplied: 6 of 6 pass, exit 0 (`run-4b-green-1.log`).
  Evidence table above.
- **Deterministic, no wall-clock, sleeps or races:** yes. The test calls the pure `vst::toChannelCount` with fixed
  integers (0, 1, 2, 128, 129, -1, 65535, 65536, 65538, INT_MIN, INT_MAX). It has no timers, threads or I/O. The seam is
  the header itself.
- **Fix at the cause, every caller and sibling checked:** the cause is the unchecked `static_cast` in the two overrides.
  Their callers in `RemoteVstPlugin.cpp` are `updateInOutCount()` (`:1788-1814`, the refusal, the
  `setInputOutputCount`, and the allocation) and the debug print. `init()` (`:751`) now checks the same result. The
  process path reads the cached counts (`RemotePluginClient::inputCount()`, `:1117-1118`), so it is unchanged. The other
  `setInputOutputCount` callers (`plugins/ZynAddSubFx/RemoteZynAddSubFx.cpp:61`, constant `(0, 2)`) are not affected.
  The host side already refuses out-of-range counts (`src/core/RemotePlugin.cpp:1137-1160`), so no host change was
  needed. Carla under `plugins/CarlaBase/` is vendored third-party code and was not touched.
- **No collateral damage:** valid counts take the unchanged path (the test covers 0, 1, 2 and 128, the host's
  inclusive bound, which is also the largest count the host accepts). A plugin reporting a negative or over-128 count now
  fails to load (`IdVstFailedLoadingPlugin`). Before this change it loaded with a wrapped or refused count, and the
  processing path then refused every period (`RemotePluginClient.h:354-363`). This is the one intended behaviour change,
  and it affects only malformed plugins. No saved data format or project field is touched.
- **Verdict:** FIXED, so the ALREADY-FIXED / FALSE-POSITIVE item does not apply.
- **Gates:** `feature_check.py` cannot run in this jail (see Gates). Its six simple gates and both ratchets were run
  individually with the same commands: all six exit 0; both ratchets exit 1 on pre-existing items in files this branch
  does not touch.
