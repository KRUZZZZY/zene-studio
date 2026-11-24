FALSE-POSITIVE

# Cluster new-p23-105 - "GigPlayer loadSample allocates its read buffer as a stack VLA sized to the whole block"

## Verdict

The line the finding names is real C++ that builds a variable-length stack array
(`plugins/GigPlayer/GigPlayer.cpp:1380`), so part of the premise is true. But the two claims that
carry the finding's severity are both wrong on this branch:

1. **It is not sized to the render block.** Its size comes from the `samples` argument, and the one
   and only caller passes a fixed 256-frame member buffer, not the render block.
2. **It cannot be very large.** The maximum is 6 bytes per frame, so the buffer is at most
   256 * 6 = 1536 bytes. It is a small, constant-bound stack buffer.

The prescribed fix ("allocate the buffer on the heap ... and free it deterministically") would
introduce the exact defect the project forbids: a malloc/free pair per load on the audio thread
(`GigSample::m_sourceBufferView.empty()` at line 1123 is the refill condition, and it is true at
least once per rendered block).

## Evidence

### 1. The only call site passes a fixed 256-frame chunk, never the render block

| File:line | Code | What it establishes |
| --- | --- | --- |
| `plugins/GigPlayer/GigPlayer.cpp:1125` | `loadSample(sample, sample.m_sourceBuffer.data(), sample.m_sourceBuffer.size());` | the only call to `loadSample` in the tree (`rg -n loadSample` returns the declaration at `GigPlayer.h:595`, this call, and the definition at `GigPlayer.cpp:1352`) |
| `plugins/GigPlayer/GigPlayer.h:219` | `std::array<SampleFrame, DEFAULT_BUFFER_SIZE> m_sourceBuffer;` | the chunk is a fixed-size array, never resized |
| `include/AudioEngine.h:77` | `constexpr f_cnt_t DEFAULT_BUFFER_SIZE = 256;` | `.size()` is the compile-time constant 256 |
| `plugins/GigPlayer/GigPlayer.cpp:1379-1380` | `unsigned long allocationsize = samples * sample.sample->FrameSize;` / `int8_t buffer[allocationsize];` | the VLA is that product |

`m_sourceBuffer` is only ever read through `.data()`, `.size()`, range-for, or assigned to a span
(`GigPlayer.cpp:1125,1127,1132,1133,1134,1140,1154`); nothing resizes or swaps it. So `samples` is
always 256.

### 2. "A large per-period frame count" cannot reach this function

`src/core/AudioEngine.cpp:491` caps the period at the same constant:

```
, m_framesPerPeriod(std::min(m_framesPerAudioBuffer, DEFAULT_BUFFER_SIZE))
```

A configured `framesperaudiobuffer` larger than 256 is clamped, so the render block itself is at
most 256 frames. Even if the engine rendered more, the value handed to `loadSample` is the voice's
own 256-frame chunk, not the period.

### 3. What the buffer size actually is (measured this session)

`FrameSize` is libgig's "size (in bytes) of one single sample point" (`/usr/include/libgig/DLS.h:466`),
i.e. `Channels * (BitDepth / 8)`, so 2..6 for GIG samples (1-2 channels, 16-24 bit).

I measured every tracked fixture in `tests/data/gigproof/`
(`logs/probe-framesize.log`, probe built and run this session):

```
dc.gig                 Channels=1 BitDepth=16 FrameSize=2  allocationsize=512 bytes
keyboard-dimension.gig Channels=1 BitDepth=16 FrameSize=2  allocationsize=512 bytes
layered12.gig          Channels=1 BitDepth=16 FrameSize=2  allocationsize=512 bytes
one-frame-release.gig  Channels=1 BitDepth=16 FrameSize=2  allocationsize=512 bytes
pingpong.gig           Channels=1 BitDepth=16 FrameSize=2  allocationsize=512 bytes
sine440.gig            Channels=1 BitDepth=16 FrameSize=2  allocationsize=512 bytes
single-frame-loop.gig  Channels=1 BitDepth=16 FrameSize=2  allocationsize=512 bytes
```

512 bytes today; 1536 bytes for the worst legal GIG format (stereo 24-bit, which
`PluginPortsMigrationTest::gigplayer24BitSamplePlaysAtFullScale` at
`tests/src/plugins/PluginPortsMigrationTest.cpp:4098` exercises). Neither is a stack-overflow risk.
By comparison, `GigSample` already holds two 256-frame `SampleFrame` arrays (8 KiB) by value, so the
~0.5 KiB staging buffer is not the dominant cost of the call either.

### 4. The compiler confirms `samples` is the constant 256

`loadSample` is inlined into `playImpl`. In the built object
(`build/plugins/GigPlayer/CMakeFiles/gigplayer.dir/GigPlayer.cpp.o`, `logs/evidence-disasm.log`):

```
3106:  mov    $0x100,%ecx          <- samples argument = 256, materialised as a constant
311b:  call   ...                  <- inlined loadSample body
314e:  addq   $0x100,0x68(%r14)    <- sample.pos += m_sourceBuffer.size()
315a:  mov    $0x100,%ebp          <- ADSR inc loop, 256 iterations
3178:  movq   $0x100,0x10b8(%r14)  <- m_sourceBufferView length = 256
```

and the VLA itself is `(samples * FrameSize + 15) & ~15` bytes off the stack:

```
1019:  imul   %r13,%r9             <- r9 = FrameSize (0x40(%rdi)) * samples (r13)
1031:  lea    0xf(%r9),%rsi
103c:  and    $0xfffffffffffffff0,%rsi
1043:  sub    %rsi,%rsp            <- the variable-length stack adjust
```

So the array is genuinely variable-length, and its length is provably the constant 256 frames times
a format-dependent frame size of at most 6.

### 5. Build/portability status of the line

The plugin builds clean today under the project's own flags
`-std=gnu++20 -Wall -Werror` (`build-1.log`: zero warnings, exit 0). The array is a GNU extension,
accepted because the project compiles with extensions on (`gnu++20`), which is a project-wide
choice, not a GigPlayer defect. See "Other findings" for the residual standards nit.

## Why this report carries no regression test

Definition-of-done item 2 asks for a test that fails without the product change and passes with it.
No such test exists for this finding, and inventing one would be a wrong test (procedure step 4b):

- The defect claimed is a *stack size*, and the observable behaviour - the rendered audio - is
  identical whichever way the staging buffer is declared. A test that renders a GIG voice returns
  the same samples before and after any re-declaration of `buffer`, so it would pass without the
  fix and prove nothing.
- The language-level property (VLA vs. fixed array) is a compile-time fact, not a runtime one. A
  test could only check it by reading the source text, which is a gate, and gates must not be
  added or re-anchored in a bug-fix job.
- I chose not to change product code, because the only honest change here (a fixed-size array)
  would be an untestable cosmetic edit to code that is already correctly sized and that must not be
  touched outside the finding. Readers who want that edit will find the exact one-liner under
  "Other findings", left undone on purpose.

`submit_check.py` treats a claim-only closure as acceptable when the verdict is FALSE-POSITIVE and
no product file changed; this commit changes only this report.

## Test matrix (why no path is exercised)

- normal path: `playImpl` -> `loadSample` with `samples = m_sourceBuffer.size() = 256` (evidence 1, 4)
- failure path: short `Read()` zero-fills the tail; `gigplayerReadForwardLoop` and
  `loadBidirectionalSample` both `memset` the unread tail, writing at most `samples * FrameSize`
  bytes (`GigPlayer.cpp:1276-1279`, `1338-1344`) - no writer overruns the buffer
- exhaustion path: voice/note pool exhaustion is a separate subsystem (`acquireVoice`,
  `requestVoicePoolGrowth`) and does not change `samples`
- reconfiguration path: a changed `framesperaudiobuffer` is clamped to `DEFAULT_BUFFER_SIZE` at
  `AudioEngine.cpp:491`, so reconfiguration cannot enlarge `samples` either
- audio-thread constraints: `loadSample` runs on the audio thread (called from `playImpl`, a
  declared audio path in `tests/rt-safety-scope.txt:179`); the fix the finding proposes would break
  it, the current code does not allocate
- supported-platform build: builds clean on this box under `-std=gnu++20 -Wall -Werror`

## Commands run and exit codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p23-105.r1/logs`.

| # | Command | Exit |
| --- | --- | --- |
| 1 | `cmake --build build --target gigplayer -j4 > <logs>/build-1.log 2>&1` (first attempt was killed by a 120 s tool timeout; rerun completed) | 0 |
| 2 | `c++ -std=gnu++20 -O1 -isystem /usr/include/libgig probe.cpp -o probe -lgig` (my throwaway probe in `$TMPDIR`, first version failed with a bad libgig API call of my own) | 0 |
| 3 | `./probe tests/data/gigproof/*.gig \| tee <logs>/probe-framesize.log` | 0 |
| 4 | `nm -C GigPlayer.cpp.o; objdump -d -C --no-show-raw-insn GigPlayer.cpp.o ... > <logs>/evidence-disasm.log` | 0 |
| 5 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > <logs>/gates-1.log 2>&1` | 0 |

Gate table (`gates-1.log`):

```
feature-check kads-new-p23-105 (base fa4a5f2e7, 506 changed files; logs in .../build/feature-check)
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

Not run: no `ctest -R` family, because no product or test file changed. `PluginPortsMigrationTest`
is not built in this lane's tree (the `GigPlayer24BitFullScale` ctest entry reports "Could not find
executable .../build/tests/PluginPortsMigrationTest"), which is another reason no runtime test
could be added here without first building that target.

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes).**
  Not applicable: the verdict is FALSE-POSITIVE and no product change exists, so there is no
  reverse-patch pair to run. The reasoning is in "Why this report carries no regression test"; the
  artifact in `proof.json` is the required `code-reference` instead of a `regression-test`.
- **The test is deterministic: no wall-clock timing, sleeps or races.** Not applicable for the same
  reason. The measurements recorded here are deterministic: a `FrameSize` read from static fixtures
  and a constant folded by the compiler, with no timing involved.
- **The fix is at the cause and complete: every caller and sibling checked.** There is no fix.
  Callers and siblings checked anyway: the single call site (`GigPlayer.cpp:1125`), and every writer
  that fills the buffer - the non-loop `Sample::Read` path (`GigPlayer.cpp:1407-1408`),
  `gigplayerReadForwardLoop` (`GigPlayer.cpp:1254-1283`, writes at most
  `samples * frameSize`), `loadBidirectionalSample` (`GigPlayer.cpp:1286-1347`, same bound), and
  `readSampleSegment`/`repeatSampleFrame`. All are bounded by the same `samples`, so the buffer is
  exactly the right size for its use and no writer overruns it.
- **No collateral damage: old projects and saved data still load, and any limit I added does not
  hit other callers.** No product file changed, so no behaviour, project format or limit changed.
  The 256-frame bound reported here is a pre-existing project invariant
  (`AudioEngine.cpp:491`, `GigPlayer.h:219`), not a limit I introduced; it does not reject anything,
  because the resampler loop in `playImpl` refills the 256-frame chunk as often as the block needs
  (`GigPlayer.cpp:1119-1156`).
- **FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.** Evidence 1-4
  above: `GigPlayer.cpp:1125` + `GigPlayer.h:219` + `include/AudioEngine.h:77` +
  `AudioEngine.cpp:491` bound the buffer at `256 * FrameSize` bytes, measured at 512 bytes for every
  tracked fixture and 1536 bytes for the worst legal format, and the built object shows the compiler
  passing the constant `$0x100` for `samples`. The finding's "very large ... stack overflow" premise
  and its "allocate on the heap" remedy are both refuted by that same evidence.
- **Sibling commands in the command files you changed.** Not affected, because I changed no command
  file. `submit_check.py` computes its changed set as `git diff <merge-base HEAD FIX-BASE>..HEAD`,
  which on this branch spans 507 files inherited from the train (`base=fa4a5f2e785c`), including
  `src/core/ControlCommands*.cpp`. My own commit is exactly one file -
  `git show --stat 34255b99e4` reports `docs/qwen-fixes-2026-10/cluster-new-p23-105.md | 217 +++++`,
  1 file changed. So `app.about`, `app.ask`, `arrangement.fit_tracks` and the rest of that list are
  train-owned code I did not touch, and no command id, command handler, reversibility class or
  `commands_snapshot.json` entry changed in this job. The same applies to the checker's "tests added
  or changed" line: the files it names (`tests/control-audio-test-tone.py` and friends) come from
  that inherited range, not from me - my commit adds no test, for the reason given above.

## Other findings

1. `plugins/GigPlayer/GigPlayer.cpp:1380` is a genuine VLA - a GNU extension, not standard C++20. It
   compiles here only because the project builds with `-std=gnu++20` (extensions on). If the project
   ever sets `CMAKE_CXX_EXTENSIONS OFF`, or builds this plugin with MSVC (clang/MSVC have no VLA),
   the file stops compiling. The one-line remedy is
   `std::array<int8_t, DEFAULT_BUFFER_SIZE * 6> buffer{};` (the 6 is libgig's maximum
   `Channels * BitDepth/8` for 2-channel 24-bit), keeping it off-heap and realtime-safe. I did not
   make that edit: it is a portability/standards change, not the defect reported, and there is no
   runtime behaviour a regression test could pin on it.
2. `plugins/GigPlayer/GigPlayer.cpp:1407-1408`: `unsigned long size = sample.sample->Read(&buffer,
   samples) * sample.sample->FrameSize;` followed by `std::memset(&buffer + size, 0, allocationsize
   - size)`. If libgig's `Read()` ever returned a negative value, `size` would wrap and both the
   memset length and its destination would be wild. It is not reachable with the current libgig
   (`Sample::Read` returns 0 on a short read), and it is not the finding's subject, so it is logged,
   not touched. Worth a defensive clamp in a future job.
3. The report is the only file in this commit; `tests/fork-sources.txt` and
   `tests/upstream-modifications.txt` needed no entry because no product file was added or changed.
