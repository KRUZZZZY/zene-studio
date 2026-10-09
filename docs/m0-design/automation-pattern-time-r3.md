# Automation render time for selected patterns — stage 3 round 3

**Branch:** `design/m0-automation-lockfree`  
**Cluster:** 5388  
**Scope:** review item (d), pattern-index time mapping only.

## Defect

`Song::collectAutomatedValuesForRender()` in `src/core/Song.cpp` receives local transport time when `Song::processAutomations()` is playing the PatternStore. It selects the right clip index but evaluates the clip at that local time. The previous path called `PatternStore::automatedValuesAt()`, which clamps the local time to the selected pattern length and adds `patternIndex * TimePos::ticksPerBar()` before `TrackContainer::automatedValuesAt()` evaluates clips (`src/core/PatternStore.cpp:239-252`). The same missing offset occurs when the Song collector descends through an arrangement `PatternClip`: it wraps the clip-local time, then evaluates PatternStore clips without the selected pattern's bar offset (`src/core/TrackContainer.cpp:470-490` describes the prior behavior).

## Chosen design

Keep pattern-local time for the arrangement clip's length and wrap calculation. Immediately before collecting or evaluating a selected PatternStore column, derive its legacy query time as `min(localTime, lengthOfPattern(index) * ticksPerBar) + index * ticksPerBar`. For pattern-editor playback, derive that query time from the collector's local input and selected `clipNum`. For a PatternClip in the Song, derive it from the already wrapped `patternTime` and the PatternTrack's index. Use the derived time for active-clip selection, absolute and relative automation evaluation, and nested value collection. Keep recording-clip discovery on its existing input time so recording behavior does not change in this narrow fix.

## Invariants and realtime argument

- The Song owns the preallocated clip and value arrays. Control-side track/clip edits still grow them under the audio change guard; this change adds no render-time storage and does not alter capacity or overflow behavior.
- The render thread computes the offset with stack `TimePos` values and integer arithmetic. PatternStore track traversal and clip/value evaluation continue to use the existing arrays. There is no allocation, mutex acquisition, wait, syscall, or Qt signal emission in the collector.
- Pattern editor playback and arrangement PatternClips use the same selected-pattern absolute time as `PatternStore::automatedValuesAt()`. Song-level automation continues to use Song time unchanged. Pattern-local wrapping remains local and occurs before the selected pattern offset is applied.
- Recording clip discovery, queue insertion, snapshot reads, model assignment order, relative-clip order, and the zero-allocation render contract remain unchanged.
- A regression renders a nonzero pattern index through both the Pattern editor and an arrangement PatternClip. Each path compares collector-applied model values and stereo samples bit-for-bit with a reference render driven by the old `PatternStore::automatedValuesAt()` mapping. The render probe checks zero allocations on warmed periods.

## Considered and rejected

- Passing the selected pattern's absolute time into the outer Song collector is rejected: arrangement PatternClip placement and wrap calculations are Song-local and must remain so.
- Adding the bar offset only to `AutomationClip::relTime` is rejected: clip selection, mute/position checks, relative automation, and nested value ordering all need the same query time used by the former container API.
- Replacing `PatternStore::automatedValuesAt()` with a render-time map call is rejected: its temporary map would reintroduce allocation into the audio path.
