# M0 blocker fixes — bar4

| Cluster | Verdict | Fix commit | Test | Red exit | Green exit | Gate exits |
|---|---|---|---|---|---|---|
| bar4 | FIXED | `c09d68a16` | `ControlFreezeCommandsTranscript`, `ControlSuccessPaths`, `ControlGoldenAudio`, `ControlBounceRenderTest`, `ControlRenderJobTest` | 8: with the realistic CLI-suffix fake renderer and product fix absent, both targeted render tests failed; the pre-fix six-test run also had the three reported render integration failures | 0: exact six-test run passed 6/6 | complexity `--check` 0; file-length `--check` 0; fork-sources 0; no-upstream 0 |
| bar4 | FIXED | `78ac388f3` | `ReversibilityContractTest::theTableHistogramIsTheDocumentedOne` | 8: pre-fix six-test run rejected the stale documented histogram | 0: the same six-test run passed | complexity `--check` 0; file-length `--check` 0; fork-sources 0; no-upstream 0 |

## Findings

`BounceInPlace` staged renders as `output.wav.part.<uuid>`, while the CLI replaces the requested final suffix with the selected format extension. The child therefore wrote `output.wav.part.wav`, and the parent checked for the path without the final `.wav`, reporting “the render failed (exit 0).” The fix retains `.wav` as the final staged suffix. The synchronous and queued render paths share this preparation, so both contracts now work.

`tools/dawproject-proof.sh` measured the local no-Wasmtime table as `rows=823 true_inverse=411 snapshot=37 irreversible=24 not_mutating=351`. The release note includes the documented Wasmtime option block (+9 rows, +1 true_inverse, +3 snapshot, +5 not_mutating), so the recorded total is `rows=832 true_inverse=412 snapshot=40 irreversible=24 not_mutating=356`.

The green run was the exact six-test ctest selection from `build/tests`. `ControlGoldenAudio` used `LADSPA_PATH=$PWD/build/plugins/ladspa` so its bundled Root84 fixture could load the built `foverdrive_1196` plugin. Red and green build/test logs are in the supplied scratch directory.
