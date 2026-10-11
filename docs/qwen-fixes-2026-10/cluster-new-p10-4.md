# Cluster new-p10-4

**Verdict: FALSE-POSITIVE**

The finding names `src/core/ClipAudioLoop.cpp:77`, which is not present in the
tracked source tree. Searches also find none of the reported `audioLoopStart`
or `audioLoopFrames` fields. The only `engaged()` helper in this area is
`prefades::engaged` in `src/core/ClipPreFades.cpp:79`; it tests clip prefades.

The current sample-clip project loader reads different attributes,
`srcin`/`srcout`, at `src/core/SampleClip.cpp:629-635`. Those values pass through
`SampleClip::setSampleWindow` (`src/core/SampleClip.cpp:448-459`), which clamps
them to the sample buffer bounds before `installWindow` stores them
(`src/core/SampleClip.cpp:483-492`). This is a separate sample-window feature and
does not reproduce the reported audio-loop overflow.

## Evidence

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Tracked finding path | `git ls-files src/core/ClipAudioLoop.cpp` | 0 | No output; file is not tracked. |
| Source path | <code>rg --files src &#124; rg '^src/core/ClipAudioLoop\.cpp$'</code> | 1 | No matching source file. |
| Reported fields | `git grep -n -e audioLoopStart -e audioLoopFrames -e audio_loop_start -e audio_loop_frames -- src include tests` | 1 | No matching identifiers. |
| `engaged()` definitions/calls | `git grep -n 'engaged(' -- src include` | 0 | Matches are confined to `ClipPreFades`. |
| RED regression | Not run; the reported code and behavior are absent, so step 2 skips regression work for a non-real finding. | N/A | No regression test added. |
| GREEN regression | Not run for the same reason. | N/A | No fix to verify. |
| Affected-family test count | No family could be identified; ctest was not run. | N/A | 0 tests executed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-4.r1/logs/gate-3.log 2>&1; gate_rc=$?; printf 'EXIT=%d\n' "$gate_rc"; exit "$gate_rc"` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-4.r1/logs/gate-4.log 2>&1; gate_rc=$?; printf 'EXIT=%d\n' "$gate_rc"; exit "$gate_rc"` | 0 | Passed. |

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p10-4.md`
