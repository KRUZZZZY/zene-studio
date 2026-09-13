# LANE STATE — 030-chain-presets (worktree zene-030/wch, branch 030-chain-presets)

Base: `501d2cd3e` (release/0.3.0). Item: OWNER-31 item 2, "plugin chains as reusable presets".

## Done
- [x] worktree `zene-030/wch` on `030-chain-presets` from `501d2cd3e`; disk checked; NO leaked
      zene/Xvfb instance older than this session (the two long-lived `mcp-zene-control/server.py`
      processes are the bridge's own servers, not DAW instances; no leaked `zene` binary).
- [x] engine half + store: `include/ControlChainPresetSupport.h`,
      `src/core/ControlChainPresetSupport.cpp` (the `.zcp` document, identity from the device
      state document, `chain apply`/`chain xml` helpers).
- [x] command group: `src/core/ControlCommandsChain.cpp` (chain.list / chain.get_state /
      chain.save) + `src/core/ControlCommandsChainEdit.cpp` (chain.apply / chain.rename /
      chain.remove); registered in `include/ControlRegistry.h` + `src/core/ControlRegistry.cpp`;
      the three new sources in `src/core/CMakeLists.txt`.
- [x] A16 rows: 4 `true_inverse` (action checkpoint) in `ControlReversibilityTableAction.cpp`,
      2 `not_mutating` in `ControlReversibilityTablePassive.cpp`; histogram moved to
      170 rows / 90 / 13 / 4 / 63 in `tests/src/core/ReversibilityContractTest.cpp` and
      `docs/RELEASE-NOTES-v0.3.0-alpha.md`.
- [x] UI-absence lines in BOTH docs (release notes section + `docs/KNOWN-LIMITATIONS.md` bullet).

## Left
- [ ] tests: `tests/src/core/ControlChainPresetTest.cpp` (in-process: contract rows, document
      identity/name rules) + `tests/control-chain-presets.py` (the registered socket ctest:
      apply to a second track, parameter values off the wire, project.save→open round trip,
      every inverse through control.undo); register both in `tests/CMakeLists.txt`.
- [ ] regenerate `tests/fork-sources.txt` and `tests/all-sources.txt` with their own recipes
      and require `REPRODUCES`; re-run gates 4/7/8.
- [ ] regenerate `tools/mcp-zene-control/zene_control/commands_snapshot.json` from a live
      instance of this build; re-run `ControlCommandsSnapshot`.
- [ ] configure + build (smallest tree), run the acceptance list in `WAVE-1-BRIEFS.md`.

## Exact next command
    cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms/zene-030/wch \
      && cmake -S . -B build -DWANT_VST3=OFF -DWANT_CLAP=OFF > /tmp/wch-cfg.log 2>&1; echo EXIT=$?
