# Cluster new-p10-435 — VST2 browser category filtering

Verdict: **NEEDS-CONTEXT**.

The effect-browser part of the finding is real. `VstSubPluginFeatures::listSubPluginKeys()` adds every scanned DLL/SO as a key (`plugins/VstEffect/VstSubPluginFeatures.cpp:56–70, 85–97`). The VST effect descriptor constructs this feature with `Plugin::Type::Effect` (`plugins/VstEffect/VstEffect.cpp:47–59`), and `EffectSelectDialog` calls it for Effect descriptors (`src/gui/modals/EffectSelectDialog.cpp:70–83`). `VstEffect` then opens the selected file (`plugins/VstEffect/VstEffect.cpp:64–79`); its render path explicitly handles an instrument loaded as an effect (`:98–102`). Thus a VST2 instrument can be offered in the effect browser.

The original title overstates the case: this scanner is attached to the Effect descriptor. The Instrument PluginBrowser does not enumerate these subplugin keys, and VeSTige is the separate VST2 instrument host. This narrows the report to instruments being offered in the effect browser; it does not make that case a false positive.

Safe type-aware discovery needs a design decision. VST2 discovery would have to execute arbitrary plugin code to inspect its classification. Runtime VST2 instances already run in a separate client (`src/core/OutOfProcessHosting.cpp:86–94`; `plugins/VstBase/VstPlugin.cpp:171–187`), but the existing parent/client protocol has no classification response (`plugins/VstBase/communication.h:56–90`), and runtime initialization waits for the client to finish loading (`plugins/VstBase/VstPlugin.cpp:213–241`). Calling the plugin directly from the browser risks taking down or hanging the DAW. Reusing or extending a client for discovery requires a bounded probe and a failure policy.

The type rule also needs to be chosen. VST2 provides signals such as `effFlagsIsSynth` and `effGetPlugCategory`, while plugins can expose unusual or inconsistent metadata. The current host does not surface those values to discovery. The fix needs a policy for which signal defines “instrument,” and what to do when probing fails, times out, or reports conflicting data. This report leaves that behavior unspecified rather than adding an unsafe or guessed probe.

## Verification evidence

| Check | Command / result |
| --- | --- |
| RED regression | Not run (exit N/A): no regression can assert the intended classification rule before that rule is decided. |
| GREEN regression | Not run (exit N/A): no production behavior was changed. |
| Affected-family test count | 0 tests run; no `ctest` command was issued. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-435.r2/logs/gate-complexity-2.log 2>&1; gate_status=$?; printf 'EXIT=%s\n' "$gate_status"` — exit 0. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-435.r2/logs/gate-filelength-2.log 2>&1; gate_status=$?; printf 'EXIT=%s\n' "$gate_status"` — exit 0. |
| Build | Not run: no production code changed. |
| Committed files | `docs/qwen-fixes-2026-10/cluster-new-p10-435.md` only. |

The fff grep tool was not available in this session. The two gate logs were searched with `rg -n -i` for each of `error`, `FAILED`, and `Assertion`; all six searches returned no matches (rg exit 1 for no match).
