FALSE-POSITIVE

# cluster new-p21-116 — `ConvolutionReverbEffect::tailFrames` reads a `FloatModel` and the engine "off the audio thread's guard"

Finding: `plugins/ConvolutionReverb/ConvolutionReverb.cpp:220` (historical). "`tailFrames()` … reads
`m_controls.m_predelayModel.value()` (a `FloatModel`, **not atomic**) and
`Engine::audioEngine()->outputSampleRate()` … so it touches non-atomic model state and the engine without the
model-change guard, racing control-thread writes. When: pre-delay knob moved while playing → data race on the model
value / torn read."

Verdict: **FALSE-POSITIVE**. Both halves of the stated mechanism are refuted by the code in this tree: the model
value is read through a `std::atomic<std::uint64_t>` acquire load (the whole point of
`AutomatableModel::publishedValue()`), and the only caller of `tailFrames()` performs the identical engine-rate and
model reads on the same thread one line earlier. No product file was changed.

The current code, for reference — `plugins/ConvolutionReverb/ConvolutionReverb.cpp`:

```
257  std::size_t ConvolutionReverbEffect::predelayFrames(float rate) const
258  {
259  	return std::min(m_predelay[0].size() - 1,
260  		static_cast<std::size_t>(m_controls.m_predelayModel.value() * 0.001f * rate));
261  }
262  int ConvolutionReverbEffect::tailFrames() const
263  {
264  	const auto predelay = static_cast<int>(predelayFrames(Engine::audioEngine()->outputSampleRate()));
265  	return m_tail.load(std::memory_order_relaxed) + predelay;
266  }
```

## 1. The model read cannot tear: `FloatModel::value()` is an atomic acquire load in this tree

| file:line (this branch) | what it shows |
| --- | --- |
| `include/AutomatableModel.h:755` | `class LMMS_EXPORT FloatModel : public TypedAutomatableModel<float>` |
| `include/AutomatableModel.h:731` | `T value( int frameOffset = 0 ) const { return AutomatableModel::value<T>( frameOffset ); }` |
| `include/AutomatableModel.h:217` | `template<class T> inline T value( int frameOffset = 0 ) const` — with no controller connection it falls through to `:239` |
| `include/AutomatableModel.h:239` | `return castValue<T>( publishedValue() );` |
| `include/AutomatableModel.h:570` | `float publishedValue() const noexcept;` |
| `include/AutomatableModel.h:631` | `std::atomic<std::uint64_t> m_publishedValuePair{0};` — "Exact old/new values published by control writes, packed as two float bit patterns." |
| `src/core/AutomatableModel.cpp:897-900` | `float AutomatableModel::publishedValue() const noexcept` → `return unpackNewValue(m_publishedValuePair.load(std::memory_order_acquire));` |
| `src/core/AutomatableModel.cpp:904-908` | `publishValuePair()`: `m_publishedValuePair.store(packValuePair(oldValue, newValue), std::memory_order_release);` plus the revision bump |
| `src/core/AutomatableModel.cpp:162` | the constructor publishes through the same pair (relaxed), so every model has a valid published value from birth |

So a knob move on the control thread is a **release store** and the audio thread's read is an **acquire load** of one
8-byte word: a concurrent write yields the old float or the new float, never a blend. There is no plain
`m_value` read on this path; `float m_value` (`include/AutomatableModel.h:629`) is only the control side's own copy.
No lock and no allocation is involved either, so the read also satisfies the realtime rule.

Two honest caveats, neither of them the finding's scenario:

- `value()` also tests `m_controllerConnection` and walks `m_nextLink` (`include/AutomatableModel.h:220-237`), plain
  pointers that `setControllerConnection()` / `linkToModel()` write. A **pre-delay knob move** does not write them;
  that is the tree-wide, pre-existing read/write pair documented in `docs/MIDI-LEARN-RACE.md` §1
  (`src/core/AutomatableModel.cpp:488-498`), reached by every model read in every `processImpl`, and not something
  this override introduces or could fix.
- With a controller connected, `value()` takes `controllerValue()` (`src/core/AutomatableModel.cpp:755-780`) — the
  established automation read path of every device's render code, again not a property of `tailFrames()`.

## 2. The engine read is not introduced here, and it cannot race a device swap

| file:line | what it shows |
| --- | --- |
| `include/Effect.h:209-217` | `Effect::timeout()` — `const float samples = Engine::audioEngine()->outputSampleRate() * m_autoQuitModel.value() / 1000.0f;` (`:211`, same thread, same expression) … `const int tail = tailFrames();` (`:215`) |
| `src/core/Effect.cpp:407-431` | `handleAutoQuit()`, the only caller of `timeout()`: returns at `:410` unless auto-quit is enabled, and calls `timeout()` at `:421` only while the output is silent |
| `src/core/Effect.cpp:292`, `:369`, `include/AudioPlugin.h:291` | the process path that reaches `handleAutoQuit` — the audio thread |
| `include/AudioEngine.h:404-407` | `outputSampleRate()` = `m_audioDev != nullptr ? m_audioDev->sampleRate() : m_baseSampleRate` |
| `include/AudioEngine.h:974` | `AudioDevice* m_audioDev;` (plain pointer) |
| `include/AudioDevice.h:70-73`, `:226` | `sampleRate()` returns `m_sampleRate`, written by `setSampleRate()` (`:152`) at device open |
| `src/core/AudioEngine.cpp:1420-1431` | `setAudioDevice()` calls `stopProcessing()` **before** `doSetAudioDevice()` (the pointer swap/delete, `:1400-1418`) |
| `src/core/audio/AudioDevice.cpp:137-142` | `stopProcessing()` clears the running flag, stops the backend and `waitForOpenCallbacks()` |
| `src/core/audio/AudioDevice.cpp:158-169` | `stopProcessingThread()` → `thread->wait(30000)` (terminate fallback) |

`tailFrames()` adds nothing to that path: the base class reads `Engine::audioEngine()->outputSampleRate()` and a
`FloatModel` (`m_autoQuitModel`) two lines above the virtual call, and this plugin's own `processImpl` reads the same
engine rate at `plugins/ConvolutionReverb/ConvolutionReverb.cpp:292` and the same pre-delay model at `:294` every
block. Caching a pre-delay value inside this override would therefore remove **no** engine read and **no** model read
from the audio thread.

## 3. Every caller and every sibling was checked

- **Callers of `tailFrames()`** (fff `grep` over the worktree, query `tailFrames`): `include/Effect.h:215` is the only
  call expression in product code. All other product hits are the override declarations/definitions
  (`plugins/AuEffect/{AuEffect.h:65,AuEffect.cpp:145}`, `plugins/ClapEffect/ClapEffect.h:74`,
  `plugins/Vst3Effect/Vst3Effect.h:101`, `plugins/CarlaVst2Effect/CarlaVst2Effect.h:115`,
  `plugins/ConvolutionReverb/{ConvolutionReverb.h:82,ConvolutionReverb.cpp:262}`, `plugins/AuEffect/AuHost.cpp:473`)
  or unrelated locals/parameters (`include/RealtimeExport.h:84`, `plugins/GigPlayer/GigPlayer.cpp:1341`,
  `src/core/ControlCommandsClipProcess.cpp:136`). The project/export paths that extend a rendered tail pass their own
  `tail` seconds argument (`src/core/ControlCommandsClipProcess.cpp:134-136`), not `tailFrames()`.
- **Callers of `timeout()`**: `src/core/Effect.cpp:421` only.
- **Siblings' caching**: `plugins/AuEffect/AuEffect.cpp:145`, `plugins/ClapEffect/ClapEffect.cpp:178`,
  `plugins/Vst3Effect/Vst3Effect.cpp:191` cache their tail because a hosted plug-in's tail is known only after
  `prepare()` — there is no boundary that could give this effect a fresher value than the atomic read it already does.

## Why the `Effect.h` note does not make this a defect

`include/Effect.h:220-222` says "Read on the audio thread, so an override returns a cached value". The reason for the
note is realtime safety, and this override satisfies it: one relaxed atomic load, one atomic acquire load, one vector
size read (the line length, set once in the constructor at `plugins/ConvolutionReverb/ConvolutionReverb.cpp:171-172`
and never resized), and one plain device-rate read — no allocation, no lock, no wait, no logging, and no value that
can tear. Its caller does strictly more reading of the same kind immediately before it. The note asks for the
*cost* the finding assumes, not for a memory-ordering property that is missing.

## Test matrix (procedure step 3, for a verdict that does not fix)

There is no regression target and no test to write: a test of the claimed defect would have to observe a torn model
read or a lock on the audio thread — a race, which the procedure forbids as a test oracle — or observe *that a model
read happens*, which is not observable without adding a product seam. A test that pinned the "cached value" shape
instead (asserting that `tailFrames()` stops following the model until the next block) would pin one period of
staleness that this code does not have today, i.e. it would lower the behaviour to satisfy a note, so it is not added.
The existing area test `tests/src/plugins/ConvolutionFftwPlanScopeTest.cpp` already exercises `tailFrames()`
(`:205,212,295,302`); it is unchanged and was not needed for this verdict.

## Commands and EXIT codes

| command | EXIT | log |
| --- | --- | --- |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p21-116 > …/logs/gate-1.log 2>&1; echo EXIT=$? >> …/logs/gate-1.log` | 0 | `logs/gate-1.log` |

The gate table as it was printed (`logs/gate-1.log`):

```
feature-check kads-new-p21-116 (base 3436a808b, 0 changed files; …)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
EXIT=0
```

No build and no test were run before this commit: no product file changes, so there is nothing to compile and no
red/green pair to produce. The source facts above were read with the fff tools (`grep` queries `tailFrames`,
`publishedValue`, `m_publishedValuePair`, `inline f_cnt_t timeout`, `handleAutoQuit`, `stopProcessing`,
`outputSampleRate`) and `read_file` on the cited line ranges; `git status --porcelain` was empty at the start of the
session and only this report is added by the commit.

## Self-check

Answers to the completion check's questions for a non-feature unit:

1. **"The regression test FAILS with your product change reverted and PASSES with it."** No product change exists to
   revert — the verdict is FALSE-POSITIVE (item 1 above). The check's claim-only path applies; the independent
   verifier can settle it from the citations in §1-§2 without a test.
2. **"The test is deterministic: no wall-clock timing, sleeps or races."** No test was added, so no oracle depends on
   timing. The evidence is source lines, not a race observation.
3. **"The fix is at the cause and complete: every caller and sibling checked."** There is no fix. The caller set was
   enumerated instead (§3): `tailFrames()` has one caller (`include/Effect.h:215`), `timeout()` has one
   (`src/core/Effect.cpp:421`), and all five sibling overrides were read.
4. **"No collateral damage: old projects and saved data still load as before."** Nothing in `src/`, `include/` or
   `plugins/` changed, so no load path, limit or behaviour moved; only `docs/qwen-fixes-2026-10/` gained this file.
5. **"If ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) that shows it."** `include/AutomatableModel.h:631` +
   `:239` with `src/core/AutomatableModel.cpp:899` (atomic acquire read of the published pair) and
   `src/core/AutomatableModel.cpp:906` (release store of the same pair) refute the torn read;
   `include/Effect.h:211` + `:215` refute "the override is where the engine is read off the guard", because the base
   class reads it on the same thread one line earlier; `src/core/audio/AudioDevice.cpp:137-142` and
   `src/core/AudioEngine.cpp:1420-1431` show the device pointer/rate cannot be swapped under a running audio thread.

## Other findings

Not fixed here (outside this cluster):

- `Effect::timeout()` (`include/Effect.h:209-217`) and almost every `processImpl` read model values and the engine's
  device rate on the audio thread. That is the fork's accepted shape (atomic publish/acquire) — noted so the next
  patrol does not file it again as a race.
- `AutomatableModel::value()` reads `m_controllerConnection` and `m_nextLink` as plain pointers
  (`include/AutomatableModel.h:220-237`); the write side of that pair is the subject of `docs/MIDI-LEARN-RACE.md` §1.
  Tree-wide, not specific to this plugin.
