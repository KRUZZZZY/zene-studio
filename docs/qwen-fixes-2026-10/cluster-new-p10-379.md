# cluster new-p10-379 — SID cycle-count multiplication can overflow

**Verdict: FALSE-POSITIVE** — the multiplication at `plugins/Sid/SidInstrument.cpp:302`
cannot overflow in this product: the frame count it multiplies is bounded by the
audio engine's period size, which is capped at 256 frames, far below the 2,180
frames the finding says are needed.

## The finding

> `clockrate * frames` is evaluated as `int` before division. It exceeds `INT_MAX`
> at 2,180 frames. ... when: Process a period of at least 2,180 frames.

## Evidence — the code is exactly as described, but unreachable

1. `plugins/Sid/SidInstrument.cpp:45` — `C64_PAL_CYCLES_PER_SEC = 985248`.
   `985248 * 2180 = 2 147 840 640 > INT_MAX (2 147 483 647)`, so the finding's
   arithmetic is right **for frames ≥ 2180**.

2. `plugins/Sid/SidInstrument.cpp:298` — the multiplicand is
   `const f_cnt_t frames = _n->framesLeftForCurrentPeriod();` (line 302 is
   `int delta_t = clockrate * frames / samplerate + 4;`).

3. `src/core/NotePlayHandle.cpp:392-399` —
   `framesLeftForCurrentPeriod()` returns
   `std::min(framesLeft(), Engine::audioEngine()->framesPerPeriod() - offset())`
   on the first period and `std::min(framesLeft(), framesPerPeriod())` otherwise
   (`offset()` is the note's in-period offset, `0 ≤ offset < framesPerPeriod`,
   `include/NotePlayHandle.h:111-116` `noteOffset()`). Either way
   `frames ≤ framesPerPeriod`.

4. `src/core/AudioEngine.cpp:78-82` — the period is fixed once in the
   constructor:
   `m_framesPerAudioBuffer = clamp(config "audioengine/framesperaudiobuffer", MINIMUM_BUFFER_SIZE, MAXIMUM_BUFFER_SIZE)`
   and `m_framesPerPeriod = std::min(m_framesPerAudioBuffer, DEFAULT_BUFFER_SIZE)`.
   `include/AudioEngine.h:59-61`: `MINIMUM_BUFFER_SIZE = 32`,
   `DEFAULT_BUFFER_SIZE = 256`, `MAXIMUM_BUFFER_SIZE = 4096`.
   So `framesPerPeriod ≤ 256` for every configuration, and a grep for
   `m_framesPerPeriod =` (any reassignment) over `src/` returns **no matches** —
   there is no `setFramesPerPeriod` either (grep `setFramesPerPeriod` → 0 matches).

5. Worst reachable product: `985248 × 256 = 252 223 488 < INT_MAX`. The
   overflow needs `frames ≥ 2180`; the product can never hand `playNoteImpl`
   more than 256 frames, because the cap is inside
   `framesLeftForCurrentPeriod()` itself — not just in the mixer's buffer size.
   Even a test cannot reach it: the only source of `frames` is that accessor,
   and nothing raises `framesPerPeriod` above 256.

So the finding describes real C++ arithmetic that would overflow **if** a
2,180-frame period existed, but no such period exists in this product (the
engine clamps the period to `DEFAULT_BUFFER_SIZE = 256`). There is no
user-visible behaviour to change, and no deterministic regression can be
written against the original code — the pre-fix path is unreachable, so a
"RED" run cannot be produced. Changing the line would be a behaviour-preserving
no-op for every reachable input; per the rules ("change only the buggy
behaviour"), nothing is changed.

## Commands run (copied, with the exit codes I saw)

```
$ bash tests/complexity-gate.sh --check > "$LOGS/gates-1.log" 2>&1; echo EXIT=$?
COMPLEXITY_EXIT=0
$ bash tests/file-length-gate.sh --check >> "$LOGS/gates-1.log" 2>&1; echo EXIT=$?
FILELENGTH_EXIT=0
```

(`$LOGS` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-379.r1/logs`.)

No build/test regression was run: the verdict is FALSE-POSITIVE, so there is
no RED/GREEN pair — the reachability proof above (items 1–5, all current-tree
file:line) is the evidence instead. No product file was changed, so nothing is
registered in `tests/fork-sources.txt` or `tests/upstream-modifications.txt`.

## Files touched

- `docs/qwen-fixes-2026-10/cluster-new-p10-379.md` (this report, only).
