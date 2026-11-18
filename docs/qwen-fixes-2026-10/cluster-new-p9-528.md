# Cluster new-p9-528

## Verdict: NEEDS-CONTEXT

The finding is **real** and **not fixed** on this branch: `Xpressive::playNoteImpl()`
builds two `ExprFront` objects and one `ExprSynth` on the audio render path, and the
`ExprSynth` constructor compiles both expressions with ExprTk. No production change was
made because a correct fix requires a per-note precompiled voice pool, and the pool's
capacity and at-capacity behaviour are a product decision that the current contract does
not state. This is the same structural blocker that made the sibling finding
`cluster-new-p9-331` (Organic) a NEEDS-CONTEXT.

## Finding evidence

| Location | Evidence |
|---|---|
| `plugins/Xpressive/Xpressive.cpp:204-234` | `playNoteImpl` runs per active note on the render path. On the first period of a note (`if (!nph->m_pluginData)` at `:209`) it heap-allocates two `ExprFront` objects at `:211`/`:213` and one `ExprSynth` at `:232`. |
| `plugins/Xpressive/ExprSynth.cpp:532-565` | `ExprFront::ExprFront` allocates `ExprFrontData` with `new` (symbol table, expression, string, vectors). |
| `plugins/Xpressive/ExprSynth.cpp:707-744` | `ExprSynth::ExprSynth` runs `init_expression_step2` twice: `add_cyclic_vector` (heap `WaveValueFunction`/`WaveValueFunctionInterpolate`), `add_variable`, `setIntegrate` (heap `IntegrateFunction` with a `double[max_counters]`) and finally `e->compile()` at `:739`. |
| `plugins/Xpressive/ExprSynth.cpp:578-597` | `ExprFront::compile()` constructs an `exprtk::parser` and parses/builds the AST. |
| `tests/src/plugins/PluginPortsHarness.h:1008-1066` | `renderInstrumentBuffers` drives the real engine dispatch (`inst.playNote(&nph, span)` once per period) and counts allocations around it via `RealtimeAllocationWatch`. |
| `tests/src/plugins/PluginPortsMigrationTest.cpp:1450-1459` | The same harness already asserts zero render allocations for `freeboy`; `xpressive` is rendered in the same loop but passes `nullptr` for the counter. |
| `include/NotePlayHandle.h:77-80` | `NotePlayHandle::pluginDataStorage()` (512 bytes) is the core in-handle buffer used by Monstro for audio-path note state. It cannot hold ExprTk's internals, and it does not remove the per-note compile. |

### RED measurement (probe, not committed)

The harness assertion was temporarily extended to `xpressive` (the ternary at
`PluginPortsMigrationTest.cpp:1452` and the guard at `:1453`), then reverted. With the
original product code it reported **1903 allocations and 1903 deallocations** for one held
note:

```
FAIL!  : PluginPortsMigrationTest::migratedPluginsPreserveBehaviour() 'realtimeCounts.allocations == 0 && realtimeCounts.deallocations == 0' returned FALSE. (render allocations=1903, deallocations=1903)
```

That is the audio-thread compile/parse cost landing in the render callback, so the finding
is confirmed rather than a false positive.

## Why the fix is blocked (design decision required)

A compiled ExprTk expression cannot be shared between simultaneous notes: `variable_node`/
`ifunction` references are bound into the symbol table at `compile()` time, and the
per-note storage (`m_note_sample_sec`, `m_frequency`, `m_released`, `m_note_rel_sec`, the
`integrate` frame counter) plus the per-note constants (`key`, `bnote`, `srate`, `v`,
`tempo`) live inside each `ExprSynth`/`ExprFront`. Therefore a correct, allocation-free
render path requires a pool of **precompiled** voices, each with its own ExprFront pair.
Choosing that pool forces several unstated product decisions:

- **Capacity and exhaustion.** `NotePlayHandleManager` has no configured upper bound
  (`cluster-new-p9-331`), so a fixed pool either drops/steals notes at capacity or grows on
  the audio thread (which is the bug). The sibling Organic finding was accepted as
  NEEDS-CONTEXT for exactly this reason.
- **Compile cost.** One note already performs ~1900 internal allocations; precompiling a
  large pool (e.g. FreeBoy's 1024) is not viable because each entry is two full ExprTk
  parses. The pool must be small, which makes the at-capacity behaviour a normal-playback
  concern, not an edge case.
- **When to rebuild.** The pool must be rebuilt when the expression strings,
  interpolation flags or sample rate change. `m_outputExpression` is mutated by the GUI in
  `XpressiveView::expressionChanged()` with no plugin hook, and interpolation/sample-rate
  changes would need new rebuild signals — a behaviour-affecting lifecycle change.
- **Random seeds.** Each `ExprFrontData` takes a fresh `SimpleRandom::generator()` seed at
  construction; a reused precompiled voice would keep one seed across notes, changing the
  `rand`/`randv` output versus today.

Making any of these choices without a stated contract would change current playback
behaviour, so no fix or RED-only test was committed (matching the Organic precedent).

## Commands and results

Build and test output is under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-528.r1/logs`.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Baseline build | `cmake --build build --target zene xpressive PluginPortsMigrationTest ExprSynth ExprSynthAllocationTest -j4` | 0 | Built `zene`, `xpressive` and the test targets. Log `build-2-baseline.log`. |
| RED probe build | `cmake --build build --target PluginPortsMigrationTest -j4` | 0 | Probe build. Log `build-probe.log`. |
| RED probe | from `build/tests`: `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 8 | `render allocations=1903, deallocations=1903`. Log `test-probe.log`. |
| Affected family (probe reverted) | from `build/tests`: `ctest -R '^(PluginPortsMigrationTest\|ExprSynth\|ExprSynthAllocation)$' --output-on-failure` | 8 | `ExprSynth` and `ExprSynthAllocation` pass; `PluginPortsMigrationTest` fails only on the pre-existing `nonRealtimeLadspaEffectsAreInvalid` SWH Decimator fixture (`PluginPortsMigrationTest.cpp:899`), unrelated to Xpressive. Log `test-1-baseline.log`. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 1 | Fails at clean HEAD (6 pre-existing new-over-target functions such as `dotCentreAlong@tests/src/gui/XyPadPairTest.cpp`, `lmms::adjust@src/core/ControlCommandsMixerTools.cpp`); no baseline was modified. Log `gate-complexity.log`. |
| File-length gate | `bash tests/file-length-gate.sh --check` | 1 | Fails at clean HEAD (14 pre-existing over-500 files absent from the 7-line baseline, e.g. `src/core/TakeLane.cpp`); no baseline was modified. Log `gate-file-length.log`. |

`git status --short` is clean except for this report; no production or test source was
changed (`git diff --stat` empty before this file was added).

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p9-528.md`
