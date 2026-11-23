FALSE-POSITIVE

# Cluster new-p9-564 - "Recording allocates and grows a container for every captured block"

## Verdict

The finding quotes the code correctly and reasons about the wrong object. `AudioSampleRecorder::writeBuffer()`
does allocate a `SampleFrame[]` and append it to a `QList` (`src/core/audio/AudioSampleRecorder.cpp:87-97`),
but the class that owns it cannot be constructed - it is abstract - and the function is private, non-virtual,
and has no base declaration to dispatch through. Nothing in this tree can call it, so no recorder "ingests
blocks on the capture/render path" (`when` clause) and no recording allocates or grows that container.
The finding as stated is not a defect. No product code, storage design or test is changed by this report.

## Evidence (all read this session)

### 1. The class is abstract: a construction expression does not compile

| File:line | Code |
| --- | --- |
| `include/AudioSampleRecorder.h:41` | `class AudioSampleRecorder : public AudioDevice` |
| `include/AudioDevice.h:220` | `virtual void startProcessingImpl() = 0;` |
| `include/AudioDevice.h:221` | `virtual void stopProcessingImpl() = 0;` |

Neither pure virtual is overridden in `include/AudioSampleRecorder.h` (the class body ends at line 56 and
declares only ctor, dtor, `framesRecorded()`, `createSampleBuffer()`, `writeBuffer()`, `m_buffers`). The two
overrides are therefore missing and the type is abstract. Proved with the compiler rather than by reading,
using the exact flags CMake uses for `AudioSampleRecorder.cpp` (taken from `build/compile_commands.json`):

```
probe-a-construct.cpp: EXIT=1  logs/probe-a-construct.cpp.compile.log
  error: invalid new-expression of abstract class type 'lmms::AudioSampleRecorder'
  note: because the following virtual functions are pure within 'lmms::AudioSampleRecorder'
```

`probe-a-construct.cpp` is the entire program:

```cpp
#include "AudioSampleRecorder.h"
namespace lmms { int probe() { bool ok = false;
  AudioSampleRecorder *r = new AudioSampleRecorder(2, ok, nullptr); return r ? 0 : 1; } }
```

Control: the same flags compile a subclass that supplies the two pure virtuals, so the failure above is
abstractness and not a missing include, a flag or a stale header.

```
probe-b-control-subclass.cpp: EXIT=0  (logs/probe-b-control-subclass.cpp.compile.log)
```

### 2. `writeBuffer()` is private and non-virtual; no base call reaches it

| File:line | Code |
| --- | --- |
| `include/AudioSampleRecorder.h:50` | `private:` |
| `include/AudioSampleRecorder.h:51` | `void writeBuffer(const SampleFrame* _ab, const f_cnt_t _frames);` - no `virtual`, no `override` |
| `include/AudioDevice.h` (whole file) | no member named `writeBuffer` |

Two compile probes:

```
probe-c-call-private-writeBuffer.cpp: EXIT=1   (calls writeBuffer from a subclass body)
  error: 'void lmms::AudioSampleRecorder::writeBuffer(const lmms::SampleFrame*, lmms::f_cnt_t)' is private within this context
probe-d-base-has-no-writeBuffer.cpp: EXIT=1    (calls writeBuffer through an AudioDevice*)
  error: 'class lmms::AudioDevice' has no member named 'writeBuffer'
```

So there is no friend seam and no virtual dispatch: a caller would need both a public/friend access path and a
constructed object, and neither exists. This is also why the storage redesign the earlier plan proposed had
nothing to attach a test to - the seam the plan needed does not exist.

`writeBuffer` namesakes elsewhere in the tree are a different family and stay consistent:
`AudioFileDevice::writeBuffer` is the pure virtual of the export/disk writer
(`include/AudioFileDevice.h:53`, implemented by `AudioFileWave`, `AudioFileOgg`, `AudioFileMp3`,
`AudioFileFlac`), and `SampleRecordHandle::writeBuffer` is the live capture path (section 4).

### 3. Zero construction sites, zero call sites

* `rg -n --no-ignore 'AudioSampleRecorder' -g '!build' src include tests` (`logs/evidence-references-1.log`,
  EXIT=0) returns only: the two own source files, `tests/all-sources.txt:221,2364` (source-list ledger),
  `tests/coverage-baseline-all.tsv:746` (0.0 coverage), and `src/core/CMakeLists.txt:1349` (the build entry).
  The ctor/dtor hits are the declaration at `include/AudioSampleRecorder.h:44-45` and the definition at
  `src/core/audio/AudioSampleRecorder.cpp:35,47` - no caller.
* `rg -n --no-ignore '#include "AudioSampleRecorder.h"' -g '!build' .` returns one hit:
  `src/core/audio/AudioSampleRecorder.cpp:27`. No other translation unit includes the header, so no file can
  name the type at all.
* `createSampleBuffer()` has no caller: `include/AudioSampleRecorder.h:48` (declaration) and
  `src/core/audio/AudioSampleRecorder.cpp:69` (definition) only.
* No test references it: `rg -ln AudioSampleRecorder tests/` returns only `tests/all-sources.txt` and
  `tests/coverage-baseline-all.tsv`. The class has 0.0 coverage because nothing executes it.

History (`logs/evidence-history-1.log`, EXIT=0): the last construction site in this repository's history was
`AudioSampleRecorder * freeze_recorder = new AudioSampleRecorder(DEFAULT_CHANNELS, b, engine::mixer());` in
`src/core/song.cpp`, removed on 2014-02-19 by `8b84526dc5` "Pattern: removed freeze functionality", together
with `Mixer::setAudioDevice(freeze_recorder)`. `git log -S 'new AudioSampleRecorder' -- src include` returns
that commit and the 2007 rename before it, nothing later.

Correction to the inherited checkpoint, recorded because the evidence says so: the class was not made dead by
the D9c fix `0b3345b7c0`. It has been unreachable since the freeze removal in 2014, 12 years before D9c. D9c
removed the same *pattern* from the path that was live - see section 4.

The repository's own audit note agrees: `docs/CLIP-CAPTURE-DESIGN.md:154-155` records
"`AudioSampleRecorder` is a third, unwired path: it copies all `DEFAULT_CHANNELS` into allocated blocks
(`src/core/audio/AudioSampleRecorder.cpp:87-97` **[sweep]**) and has no caller outside its own file".

### 4. The live capture path no longer has the pattern the finding describes

| File:line | Code |
| --- | --- |
| `src/core/SampleRecordHandle.cpp:404-412` | `void SampleRecordHandle::writeBuffer(...)` -> `if (m_accum != nullptr) { m_accum->append( _ab, _frames ); }` |
| `include/SampleRecordAccumulator.h` | pre-allocated ring (`SampleFrameRingBuffer`) plus a drain thread; the take's `SampleBuffer` is built off the audio thread |

The body carries the D9c comment: "this used to be a per-period `new SampleFrame[_frames]` appended to a QList
that grew for the whole take. The accumulator stages the frames into one pre-allocated ring instead, so the
audio thread performs no allocation here." Commit `0b3345b7c0` (D9c paragraph, `logs/evidence-history-1.log`)
says the same. `tests/src/core/RecordingRealtimeTest.cpp:399` is the allocation probe for that path; rebuilt and
run this session, it passes (section 5). So even the live path, which the finding does not name, has no such
allocation - and the file the finding does name is not on any path.

## Brief (regenerated, without the inherited checkpoint)

Task: one finding, cluster `new-p9-564` alone. No batch: `new-p10-505`, `new-p14-406` and the uint64 overflow
item are not part of this job.

Finding: "Recording allocates and grows a container for every captured block" -
`src/core/audio/AudioSampleRecorder.cpp:89-97`, claimed `when`: "a recorder ingests blocks on the
capture/render path".

Scope: decide REAL / ALREADY-FIXED / FALSE-POSITIVE with file:line evidence, and stop there. No storage
redesign, no six-case matrix (no TSAN case, no INT_MAX case, no exhaustion-policy case - those belong to two
other findings), no product code unless the pipeline refuses a FALSE-POSITIVE.

Inheritance: both files the finding names - `include/AudioSampleRecorder.h` and
`src/core/audio/AudioSampleRecorder.cpp` - are inherited vendor LMMS source. This report changes neither, so
no ledger entry is due. If a later round changes either one (for example deleting the dead class with its
`src/core/CMakeLists.txt:1349` entry and its `tests/all-sources.txt` lines), that file is inherited and needs a
`tests/upstream-modifications.txt` entry with a reason in the same commit; the list file must end with a
newline.

## Commands run and EXIT codes

| Command | EXIT | Log |
| --- | --- | --- |
| `rg -n --no-ignore 'AudioSampleRecorder' -g '!build' src include tests` and the three companion searches | 0 | `logs/evidence-references-1.log` |
| `git log --oneline -S 'new AudioSampleRecorder' -- src include`; `git show 8b84526dc5 \| grep -A4 AudioSampleRecorder`; `git show --format='%B' --no-patch 0b3345b7c0 \| sed -n '32,42p'`; `sed -n '404,412p' src/core/SampleRecordHandle.cpp`; `sed -n '399,403p' tests/src/core/RecordingRealtimeTest.cpp` | 0 | `logs/evidence-history-1.log` |
| compile `probe-a-construct.cpp` with the real flags | 1 | `logs/probe-a-construct.cpp.compile.log` |
| compile `probe-b-control-subclass.cpp` | 0 | `logs/probe-b-control-subclass.cpp.compile.log` |
| compile `probe-c-call-private-writeBuffer.cpp` | 1 | `logs/probe-c-call-private-writeBuffer.cpp.compile.log` |
| compile `probe-d-base-has-no-writeBuffer.cpp` | 1 | `logs/probe-d-base-has-no-writeBuffer.cpp.compile.log` |
| summary of the four probes | 0 | `logs/evidence-probes-1.log` |
| `cmake --build build --target RecordingRealtimeTest -j4` | 0 | `logs/build-1.log` |
| `cd build/tests && ctest -R "^RecordingRealtimeTest$" --output-on-failure` | 0 (1/1 passed, 12.22 s) | `logs/test-1.log` |
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 (`RESULT: PASS`, 8 PASS rows, 0 FAIL) | `logs/feature-check-1.log` |
| `git add docs/qwen-fixes-2026-10/cluster-new-p9-564.md`, `git commit`, `git status --short`, `git rev-parse HEAD` | 0, 0, 0, 0 | `logs/git-record-1.log` (written after the first commit) and `logs/git-record-2.log` (written after the commit that carries this line; the labelled exit codes and SHA cannot appear inside the commit they describe) |

The probes are hand-compiled translation units in the log folder, not part of the build; they reuse the
`-D`/`-I`/`-isystem` flags CMake emits for `src/core/audio/AudioSampleRecorder.cpp`.

## What I did not do, and what is unverified

* No RED/GREEN pair and no regression test: there is no product change to revert, so a test would pass in both
  directions. The proof is a compile-time impossibility (probe A) plus zero call sites, which a runtime test
  cannot strengthen.
* No test matrix (normal, failure, exhaustion, reconfiguration, audio-thread constraint, platform rows): every
  row needs a code path into the class, and there is none - the construction expression is the probe (EXIT=1),
  not a test case. Adding rows would invent paths that the compiler rules out.
* No TSAN run (local TSan does not run on this box, and there is no concurrent code in scope), no INT_MAX case,
  no exhaustion-policy case - none of them apply to code that cannot be entered.
* Not verified at runtime, because no runtime path exists. That is the verdict, not a gap in it.
* The `docs/CLIP-CAPTURE-DESIGN.md:154-155` note was read, not re-audited.

## Self-check

1. **"The regression test FAILS with your product change reverted and PASSES with it (exit codes)."**
   No product change was made (FALSE-POSITIVE), so there is no such pair. `git show --numstat --format=''` on
   this job's commit is `200 0 docs/qwen-fixes-2026-10/cluster-new-p9-564.md` - one file, no source. The proof
   is `probe-a-construct.cpp` EXIT=1
   (abstract class, `logs/probe-a-construct.cpp.compile.log`) with the EXIT=0 control
   `probe-b-control-subclass.cpp`, plus zero call sites in `logs/evidence-references-1.log`.
2. **"The test is deterministic: no wall-clock timing, sleeps or races."**
   Nothing ran under a clock: the probes are compiler invocations on fixed sources. The one test I ran,
   `RecordingRealtimeTest`, is pre-existing and unchanged; it passed 1/1 in 12.22 s
   (`logs/test-1.log`) and proves the live capture path, not the finding.
3. **"The fix is at the cause and complete: every caller and sibling of what you changed was checked."**
   Nothing changed. The callers and siblings were checked anyway: zero construction sites and one `#include`
   for `AudioSampleRecorder`; `createSampleBuffer()` has no caller; the `writeBuffer` family
   (`AudioFileDevice` and its four overrides) is the export writer and is untouched;
   `SampleRecordHandle::writeBuffer` is the live capture path and already stages into
   `SampleRecordAccumulator` (`src/core/SampleRecordHandle.cpp:404-412`).
4. **"No collateral damage: old projects and saved data still load as before."**
   No production file changed, so no project, save file or caller is affected. No limit or check was added.
5. **"If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that
   shows it."**
   `include/AudioSampleRecorder.h:41` (abstract class, pure virtuals at `include/AudioDevice.h:220-221`),
   `include/AudioSampleRecorder.h:50-51` (private, non-virtual `writeBuffer`),
   `include/AudioDevice.h` (no `writeBuffer` member), `src/core/audio/AudioSampleRecorder.cpp:87-97` (the code
   the finding quotes, unreachable), last construction site removed by `8b84526dc5` (2014-02-19). Compile
   evidence: probes A/C/D EXIT=1, control B EXIT=0.

6. **"Sibling commands in the command files you changed - confirm each is consistent with your fix (or say why it
   is not affected): app.about, app.ask, app.help, ... arrangement.fit_tracks, arrangement.get_state."**
   I changed no command file and no command. `git show --numstat --format='' 1e76fd6122` is one line:
   `200 0 docs/qwen-fixes-2026-10/cluster-new-p9-564.md`. The check's sibling list comes from the branch's
   view of the train it was cut from (`merge-base` with `050/m0-train-35`, 429 files), not from this job; the
   same inherited diff is why its "tests added or changed" row is green without a test of mine. Every one of
   those commands is unaffected because this job changes no command, handler, schema or reversibility class.

## Other findings

`AudioSampleRecorder` is dead code: an abstract `AudioDevice` with two unused public methods, a private
`writeBuffer` and its own `QList` storage, built by `src/core/CMakeLists.txt:1349` and listed in
`tests/all-sources.txt:221,2364` and `tests/coverage-baseline-all.tsv:746`. Deleting it would remove a false
positive generator for patrols, but that is a cleanup outside this finding's scope and I did not do it.
