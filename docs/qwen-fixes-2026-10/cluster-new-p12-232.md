# cluster-new-p12-232

Verdict: **FIXED**

Bug (Luna patrol BUG 17): `src/core/ScriptCommandBridge.cpp:278` — a `zene.command(id, args)`
call whose second argument was **not** a Lua table was silently coerced to `{}` instead of being
rejected, so the registry validated and ran the command against defaults the caller never passed.

## Evidence (file:line)

- Bug site: `src/core/ScriptCommandBridge.cpp:278` (old), in `luaCommand`:
  `QJsonObject args = lua_istable(L, 2) ? luaToJson(L, 2, 0).toObject() : QJsonObject();`
  Any non-table, non-nil arg (string, number, bool, …) fell into the `: QJsonObject()` branch and
  became an empty args object. The registry's own schema validation (`ControlRegistry.cpp:251`)
  therefore saw `{}`, not the caller's value.
- Fix: added a guard immediately above that line. `luaCommand` now returns the standard typed
  failure `(nil, message, "invalid_args")` when arg #2 is present, is not `nil`, and is not a table.
  A missing arg and an explicit `nil` arg both still mean "no arguments" (unchanged).
  The kind string `invalid_args` is the wire name for `ControlErrorKind::InvalidArgs`
  (`include/ControlRegistry.h:52-67`, closed set "not_found | requires | invalid_args | busy |
  refused | irreversible").
- Regression test: `tests/src/core/ScriptCommandBridgeTest.cpp:126`
  `aNonTableArgumentIsRejectedTyped()` — calls `zene.command('track.list', 'oops')` and
  `zene.command('track.list', 42)` and expects `nil invalid_args` for both, and
  `zene.command('track.list', nil)` to still succeed.

## RED (before fix, on unpatched tree)

- Build: `cmake --build build --target zene ScriptCommandBridgeTest`
  → logs/build-1.log, EXIT=0
- `ctest -R '^ScriptCommandBridgeTest$' --output-on-failure`
  → logs/test-4.log (unpatched tree), EXIT=8
  Failure: `FAIL! ScriptCommandBridgeTest::aNonTableArgumentIsRejectedTyped()` —
  "Compared lists have different sizes" (scalar arg ran the command instead of erroring).
  Test case totals: 8 passed, 1 failed.

## GREEN (after fix)

- Build: `cmake --build build --target zene ScriptCommandBridgeTest ScriptDawEffectsTest`
  → logs/build-5.log, EXIT=0
- `ctest -R '^ScriptCommandBridgeTest$' --output-on-failure`
  → logs/test-6.log, EXIT=0 (1/1 test passed; 9 test functions in the target all PASS)

## Test catches the bug (reverse-apply proof)

- `git diff -- src/core/ScriptCommandBridge.cpp > logs/fix.patch && git apply -R logs/fix.patch`
  rebuild → `ctest -R '^ScriptCommandBridgeTest$'` → logs/test-4.log, **EXIT=8** (fails)
- `git apply logs/fix.patch`, rebuild → `ctest -R '^ScriptCommandBridgeTest$'`
  → logs/test-6.log, **EXIT=0** (passes)

The regression FAILS on the unpatched code and PASSES with the fix.

## Sibling tests in the family

- `ctest -R '^ScriptDawEffectsTest$'` → logs/test-7.log, EXIT=0 (passes with fix).
- `ctest -R '^ControlAutomationScriptTest$'` → logs/test-5.log, EXIT=8 (4 failures).
  These are **pre-existing and unrelated**: they fail with `'!trackId.isEmpty()'` /
  "no track with parameters in the fresh song" because this headless host cannot create an
  instrument track. Confirmed identical on the unpatched tree (logs/test-3.log baseline run).

## Gates

- `bash tests/complexity-gate.sh --check` → EXIT=1 (logs/gate-complexity.log)
- `bash tests/file-length-gate.sh --check` → EXIT=1 (logs/gate-filelength.log)

Both FAIL, but **pre-existing**: re-running both on the reverse-applied (unpatched) tree
produces byte-identical REGRESSION lists (logs/gate-*-baseline.log; diff of
`REGRESSION|FAIL` lines is empty). No gate line names `ScriptCommandBridge*` or `luaCommand`.
My change adds one `if` to `luaCommand` (CCN ≈ 5, well under 10) and 15 lines to a 183-line
test; neither file is a gate subject. Gate limits/baselines were not touched.

## Files committed

- src/core/ScriptCommandBridge.cpp (fix)
- tests/src/core/ScriptCommandBridgeTest.cpp (regression)
- docs/qwen-fixes-2026-10/cluster-new-p12-232.md (this report)

Both source files were already registered fork sources in `tests/fork-sources.txt`; no new
files, so `tests/upstream-modifications.txt` is unchanged.
