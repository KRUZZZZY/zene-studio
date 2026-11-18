# LV2 null-key blocker

## Root cause and fix

`ControlDeviceCatalogue::appendBuiltinDevices()` included every plugin descriptor in the generic builtin catalogue, including the `lv2instrument` host container. The container has sub-plugin features but no individual plugin URI. `ReversibilityTestSupport::addInstrumentTrack()` selected the first loadable device and instantiated this descriptor as a regular instrument, so `Lv2Instrument::Lv2Instrument()` received a null key and dereferenced it while reading the URI.

The LV2 constructor now rejects null, invalid, or URI-less keys with an error before using the key. `Lv2ControlBase` also rejects a missing LV2 manager cleanly. The generic catalogue omits sub-plugin host containers, while `addInstrumentTrack()` prefers `tripleoscillator`; `SessionTuningTest` now depends on that module.

The regression coverage checks that the builtin catalogue does not expose sub-plugin host containers and that null and invalid keys produce a `DummyPlugin` rather than crashing. To prove red, I restored only the prior LV2 constructor implementation, rebuilt, and ran the regression: it segfaulted (CTest exit 8). I restored the fix, verified it against the saved fixed source, rebuilt, and reran it successfully (exit 0).

## Verification

Build targets `zene`, `SessionTuningTest`, `ControlDeviceCatalogueTest`, `Lv2StateTest`, `PluginPortsMigrationTest`, and `InstrumentLatencyPdcTest` built successfully (exit 0). The focused key regression, `SessionTuningTest`, `ControlDeviceCatalogueTest`, `Lv2StateTest`, `PluginPortsMigrationTest`, and `InstrumentLatencyPdcTest` all passed. The latter includes the LV2 instrument latency checks.

An initial CTest invocation was denied permission to write its plugin scan cache under the user's Documents directory. I reran tests with `LMMS_PLUGIN_SCAN_CACHE` directed to the task scratch directory under `/tmp`; all requested tests then completed successfully.

| cluster | verdict | fix commit | test(s) | red exit | green exit | gate exits |
|---|---|---|---|---:|---:|---|
| `lv2-null-key` | FIXED | `60d2b555f3983eb6c21190e9ac89d691bc078153` | `Lv2InstrumentKeyRegressionTest` (red/green), `SessionTuningTest`, `ControlDeviceCatalogueTest`, `Lv2StateTest`, `PluginPortsMigrationTest`, `InstrumentLatencyPdcTest`; build exit 0 | 8 | 0 | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0; `git diff --check` 0 |
