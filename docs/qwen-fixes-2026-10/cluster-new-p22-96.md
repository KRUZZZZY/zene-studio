ALREADY-FIXED — the null check the finding asks for is in place on this branch (guard at src/core/ControlDeviceMoves.cpp:134, added in 65ef9cf4d, an ancestor of HEAD `cf3618e61`); no product change was needed. One pinning regression test was added in tests/src/core/DeviceMovesUndoTest.cpp.

# Cluster new-p22-96 — ControlDeviceMoves::removeDevicesRecorded and chainOf()

Finding: "dereferences chainOf() without a null check" at src/core/ControlDeviceMoves.cpp:133 (historical line).

## Evidence (current tree)

- `src/core/ControlDeviceMoves.cpp:131-134`: the finder overload does `EffectChain* chain = chainOf();` and the loop header is
  `for (auto it = effects.rbegin(); it != effects.rend() && chain != nullptr; ++it)`. The `chain != nullptr` term is evaluated
  before the first iteration, so `chain->effects()` (line 136) is never reached with a null chain. `chain` is not reassigned in the
  loop, so the guard holds for every iteration.
- `git blame`: line 134 is from `65ef9cf4d` (2026-10-03, "0.5.0 W2/W3: review L10-L03-L02-1 fixes ... structural undo for device
  removal"). `git merge-base --is-ancestor 65ef9cf4d HEAD` → ancestor (exit 0). The finding's "no guard" line is older than that.
- The finder is `src/core/ControlStructuralSupport.cpp:581-590` (`recordedChainFinder`): it returns `nullptr` when the owning
  target no longer resolves (`resolveChainTarget` fails), and a `QPointer` guard for a chain no target owns. So "chain gone" is a
  defined `nullptr` return, and the loop treats it as a no-op.
- Callers that pass a finder or chain (all dereference the same chain before the call, so it is non-null on those paths):
  - `src/core/ControlCommandsRackWrap.cpp:157,164` (`target.chain->effects()` at 157, call at 164)
  - `src/core/ControlCommandsMixerTools.cpp:507,512` (`chain->effects()` at 507, `chainFinder(args)` at 512)
  - `src/core/ControlCommandsDeviceCopy.cpp:222-223` (`to.chain->effects()` at 222, `destinationResolver(oneArgs)` at 223)
  - `src/core/ControlCommandsDeviceTools.cpp:285` (`channelChain(...)` resolves the channel just iterated; a missing channel gives a no-op, not a crash)
- Verdict basis: the crash in the finding cannot happen on this branch. The remaining behaviour (a vanished chain is a silent
  no-op) is the designed contract of `recordedChainFinder`, not a defect.

## What changed

- `tests/src/core/DeviceMovesUndoTest.cpp`: new slot `removeDevicesRecordedToleratesAMissingChain` (about 18 lines). It loads an
  amplifier on a new instrument track, then calls the finder overload with a finder that returns `nullptr`, and the `EffectChain*`
  overload with `nullptr`. Both must leave the devices in place (`names(target) == {"amplifier"}`). No sleeps, clocks or races:
  the seam is the injected `nullptr` finder.
- No product file changed. No new control commands. No gate, test or baseline was weakened.

## Regression proof (RED/GREEN)

The regression is a pin of the existing guard, so it passes on HEAD (no RED is possible without re-introducing a defect). The
proof that it catches the crash: with the guard removed it segfaults; with the guard restored it passes.

| Step | Command (run from `build/tests` unless noted) | Result |
| --- | --- | --- |
| Build the test target | `CCACHE_DISABLE=1 cmake --build build --target DeviceMovesUndoTest -j4` | EXIT=0 (build-2.log) |
| Build plugin fixtures the family needs | `CCACHE_DISABLE=1 cmake --build build --target amplifier bassbooster -j4` | EXIT=0 (build-3.log) |
| Family run on HEAD+test (GREEN) | `ctest -R '^DeviceMovesUndoTest$' --output-on-failure` | EXIT=0, 100% (test-2.log) |
| Function-level run | `./DeviceMovesUndoTest` | EXIT=0, 7 passed, 0 failed (run-green-1.log) |
| Mutant: guard removed (`src/core/ControlDeviceMoves.cpp:134` edited to drop `&& chain != nullptr`) | `CCACHE_DISABLE=1 cmake --build build --target DeviceMovesUndoTest -j4` | EXIT=0 (build-4-mutant.log) |
| Mutant run | `./DeviceMovesUndoTest removeDevicesRecordedToleratesAMissingChain` | EXIT=139 (SIGSEGV) — the test catches the dereference (run-red-mutant.log) |
| Guard restored | `git status` shows only the test file; rebuild `CCACHE_DISABLE=1 cmake --build build --target DeviceMovesUndoTest -j4` | EXIT=0 (build-5-restored.log) |
| Restored run | `ctest -R '^DeviceMovesUndoTest$'` / `./DeviceMovesUndoTest` | EXIT=0 / EXIT=0, 7 passed (test-3-green.log, run-green-2.log) |
| Family: UndoHonestyStateTest | `CCACHE_DISABLE=1 cmake --build build --target UndoHonestyStateTest -j4`; `ctest -R '^UndoHonestyStateTest$'` | EXIT=0 / EXIT=0, 100% (build-6-undohonesty.log, test-4-undohonesty.log) |
| Final family run after the indentation-only fix to the test | `cmake --build ... DeviceMovesUndoTest UndoHonestyStateTest`; `ctest -R '^(DeviceMovesUndoTest|UndoHonestyStateTest)$'` | build EXIT=0; ctest EXIT=0, 100% of 2 (build-7-final.log, test-5-final.log) |

Affected-family test count: 2 ctest tests (DeviceMovesUndoTest, UndoHonestyStateTest), 0 failing. DeviceMovesUndoTest runs 7 QTest functions, all PASS (run-final-dmu.log).

Note on the pre-existing failure seen first: DeviceMovesUndoTest failed with "the amplifier fixture must be built" until `libamplifier.so` and `libbassbooster.so` were built (build/plugins). That is a build-tree prerequisite, not a code issue; after building them all seven functions pass.

## Gates

- `feature_check.py` (the shared gate, with its log directory redirected into this run folder, see below): EXIT=0, `RESULT: PASS`, all eight rows PASS (gates-1.log, gates-feature-check/).
- Command as run: `python3 -I <run-logs>/feature_check_logdir.py <worktree>` — a copy of `qwen/feature_check.py` with only `ROOT` and the log directory changed, because the original writes logs to `<worktree>/../<name>-feature-check`, which the write-jail denies (the first attempt: `PermissionError`, gates-1 first attempt, not kept). The checks themselves are unchanged.
- `submit_check.py`: run before the commit (submit-1.log, EXIT=1: nothing committed, report missing, test file uncommitted). Its own "gates" row runs the unmodified `feature_check.py`, which hits the same log-directory denial in this sandbox; that row is an environment failure, not a gate failure.

## Environment notes

- `ccache` under this shell points at a directory the sandbox cannot write (`ccache: error: failed to create temporary file ... Permission denied`); builds were run with `CCACHE_DISABLE=1` in the environment. No source or build config was changed for it.

## Self-check

- *The regression test FAILS with the product change reverted and PASSES with it (exit codes).* Not applicable as a RED: there is no product change, the guard already exists. The reverted-guard mutant gives EXIT=139 (SIGSEGV) and the restored guard gives EXIT=0 (table above). This is the honest proof available.
- *The test is deterministic.* Yes: no clock, sleep or thread. The seam is an injected finder returning `nullptr` plus a direct `nullptr` `EffectChain*`.
- *The fix is at the cause and complete.* No fix. Callers and the finder were checked (evidence above): every caller dereferences the same chain before the call, and the finder's `nullptr` path is the designed no-op.
- *No collateral damage.* No product file changed; the test file change adds one slot.
- *ALREADY-FIXED: the code (file:line) that shows it.* `src/core/ControlDeviceMoves.cpp:134` (`&& chain != nullptr`), introduced in `65ef9cf4d`, which is an ancestor of HEAD.
