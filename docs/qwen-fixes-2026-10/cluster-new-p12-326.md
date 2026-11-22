FALSE-POSITIVE

new-p12-326: FALSE-POSITIVE

Finding: an empty connection list makes `AutomationClip::firstObject()` return null, so the dereference in the automation clip paint path crashes.

Verdict: the null-model claim is false. `AutomationClip::firstObject()` has two return paths, and both return a non-null pointer. The paint path's sampling loop already guards the `valuesAfter` null case. This is a documentation-only verdict; no product file is changed.

## Evidence

- `src/gui/clips/AutomationClipView.cpp:271-272`: `m_clip->firstObject()->minValue<float>()` and `->maxValue<float>()` dereference the result with no null check. This is the claimed crash site.
- `src/core/AutomationClip.cpp:286-298`: `AutomationClip::firstObject()`. Line 293 `return model;` is taken when `m_objects` is non-empty and its front is non-null. Lines 296-297 `static FloatModel fm(0, DEFAULT_MIN_VALUE, DEFAULT_MAX_VALUE, 0.001f); return &fm;` are the fallback taken otherwise. Both paths return non-null.
- `include/AutomationClip.h:239` and `:244`: the inline wrappers call `firstObject()->minValue` / `->maxValue`, so they reach the same fallback.
- `src/gui/clips/AutomationClipView.cpp:327`: `if (values == nullptr) { continue; }`, the existing sampling guard for `valuesAfter`.
- `docs/qwen-fixes-2026-10/cluster-new-p9-439.md:5`: "The crash the finding describes is not reachable: the no-target case never yields a null model."
- `docs/qwen-fixes-2026-10/cluster-new-p9-439.md:9`: "The null-model claim is false", with the fallback at `AutomationClip.cpp:296-297`.
- `docs/qwen-fixes-2026-10/cluster-new-p9-439.md:105`: "The null-dereference claim is FALSE-POSITIVE: `AutomationClip::firstObject()` never returns null."

## Commands and exit codes

Each command below wrote its output to a log under `/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-326.r4/logs/`.

1. `rg -n 'firstObject|values == nullptr' src/core/AutomationClip.cpp src/gui/clips/AutomationClipView.cpp include/AutomationClip.h` → EXIT=0 (`evidence-1.log`)
2. `sed -n '286,298p' src/core/AutomationClip.cpp` → EXIT=0 (`evidence-2.log`)
3. `sed -n '265,275p;320,330p' src/gui/clips/AutomationClipView.cpp` → EXIT=0 (`evidence-3.log`)
4. `sed -n '1,12p;95,110p' docs/qwen-fixes-2026-10/cluster-new-p9-439.md` → EXIT=0 (`evidence-4.log`)
5. `rg -n 'null-model claim is false|crash the finding describes is not reachable' docs/qwen-fixes-2026-10/cluster-new-p9-439.md` and `sed -n '105p'` on the same file → EXIT=0 (`evidence-5.log`). This confirmed the line numbers cited above.

The build and test runner was not run. The verdict rests on reading the code; no product change was made, so there was nothing to build for it.

## Not added: paint guard test

No paint guard test was added. A guard test for the paint path would pass with or without any p12-326 product change, so it cannot meet the reverse-RED rule (it must fail on the unfixed code and pass on the fix). For a false-positive verdict there is no fix to test.

## Separate units, not covered here

These were left out of this verdict and need their own units:

- Degenerate-range NaN: `y_scale = max - min` at `src/gui/clips/AutomationClipView.cpp:274` feeds `h` at `:275`. A zero range gives a non-finite `h`. This is a different defect from the null-model claim.
- Unlocked-map paint race: a paint-path read of a map without its lock. Not checked in this verdict; no line reference is given.
