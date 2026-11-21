# Cluster new-p12-243: embedded NUL bytes truncate command text

**Verdict: FIXED**

A script that passed a name with an embedded NUL byte, such as `channel:setName("A\0B")`, was accepted and renamed to `"A"`. `setCommandText` copied the UTF-8 bytes with `strncpy`, which stops at the NUL. The apply side reads the payload back with `QString::fromUtf8(command.text)`, which also stops at the NUL. The bytes after the NUL were lost without any refusal. The fix refuses such a name before it reaches the payload, the same way an overlong name is refused.

## Evidence

- `src/core/ScriptDawEdit.cpp:65-74`: `setCommandText` now returns `false` when `utf8.contains('\0')` (`:70`), before the `strncpy` at `:71`. `QString::toUtf8` keeps embedded NULs, so the check sees them.
- Lua reaches this path unchanged: `include/ScriptLuaQtTypes.h:53-68` builds the `QString` with `QString::fromUtf8(text, length)`, which keeps the NUL. The reported input therefore reaches `setCommandText`.
- Callers, all covered by the one change: `src/core/ScriptDawEdit.cpp:361-374` (`LuaMixerChannel::setName`, via `namedDawCommand`), `:380-395` (`LuaEffectChain::loadEffect` device reference, via `namedDawCommand` at `:389`), `src/core/ScriptBindings.cpp:208` (track and instrument-track rename, via `enqueueTrackRename`) and `:755` (`LuaPatternClip::setName`). The command bridge (`src/core/ScriptCommandBridge.cpp:284`) never writes `command.text`.
- Refusal wording updated so a NUL name is not reported as "exceeds the 255-byte limit": `ScriptDawEdit.cpp:96-98` (comment), `:369`, `:392`; `ScriptBindings.cpp:208`, `:755`. Each still contains "255-byte", so the existing overlong-name tests keep matching.
- Contract documented in `include/ScriptEngine.h:120-133` (comment only; no declaration changed).
- Precedent for refusing NUL in a script-supplied name: `src/core/ScriptImport.cpp:287`.
- Regression tests in `tests/src/core/ScriptDawBindingTest.cpp`:
  - `:254-260` `aNameWithAnEmbeddedNulIsRefusedBeforeItReachesThePayload`: calls `setCommandText` on `"A\0B"` directly and expects `false` with an empty payload.
  - `:263-287` `anEmbeddedNulInChannelNameIsRejectedWithoutChangingTheEngine`: the Lua path `c:setName(string.char(65, 0, 66))`. It expects the channel name unchanged and exactly one refusal log containing "NUL".

No timing, sleeps or races are involved; both tests are deterministic.

## Regression proof (RED / GREEN / mutation)

RED, before the fix (the test already in place, product code unchanged):

- Build: `cmake --build build --target ScriptDawBindingTest -j4 > .../build-2-red.log 2>&1; echo EXIT=$?` → **EXIT=0**.
  - The first attempt (`build-1-red.log`) failed with **EXIT=2** because ccache could not write to `/run/user/1000/ccache-tmp`. It was rerun with `env CCACHE_TEMPDIR=/tmp/zene-ccache-tmp`. No code was involved.
- Test: `ctest -R ScriptDawBindingTest --output-on-failure > .../test-1-red.log 2>&1; echo EXIT=$?` from `build/tests` → **EXIT=8**.
  - `FAIL!` for `aNameWithAnEmbeddedNulIsRefusedBeforeItReachesThePayload()`: `'!setCommandText(command, withNul)' returned FALSE` (`ScriptDawBindingTest.cpp:258`).
  - `FAIL!` for `anEmbeddedNulInChannelNameIsRejectedWithoutChangingTheEngine()`: `Actual (channel->m_name): "A"`, `Expected (originalName): "Channel 3"` (`:281`). This is the reported bug.
  - Totals: 11 passed, 2 failed, 1 skipped. The skip, `aScriptsLoadedEffectIsUndoneOnTheChannelItNames`, is pre-existing ("this build has no amplifier module").

GREEN, with the fix:

- Build: `env CCACHE_TEMPDIR=/tmp/zene-ccache-tmp cmake --build build --target zene ScriptDawBindingTest -j4 > .../build-3-green.log 2>&1; echo EXIT=$?` → **EXIT=0**.
- Test: `ctest -R ScriptDawBindingTest --output-on-failure > .../test-2-green.log 2>&1; echo EXIT=$?` → **EXIT=0** (1/1 CTest target passed).
- Verbose: `ctest -R ScriptDawBindingTest -V > .../test-3-green-verbose.log 2>&1; echo EXIT=$?` → **EXIT=0**. Totals: 13 passed, 0 failed, 1 skipped (the pre-existing skip). Both new tests `PASS`. The refusal log reads `renaming this channel refused: name exceeds the 255-byte command payload limit or contains a NUL byte`.

Mutation proof (step 4b):

- Saved the product diff: `git diff -- src/core/ScriptDawEdit.cpp src/core/ScriptBindings.cpp include/ScriptEngine.h > .../logs/fix.patch; echo EXIT=$?` → **EXIT=0**.
- Reverted: `git apply -R .../logs/fix.patch; echo EXIT=$?` → **EXIT=0**. Rebuild (`build-4-reverted.log`) → **EXIT=0**.
- Test with the fix reverted (`test-4-reverted.log`) → **EXIT=8**. Both new tests fail: `returned FALSE` at `:258`, and `Actual "A"` at `:281`. Totals 11 passed, 2 failed, 1 skipped.
- Re-applied: `git apply .../logs/fix.patch; echo EXIT=$?` → **EXIT=0**. Rebuild (`build-5-reapplied.log`) → **EXIT=0**.
- Test with the fix restored (`test-5-reapplied.log`) → **EXIT=0**, Totals 13 passed, 0 failed.

## Affected family (Script* tests that include `ScriptEngine.h`)

`ScriptEngine.h` changed, so every test target that includes it was rebuilt: `ControlAutomationScriptTest ScriptBindingsTest ScriptBudgetActionTest ScriptCommandBridgeTest ScriptDawBindingTest ScriptDawEffectsTest ScriptDawRemovalUndoTest ScriptEngineTest ScriptImportTest ScriptMemoryBudgetTest ScriptStabilisationTest`, plus `zene`. Rebuild with the fix: `build-13-fixed.log` → **EXIT=0**. Test run `test-14-fixed-family.log` → **EXIT=8**: 11 CTest targets run; 7 passed; 4 failed.

The same four fail on the unmodified code, so they are pre-existing and environmental. Baseline run (fix reverted, `build-12-baseline.log` **EXIT=0**, `test-13-baseline-family.log` **EXIT=8**) fails the identical set of four plus `ScriptDawBindingTest`, which contains only my two new tests (`test-13-baseline-family.log`: two `FAIL!` lines for `aNameWithAnEmbeddedNul…`). Failing sets compared with `diff`: the only difference is `ScriptDawBindingTest`, which passes with the fix.

- `ControlAutomationScriptTest`: "no track with parameters in the fresh song" (`ControlAutomationScriptTest.cpp:253, 327, 387, 427`).
- `ScriptDawRemovalUndoTest`: "the LADSPA host plugin 'ladspaeffect' is not in this build" (`ScriptDawRemovalUndoTest.cpp:132` and others).
- `ScriptEngineTest::testInstrumentParameterReadWrite`: instrument `displayName` is `"dummy"`, expected `"TripleOscillator"` (`ScriptEngineTest.cpp:608`).
- `ScriptMemoryBudgetTest::aSystemAllocationFailureIsNotReportedAsBudgetExhaustion`: `ScriptMemoryState::allocate` of an impossible size returned non-null (`ScriptMemoryBudgetTest.cpp:231`).

Two earlier attempts at the baseline comparison are in the log folder as `test-8-baseline-family.log` and `test-9-fixed-family.log`. **They are invalid**: a quoting mistake in my build command meant the builds did not run, so both used the fixed binaries. They are kept only for transparency and are not used as evidence.

## Gates (step 5)

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` → **EXIT=1** (`gates-1.log`). It did not run any gate: it tries to create `<worktree>/../zene-qwen-new-p12-243-feature-check/`, which is outside the write area, and gets `PermissionError`.

The run used a copy of the same runner in the run folder, `feature_check_runlocal.py`. The only changes are the `ROOT` path (kept at the shared qwen folder, so `FIX-BASE` and the cached base listings still resolve) and the log directory (moved into `logs/feature-check-zene-qwen-new-p12-243/`). The gate logic is unchanged. `diff` against the original shows only those two lines. Command: `python3 .../runs/new-p12-243.r1/feature_check_runlocal.py <worktree> > .../gates-2.log 2>&1; echo EXIT=$?` → **EXIT=0**.

| Gate | Result |
| --- | --- |
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS (no new item in changed files) |
| file-length | PASS (no new item in changed files) |
| RESULT | PASS |

## Evidence table

| Check | Command (from the worktree or `build/tests`) | EXIT |
| --- | --- | --- |
| RED build | `cmake --build build --target ScriptDawBindingTest -j4` (`build-2-red.log`) | 0 |
| RED test | `ctest -R ScriptDawBindingTest --output-on-failure` (`test-1-red.log`) | 8 |
| GREEN build | `cmake --build build --target zene ScriptDawBindingTest -j4` (`build-3-green.log`) | 0 |
| GREEN test | `ctest -R ScriptDawBindingTest --output-on-failure` (`test-2-green.log`) | 0 |
| Mutation: revert | `git apply -R fix.patch` (rebuild `build-4-reverted.log`, exit 0) → test (`test-4-reverted.log`) | 8 |
| Mutation: restore | `git apply fix.patch` (rebuild `build-5-reapplied.log`, exit 0) → test (`test-5-reapplied.log`) | 0 |
| Affected family, fixed | `ctest -R <11 Script* targets>` (`test-14-fixed-family.log`) | 8 (4 pre-existing, see above) |
| Affected family, baseline | same, fix reverted (`test-13-baseline-family.log`) | 8 (same 4 + `ScriptDawBindingTest`) |
| Gates | `feature_check_runlocal.py` (`gates-2.log`) | 0 |
| Gates, original runner | `feature_check.py` (`gates-1.log`) | 1 (`PermissionError`, see above) |

Tests executed: `ScriptDawBindingTest` 13 passed, 0 failed, 1 skipped (pre-existing) with the fix. The family above is 11 CTest targets; 7 pass with the fix, the 4 failures are the same as baseline.

## Not changed, noticed while reading

- `src/core/ScriptDawEdit.cpp` (the `loadEffect` refusal path) and the catalogue refusal print `'%1'` literally: the message is "no device named '%1' in this build's catalogue". The `QStringLiteral` format argument is never substituted. Seen at `ScriptDawBindingTest` run output, `theChainBindingIsTheSocketAddressableChain`. Out of scope for this finding; not touched.
- Control-surface string arguments are not checked for NUL in this change. The finding is about the script path only.

## Self-check

`submit_check.py` run: `.../logs/submit-1.log`, **EXIT=1**. Passing: commits on the branch, working tree clean, this report, tests changed. **FAIL: `gates (feature_check.py)`** is an environment block, not a gate failure: the shared runner creates `<worktree>/../zene-qwen-new-p12-243-feature-check/` and gets `PermissionError` (`gates-1.log`) because that location is outside the write area. The gates themselves pass with the identical runner pointed at the run folder (`gates-2.log`, EXIT=0, table above). I did not edit the shared runner or the check. The loop should run `feature_check.py` where it can write its log directory.

ASK items:

1. **Regression fails reverted, passes with the fix.** RED `test-1-red.log` EXIT=8 (two FAIL!, actual `"A"`); GREEN `test-2-green.log` / `test-5-reapplied.log` EXIT=0; reverted `test-4-reverted.log` EXIT=8. Mutation-proof commands are in the Regression proof section.
2. **Deterministic.** No sleeps, timing, threads or races. The direct test feeds a literal `QByteArray("A\0B", 3)`. The Lua test uses one fixed mixer channel and asserts right after `runString`. The rename is applied inside that call: the RED run already shows `Actual "A"` at that point, so there is nothing to wait for. Same pattern as `anOverlongChannelNameIsRejectedWithoutChangingTheEngine`.
3. **At the cause and complete.** The single rejection point is `setCommandText` (`ScriptDawEdit.cpp:70`). All four callers route through it (listed in Evidence). The only writer of `command.text` is `setCommandText` (checked: `ScriptCommandBridge.cpp:284` never writes text). The refusal messages were made consistent at all four sites.
4. **No collateral damage.** The check refuses only NUL, which the payload already could not carry (any byte after a NUL was lost). Names without NUL, and every name within the 255-byte limit, take the same path as before. The payload is in memory only; no save format or stored project data changes. The overlong refusals keep their "255-byte" text, and the existing overlong tests pass (`test-2-green.log`, `test-3-green-verbose.log`). The 11-target family has the same four pre-existing environmental failures as baseline and no new ones (see the family section).
5. **Verdict FIXED**, so ALREADY-FIXED / FALSE-POSITIVE evidence does not apply.
