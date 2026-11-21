# Cluster new-p9-501 — Vibed note-start allocation

**Verdict: BLOCKED.** The reported allocation is real. A behavior-preserving realtime fix needs one owner decision: define Vibed’s finite realtime state budget and its overflow behavior. This is the maximum delay-line size and simultaneous note-state capacity that the instrument promises to support, plus what happens when a note exceeds that budget. No code limit defines those values today. A finite pool without that contract changes which pitches or notes sound; an unbounded fallback keeps allocating on the render path.

No product code was changed. This report does not claim the bug is fixed.

## Code evidence

| Evidence | Code |
| --- | --- |
| `renderNextPeriod()` enters `RealtimeScope` before note setup and instrument rendering | `src/core/AudioEngine.cpp:914-930` |
| A note handle calls the track’s instrument while rendering | `src/core/NotePlayHandle.cpp:304-307`; `src/tracks/InstrumentTrack.cpp:697-701` |
| First Vibed render allocates `StringContainer`, then constructs each enabled string | `plugins/Vibed/Vibed.cpp:204-229` |
| Each `VibratingString` allocates output, impulse, delay-line objects, and delay-line arrays; the arrays scale with `stringLength` | `plugins/Vibed/VibratingString.cpp:37-68,71-80` |
| The string length is calculated from `m_oversample * sampleRate / pitch`; no length or pitch floor is checked here | `plugins/Vibed/VibratingString.cpp:46-47` |

Vibed supports nine strings (`plugins/Vibed/Vibed.h:71-72`), harmonic factors from 0.25 to 7.0 (`plugins/Vibed/Vibed.cpp:78`), oversampling values 1 to 16 (`:118-119`), and per-string detune of ±10% (`:114-115`). At the lowest harmonic, the delay line is four times longer than the note-frequency period would require at the fundamental.

## Tuning, pitch, note creation, and voice bounds

- Note keys and the track base note each range over 0..127 (`include/Note.h:92`; `src/tracks/InstrumentTrack.cpp:61`). The normal pitch path combines those with master pitch, note detuning, and track pitch (`src/core/NotePlayHandle.cpp:588-656`).
- Track pitch range is 1..60 semitones: `InstrumentTrack` initializes a 1..60 range (`src/tracks/InstrumentTrack.cpp:69`) and applies ±100 cents times that value (`:774-778`). Note detuning allows ±60 semitones (`include/Note.h:98`; `src/core/Note.cpp:600-608`). Master pitch allows ±12 semitones (`src/core/Song.cpp:110`), and MPE pitch allows ±4800 cents (`include/MpeExpression.h:52-53`; `src/core/NotePlayHandle.cpp:649-656`). These controls reach Vibed’s frequency without a safety clamp at the string constructor.
- The session tuning API accepts every finite positive frequency through the largest float value and sets no positive minimum (`src/core/SessionTuning.cpp:365-380`). `Microtuner::keyToFreq()` also derives its result from loaded keymap and scale data (`src/core/Microtuner.cpp:65-108`). Therefore the tuning paths do not establish a useful lower-frequency or maximum-buffer bound for a preallocated state.
- Pattern notes acquire `NotePlayHandle`s in `InstrumentTrackNotes::triggerNote()` (`src/tracks/InstrumentTrackNotes.cpp:186-215`), which runs from the render-period setup stage (`src/core/AudioEngine.cpp:929`). The handle starts with `m_pluginData == nullptr` (`src/core/NotePlayHandle.cpp:51-70`), and the Instrument interface’s per-note call is the play entry point (`include/Instrument.h:106-110`). Moving construction into handle creation would still be on this render path; the interface has no earlier per-note preparation hook.
- `NotePlayHandleManager` starts with 256 handles but extends by 16 whenever its pool empties (`include/NotePlayHandle.h:397-398`; `src/core/NotePlayHandle.cpp:718-749,762-775`). That is a growth increment, not a polyphony ceiling. Pattern notes use this same manager, so it provides no maximum number of concurrent Vibed states.

## Why the owner decision is necessary

A fixed pool must choose a maximum delay-line sample count and a maximum number of simultaneous note states. Session tuning has no positive frequency floor, and the handle manager has no voice ceiling. The pool cannot preserve the current domain at a finite practical size. If it rejects or clamps over-budget pitches, or silences notes when full, it changes sound. If it falls back to the current construction path, the realtime allocation remains.

Prebuilding only the outer `StringContainer` does not resolve the per-string buffer allocations. Prebuilding exact note states asynchronously cannot guarantee readiness for a note at its current-period offset; waiting or deferring changes the onset, while a fallback allocation preserves the bug. Reusing buffers also leaves the per-note initialization loops proportional to string length (`VibratingString.cpp:78-80,94-107`), so a maximum length is needed to bound that work.

The required owner decision is one resource policy for Vibed: its maximum realtime memory/work budget and the audible behavior when a note exceeds it. Once that policy is set, an implementation can use preallocated states and a registered allocation probe without silently inventing a pitch or polyphony limit.

## Verification record

| Check | Command/result |
| --- | --- |
| RED regression | Not run: this disposition is blocked pending the resource policy; no regression is claimed. |
| GREEN regression | Not run: no product fix was made. |
| Existing Vibed ctest discovery / family count | From `build/tests`: `ctest -N -R '^Vibed$'` exited 0 and listed 0 registered tests; 0 tests were executed. `tests/reference/Vibed/Vibed.cpp` is compiled into the `partc_ref_vibedstrings` reference module (`tests/CMakeLists.txt:2439,2566-2575`), not registered as a ctest. This is an unavailable test family, not a passing test result. |
| Complexity gate | `bash tests/complexity-gate.sh --check` exited 1. Log: `gate-complexity-2.log`. It reports six CCN regressions in files outside this cluster’s changes; the gate ends with `FAIL: complexity ratchet regressed`. |
| File-length gate | `bash tests/file-length-gate.sh --check` exited 1. Log: `gate-filelength-2.log`. It reports 14 files over 500 lines outside this cluster’s changes; the gate ends with `FAIL: file-length ratchet regressed`. |
| Product build | Not run; no product source or header changed. |

The exact gate invocations captured the unpiped status as:

```sh
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-501.r3/logs/gate-complexity-2.log 2>&1
status=$?
printf 'EXIT=%s\n' "$status"
```

```sh
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-501.r3/logs/gate-filelength-2.log 2>&1
status=$?
printf 'EXIT=%s\n' "$status"
```

The test discovery command was run from `build/tests`:

```sh
ctest -N -R '^Vibed$' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-501.r3/logs/test-discovery-1.log 2>&1
status=$?
printf 'EXIT=%s\n' "$status"
```

No RED or GREEN command was run because this report takes the blocked path, rather than claiming a behavior fix. The ctest discovery found no registered Vibed test.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p9-501.md` — this BLOCKED evidence report.
