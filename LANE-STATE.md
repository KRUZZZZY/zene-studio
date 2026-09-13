# LANE STATE — 030-chain-presets (worktree zene-030/wch, branch 030-chain-presets)

Base: `501d2cd3e` (release/0.3.0). Item: OWNER-31 item 2, "plugin chains as reusable presets".
HEAD at sign-off: `6a2ec5bc6` (7 commits above the base; all of them committed, tree clean).

## The feature
An effect chain can be captured as a NAMED preset (the ordered device list plus each device's own
state document, which is `plugin.state_save`'s document - no second serialiser) and applied to
another track. The store is the product's user preset tree,
`<userPresets>/chainpresets/<name>.zcp`, i.e. OUTSIDE the project: that is what makes a preset usable
in another project, and what makes it survive `project.save` / `project.open` by construction. The
store is per-user, not per-project, and `docs/KNOWN-LIMITATIONS.md` says so.

Ids (all six headless-safe, no `requires`): `chain.list` · `chain.get_state` · `chain.save` ·
`chain.apply` · `chain.rename` · `chain.remove`.
A16: `chain.save` / `chain.apply` / `chain.rename` / `chain.remove` are `true_inverse` (recorded
ACTION checkpoints; `chain.apply`'s writes the chain's own `<fxchain>` XML back through
`EffectChain::loadSettings`, the project loader's path, and REFUSES a chain too large for the bounded
snapshot rather than replacing it without an inverse). `chain.list` / `chain.get_state` are
`not_mutating`. Table histogram now 170 rows / 90 / 13 / 4 / 63.

## Verified (every number unpiped; logs in /tmp/wch-030-verify/)
| command | exit |
|---|---|
| `cmake --build build -j2` (reduced config: `-DWANT_QT6=ON -DWANT_VST3=OFF -DWANT_CLAP=OFF -DWANT_WASM=OFF -DWANT_STEM_SPLIT=OFF`) | **0** |
| `ctest -j2` in `build/tests` (the whole suite, reduced config) | **0** — 123/123 passed |
| `ctest -R ControlChainPresetTest` | **0** |
| `ctest -R ControlChainPresets` (the socket proof; by hand too: 28/28 checks) | **0** |
| `ctest -R ReversibilityContractTest` / `ReversibilityUndoTest` / `ControlRegistryTest` | **0** |
| `ctest -R ControlCommandsSnapshot` (after regenerating the snapshot) | **0** |
| `ctest -R agent_surface` (the junk-argument sweep over every registered id) | **0** |
| `bash tools/local-ci.sh --build-dir build-ci --jobs 2` (CI config: `-DUSE_WERROR=ON -DWANT_VST3=ON -DWANT_CLAP=ON`) | **0** — configure OK, build OK, ctest 100% (0 failed of 129) |
| the lane's four ctests in `build-ci` (CI config) | **0** each |
| `bash tests/release-honesty-gate.sh --header build-ci/lmmsversion.h --artifacts build-ci` | **0** (all 6 documented features match) |
| `bash tests/complexity-gate.sh --check` (gate 4) | **0** |
| `bash tests/duplication-gate.sh --check` (gate 8) | **0** |
| `bash tests/fork-sources-gate.sh` (gate 9) | **0** — 366 entries, 0 stale |
| `bash tests/no-upstream-regression-gate.sh` (gate 6) | **0** |
| `bash tests/unregistered-tests-gate.sh` | **0** |
| `bash tests/run-all-gates.sh --no-mutation` | **1** — gates 1/3/4/6/8/9/10 PASS, gates 7 + 11 FAIL, both pre-existing (below); 2 and 5 SKIP |
| `bash tests/file-length-gate.sh --check` (gate 7) | **1** — `tests/control_socket_harness.py` (511) and `tests/control-socket-path-safety.py` (551→574); both byte-identical to `501d2cd3e` |
| `bash tests/evidence-gate.sh` | **1** — 27 refused run-logs under `tests/evidence*` / `tests/control-*-logs`; all unchanged since `501d2cd3e`, none in this lane's commits |
| `bash tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build` (REDUCED config) | **1** — the 3 FAILs are `WANT_VST3`/`WANT_CLAP` OFF, i.e. the smallest-tree configuration the brief asks for; the same gate is exit 0 against `build-ci` |
| `tests/fork-sources.txt` and `tests/all-sources.txt`, each run through its own "Verify it" | prints `REPRODUCES` |

## What could not be verified here
- The two pre-existing gate reds (7, 11) are not this lane's to re-anchor; the parent decides.
- Gate 5 (mutation) was not run (`--no-mutation`); gate 2 (coverage) was not run.
- The MCP offline snapshot is regenerated for THIS tip (170 ids); the parent must regenerate it once
  more after the last command-group merge of the wave, from a live instance of the merge tip.

## Cleanup
Both build trees (`build/`, `build-ci/`) are deleted, as the brief asks. To rebuild:
    cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWANT_QT6=ON \
      -DWANT_VST3=OFF -DWANT_CLAP=OFF -DWANT_WASM=OFF -DWANT_STEM_SPLIT=OFF && cmake --build build -j2
    # then, for the socket proof by hand:
    cd build/tests && QT_QPA_PLATFORM=offscreen python3 ../../tests/control-chain-presets.py ../zene
