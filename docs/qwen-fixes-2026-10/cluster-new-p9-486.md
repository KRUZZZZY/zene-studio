# Cluster new-p9-486

**Verdict: FALSE-POSITIVE**

The reported missing-plugin and load-failure paths do not return a null pointer
from `Plugin::instantiate()` on this branch. In `src/core/Plugin.cpp:239-250`,
safe-start returns a `DummyPlugin`; `src/core/Plugin.cpp:254-273` creates a
`DummyPlugin` for a missing plugin; and `src/core/Plugin.cpp:276-282` replaces
a null plugin hook result with a `DummyPlugin`. The failed-symbol path at
`src/core/Plugin.cpp:284-298` also creates a `DummyPlugin`. The function returns
that initialized pointer at `src/core/Plugin.cpp:301`.

`src/core/ToolPlugin.cpp:43-53` therefore receives a non-null plugin for the
reported cases. A missing or failed plugin has a non-tool type, so the type
check succeeds safely and the function deletes the dummy and returns `nullptr`
as its documented failure result. No production change is warranted.

## Evidence

| Check | Command | Exit | Result |
| --- | --- | ---: | --- |
| Plugin return-path source check | `rg -n 'return new DummyPlugin|inst = new DummyPlugin|if\(!inst\)|return inst;' src/core/Plugin.cpp` | 0 | Each failure branch supplies a `DummyPlugin`, and the function returns `inst`. |
| Tool-instantiation source check | `rg -n 'Plugin \* ToolPlugin::instantiate|Plugin \* p =|if\( p->type|return nullptr' src/core/ToolPlugin.cpp` | 0 | The reported call is followed by a type check; non-tools are deleted and return null. |
| RED regression | Not run (false positive; the reported path does not produce a null `Plugin *`). | N/A | No regression test was added because it could not fail on the original code for this finding. |
| GREEN regression | Not run (no production fix). | N/A | No fix was needed. |
| Affected-family test count | Not run | N/A | This finding was classified false positive from the complete return path. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-486.r1/logs/complexity-2.log 2>&1; complexity_exit=$?; printf 'EXIT=%s\n' "$complexity_exit"` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-486.r1/logs/file-length-1.log 2>&1; file_length_exit=$?; printf 'EXIT=%s\n' "$file_length_exit"` | 0 | Passed. |
| Files committed | `docs/qwen-fixes-2026-10/cluster-new-p9-486.md` | — | Report only; no product or test files changed. |

No build or ctest command was run because there is no production or test change.
