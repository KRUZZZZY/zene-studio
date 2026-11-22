Verdict: FALSE-POSITIVE

new-p22-9: the NaN seed the finding describes never reaches `seedOf`. The schema gate refuses it first, so there is no production change. A guard test pins that gate (it passes on the unchanged code; it is not a RED regression test).

## Evidence

**The finding's code is as described** (`src/core/ControlCommandsDeviceParams.cpp:308-318`): `seedOf` checks `number < 0.0 || number > max`, both false for NaN, then casts at line 316. The cast is only reached if a NaN gets this far.

**The schema refuses NaN before the handler runs:**

- `plugin.randomise` declares `seed` as `integerProperty()` (`src/core/ControlCommandsDeviceParams.cpp:417`), which is `{"type":"integer"}` with no bounds (`src/core/ControlVocabulary.cpp:85-88`).
- The integer type check is `value.isDouble() && std::floor(value.toDouble()) == value.toDouble()` (`src/core/ControlSchema.cpp:52`). For NaN, `floor(NaN) == NaN` is false, so the check fails.
- `ControlRegistry::invoke` calls `validateArgs` at `src/core/ControlRegistry.cpp:277`. `validateArgs` is defined at `src/core/ControlSchema.cpp:178` and runs the type check on each present property (`checkProperties`, `ControlSchema.cpp:129-146`). The handler is dispatched later, at `ControlRegistry.cpp:514` (`runHandlerGuarded`), so a refused NaN never reaches `randomise`.
- Infinities pass the integer check (`floor(inf) == inf`), but `seedOf`'s range test refuses them (`ControlCommandsDeviceParams.cpp:311`). That path is covered by the new test.

**Every route in:**

- Control socket: requests are parsed with `QJsonDocument::fromJson` (`src/core/ControlServer.cpp:514`). JSON has no NaN literal, so NaN cannot arrive on the wire.
- Lua scripts: `ScriptCommandBridge.cpp:154-161` (`luaToJson`) turns a Lua float NaN (e.g. `0/0`) into `QJsonValue(double)`. It then goes through `ControlRegistry::instance()->invoke` at `ScriptCommandBridge.cpp:358`, so the same schema gate applies. This is the only realistic route to a NaN, and it is refused with `InvalidArgs`.
- Other callers of the handler: none. `randomise` is in an anonymous namespace and is only reached through `invoke`.

**Precedent:** `src/core/ControlCommandsNoteRandom.cpp:212` adds an explicit `std::isfinite` check for a related case, where the schema type is `number` and NaN does pass. The `seed` schema here is `integer`, so the gate is different.

**Latent gap (not changed):** `seedOf` still relies on the schema for its NaN safety. A future schema change to `number`, or a direct caller, would reach the UB cast. The guard test below catches that: with the gate weakened, the test fails (see the mutation check). I did not add an `isfinite` check in `seedOf`, because the fix could only be shown by a test that fails before and passes after, and no path reaches that line. Adding it is the owner's call.

## Change

- `tests/src/core/DeviceToolsTest.cpp:38` - `#include <limits>`.
- `tests/src/core/DeviceToolsTest.cpp:620-637` - new test `aNonFiniteSeedIsRefusedBeforeItIsRead`. It loads the amplifier onto channel 1, sets Volume to 100, and calls `plugin.randomise` with seed NaN, +inf and -inf. Each call must return `InvalidArgs` and leave Volume at 100. Channel 1's chain is cleared afterwards.
- No production file changed. `DeviceToolsTest.cpp` is already registered in `tests/fork-sources.txt:2403` and `tests/all-sources.txt:2724`, so no registration change was needed.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-9.r1/logs`

| Step | Command (abridged; full command is in the logs' context) | EXIT | Result |
| --- | --- | --- | --- |
| Build, first try | `cmake --build build --target zene DeviceToolsTest -j4 > logs/build-1.log 2>&1` | 2 | ccache could not write its temp file (`Permission denied` under `dsh/home/ccache-tmp-r6`). Environment failure, not code. |
| Build, retry | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p22-9 cmake --build build --target zene DeviceToolsTest -j4 > logs/build-2.log 2>&1` | 0 | zene and DeviceToolsTest built |
| Family run, before amplifier built | `ctest -R DeviceToolsTest` from `build/tests`, `> logs/test-1.log` | 0 | Passed, but the new case SKIPPED: "this build has no amplifier module" (`logs/test-2.log`, `logs/test-3.log`). Not evidence. |
| Build amplifier plugin | `cmake --build build --target amplifier -j4 > logs/build-4.log 2>&1` | 0 | `build/plugins/libamplifier.so` built |
| New case, amplifier present | `./DeviceToolsTest aNonFiniteSeedIsRefusedBeforeItIsRead randomiseKeepsLargeSeeds -v2 > logs/test-4.log` | 0 | new case PASS; `randomiseKeepsLargeSeeds` SKIP when run alone (pre-existing: it needs an effect that an earlier test leaves on channel 1) |
| Full binary, GREEN | `./DeviceToolsTest -v2 > logs/test-8-full-green.log` | 0 | **Totals: 19 passed, 0 failed, 0 skipped** |
| Family ctest, GREEN | `ctest -R DeviceTools --output-on-failure > logs/test-7-green.log` (from `build/tests`) | 0 | 2/2 passed (DeviceToolsTest, DeviceToolsChainTest) |

**Mutation check (proves the test catches a weakened gate; this stands in for step 4b because no product fix exists):**

1. `cp src/core/ControlSchema.cpp logs/ControlSchema.orig.cpp`, then `sed` line 52 to `return value.isDouble();` (accepts NaN; the integer check is gone).
2. `cmake --build build --target DeviceToolsTest -j4 > logs/build-mut.log 2>&1` - EXIT 0.
3. `./DeviceToolsTest aNonFiniteSeedIsRefusedBeforeItIsRead -v2 > logs/test-mut-red.log` - **EXIT 1**. `FAIL! : DeviceToolsTest::aNonFiniteSeedIsRefusedBeforeItIsRead() Compared values are not the same`, at `tests/src/core/DeviceToolsTest.cpp:633` (the `errorKind` comparison; with NaN accepted the roll succeeds).
4. Restored: `cp logs/ControlSchema.orig.cpp src/core/ControlSchema.cpp`; `git diff --quiet -- src/core/ControlSchema.cpp` - EXIT 0 (byte-identical to HEAD).
5. `cmake --build build --target DeviceToolsTest DeviceToolsChainTest -j4 > logs/build-6-restore.log 2>&1` - EXIT 0 (the log lists `ControlSchema`, so it was recompiled).
6. Full binary run again: `test-8-full-green.log`, 19 passed, EXIT 0.

## Gates

`feature_check.py` could not run in this session: it writes its per-gate logs to `<worktree>/../<name>-feature-check`, which is outside the write-jail (`PermissionError` at its `logs.mkdir`, `logs/gate-feature-1.log`, EXIT 1). I ran the same commands it runs, in the worktree, with base `639e1d23e` (`merge-base HEAD 050/m0-train-17`; baseline cache present):

| Gate | Command | EXIT | Row under feature_check's rule |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | PASS |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | PASS |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | PASS |
| check-namespace | `tests/scripted/check-namespace` | 0 | PASS |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | PASS |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | PASS |
| complexity | `bash tests/complexity-gate.sh --check` | 1 | PASS: 7 regression lines, 0 new against the base baseline; none in `DeviceToolsTest` |
| file-length | `bash tests/file-length-gate.sh --check` | 1 | PASS: 22 regression lines, 0 new; `DeviceToolsTest.cpp` (533) is in the base baseline |

Raw exit codes are the table's EXIT column. The logs are `logs/g-*.log`.

## Self-check

- **Regression test RED/GREEN:** there is no product change, so there is no fix to revert. The test passes on the unchanged code, and it is a guard, not a RED regression. The RED-equivalent is the mutation check above: weakening `ControlSchema.cpp:52` makes the test FAIL (EXIT 1, `DeviceToolsTest.cpp:633`), and restoring it makes the test PASS (EXIT 0, 19/19 in the full binary).
- **Deterministic:** no timing, sleeps or races. The inputs are the constants NaN, +inf and -inf. The amplifier is loaded by `plugin.load`, not by a previous test. The assertions are on the returned error kind and the Volume value.
- **Fix at the cause, complete:** no production change. I checked every route into `randomise`: the wire parser (`ControlServer.cpp:514`), the Lua bridge (`ScriptCommandBridge.cpp:154-161, 358`), and the registry (`ControlRegistry.cpp:277` validate, `:514` dispatch). Every route passes the same schema gate. The helper's own UB-prone cast (`ControlCommandsDeviceParams.cpp:316`) is unchanged and noted as a latent gap.
- **No collateral damage:** nothing outside `DeviceToolsTest.cpp` changed. The test clears channel 1's effect chain when it ends, as its neighbour does for channel 3.
- **Not-real verdict:** cited above (`ControlCommandsDeviceParams.cpp:417`, `ControlVocabulary.cpp:85-88`, `ControlSchema.cpp:52, 129-146, 178`, `ControlRegistry.cpp:277, 514`, `ScriptCommandBridge.cpp:154-161, 358`, `ControlServer.cpp:514`).
- **submit_check.py (`logs/submit-check-1.log`, EXIT 1) before the commit:** FAIL "nothing committed since the base" (fixed by the commit); FAIL "working tree clean" (fixed by the commit); PASS report present; PASS tests added; FAIL "gates (feature_check.py)" - the script raised `PermissionError` writing `<worktree>/../new-p22-9-feature-check` (outside the write-jail). The gate's commands were run one by one above, and all eight rows are PASS under feature_check's rule. This row is blocked by the sandbox, not by a gate failure.
