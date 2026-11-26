FALSE-POSITIVE

# Cluster new-p12-165 - Multitap Echo work buffer assumes the construction-time period

The arithmetic the finding describes is there: `m_work` is allocated once with
`framesPerPeriod()` (`plugins/MultitapEcho/MultitapEcho.cpp:65`) and is then
written and read with `inOut.frames()` (`:157`, `:160`, `:175`, `:181`, `:183-187`).
What cannot happen is the state it needs - "the audio period grows while the
effect remains alive", or a block larger than `framesPerPeriod()`. The period is
a constant of one `AudioEngine`, an effect cannot outlive the engine that sized
it, and every production caller builds its block from the same
`framesPerPeriod()`. The allocation the finding asks for ("storage sized for the
supported maximum") is already what the constructor makes, because the period is
capped at `DEFAULT_BUFFER_SIZE` (256) no matter what the device buffer is.

## The finding

```json
{"cluster": "new-p12-165",
 "title": "Multitap Echo work buffer assumes the construction-time period",
 "file": "plugins/MultitapEcho/MultitapEcho.cpp", "line": "64",
 "what": "m_work is allocated for the current framesPerPeriod() and later indexed using inOut.frames() with no capacity check or resizing.",
 "when": "The audio period grows while the effect remains alive. Processing writes beyond m_work."}
```

## Where the code is, on this branch

- `plugins/MultitapEcho/MultitapEcho.cpp:65` -
  `m_work = new SampleFrame[Engine::audioEngine()->framesPerPeriod()];`
  (the historical line 64; the member is declared `plugins/MultitapEcho/MultitapEcho.h:80`).
- `plugins/MultitapEcho/MultitapEcho.cpp:122-190` - `processImpl()`. Every index
  into `m_work` comes from `inOut.frames()`: `runFilter(m_work, ..., inOut.frames())`
  at `:157` and `:172`, `writeSwappedAddingMultiplied(m_work, offset, inOut.frames(), ...)`
  and `writeAddingMultiplied(m_work, offset, inOut.frames(), ...)` at `:160` and `:175`,
  `m_buffer.pop(m_work)` at `:181`, and the wet mix loop at `:183-187`.

So the finding's reading of the code is correct. The question is reachability.

## 1. The period is a constant of one engine - nothing reassigns it

- `src/core/AudioEngine.cpp:487-491` - the constructor computes
  `m_framesPerAudioBuffer = std::clamp(config "audioengine/framesperaudiobuffer", MINIMUM_BUFFER_SIZE, MAXIMUM_BUFFER_SIZE)`
  and `m_framesPerPeriod = std::min(m_framesPerAudioBuffer, DEFAULT_BUFFER_SIZE)`.
  `include/AudioEngine.h:76-78` = `MINIMUM_BUFFER_SIZE 32`, `DEFAULT_BUFFER_SIZE 256`,
  `MAXIMUM_BUFFER_SIZE 4096`.
- `include/AudioEngine.h:539` - `f_cnt_t framesPerPeriod() const { return m_framesPerPeriod; }`
  is the only accessor and it is `const`; `include/AudioEngine.h:868` is the only
  declaration of the member, and it is not atomic and has no setter.
- Source-wide check with the fff tools (shell search is disabled in this lane):
  `m_framesPerPeriod` in `src/` matches only `src/core/AudioEngine.cpp:491` as a
  **write** (the constructor initialiser) plus nine reads (`:546-549`, `:855`, `:914`,
  `:919-920`, `:1127`); in `include/` it matches `:539`, `:737`, `:868` only. A search
  for `setFramesPerPeriod` matches no source file at all (only the older report
  `docs/qwen-fixes-2026-10/cluster-new-p10-379.md`).

So `framesPerPeriod()` cannot grow for a live engine: a different period means a
different `AudioEngine` object.

## 2. An effect cannot outlive the engine that sized it

- `src/core/Engine.cpp:72` is the only construction site of the engine in `src/`:
  `s_audioEngine = new AudioEngine( renderOnly );`. `Mixer` (which owns every
  mixer channel and therefore every effect inserted into one) is created right
  after it at `:74`.
- `src/core/Engine.cpp:120-121` - `Engine::destroy()` deletes the mixer at `:120`
  and the audio engine only after it, at `:121`. The mixer's channels (and their
  effect chains) are gone before the engine object is.
- The product initialises the engine once per process - `src/core/main.cpp:1479`,
  `:1626` (headless paths) and `src/gui/GuiApplication.cpp:215` (GUI). The config
  value the setup dialog writes (`src/gui/modals/SetupDialog.cpp:1118-1119`) is read
  by the next engine construction, and there is no runtime "restart the engine"
  path: `Engine::destroy()` is exit-time. So the period in force when the effect
  was constructed is the period in force for every block it ever processes.

## 3. Every production caller hands the effect exactly `framesPerPeriod()` frames

The plugin's entry point is `Effect::processAudioBuffer(AudioBus&)`, which calls
`processImpl(inOut.bus()[0], inOut.frames())` (`src/core/Effect.cpp:356`). Every
caller of the chain in `src/` passes a bus whose frame count was built from
`framesPerPeriod()`:

| caller | why the block cannot be larger |
| --- | --- |
| mixer channel inserts | `m_buffer` and `m_bus` are allocated with exactly `framesPerPeriod()` (`src/core/Mixer.cpp:384-385`); `MixerChannel::doProcessing()` reads `fpp = Engine::audioEngine()->framesPerPeriod()` (`:863`) and drives the chain with that same `m_bus` (`:782-799`); the surround wide-bus variant builds its bus with the same `frames` (`src/core/MixerSurroundMix.cpp:311-323`, gated at `:272-278`) |
| track buses | `m_bus` is rebuilt over `BufferManager::acquire()` (`src/core/AudioBusHandle.cpp:117-121`), whose size is `s_framesPerPeriod` - set from the engine's period by `BufferManager::init()` (`src/core/BufferManager.cpp:32-42`, called at `src/core/AudioEngine.cpp:546`); the chain runs at `src/core/AudioBusHandle.cpp:188` |
| routing-graph path | `EffectChain::rebuildRoutingGraph()` prepares the graph with `framesPerPeriod()` (`src/core/EffectChainPatcher.cpp:129`, `:187`), and `canProcessThroughGraph()` only routes when `m_graph->frames() == bus.frames()` (`src/core/EffectChain.cpp:464`); the rack's graph is built the same way (`src/core/Rack.cpp:110`) |
| clip-process command | builds its bus from `period = Engine::audioEngine()->framesPerPeriod()` (`src/core/ControlCommandsClipProcess.cpp:83`, `:91-92`) |

There is no path that passes an `inOut.frames()` bigger than the period: the
effect's block *is* the mixer block, and the mixer block is the period.

## 4. The plugin's own ring buffer is fixed to the same period, so the pop agrees

`RingBuffer` stores `m_fpp = Engine::audioEngine()->framesPerPeriod()` at
construction (`src/core/RingBuffer.cpp:113-116`, `:124-127`) and `pop()` copies
exactly `m_fpp` frames into its destination (`src/core/RingBuffer.cpp:200-205`).
In `MultitapEchoEffect` both numbers are read in the same constructor -
`m_buffer( 16100.0f )` at `plugins/MultitapEcho/MultitapEcho.cpp:62` and the
`m_work` allocation at `:65` - so the only period-sized write to `m_work`
(`m_buffer.pop( m_work )`, `:181`) fills exactly the frames that were allocated.
The finding's premise would have to break `RingBuffer` too.

## 5. The proposed remedy is the current code

The finding's own fix is "provide storage sized for the supported maximum".
`framesPerPeriod()` is `min(clamp(config, 32, 4096), DEFAULT_BUFFER_SIZE = 256)`,
so the supported maximum period is 256 and the constructor allocates
`framesPerPeriod()` frames of it. Allocating `MAXIMUM_BUFFER_SIZE` (4096) would not
widen any reachable case; it would only add about 30 kB of never-touched memory per
instance (4096 - 256 frames of `SampleFrame`). The other half of the suggestion -
"rebuild the effect when period size changes" - has no event to hook (see
section 1) and would put an allocation on the audio path, which the realtime rule
forbids.

## Siblings and the contract

The same construction-time sizing is the house pattern for every plugin with a
period-sized work buffer, and much of it is inherited upstream code:

- `plugins/CrossoverEQ/CrossoverEQ.cpp:67-69` allocates `m_tmp1`, `m_tmp2` and
  `m_work` with `framesPerPeriod()` and indexes them with `inOut.frames()`
  (`:137-165`). The identical lines are in the upstream reference copy,
  `tests/reference/CrossoverEQ/CrossoverEQ.cpp:67-69` - this fork did not introduce
  the pattern.
- `plugins/Bitcrush/Bitcrush.cpp:66` (`framesPerPeriod() * OS_RATE`) indexed with
  `inOut.frames() * OS_RATE` at `:195`; `plugins/LadspaEffect/LadspaEffect.cpp:433-461`;
  `plugins/Lv2Effect/Lv2Effect.cpp:66`.

The intended contract is written down where the accessor is:
`include/AudioEngine.h:534-539` - `framesPerPeriod()` is
"the internal buffer size used by audio plugins and other processing done within
the audio engine", i.e. the exact block an effect is handed. A per-plugin capacity
check would encode a different contract for one plugin and leave the other
period-coupled buffers with the same unreachable premise.

## Why there is no regression test (and no product change)

The procedure asks for a test that fails without the fix. The finding's state
cannot be produced from a test: there is no API that changes the period (no
setter, private non-atomic member, `include/AudioEngine.h:868`), and every host
path constructs its block from `framesPerPeriod()`. Writing to
`AudioEngine::m_framesPerPeriod` through a `#define private public` seam would
create a state no product path reaches, and the "old" behaviour in that state - a
write past a 256-frame allocation - is undefined behaviour with no deterministic
observable: this lane has no sanitizer build, so a test could neither fail before
nor prove a fix. A test asserting the current block sizes would pass with and
without any change, which the brief calls a wrong test.

Per procedure step 2 the item closes on the evidence above instead.

## Commands run (copied from the logs, exit codes as seen)

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-165.r1/logs`
(`$LOGS` below). The two evidence scripts are saved there, so every line cited above
can be re-printed.

```
$ bash $LOGS/evidence-1-source-lines.sh > $LOGS/evidence-1-source-lines.log 2>&1; echo EXIT=$?
EXIT=0
   (32 file/line ranges, each with its own `EXIT=0` inside the log)

$ bash $LOGS/evidence-2-git.sh > $LOGS/evidence-2-git.log 2>&1; echo EXIT=$?
EXIT=0
   branch bugs/qwen-new-p12-165; HEAD 3436a808b65a59d362b15837adc1308ea67bf675;
   base 050/m0-train-53 = 3436a808b65a59d362b15837adc1308ea67bf675; no files differ

$ FEATURE_CHECK_LOG_DIR=$LOGS/gate-logs python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py \
    /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p12-165 \
    > $LOGS/gates-1-feature_check.log 2>&1; echo EXIT=$?
EXIT=0
feature-check kads-new-p12-165 (base 3436a808b, 0 changed files)
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

Re-run after this report was committed (the gate now sees one changed file):

```
$ FEATURE_CHECK_LOG_DIR=$LOGS/gate-logs python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py \
    /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p12-165 \
    > $LOGS/gates-2-feature_check.log 2>&1; echo EXIT=$?
EXIT=0
feature-check kads-new-p12-165 (base 3436a808b, 1 changed files)
  PASS  fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace /
        no-tautology / doc-figures / complexity / file-length
RESULT: PASS

$ python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py \
    /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p12-165 new-p12-165 \
    > $LOGS/submit-1-check.log 2>&1; echo EXIT=$?
EXIT=0
  PASS  commits on the branch
  PASS  working tree clean
  PASS  report docs/qwen-fixes-2026-10/cluster-new-p12-165.md
  PASS  tests added or changed - claim-only closure: independent QA must verify its cited code/contract
  PASS  gates (feature_check.py)
RESULT: PASS - submit
```

Source queries were made with the fff tools (shell search is refused in this
session), not with shell commands; their results are quoted in sections 1-4 and the
lines they point at are in `evidence-1-source-lines.log`.

## Files touched

- `docs/qwen-fixes-2026-10/cluster-new-p12-165.md` (this report, only).

No product file, no test and no registration list changed: the verdict is
FALSE-POSITIVE, so there is nothing to register in `tests/fork-sources.txt` or
`tests/upstream-modifications.txt`, and `git status` is clean.

## Self-check

Every ASK from `submit_check.py` for this unit, answered with the evidence above.

1. **"The regression test FAILS with your product change reverted and PASSES with
   it (exit codes)."** - Not applicable and deliberately so: the verdict is
   FALSE-POSITIVE, there is no product change, and therefore no RED/GREEN pair.
   The finding's trigger cannot be produced from a test (no period setter, every
   block built from `framesPerPeriod()`; see "Why there is no regression test").
   The proof is the code and contract cited in sections 1-5, not a test run.
2. **"The test is deterministic: no wall-clock timing, sleeps or races."** - No test
   was added, so no timing, sleep or race exists in this change. The evidence is
   static (source ranges in `evidence-1-source-lines.log`) plus the gate run.
3. **"The fix is at the cause and complete: every caller and sibling of what you
   changed was checked and stays consistent."** - No code was changed. The caller
   sweep in section 3 covers every production entry point into an effect
   (`EffectChain` through the mixer bus, the track `AudioBusHandle`, the routing
   graph, the rack, and the clip-process command), and the sibling sweep covers
   `CrossoverEQ` (upstream-identical), `Bitcrush`, `LadspaEffect` and `Lv2Effect`,
   all of which size their work buffers the same way and would contradict the
   finding equally.
4. **"No collateral damage: old projects and saved data still load as before, and
   any limit or check you added does not hit other callers that legitimately exceed
   it."** - Nothing was changed: no serializer, no model, no command, no limit. Old
   projects load through the unchanged `MultitapEchoControls::loadSettings()`
   (`plugins/MultitapEcho/MultitapEchoControls.cpp:80-91`), which this report does
   not touch, and no caller's block size is clamped or rejected anywhere.
5. **"If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or
   the intended contract that shows it."** - FALSE-POSITIVE. Code: the period is
   written only at `src/core/AudioEngine.cpp:491` and read through the `const`
   accessor at `include/AudioEngine.h:539`; the only engine construction is
   `src/core/Engine.cpp:72` and `Engine::destroy()` removes the mixer before the
   engine (`src/core/Engine.cpp:120-121`); the mixer bus that carries every effect
   block is allocated with `framesPerPeriod()` (`src/core/Mixer.cpp:384-385`,
   `:863`) and reaches `processImpl` at `src/core/Effect.cpp:356`. Contract:
   `include/AudioEngine.h:534-539` defines `framesPerPeriod()` as the block size
   audio plugins are processed with, and `plugins/MultitapEcho/MultitapEcho.cpp:65`
   allocates exactly that - the supported maximum.

## Other findings

None raised: the ring buffer behind `m_work` is sized in milliseconds
(`m_buffer( 16100.0f )` = 16.1 s, `plugins/MultitapEcho/MultitapEcho.cpp:62`), which
covers the largest tap the controls allow (32 steps x 500 ms = 16 s,
`plugins/MultitapEcho/MultitapEchoControls.cpp:42-43`), and its offset writes wrap
inside the ring (`src/core/RingBuffer.cpp:329-347`).
