# Cluster new-p10-45

**Verdict: FALSE-POSITIVE**

The reported behavior cannot occur on this branch because `plugin.midi_part_set` is not implemented or registered. [ControlCommandsPluginParts.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-45/src/core/ControlCommandsPluginParts.cpp:142) registers `plugin.midi_parts`; lines 187–211 register `plugin.midi_part_add`; lines 239–259 register `plugin.midi_part_remove`; and `registerPluginPartCommands` calls only those three registrations at lines 263–268. [ControlInstrumentCommands.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-45/include/ControlInstrumentCommands.h:46) likewise documents only the list, add, and remove commands. The plugin command group invokes this registration function at [ControlCommandsPlugin.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-45/src/core/ControlCommandsPlugin.cpp:479).

The repository search for `midi_part_set` found no match in `src`, `include`, or `tests`. The closest command-family test exercises add and remove in [InstrumentLayersCommandsTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-45/tests/src/core/InstrumentLayersCommandsTest.cpp:187). No production or test files were changed, and no regression test was run because the alleged command is absent. The affected-family test count is **N/A (0 run)**; there is no current setter behavior to exercise. RED/GREEN and the revert-and-retest proof are **N/A** for the same reason.

## Evidence and verification

Search for the alleged command:

```sh
rg -n 'plugin\.midi_part_set|midi_part_set' src include tests
```

Exit 1 (no matches).

Inventory of exposed commands and registration:

```sh
rg -n 'plugin\.midi_parts|plugin\.midi_part_add|plugin\.midi_part_remove|registerPluginPartCommands' src/core/ControlCommandsPluginParts.cpp src/core/ControlCommandsPlugin.cpp include/ControlInstrumentCommands.h tests/src/core/InstrumentLayersCommandsTest.cpp
```

Exit 0.

- RED regression: N/A; no `plugin.midi_part_set` handler exists.
- GREEN regression: N/A; no `plugin.midi_part_set` handler exists.
- Affected-family tests: N/A; 0 run because no setter behavior exists to exercise.
- Complexity gate: `bash tests/complexity-gate.sh --check` — exit 0 (`gate-1.log`).
- File-length gate: `bash tests/file-length-gate.sh --check` — exit 0 (`gate-2.log`).

Build/test logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-45.r1/logs/`. The inventory and gate commands were captured in `inventory-1.log`, `inventory-2.log`, `gate-1.log`, and `gate-2.log` respectively.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p10-45.md` (this report)
