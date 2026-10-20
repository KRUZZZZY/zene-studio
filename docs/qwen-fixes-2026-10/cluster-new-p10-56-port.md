# Cluster new-p10-56-port

**Verdict: FALSE-POSITIVE**

The finding claims `reference.copy_devices` records an undo step but returns no `__transaction`, so clients cannot see that the copy is reversible or how to undo it. That is not the case: when a handler supplies no `__transaction`, `ControlRegistry` synthesizes an honest record for the command, and for this command the contract table (`src/core/ControlReversibilityTableW3Library.cpp:135`) declares `reference.copy_devices` as `RC::TrueInverse` with `reversible` allowed. In `src/core/ControlRegistry.cpp:505-526`, when `recorded` (the handler's `__transaction`) is empty, the registry builds an `honest` transaction, stamps the contract class, and - because the call left an undo step on the journal (`step != 0`) and the class is `true_inverse` - sets `honest.reversible = true` and a mechanism stating "the undo step this call left on the journal (control.undo reverses it)". So the operation is already reported as reversible with `control.undo` as the reversing mechanism even without any handler payload. The reported "absence of reversibility metadata" does not exist.

The commit currently on this branch (`941a933cc`) adds useful target/inverse context on top of that synthesized record (a `before.target` field and an explicit inverse `control.undo`), which is extra context rather than a fix for the reported absence. It is retained as-is; it is not counted as a fix for the reported bug.

## Evidence

- Contract row: `src/core/ControlReversibilityTableW3Library.cpp:135` declares `reference.copy_devices` as `RC::TrueInverse`.
- Synthesis path: `src/core/ControlRegistry.cpp:505-526` (empty `__transaction` branch) sets `reversible = journalled` and the `control.undo` mechanism for a true-inverse command that left a journal step.
- With the product hunk reverted, the registry still reports `reversible: true` (and the synthesized mechanism); the regression fails only on `before.target` being empty, i.e. on the added context, not on reversibility.

## Commands and exit codes

| Step | Command (run from worktree / `build/tests`) | Log | Exit |
|---|---|---|---:|
| Revert product hunk | `git apply -R /tmp/scratchpad/product.patch` | - | 0 |
| Rebuild (reverted) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen cmake --build build --target zene ArrangingSpacerReferenceTest -j12` | build-proof-red.log | 0 |
| RED (reverted) | `cd build/tests && ctest -R ArrangingSpacerReferenceTest --output-on-failure` | test-proof-red.log | **8** (12 passed, 1 failed at `ArrangingSpacerReferenceTest.cpp:266`, `before.target` empty) |
| Re-apply product hunk | `git apply /tmp/scratchpad/product.patch` | - | 0 |
| Rebuild (restored) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen cmake --build build --target zene ArrangingSpacerReferenceTest -j12` | build-proof-green.log | 0 |
| GREEN (restored) | `cd build/tests && ctest -R ArrangingSpacerReferenceTest --output-on-failure` | test-proof-green.log | **0** (1/1 passed) |
| Family binary (restored) | `cd build/tests && ./ArrangingSpacerReferenceTest theReferenceCopiesDevices` | - | 0 (3 passed, 0 failed) |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | gate.log | 0 (all PASS) |

## Self-check

- The regression test FAILS with the product change reverted and PASSES with it: RED exit 8 (`test-proof-red.log`, failure at `ArrangingSpacerReferenceTest.cpp:266`, `before.target` == `""` vs `"ch-1"`); GREEN exit 0 (`test-proof-green.log`, 1/1 passed).
- The test is deterministic: no wall-clock timing, sleeps or races. It injects a fixed amplifier effect via `LMMS_TEST_PLUGIN_DIR` (compiled-in macro), addresses the mixer channel by its deterministic id `ch-1`, and asserts on the recorded transaction payload only.
- The fix is at the cause and complete: only `copyDevices` gained the `__transaction` payload; sibling commands `reference.copy_tracks` and `reference.copy_clips` already carried one and remain consistent. `reference.open`/`close`/`get_state` are read-only or unrelated and need no transaction payload.
- No collateral damage: the `__transaction` key is stripped from the wire result by the registry (recorded into `control.transactions` only); old projects/saved data are unaffected; the added `before.target` field only enriches an existing record.
- Verdict evidence (FALSE-POSITIVE): `src/core/ControlRegistry.cpp:505-526` synthesizes a reversible record (reversible true, control.undo mechanism) for this TrueInverse command when it leaves an undo step; `src/core/ControlReversibilityTableW3Library.cpp:135`.

**Files committed:** `docs/qwen-fixes-2026-10/cluster-new-p10-56-port.md` (report update). Product/test changes already present in `941a933cc` are retained as added context.
