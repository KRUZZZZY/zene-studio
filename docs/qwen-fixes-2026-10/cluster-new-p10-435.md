# Cluster new-p10-435 — VST2 instruments and effects appear in both browser categories

Verdict: **FALSE-POSITIVE** (the described symptom cannot occur in this tree; the proposed
fix has no mechanism here and would invent behavior).

No production code was changed. No test was added (procedure step 2 → step 6).

## Finding

- file: `plugins/VstEffect/VstSubPluginFeatures.cpp`, line 62 (`// TODO: eval m_type`)
- claim: "The scan ignores `m_type` and adds every discovered DLL or SO to the key list";
  symptom: "VST2 instruments and effects appear in both browser categories".

## Why the symptom cannot occur

1. `VstSubPluginFeatures` is instantiated exactly once in the whole tree, always with
   `Plugin::Type::Effect`:
   - `plugins/VstEffect/VstEffect.cpp:58` — `new VstSubPluginFeatures( Plugin::Type::Effect )`
     inside the `vsteffect_plugin_descriptor` (`Plugin::Type::Effect`, line 55).
   - A tree-wide search for `VstSubPluginFeatures` finds only these three files
     (`VstEffect.cpp`, `VstSubPluginFeatures.cpp/.h`) plus registries/translations — no
     `Plugin::Type::Instrument` instantiation exists.

2. The instrument browser only iterates Instrument descriptors, so this scan is never
   reached from the instrument category:
   - `src/gui/PluginBrowser.cpp:274` — `auto descs = getPluginFactory()->descriptors(Plugin::Type::Instrument);`
     then line 296-302 calls `desc->subPluginFeatures->listSubPluginKeys(...)` only for those.
   - The VST2 instrument host is Vestige, and its descriptor carries `nullptr` for
     subPluginFeatures: `plugins/Vestige/Vestige.cpp:65-85`
     (`Plugin::Type::Instrument` at line 73, trailing `nullptr,` at line 84). Vestige has no
     `listSubPluginKeys` at all (grep for `SubPluginFeatures` under `plugins/Vestige`: no matches);
     it loads plugins via a file dialog instead.

3. The effect browser likewise iterates Effect descriptors only:
   `src/gui/modals/EffectSelectDialog.cpp:72` — `descriptors(Plugin::Type::Effect)`.

So the scan runs only for the effect browser (`m_type` is always `Effect`); nothing from it
can appear in the instrument category. "Appears in both browser categories" is not reachable.

## Why the proposed fix is not implementable as a minimal change

"Inspect each plugin's type during discovery and filter by `m_type`" requires a VST2 type
probe, which this tree does not have:

- `plugins/VstBase` has no `isInstrument` anywhere (grep `isInstrument|m_type` under
  `plugins/VstBase`: no matches). VST2 loading is out-of-process:
  `src/core/OutOfProcessHosting.cpp:88-94` — "VST2 has no in-process path in this tree:
  VstPlugin::tryLoad() starts the remote VST client unconditionally".
- The formats that DO filter by `m_type` use per-format class-listing helpers that report
  `isInstrument`: `plugins/Vst3Effect/Vst3SubPluginFeatures.cpp:101-107`
  (`vst3::listClasses`, `if (info.isInstrument != wantInstrument) continue;`) and
  `plugins/ClapEffect/ClapSubPluginFeatures.cpp:95-101` (`clap::listClasses`). VST2 has no
  equivalent helper; adding one would mean a new out-of-process probe protocol — inventing
  behavior, not a behavior-preserving fix.

## The file is unmodified upstream code

- `plugins/VstEffect/VstSubPluginFeatures.cpp` is NOT listed in `tests/upstream-modifications.txt`
  (grep `VstSubPluginFeatures` in that file: no matches) — the fork inherited it as-is, and the
  `// TODO: eval m_type` comment is upstream LMMS's own known limitation.
- The fork explicitly tolerates the residual case (an instrument offered by the effect browser):
  `plugins/VstEffect/VstEffect.cpp:100-101` — "Do not process wet/dry for instruments loaded as
  effects / TODO: Prevent instruments from loading as effects?" — i.e. the current listing
  behavior is deliberate, matching upstream.

## Commands run, with exit codes

Search/inspection (rg via grep tool, `git status --short` clean before and after):

```
bash: ls tests/reference/VstEffect/ ; diff -u tests/reference/VstEffect/VstSubPluginFeatures.cpp plugins/VstEffect/VstSubPluginFeatures.cpp | head -40 ; git status --short ; git branch --show-current
      -> tests/reference/VstEffect/ does not exist (no upstream reference copy for this plugin);
         branch bugs/qwen-new-p10-435; worktree clean.
```

Gates (both required before commit; logs in this run's log folder):

```
bash tests/complexity-gate.sh --check > $LOGS/gate-complexity-1.log 2>&1   EXIT=0
bash tests/file-length-gate.sh   --check > $LOGS/gate-filelength-1.log 2>&1   EXIT=0
```

No build/test logs: no code was changed, so no regression was built (FALSE-POSITIVE path per
procedure).

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p10-435.md` (this report) — one commit, no production
  files touched.
