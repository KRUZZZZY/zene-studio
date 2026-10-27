FIXED

# cluster new-p22-8: a NaN `amount` gets through `std::clamp` into randomise and blend

**Decision: REAL.** A non-finite number reaches the handler and is written. Reachability is narrower than the finding says: the control socket cannot carry NaN (JSON has no NaN literal, and the wire parser is `QJsonDocument::fromJson`, `src/core/ControlServer.cpp:514`), so the path is in-process (`ControlRegistry::invoke`, as used by scripts and other commands). I did not exercise the socket with `1e400` (which Qt may parse to infinity); that is unverified.

## Evidence (final tree, `bugs/qwen-new-p22-8`)

- **Schema lets NaN through.** `numberProperty()` has no bounds (`src/core/ControlVocabulary.cpp:80-83`); the number check is `value.isDouble()` (`src/core/ControlSchema.cpp:49`). A NaN `QJsonValue` is a double, so it passes and the handler runs. `ControlRegistry.cpp:277` validates before the handler.
- **Defect at the cause.** Before the fix `amountOf` returned `std::clamp(NaN, 0, 1)`, which is NaN, and randomise/blend computed `now + NaN * (...)`. The fix is `amountOf` at `src/core/ControlCommandsDeviceParams.cpp:305-317`: a non-finite amount is refused with `InvalidArgs` before the clamp. Finite amounts clamp exactly as before.
- **Callers.** `amountOf` has two callers, `randomise` (`:343`) and `blend` (`:377`); both now return the refusal. Both helpers sit in the file's anonymous namespace (`:74-408`), so there are no callers outside this file.
- **Sibling with the same defect, fixed too.** `readScale` (`src/core/ControlCommandsDeviceParams.cpp:164-169`) checked `scale < Min || scale > Max`, so a NaN `editor_scale` passed and was stored. Its schema is also `numberProperty()` (`:447`). It now tests `std::isfinite` first (`:169`). It is reached only through `deviceOptions`, the `plugin.options` handler (`:182`, registered at `:452`).
- **Sibling checked, safe.** `seed` is `integerProperty()` (`:430`), and the integer check `std::floor(v) == v` rejects NaN (`ControlSchema.cpp:52`) before `seedOf` runs. Infinity is refused by `seedOf`'s range test.
- **Regression test** `DeviceToolsChainTest::aNonFiniteNumberIsRefusedNotWritten` (`tests/src/core/DeviceToolsChainTest.cpp`, from line 361). It covers NaN and ±inf for randomise (refused, Volume unchanged at 100), NaN for blend (refused, Volume unchanged at 50), NaN `editor_scale` (refused, stored zoom unchanged), and amount 2.0 (clamped, lands on B = 50).

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-8.r1/logs` (abbreviated below as LOG).

| # | Command (cwd: worktree unless noted) | EXIT | Log | Note |
|---|---|---|---|---|
| 1 | `cmake --build build --target DeviceToolsTest -j4` | 2 | LOG/build-1-red.log | ccache temp dir not writable (environment) |
| 2 | `CCACHE_DISABLE=1 cmake --build build --target DeviceToolsTest -j4` | 0 | LOG/build-2-red.log | |
| 3 | `cd build/tests && ctest -R DeviceToolsTest -V` | 0 | LOG/test-1-red.log | **Not a RED.** The new case was SKIPped: "this build has no amplifier module" |
| 4 | `CCACHE_DISABLE=1 cmake --build build --target amplifier -j4` | 0 | LOG/build-3-amplifier.log | builds `build/plugins/libamplifier.so` |
| 5 | `cd build/tests && ctest -R DeviceToolsTest -V` (test in DeviceToolsTest.cpp, superseded) | 8 | LOG/test-3-red-verbose.log | FAIL `aNonFiniteAmountIsRefusedNotWritten`: randomise NaN returned errorKind 0, expected 3 (InvalidArgs) at DeviceToolsTest.cpp:652 |
| 6 | fix applied, same build/test (superseded placement) | 0 | LOG/test-4-green-verbose.log | 19/19 |
| 7 | `git diff -- src/core/ControlCommandsDeviceParams.cpp > LOG/fix.patch`, then `git apply -R LOG/fix.patch`, build, run | 0, 0, 8 | LOG/test-5-revert-red.log | RED on the superseded placement |
| 8 | `git apply LOG/fix.patch`, build, run | 0, 0, 0 | LOG/test-6-reapply-green.log | GREEN, 19/19 |
| 9 | `ctest -N -R "DeviceTool\|Randomi\|Blend\|ControlCommandsDevice"` | 0 | LOG/ctest-list.log | family = 2 tests |
| 10 | `cmake -S . -B build` | 97 | LOG/cmake-reconfigure.log | **Blocked:** lane shim `bunny-guard` refuses configuring ("configuring is the parent's job") |
| 11 | `cmake --build build --target DeviceToolsAmountTest -j4` | 2 | LOG/build-8-amount-target.log | No target: a new test file needs the regeneration blocked in row 10 |
| 12 | Moved the test into `DeviceToolsChainTest.cpp` (no CMake change). Revert: `git apply -R LOG/fix.patch` (v1, amount-only); build `zene DeviceToolsChainTest`; `ctest -R ^DeviceToolsChainTest$ -V` | 0, 0, **8** | LOG/build-9-red-chain.log, LOG/test-9-red-chain.log | **RED:** FAIL at DeviceToolsChainTest.cpp:375, randomise NaN errorKind 0, expected 3 |
| 13 | `git apply LOG/fix.patch`; build `zene DeviceToolsTest DeviceToolsChainTest`; `ctest -R DeviceTool -V` | 0, 0, 0 | LOG/build-10-green-chain.log, LOG/test-10-green-family.log | **GREEN:** DeviceToolsTest 18/18, DeviceToolsChainTest 10/10 |
| 14 | Final fix diff saved to `LOG/fix.patch` (adds `readScale`). `git apply -R LOG/fix.patch`; build; `ctest -R ^DeviceToolsChainTest$ -V` | 0, 0, **8** | LOG/build-11-red-final.log, LOG/test-11-red-final.log | **RED (final):** FAIL at DeviceToolsChainTest.cpp:373, `plugin.options` NaN `editor_scale` errorKind 0, expected 3 |
| 15 | `git apply LOG/fix.patch`; build `zene DeviceToolsTest DeviceToolsChainTest`; `ctest -R DeviceTool -V` | 0, 0, **0** | LOG/build-12-green-final.log, LOG/test-12-green-final.log | **GREEN (final):** `DeviceToolsChainTest` 10/10 with `aNonFiniteNumberIsRefusedNotWritten` PASS; `DeviceToolsTest` 18/18. Family: 2/2 suites passed |
| 16 | `bash tests/fork-sources-gate.sh`; `bash tests/all-sources-reproduce.sh`; `bash tests/no-upstream-regression-gate.sh`; `tests/scripted/check-namespace`; `bash tests/no-tautology-gate.sh`; `python3 tests/doc-figures-gate.py` | 0, 0, 0, 0, 0, 0 | LOG/gate3-*.log | |
| 17 | `bash tests/complexity-gate.sh --check`; `bash tests/file-length-gate.sh --check` (raw) | 1, 1 | LOG/gate3-complexity.log, LOG/gate3-file-length.log | Train-wide items (7 complexity and 22 file-length REGRESSION lines). None names `ControlCommandsDeviceParams.cpp` or `DeviceToolsChainTest.cpp` |
| 18 | `python3 .../qwen/feature_check.py <worktree>` as shipped | 1 | (terminal) | `PermissionError` creating `<worktree>-feature-check`, which is outside the write-jail. Nothing ran |
| 19 | Same script, copied to LOG/feature_check_logdir_copy.py with only `ROOT` (shared `gate-baselines`) and the log dir changed, run on the worktree | **0** | LOG/feature-check-table-final.log | **Table: all 8 rows PASS, RESULT: PASS** |
| 20 | `python3 .../qwen/submit_check.py <worktree> new-p22-8` before commit | 1 | LOG/submit-check-precommit.log | Expected pre-commit failures (no commit, no report, no self-check). Rerun after commit, see below |

Gate note: row 19 is `feature_check.py` with two path constants changed, nothing else. It reads the same cached base listing for `639e1d23e`.

Files committed: `src/core/ControlCommandsDeviceParams.cpp`, `tests/src/core/DeviceToolsChainTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p22-8.md`. Nothing is pushed.

## Self-check

1. **RED and GREEN.** Rows 12-15 (final placement): RED exit 8 on the reverted fix (line 373, and line 375 in the amount-only version), GREEN exit 0 on the fix, family 2/2. Rows 7-8 are the same check on the superseded placement. The amount assertions were red on the reverted code at line 375 (row 12). The final-placement RED fails first at the `editor_scale` assertion, which runs before the amount loop.
2. **Deterministic.** No clock, sleep, thread, or race. The inputs are fixed doubles (NaN, +inf, -inf, 2.0), a fixed seed (7), and values set by `plugin.param_set` / `plugin.ab`. The test calls `ControlRegistry::invoke` directly. Its only ordering assumption, that the amplifier is on channel 1, is established in `initTestCase` by `loadByName`, the same way the sibling cases do it.
3. **Fix complete.** Callers of `amountOf` (randomise, blend), the sibling `readScale` (editor_scale, same defect, fixed in the same commit), and the sibling `seedOf` (already protected by `integerProperty` in the schema, `ControlSchema.cpp:52`) were checked. Both helpers are in an anonymous namespace, so no caller outside the file exists.
4. **No collateral damage.** Finite `amount` values, including the default 0.5, 0 and values above 1, take the same clamped path (row 15 asserts amount 2.0 clamps to 1). Finite `editor_scale` values go through the unchanged range test. Project load does not reach `readScale` or `amountOf`; they run only in the `plugin.randomise`, `plugin.blend` and `plugin.options` commands. Behaviour change: an infinite `amount` used to clamp to 1 and is now refused with `InvalidArgs`. The finding asks for exactly that. No new control command was added and the description text is unchanged, so `ControlCommandsSnapshot` does not move.
5. **Verdict.** FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so the "code or contract" item is not applicable.

## Notes and deviations

- **The new test is not in its own file.** I first wrote it in a new `DeviceToolsAmountTest.cpp`. Configuring it needs a CMake regeneration, which the lane guard blocks (rows 10-11), so I moved it into `DeviceToolsChainTest.cpp`, which is already built and already loads the amplifier. The topic is a little off: the randomise/blend tests live in the chain binary. `DeviceToolsTest.cpp` is at 514 code lines on the train already and cannot take the test. A follow-up from the parent can move it into its own file.
- **Test-fixture trap (found and fixed in this lane).** `DeviceToolsTest` SKIPs its randomise/blend cases on a build without `libamplifier.so`. The first RED run therefore passed for the wrong reason. I built the `amplifier` target (row 4) and re-ran, which gave a real FAIL. Any future run of these binaries needs that module built.
- **ccache.** A plain build fails (row 1) because ccache cannot write its temp dir here. All later builds used `CCACHE_DISABLE=1`.
- **Gate wrapper.** `feature_check.py` cannot create its log dir outside the write-jail (row 18). Row 19 is the same logic with only the paths changed.
- **Not checked.** The socket path with `1e400`; an undo assertion after a refused call (the refusal happens before `writeValues` and `writeModelsRecorded`, so no write or undo step is made, but I did not add an undo check); Windows and macOS builds (not run here).
