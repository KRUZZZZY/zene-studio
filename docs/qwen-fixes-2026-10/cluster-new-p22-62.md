Verdict: FALSE-POSITIVE

# Cluster new-p22-62: range.paste dereferences a possibly null created clip

The null return is real in one place: `TrackFolder::createClip()` returns `nullptr` (`src/tracks/TrackFolder.cpp:179-182`). It is not reachable from `range.paste`. A folder cannot be the destination of a paste that dereferences the null, because of two invariants the code already enforces.

## Evidence (file:line, on branch `bugs/qwen-new-p22-62`)

1. **The paste path is gated by track type before `createClip`.** `pasteAt()` skips a clipboard entry unless the destination track's type equals the entry's recorded source type (`src/core/ControlCommandsRange.cpp:321`: `if (copied.trackIndex >= tracks.size() || tracks.at(copied.trackIndex)->type() != copied.type) { continue; }`). Only then does the code reach `createClip` (`:325`) and the unguarded dereferences (`:326`, `:329`, `:331`).

2. **A folder destination always fails that gate.** `TrackFolder` is constructed with `Track::Type::Folder` (`src/tracks/TrackFolder.cpp:98-99`). The clipboard's `type` is the source track's type, set in `copyRange()` from `tracks.at(i)->type()` (`src/core/ControlCommandsRange.cpp:308`). `copyRange()` only records clips found on a track (`:302-309`). So `copied.type == Folder` would need a clip on a folder track.

3. **A folder never owns clips.** Clips are registered with their track only by the `Clip` constructor and copy constructor (`src/core/Clip.cpp:73` and `:119`, both `getTrack()->addClip(...)`). Every construction site passes a clip-bearing track:
   - `new PatternClip(this)` / `new MidiClip(this)` / `new SampleClip(this)` / `new AutomationClip(this)` in the four `createClip` overrides (`src/tracks/PatternTrack.cpp:149`, `InstrumentTrack.cpp:1093`, `SampleTrack.cpp:320`, `AutomationTrack.cpp:61`).
   - `new MidiClip(firstTrack)` where `firstTrack` is an `InstrumentTrack*` (`src/gui/editors/PianoRoll.cpp:5850`, `:5859`, `:5891`).
   - `new SampleClip(track)` where `track` is `new SampleTrack(container)` (`src/core/StemTrackBuilder.cpp:83`, `:92`).
   - `new SampleClip(track, ...)` where `track` is `dynamic_cast<SampleTrack*>` (`src/core/SampleClip.cpp:361`, `:377`); the `new SampleClip(*source)` at `:377` copies a clip that already lives on its track.
   The loader agrees: `claimClipOrPreserveChild()` (`src/core/Track.cpp:170`) already handles a null `createClip` (`:189-192`) by preserving the element as unclaimed, with the comment "TrackFolder::createClip() returns null - cannot claim the element" (`:184-188`). So a malformed `<clip>` under a folder is preserved, not added to `m_clips`.

4. **Consequence.** A folder has an empty `m_clips`, so `copyRange()` never records a `Folder` clipboard entry. The type gate at `:321` then rejects every folder destination. `pasteAt()` never calls `createClip` on a folder, so the null return cannot be dereferenced from `range.paste`, `range.duplicate`, or the arranger paste (`range.paste` is the only caller of `pasteAt`, `:344` and `:355`).

The finding's stated failure needs `createClip` to return null on a track that accepts the paste. Neither the type gate nor the folder invariant allows that.

## Why no code change and no new regression test

- A null-check fix here would be dead code on every reachable path. A regression test would need a folder that owns a clip, which the code never builds. Building that state would need a test seam into an invariant the product does not allow, so the test would not model a real user path.
- The verdict is "not real", so per procedure step 2 I skipped to the report. No product file changed, so there is no RED/GREEN pair and no step 4b patch.

## Commands run and results

Build tree was configured. The first build attempt failed on a ccache temp-dir permission error (`ccache: error: failed to create temporary file for .../dsh/home/ccache-tmp-r6/...: Permission denied`, see `logs/001-build-rangeedits.log`, EXIT=2). The retry set `CCACHE_TEMPDIR` to a writable directory under `~/.cache/zene-tmp`, with no code change:

| # | Command | EXIT | Log |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target RangeEditsTest > logs/001-build-rangeedits.log 2>&1` | 2 (ccache permission error, not a compile error) | `logs/001-build-rangeedits.log` |
| 2 | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-p22-62 cmake --build build --target RangeEditsTest -j4 > logs/002-build-rangeedits.log 2>&1` | 0 | `logs/002-build-rangeedits.log` |
| 3 | from `build/tests`: `ctest -R RangeEdits --output-on-failure > logs/003-ctest-rangeedits.log 2>&1` | 0 (`100% tests passed out of 1`) | `logs/003-ctest-rangeedits.log` |

Test family `RangeEdits`: 1 test ran, 1 passed. The test covers the paste path on a non-folder track (`tests/src/core/RangeEditsTest.cpp:126`, `copyPasteAndDuplicate`).

Gates and completion check are below in `## Self-check`.

## Not checked

- Other `createClip` callers that dereference the result without a type gate (for example `src/core/TrackVersions.cpp:90`, `src/gui/tracks/TrackContentWidget.cpp:533`, `:627`, and `src/core/Track.cpp:1193`) were not analysed for folder reachability. They are outside this finding. Unverified.

## Self-check

Filled in after running `feature_check.py` and `submit_check.py`; see the completion section below.
