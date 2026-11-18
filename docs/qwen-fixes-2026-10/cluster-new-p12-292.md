# Cluster new-p12-292

Verdict: **FALSE-POSITIVE**

The finding says restoring a settings element without a module unloads the worker but leaves the saved module path stale. The empty-path branch in `WasmEffectControls::loadSettings()` delegates to `WasmEffect::loadModule(QString())` at `plugins/WasmEffect/WasmEffectControls.cpp:162-170`. The empty-path branch of that method stops the worker and calls `m_controls->setModulePath(QString())` at `plugins/WasmEffect/WasmEffect.cpp:91-97`. That clears the same `m_modulePath` that `saveSettings()` checks before writing the `module` attribute at `plugins/WasmEffect/WasmEffectControls.cpp:128-133`. A later save therefore omits the stale path.

No code change or regression test was made: a regression for the reported stale-path behavior would pass on the current branch and would not prove a fix.

## Evidence

| Check | Command | Result |
|---|---|---|
| Regression RED | Not run; the finding is disproven by the existing empty-path delegation and path clear. | N/A |
| Regression GREEN | Not run; no production change was needed. | N/A |
| WasmSandboxTest family count | Not run; no tests were executed. | 0 executed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-292.r1/logs/gate-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | Exit 0 |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-292.r1/logs/gate-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | Exit 0 |

The `fff grep` tool was not available in this session. The gate logs were searched separately for `error`, `FAILED`, and `Assertion` with `rg`; none matched.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p12-292.md` (this report only)
