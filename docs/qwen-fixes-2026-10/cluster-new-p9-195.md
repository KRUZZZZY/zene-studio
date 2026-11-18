# Cluster new-p9-195

**Verdict: FALSE-POSITIVE on this branch.** The described `layerlevel` load path and
`AudioBusHandle::setGain` call do not exist here, so there is no behavior to fix and no
regression test was added.

## Evidence

| File and lines | What the code does |
| --- | --- |
| `src/core/InstrumentLayers.cpp:253-264` | Saves each layer as a child track under `<layers>`. |
| `src/core/InstrumentLayers.cpp:267-279` | Reads each child `<track>` with `Track::create` and adopts it; no layer level is parsed. |
| `src/core/InstrumentLayers.cpp:127-142` | Links each layer's volume, pan, and mute models to the owning track. |
| `src/tracks/InstrumentTrackNotes.cpp:326-342` | Delegates persistence to `InstrumentLayers::saveSettings` / `loadSettings`. |
| `src/tracks/InstrumentTrack.cpp:967-970, 1043-1046` | The track persists and restores its regular `vol` and `pan` models. |
| `include/AudioBusHandle.h:60-92`; `src/core/AudioBusHandle.cpp:127-142` | The handle has no `setGain` method; it applies the track's volume model. |

The source probes below found no `layerlevel` in `src` or `include`, no `setGain` declaration
or definition on `AudioBusHandle`, and no `std::clamp` in `InstrumentLayers.cpp`. The reported
XML attribute therefore cannot reach the claimed bus gain path in this checkout.

## Verification

| Check | Command / result | Tests / files |
| --- | --- | --- |
| RED | Not run: finding is absent on this branch; the procedure says to skip regression work for a non-real finding. | `InstrumentLayersCommandsTest`: not run. |
| GREEN | Not run: no product or test change was made. | Affected-family test count: not run. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-195.r1/logs/gate-3.log 2>&1; echo EXIT=$?` — `EXIT=0`. | — |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-195.r1/logs/gate-4.log 2>&1; echo EXIT=$?` — `EXIT=0`. | — |
| Files committed | `docs/qwen-fixes-2026-10/cluster-new-p9-195.md` | This report only. |

### Source probe commands

- `git grep -in 'layerlevel' -- src include` — exit 1 (no matches).
- `git grep -n 'setGain' -- include/AudioBusHandle.h src/core/AudioBusHandle.cpp` — exit 1 (no matches).
- `git grep -n 'std::clamp' -- src/core/InstrumentLayers.cpp` — exit 1 (no matches).

No build or ctest command was run because the finding is not present and no code or test changed.
