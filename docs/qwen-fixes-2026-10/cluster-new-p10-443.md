# Cluster new-p10-443: VST3 instrument editor scale

**Verdict: NEEDS-CONTEXT**

## Evidence

- [Vst3Instrument.cpp:160] opens the instrument editor through `m_editor.open()` and its hooks; it does not read a scale value.
- [Vst3Instrument.h:85] derives `Vst3Instrument` from `AudioPluginExt<Instrument, ...>`. [Instrument.h:56] derives `Instrument` from `Plugin`.
- The scale option belongs to `Effect`: [Effect.h:227-230] documents and exposes `deviceOptions()`. The command handler describes `plugin.options` as effect-only at [ControlCommandsDeviceParams.cpp:398-402].
- [ClapEffect.cpp:91-95] applies that effect option before opening its editor. The VST3 effect path at [Vst3Effect.cpp:101-107] does not apply it either.

So the reported instrument path has no existing `deviceOptions().editorScale` value to pass to the VST3 host. Adding one would require deciding whether instruments should gain a persisted editor-scale option and how users set it. That is a product contract change beyond the reported missing call, so I did not invent an instrument option or add a regression for behavior the current API does not provide.

## Verification

| Check | Command / result |
|---|---|
| RED regression | Not run: no instrument editor-scale setting exists to exercise. |
| GREEN regression | Not run: no product fix was made pending the missing contract. |
| Affected-family test count | Not run; ctest was not invoked. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-443.r1/logs/complexity-gate-1.log 2>&1; gate_rc=$?; printf 'EXIT=%d\n' "$gate_rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-443.r1/logs/complexity-gate-1.log; exit "$gate_rc"` — exit 0. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-443.r1/logs/file-length-gate-1.log 2>&1; gate_rc=$?; printf 'EXIT=%d\n' "$gate_rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-443.r1/logs/file-length-gate-1.log; exit "$gate_rc"` — exit 0. |
| Log searches | fff was unavailable in this session; searched with rg. `rg -n -i 'error' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-443.r1/logs`, `rg -n 'FAILED' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-443.r1/logs`, and `rg -n 'Assertion' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-443.r1/logs` each exited 1 (no matches). |
| Files committed | `docs/qwen-fixes-2026-10/cluster-new-p10-443.md` only. |

Build and test commands were not run because the finding could not be expressed as a regression against an existing instrument scale option. No product or test files were changed.
