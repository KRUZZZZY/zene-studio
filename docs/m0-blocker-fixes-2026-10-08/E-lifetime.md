# Batch E: object lifetime fixes (2026-10-08)

Branch: `bugs/m0-blk-lifetime`. The actual base is `8eacc41d2`, train 2's tip as supplied by the task; the brief's `28cc3a0b5` base did not match this worktree.

| Cluster | Verdict | Fix summary | Fix commit | Test | Red exit | Green exit | Gate exits |
| --- | --- | --- | --- | --- | ---: | ---: | --- |
| 2403 | FIXED | Live plugin instances retain their module library. | `8dab701ac` | `PluginScanCacheTest` | 8 | 0 | 0 / 0 / 0 / 0 |
| 2404 | FIXED | Rescans retire old descriptor storage instead of freeing pointers still held by browser rows. | `8fd27d5e1` | `PluginScanCacheTest` | 8 | 0 | 0 / 0 / 0 / 0 |
| 5051 | FIXED | Routing nodes are cleared inside the model-change guard before effects are deleted. | `71f388ded` | `RoutingGraphLiveTest` | 8 | 0 | 0 / 0 / 0 / 0 |
| 5430 | FIXED | The audio model-change guard now spans the full clip replacement and state restore loop. | `1ef0672e9` | `ArrangeToolsTest` | 8 | 0 | 0 / 0 / 0 / 0 |
| 5398 | FIXED | Stem-split jobs snapshot clip metadata and resolve the current target track before writing results. | `9c784aa30` | `StemSplitPipelineTest` | 8 | 0 | 0 / 0 / 0 / 0 |
| 3711 | FIXED | JACK port and temporary-buffer lists are cleared before a replacement client is initialized. | `d00cec781` | `AudioJackPortRestartTest` | 8 | 0 | 0 / 0 / 0 / 0 |
| 5704 | FIXED | MIDI undo and redo resolve the current port by stable track id instead of capturing a raw pointer. | `3f906efad` | `MidiReconnectTest` | 8 | 0 | 0 / 0 / 0 / 0 |
| new-p8-306 | FIXED | Broadcasts iterate a peer-id snapshot and re-look up each peer after sending. | `c9b297bb1` | `CollabTransportTest` | 8 | 0 | 0 / 0 / 0 / 0 |

Gate columns are, in order: complexity, file length, fork sources, and no upstream regression. All four returned exit 0 before each fix commit. The final report commit was also gated.

For new-p8-306, the live oversized-broadcast case did not fail on the old code in this Qt build, so the regression also asserts the ID-snapshot and fresh-lookup structure in `sendToProjectPeers()`. That invariant failed against the old range loop (CTest exit 8); the over-limit broadcast then passed with the fix (CTest exit 0).

Only the listed target tests were run, from `build/tests`; the full CTest suite was not run.

## Fix commits

- `8dab701ac` — fix(plugins): keep modules loaded by live instances (cluster 2403)
- `8fd27d5e1` — fix(plugins): keep cached descriptors alive after rescans (cluster 2404)
- `71f388ded` — fix(audio): clear routing nodes before destroying effects (cluster 5051)
- `1ef0672e9` — fix(versions): extend audio guard over clip replacement (cluster 5430)
- `9c784aa30` — fix(gui): snapshot stem split targets (cluster 5398)
- `d00cec781` — fix(audio): reset JACK ports after client shutdown (cluster 3711)
- `3f906efad` — fix(midi): resolve reconnect bindings by track id (cluster 5704)
- `c9b297bb1` — fix(collab): snapshot peers before broadcast (cluster new-p8-306)
