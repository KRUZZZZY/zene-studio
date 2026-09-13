# LANE STATE — 030-chain-presets (worktree zene-030/wch, branch 030-chain-presets)

Base: `501d2cd3e` (release/0.3.0). Item: OWNER-31 item 2, "plugin chains as reusable presets".
Build: `build/` configured `-DCMAKE_BUILD_TYPE=RelWithDebInfo -DWANT_QT6=ON -DWANT_VST3=OFF
-DWANT_CLAP=OFF -DWANT_WASM=OFF -DWANT_STEM_SPLIT=OFF`; `cmake --build build -j2` → BUILD_EXIT=0,
0 `error:` lines.

## Done and VERIFIED (unpiped exit codes; logs in /tmp/wch-030-verify/)
- [x] engine half + store: `include/ControlChainPresetSupport.h`,
      `src/core/ControlChainPresetSupport.cpp` (the `.zcp` document; the device identity read out of
      the device's own `zenepluginstate` document; the identity→catalogue lookup; the apply).
- [x] command group: `src/core/ControlCommandsChain.cpp` (chain.list / chain.get_state /
      chain.save) + `src/core/ControlCommandsChainEdit.cpp` (chain.apply / chain.rename /
      chain.remove), registered in `include/ControlRegistry.h` + `src/core/ControlRegistry.cpp`,
      sources in `src/core/CMakeLists.txt`.
- [x] A16: 4 `true_inverse` (recorded action checkpoints) in
      `ControlReversibilityTableAction.cpp`, 2 `not_mutating` in
      `ControlReversibilityTablePassive.cpp`; histogram 170 / 90 / 13 / 4 / 63 in
      `tests/src/core/ReversibilityContractTest.cpp` + `docs/RELEASE-NOTES-v0.3.0-alpha.md`.
      `ReversibilityContractTest` PASS (exit 0), `ReversibilityUndoTest` PASS, `ControlRegistryTest` PASS.
- [x] UI-absence lines in BOTH docs.
- [x] proofs: `tests/src/core/ControlChainPresetTest.cpp` — PASS (exit 0);
      `tests/control-chain-presets.py` (ctest `ControlChainPresets`) — PASS, 28 checks, run by hand
      too (exit 0).
- [x] manifests regenerated with their own recipes, both print `REPRODUCES`
      (`tests/fork-sources.txt`, `tests/all-sources.txt`).
- [x] gates: 4 PASS (exit 0), 6 PASS, 8 PASS, 9 PASS, unregistered-tests PASS.
- [x] `commands_snapshot.json` regenerated from a live instance of this build (170 ids, six
      `chain.*`); `ControlCommandsSnapshot` PASS (exit 0). Instance exited cleanly (no leak).

## Red, and NOT this lane's (proved: unchanged since `501d2cd3e`)
- [ ] gate 7 (file length): `tests/control_socket_harness.py` 511, `tests/control-socket-path-safety.py`
      551→574. Neither file is touched by this lane.
- [ ] evidence gate: 27 refused log files under `tests/evidence*`, `tests/control-*-logs` — all
      unchanged since the base tip.
- [ ] release-honesty gate: 3 FAILs, all `WANT_VST3`/`WANT_CLAP` OFF — the smallest-tree
      configuration WAVE-1-BRIEFS.md asks for. Needs a CI-config build (tools/local-ci.sh).

## Left
- [ ] full-suite `ctest -j2` result (running) and `tests/run-all-gates.sh --no-mutation` (running).
- [ ] optional: `tools/local-ci.sh` (its own `build-ci/` tree, VST3+CLAP ON) for CI-exactness and a
      release-honesty PASS.
- [ ] delete `build/` when the parent has re-run the acceptance on the merged tip.

## Exact next command
    cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms/zene-030/wch \
      && tail -20 /tmp/wch-030-verify/ctest-full.log; tail -40 /tmp/wch-030-verify/run-all-gates.log
