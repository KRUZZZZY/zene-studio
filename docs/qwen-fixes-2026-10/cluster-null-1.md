null-1: FIXED

# cluster null-1

**FIXED** — the offline render is no longer one sample late. The defect is fixed at its
measured cause in `Sample::play()`, with a permanent no-alignment regression test that is
bit-exact at 44.1 and 48 kHz and has a negative control, plus a recorded RED/GREEN proof.

**Scope.** This unit is `null-1` only. An earlier attempt in this branch also carried the
independent `new-p9-551` (render-tap buffer) and `new-p12-313` (bus registration) fixes;
the review flagged them as out of scope, and they were removed in follow-up commits (no
amend, no rebase): their product changes, header seams, tests, manifest/CMake entries and
report sections are gone. They are still real defects and belong in their own units;
nothing from them remains here. The branch now differs from base `2a7fd9f9c7` only in
`src/core/Sample.cpp`, the new null test, its registrations, and this report.

| item | detail |
|---|---|
| verdict | **FIXED** |
| product change | `src/core/Sample.cpp` (`Sample::play`, the ratio-1.0 branch) |
| regression test | `tests/src/core/AudioEngineNullRenderTest.cpp` (530 lines, 371 code lines) |
| commits | `f121ea2340` (the first fix) plus the scope-reduction, corrected-fix and report commits |

**Log location.** The given LOG FOLDER (`~/.local/share/zene-bunny-fix/…`) is not
writable in this session: writes there fail with `EACCES`, including `touch` on the
existing directories, and `chmod`/`setfacl` return 0 without changing it. All logs for
this unit therefore live in `$TMPDIR/logs-null-1/`
(`$TMPDIR=/home/kruzzzzy/.cache/zene-tmp/zene-qwen-null-1`), and nothing was left in
`/tmp`.

---

## The defect

A 4 s 32-bit float WAV on one sample track, clip at position 0, track volume 100, master
at unity, rendered 32-bit float at the source's own rate, came out **exactly one sample
late** at 48000 Hz and at 44100 Hz. Shifting the render by one made it bit-exact; with no
shift the residual peaked at about −2.8 dBFS. Two renders were identical, so it was
deterministic.

The finding's `file:line` was explicitly a starting guess, so the render was measured
first with `research/null-test-2026-10-28/null_test.py` (the harness from the original
report):

```
$TMPDIR/logs-null-1/01-null-test-baseline.log   EXIT=0
unity-48k-a: bitexact at lag 1, out_sha 40b44c5ab634
unity-48k-b: bitexact at lag 1, out_sha 40b44c5ab634   (deterministic)
unity-44k:   bitexact at lag 1, out_sha 461fa4295abf
vol99-48k:   bitexact False, residual -54.35 dBFS RMS   (negative control)
```

A probe of the first frames (`03-probe-first-frames.log`) then showed the output starts
with source frame 0 **duplicated**:

```
out[0]=s[0], out[1]=s[0], out[2]=s[1], out[3]=s[2], …
```

So engine frame 0 *is* master-mix frame 0 — the `AudioEngine` double buffer is not off by
one, and trimming the render would have hidden the cause. Direct probes of the linked
libsamplerate (`05-…` … `10-libsamplerate-*.log`) showed every converter mode delays at
ratio 1.0, and the linear one — the engine's default — emits the first input frame twice:

```
LINEAR ratio 1.0: gen=N used=N out: 0 0 1 2 3 4 …
```

`Sample::play()` sent **every** read through that converter, so a 1:1 read (a clip at the
source's own rate with default pitch) came back one sample late. That is the cause, at
`src/core/Sample.cpp:100-135`.

## The fix

`src/core/Sample.cpp:111-176`. When the composed ratio is exactly `1.0` the **output** is
the source's own frames, so the converter's delay cannot enter the render. Crucially, the
converter is still **driven over every frame the call consumes** — exactly once, in order,
through the same `m_bufferView` staging the non-unity path uses — and only its output is
discarded. The source frames it just consumed are copied to `dst` in place of that output.

Driving the converter is not incidental: it carries filter history and a phase, and a
later non-unity call resumes from whatever state it was left in. An earlier version of
this fix skipped the converter during unity and advanced the staging directly; that left
the converter behind the cursor, so a `.5 → 1.0 → .5` run produced different output after
the unity span (the round-3 review's finding). With the converter driven, the same
sequence hands it the same input history it would have had without the unity segment.

The converter is given the whole remaining block as its output capacity (as the non-unity
path does) and at most `numFrames` of input, so its `input_frames_used` can never exceed
the frames this call emits; a zero-consumption result stops the loop rather than spinning.
Every other ratio keeps the converter path below unchanged, with its exact arithmetic,
including `SrcQuality` selection and the mismatch-rate path. No output trimming, no latency
compensation, no change to the render pipeline.

## The regression test

`tests/src/core/AudioEngineNullRenderTest.cpp` builds the fixture project (one sample
track, one clip at position 0, master unity) and renders it through `ProjectRenderer`
exactly as the CLI does, comparing **frame 0 to frame 0** with `==` on the float bits at
48000 Hz and 44100 Hz. There is no cross-correlation, no lag search, no tolerance.

- The source's first frame is deliberately non-zero (`SampleFrame(0.5f, -0.25f)`) and the
  two channels carry different content, so a one-sample shift is a mismatch at index 0
  rather than a run of zeros that could pass unnoticed.
- The negative control is the same project at track volume 99 %: it must **not** null, or
  the equality above would be measuring nothing.

The file is 530 lines raw but **371 code lines**, under the gate's 500-code-line limit
(comments and blank lines are not counted), and no new function exceeds the complexity
target (the WAV reader is split into `walkFloatChunks` / `decodeFloatFrames` /
`readFloatWav`, and the transition oracle into small steps).

### Ratio transitions through unity

`ratioTransitionsThroughUnityPreserveConverterStateAndCursor` pins the round-3 regression
deterministically — no renderer, no timing. One `Sample` and one `PlaybackState` are
driven in fixed 256-frame calls: four at ratio 0.5, three at 1.0, four at 0.5 again (the
source is at the engine's rate, so a ratio argument of 1.0 is the real unity case and 0.5
is a genuine 1:2 resample on either side). The oracle is an **independent reference**: a
bare `AudioResampler` replayed with exactly the per-call input counts and ratios that
`Sample::play` used, whose output during the unity span is what the fix discards. The
post-unity 0.5 output must match that reference, and the cursor must stay within one
staging block of the frames the converter consumed.

The comparison is a state check, not a bit-identity check: the reference drives the
converter once per call while `Sample::play` fills a whole block, so their frame partition
differs slightly and they can differ by round-off in the converter's own interpolation
(measured peak `1.4e-4`, bound `1e-3`). The reviewed bug is four orders of magnitude
larger: with the skip-the-converter variant restored, the same case reports peak
`0.355633` at frame 0.

## RED / GREEN proof

Reverse **only** the product hunk (`src/core/Sample.cpp`), keep the test unchanged,
rebuild, run; then restore and run again.

RED:

```
git diff -- src/core/Sample.cpp > $TMPDIR/logs-null-1/fix-1.patch    # 56 lines, EXIT=0
git apply -R $TMPDIR/logs-null-1/fix-1.patch                         # EXIT=0
/usr/bin/cmake --build build --target AudioEngineNullRenderTest -j4  # EXIT=0  (20-red-proof-build.log)
ctest -R '^AudioEngineNullRenderTest$'                              # EXIT=8  (21-red-proof-run.log)
FAIL!  : unityClipAtPositionZeroRendersItsSourceBitExactly()
         the 48000 Hz unity render is not its source: 9598 mismatched samples,
         first at frame 1 (render 0.5/-0.25, source 0.278782/-0.399834),
         peak residual 0.221218
Totals: 3 passed, 1 failed, 0 skipped, 0 blacklisted
```

GREEN, restored:

```
git apply $TMPDIR/logs-null-1/fix-1.patch                            # EXIT=0
/usr/bin/cmake --build build --target AudioEngineNullRenderTest zene -j4  # EXIT=0  (22-green-proof-build.log)
ctest -R '^AudioEngineNullRenderTest$'                               # EXIT=0  (23-green-proof-run.log)
100% tests passed out of 1
```

The focused proof on the final tree (corrected fix + transition case):

```
git diff 2a7fd9f9c7 -- src/core/Sample.cpp > $TMPDIR/logs-null-1/fix-r3.patch  # 79 lines
git apply -R $TMPDIR/logs-null-1/fix-r3.patch         # EXIT=0
/usr/bin/cmake --build build --target AudioEngineNullRenderTest -j4  # EXIT=0  (150-red-build.log)
ctest -R '^AudioEngineNullRenderTest$'                # EXIT=8  (151-red-run.log)
FAIL!  : unityClipAtPositionZeroRendersItsSourceBitExactly()
         the 48000 Hz unity render is not its source: 9598 mismatched samples,
         first at frame 1 (render 0.5/-0.25, source 0.278782/-0.399834),
         peak residual 0.221218
FAIL!  : ratioTransitionsThroughUnityPreserveConverterStateAndCursor()
         the post-unity length changed (1024, reference 970)
Totals: 3 passed, 2 failed, 0 skipped, 0 blacklisted

git checkout -- src/core/Sample.cpp   (restores the committed fix)   # EXIT=0
/usr/bin/cmake --build build --target zene AudioEngineNullRenderTest -j4  # EXIT=0  (152-green-build.log)
ctest -R '^AudioEngineNullRenderTest$'                # EXIT=0  (153-green-run.log)
1/1 Test #14: AudioEngineNullRenderTest ........   Passed    0.53 sec
100% tests passed out of 1
```

The transition case was also checked directly against the **buggy** skip-the-converter
variant (the round-3 regression), which it must catch:

```
# buggy variant in place (converter skipped during unity)
/usr/bin/cmake --build build --target AudioEngineNullRenderTest -j4  # EXIT=0  (147-buggy-build.log)
./AudioEngineNullRenderTest ratioTransitionsThroughUnityPreserveConverterStateAndCursor
FAIL! ... the non-unity output after a unity segment is not what the converter produces
        for the same sequence: first difference at 0, peak 0.355633 (bound 0.001)
```

Affected family on the final tree (`159-family-run.log`, EXIT=0, **9/9**):
`AudioEngineNullRenderTest`, `AudioResamplerRatioTest`, `ClipFadesRenderTest`,
`SampleClipWindowTest`, `AudioEditingTest`, `AudioStretcherTest`, `AudioClipLoopTest`,
`ClipOffsetFadePairTest`, `SampleClipWarpTest`.

End-to-end, the original harness against the fixed binary:

```
$TMPDIR/logs-null-1/19-null-test-after.log   EXIT=0
unity-48k-a: lag 0, bit-exact True, out_sha da394a577843
unity-44k:   lag 0, bit-exact True, out_sha a08f11e55196
vol99-48k:   bit-exact False, residual -54.35 dBFS RMS   (negative control intact)
determinism (two renders identical): True
```

## Files changed

| file | why |
|---|---|
| `src/core/Sample.cpp` | the ratio-1.0 direct read |
| `tests/src/core/AudioEngineNullRenderTest.cpp` | new: the no-alignment null test + negative control |
| `tests/CMakeLists.txt` | registers the new test |
| `tests/fork-sources.txt` | registers the new test source |
| `tests/all-sources.txt` | registers the new test source (regenerated from its header recipe) |
| `tests/upstream-modifications.txt` | records the `Sample.cpp` divergence, same commit |
| `docs/qwen-fixes-2026-10/cluster-null-1.md` | this report |

No other product file differs from base. The new test is a fork-NEW source, so it is
listed in both manifests; `all-sources.txt` is derived from its own header recipe and
reproduces.

## Gates

`feature_check.py` runs eight gates. It can only write its log directory to
`<worktree>-feature-check/`, a sibling of the worktree, and this session's sandbox denies
every write outside the worktree — including that directory at mode 0777 (`touch` →
`EACCES`, and its own `probe.txt` is denied too). The gates were therefore run directly in
the worktree with the identical command, and then once more through the unmodified runner
with only its log path redirected into the ignored `build/` directory.

| gate | command | exit |
|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 0 hits in this unit's files |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 0 hits in this unit's files |

Through the unmodified runner on the reduced tree (log path redirected only,
`$TMPDIR/logs-null-1/124-feature-check.log`, exit 0):

```
feature-check zene-qwen-null-1 (base 2a7fd9f9c, 7 changed files)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

---

## Self-check

**Q: The regression test FAILS with your product change reverted and PASSES with it
(exit codes).**

Yes. With only `src/core/Sample.cpp` reverted (`git apply -R` of its diff, EXIT=0) and
the test untouched: build EXIT=0, `ctest -R '^AudioEngineNullRenderTest$'` **EXIT=8**,
9598 mismatched samples with the first mismatch at frame 1. Restored (`git apply`,
EXIT=0): build EXIT=0, ctest **EXIT=0**. Logs `20/21` and `22/23` under
`$TMPDIR/logs-null-1/`. The final tree, after the out-of-scope removals, was rebuilt and
re-run at **EXIT=0** (`110-r2-build.log`, `111-r2-green.log`).

**Q: The test is deterministic: no wall-clock timing, sleeps or races (name the seam or
injected values it uses).**

No timing of any kind. The test renders synchronously through `ProjectRenderer` at a
fixed sample rate and compares float bits. The values that make it discriminating are
injected, not timing-dependent: the source's first frame is `SampleFrame(0.5f, -0.25f)`
(non-zero, channels different), and the negative control is track volume 99 versus 100.
Output length is compared with `>=` against the source length, so the renderer's
whole-bar rounding cannot make the case flaky.

**Q: The fix is at the cause and complete: every caller and sibling of what you changed
was checked and stays consistent.**

`Sample::play()` is the single resampling entry point for sample playback, and every user
of the composed ratio goes through it: `SamplePlayHandle` (sample tracks and the pooled
metronome/frozen-take voices), `SessionAudioCells`, `SessionAudioBake` and
`SampleBufferResample`. None keeps a private converter, so a 1:1 read is now the source
for all of them — and, because the unity branch still drives that converter, none of them
can observe a converter left behind by a unity span either. A clip whose pitch or warp
ratio moves through 1.0 is the case the round-3 transition regression pins. Non-unity ratios — including `SrcQuality::Sinc*` and pitched or warped
clips — still take the converter path unchanged, which `AudioResamplerRatioTest` pins
(`theConvertersProduceDifferentSamples`,
`theExportQualityReachesTheResamplerThroughSamplePlay`,
`sampleTrackVoiceUsesPreparedConverterOnRenderPath`). The family run
(`$TMPDIR/logs-null-1/25-family-run.log`) covered `AudioResamplerRatioTest`,
`ClipFadesRenderTest`, `SampleClipWindowTest`, `AudioEditingTest`, `AudioStretcherTest`,
`AudioClipLoopTest`, `ClipOffsetFadePairTest` and `AudioEngineNullRenderTest`: **8/8
pass, EXIT=0**.

**Q: No collateral damage: old projects and saved data still load as before, and any
limit or check you added does not hit other callers that legitimately exceed it.**

No project XML, save/load path or control command changed; the fix is one branch in one
playback function. `loadProject` + `ProjectRenderer` is exactly what the new test
exercises, and old projects still load. Default renders are unchanged in every case
except the one that was wrong: a unity clip at the source's rate now renders as its
source, while the end-to-end harness keeps `determinism: True` and its −54.35 dBFS
negative control. The unit adds **no limit or threshold at all** — there is nothing new
that another caller could legitimately exceed.

**Q: Does the unity branch leave the converter in step for a later non-unity call?**

Yes — that was the round-3 finding, and it is fixed and pinned. The first version skipped
the converter during unity and advanced `m_bufferView` directly, so the next `.5` call
resumed from stale filter state and a stale input position; on the reviewer's probe a
`.5 → 1.0 → .5` run gave checksum 33.13 where the base implementation gave 83.45. The
unity branch now drives the converter over every frame it consumes (same staging, same
call order) and replaces only its output, so the converter's history and the source cursor
advance exactly as they would without the unity segment. The new
`ratioTransitionsThroughUnityPreserveConverterStateAndCursor` case compares the post-unity
output against a bare-converter replay of the same per-call counts and ratios (bound
`1e-3`; this fix measures `1.4e-4`, the buggy variant `0.355633` at frame 0) and checks the
cursor stays within one staging block of the frames consumed. That case is part of the
RED proof above: it fails with the product change reversed and passes restored.

**Q: If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the
intended contract that shows it.**

Not applicable — the verdict is FIXED, with the RED/GREEN pair above. The one claim that
could have been FALSE-POSITIVE, that `null-1` was an `AudioEngine` double-buffer
off-by-one, was disproved by measurement before any code changed: engine frame 0 equalled
master-mix frame 0 (`03-probe-first-frames.log`), and the duplicated frame came from the
unity-ratio converter (`05…10-libsamplerate-*.log`). The finding's `file:line` was
explicitly a guess, so the fix was placed at the measured cause.

**Pre-submit check.** `submit_check.py` (unmodified; only `FEATURE_CHECK_LOGS`
redirected into the ignored `build/feature-check/`) on the reduced tree reports
`RESULT: PASS - submit`, exit 0: commits on the branch, clean tree, report with the
verdict word, the test change, the CMake registration and all eight gates PASS. The one
`WARN` is the optional `scope_guard` module not importable from the temporary shim
directory, not a finding about this patch.

**Q: Are the out-of-scope `new-p9-551` and `new-p12-313` changes really gone?**

Yes. Follow-up commits (no amend, no rebase) restored `include/AudioEngine.h` and
`src/core/AudioEngine.cpp` to their base content, restored
`tests/src/core/AudioEngineNullRenderTest.cpp`, `tests/CMakeLists.txt` and
`tests/upstream-modifications.txt` to the null-1 commit's content, deleted
`tests/src/core/AudioBusRegistrationTest.cpp`, and removed it from `tests/fork-sources.txt`
and `tests/all-sources.txt`. `git diff 2a7fd9f9c7 --stat` now lists only the files above.
The other two findings remain real and are worth their own units; they are simply not
this one.
