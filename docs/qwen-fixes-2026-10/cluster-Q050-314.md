IMPLEMENTED

# Q050-314 - WASM offline-render memory pointer

Branch `feat/050-Q050-314-oc` (base `050/m0-train-24`). Full write-up: `docs/050-features/Q050-314.md`.

**What happened:** the stale-pointer fix was already in the base (`detail::callProcessAndRefreshMemory`,
`src/wasm/WasmSandbox.h:154`, used at `src/wasm/WasmOfflineRender.cpp:124` and `src/wasm/WasmWorker.cpp:355`). No product code
changed. The unit adds the missing proof (tests that fail when the refresh is removed, invalid-view and refused-growth
failures, ASan run) and records the contract in `docs/WASM-EFFECT-ABI.md`.

**Caveat that matters:** the unit's tree has `WANT_WASM=OFF` and no wasmtime, and I was told not to configure. So
`WasmWorkerPoolTest` was built standalone (same source file; it compiles its own sources) against
`zene-cachyos/third_party/wasmtime` and run directly, **not through ctest**. `ControlWasmRenderTest` could not be linked: its two
new cases were only syntax-checked with the tree's flags, and their WAT modules run through `renderOffline`. Those two cases
have not been executed.

## Acceptance

| Criterion | Test | Command / result |
|---|---|---|
| Growth during `callProcess`: output from the refreshed view | `WasmWorkerPoolTest::testOfflineRenderReadsTheRefreshedViewNotTheOneTakenBeforeProcess` (inline control + pooled: 42, 7) | `logs/standalone-build.sh /tmp/q314-plain` exit 0; `./WasmWorkerPoolTest` 21 passed, exit 0 (`test-1.log`) |
| Same via the agent command | `ControlWasmRenderTest::renderOfflineReadsTheOutputPlaneAfterTheModuleGrewItsMemory` | `-fsyntax-only` exit 0 (`syntax-check.log`); WAT run via `renderOffline` gives `[42,7]` (`test-cmdmodules.log`); NOT run as a test |
| Invalid view after growth: typed render failure | `testOfflineRenderFailsWhenTheViewIsInvalidAfterProcess` (shrunk, gone) | exit 0 (same run) |
| Refused growth then trap: failed render | `testOfflineRenderReportsAFailedGrowthTrapAsAFailedRender` | exit 0 (same run) |
| `ControlErrorKind::Refused` from `wasm.render_offline` | `ControlWasmRenderTest::renderOfflineRefusesAModuleThatTrapsAfterAFailedGrowth` | syntax-checked only; handler maps not-ok to `Refused` at `src/core/ControlCommandsWasmRender.cpp:244`; `renderOffline` fails on the module (`test-cmdmodules.log`); NOT run as a test |
| Under ASan | whole `WasmWorkerPoolTest`, `-fsanitize=address,undefined -fno-sanitize=vptr` | build exit 0, run: 21 passed, no sanitizer report, exit 0 (`test-asan.log`) |

## Self-check

- **New tests fail without the behaviour.** Two mutations of `WasmSandbox.h`, each restored with `git checkout`:
  (a) refresh no longer updates the caller's pointer: run exit 3, `testOfflineRenderReadsTheRefreshedView...` FAILS
  (`test-mut-stale.log`); (b) invalid refreshed view no longer becomes an error: the shrink row FAILS
  ("reported success") and the view-gone row SIGSEGVs, so I killed the run (`test-mut-noerror.log`; no clean exit code).
  Restored build: exit 0, 21 passed. There is no product hunk to revert, so this is the mutation proof.
- **Every criterion proven by a test I ran:** see the table; the two `ControlWasmRenderTest` cases are the exception and
  are marked as not run.
- **Undo / reversibility:** no edit and no command added or changed; `wasm.render_offline` stays `not_mutating`.
- **Realtime:** nothing in `src/` changed. `callProcessAndRefreshMemory` (already in base) is the only audio-path function
  involved; it does two accessor calls and no allocation, lock, I/O or signal.
- **Projects:** no project data or format touched.
- **Portability:** test code uses `<array>`, `<atomic>`, `<cstring>`, `<cmath>` and QtTest only; no Qt-6-only API, no
  `std::filesystem`, no reserved identifiers. Not built on Qt 5.15 / MSVC / macOS here (hosted CI only).
- **GUI / agent / manual warnings:** no user-facing action exists, so no GUI path or new command; the manual has no WASM
  section and nothing user-visible changed.
- **Gates** (run one by one, because `feature_check.py` writes its logs outside the write-jail):
  fork-sources 0, all-sources-reproduce 0, no-upstream-regression 0, check-namespace 0, no-tautology 0, doc-figures 0.
  complexity and file-length are red on the train; the only items they list in files related to this unit are in
  `src/wasm/WasmSandbox.cpp`, which this branch does not change.
