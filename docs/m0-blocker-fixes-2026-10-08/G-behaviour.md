# Batch G-behaviour — 2026-10-08

Branch: `bugs/m0-blk-behaviour`, based on the provided train-2 tip `8eacc41d2`.

Test exit codes are from the focused `ctest -R` runs in `build/tests`. Gate exits are ordered complexity / file-length / fork-sources / no-upstream-regression. Each fixed item had all four gates exit 0 before its fix commit.

Cluster 4647's Windows drive-letter and UNC cases are compiled only on Windows. On this Linux host, reversing the path check still leaves the POSIX test green; the Windows-specific red cannot be produced here. Cluster 917 remains blocked: `runCliRender()` returns a completed render synchronously, and its callers immediately use that result to mutate clips/tracks and create undo state. Making it asynchronous requires a job/result lifecycle and corresponding changes across bounce, freeze, flatten, and range-bounce commands; a local change cannot safely report `landed` or preserve undo timing.

| Cluster | Verdict | Fix commit | Targeted test | Red exit | Green exit | Gates |
|---|---|---|---|---:|---:|---|
| 2492 | FIXED — close the previous Repeat span at retrigger | `769b4e3f4c27c238953f65396a68f02c2cccd063` | `SessionArrangementRecordTest` | 8 | 0 | 0/0/0/0 |
| 3982 | FIXED — refuse landing content that is not copied into the clip | `a6edc4ea544efca930d1147eabb9a4b02966eb0e` | `SessionGridRecordTest` | 8 | 0 | 0/0/0/0 |
| 4647 | FIXED — use platform-aware absolute-path validation | `c75b15093a11b06e40118cfd88894176cf5c1f5f` | `BounceInPlaceTest` | 0 (Windows path case unavailable on Linux) | 0 | 0/0/0/0 |
| 917 | BLOCKED — synchronous render result is consumed by several immediate model/undo mutations; see note above | — | — | — | — | — |
| 5424 | FIXED — hidden track rows no longer occupy hit-test space | `a6037addeaea4b3ead30430565e79ab9bd603170` | `ArrangerTimelineTest` | 8 | 0 | 0/0/0/0 |
| 5459 | FIXED — audio processing is quiesced while GUI-side VST3 re-prepare mutates host state | `51b528f7c` | `Vst3HostTest`; `Vst3EffectIntegrationTest`, `Vst3InstrumentIntegrationTest` also green | 8 | 0 | 0/0/0/0 |
| 5543 | FIXED — resolve CLAP bundle directories to their module binary | `b7146f4aea5c3914ea58bfcba7df09f2889ed183` | `ClapLoaderErrorTest` | 8 | 0 | 0/0/0/0 |
| 5074 | FIXED — advertise the supported STORE-only DAWproject import contract | `2da1b4500ca1a31f929d482e429fbe237a2db7de` | `TrackRenderMenuTest` | 8 | 0 | 0/0/0/0 |
| new-p8-165 | FIXED — loading preserves only persisted feedback-send flags | `5cf73dea63340f882d9b6925818f5cb652831de5` | `MixerRoutingBackwardCompatTest` | 8 | 0 | 0/0/0/0 |
| new-p8-175 | FIXED — reject Neural Amp models with a mismatched declared sample rate | `3d83d022dad6752d562019deb7ab13e100d536ae`; follow-up `71a36b4ff13f16422285c106f2529cefe1ff08bc` | `AudioPluginTest` | 8 | 0 | 0/0/0/0 |
| new-p8-200 | FIXED — transformed resample buffers no longer retain the source file path | `8077b2a143a2091ce674579c1b1cd5376745b7a9` | `SampleBufferResampleTest` | 8 | 0 | 0/0/0/0 |
| new-p8-265 | FIXED — persist exposed VST3 parameter values when state chunks are absent | `3749b602d38f7d163a4a17734fc049939ab0a1fd` | `Vst3EffectIntegrationTest`, `Vst3InstrumentIntegrationTest` | 8 | 0 | 0/0/0/0 |
| new-p8-299 | FIXED — selection deletion creates one journal undo step | `298da770cc321181c98a6c321b5375f33743fa22` | `ArrangerTimelineTest` | 8 | 0 | 0/0/0/0 |
| control-undo-stale-name | FIXED — identify unmatched journal steps without popping or renaming command records | `61212e169aaecfa469afec70d965a0b6f1249f47` | `UndoHonestyStateTest` | 8 | 0 | 0/0/0/0 |
