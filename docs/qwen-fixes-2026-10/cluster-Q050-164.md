IMPLEMENTED

# Q050-164 - Expose per-sample modulation in the panel

Earlier rounds failed at link and left no commits on this branch, so this is a fresh implementation on `050/m0-train-24`.

Tests: `ctest -R '^(ModulatorPanelTest|ControlModulatorCommandsTest|ModulationLayerProjectRoundTripTest)$'` from `build/tests`.

| Criterion | Test | Exit with change | Exit with product reverted |
| --- | --- | --- | --- |
| Panel switch, current value after reopening | `ModulatorPanelTest` | 0 | 8 (shared ctest run; this test failed) |
| Evaluation, undo/redo | `ControlModulatorCommandsTest` | 0 | 8 (same run; failed) |
| Save/load | `ModulationLayerProjectRoundTripTest` | 0 | 8 (same run; failed) |
| Limitation text | `docs/KNOWN-LIMITATIONS.md` reviewed | n/a | n/a |

## Self-check

- **Each new test fails reverted, passes restored:** product hunks reversed and rebuilt: ctest exit 8, all three targets failed, including `perSampleSetSwitchesEvaluationAndIsOneUndoStep`, `createdModulatorsSurviveSaveClearAndReopen`, and both new panel tests. Restored and rebuilt: exit 0.
- **Every criterion proven:** see the table. Final run including `ReversibilityContractTest`, `ControlCommandsSnapshot` and `ModulationRateTest`: 6/6, exit 0. The limitation text is a documentation change with no test.
- **One undo step, honest class:** a single `control.undo` reverses a click or a command and the route stays bound; a repeated value records nothing. `modulator.per_sample_set` is `true_inverse` through an action checkpoint.
- **Realtime:** no audio-thread code changed. The command runs on the control thread through `editModulationLayer`, as `modulator.depth_set` does. Local TSan does not run here; by design the publication path is the same one `depth_set` uses.
- **Old projects, round trip:** no format change; an absent `persample` still means off. The flag round-trips through `project.save` and `project.open` in `ModulationLayerProjectRoundTripTest`. No old-project render comparison was run; the load code is unchanged.
- **Portability:** only long-standing Qt widgets (`QCheckBox`, `QTableWidget::setCellWidget`); no new platform calls and no reserved identifiers. Not compiled here on Qt 5.15, MSVC or macOS.
- **Gates:** `feature_check.py` cannot run in this lane: it creates a sibling directory outside the write-jail (`PermissionError`). I ran its gates directly. fork-sources, all-sources, no-upstream, namespace, no-tautology and doc-figures each exit 0. `file-length --check` and `complexity --check` exit 1, and none of their REGRESSION lines name a file this branch changed; `feature_check.py` only fails on items in changed files, but I could not run its base comparison.
- **Full ctest:** not run, by instruction; only the targets above.
- **Build note:** the configured ccache `temporary_dir` is not writable in this jail, so I set `CCACHE_TEMPDIR=/tmp/ccache-q164`.
