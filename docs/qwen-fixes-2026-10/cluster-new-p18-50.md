FALSE-POSITIVE

## Evidence

| Location | Evidence |
|---|---|
| `/usr/include/lilv-0/lilv/lilv.h:1266-1274` | The `lilv_port_get_symbol()` declaration returns `const LilvNode*`; its contract says the node is owned by the port and must not be freed. |
| `/usr/include/lilv-0/lilv/lilv.h:1276-1282` | `lilv_port_get_name()` has the opposite contract: it returns a node the caller must free. `PortBase::name()` correctly wraps that allocated result in `AutoLilvNode`. |
| `src/core/lv2/Lv2Ports.cpp:299-302` | `PortBase::uri()` converts the borrowed symbol to a string while its owning port remains alive; it does not own a Lilv node. |
| `src/core/lv2/Lv2Proc.cpp:849-851`, `src/gui/Lv2ViewBase.cpp:120-122` | The other two `lilv_port_get_symbol()` call sites also convert the port-owned node without freeing it. |

Wrapping the symbol in `AutoLilvNode` would be invalid: the API returns a `const` borrowed pointer, while `AutoLilvNode` invokes `lilv_node_free`. A compile attempt confirmed the type mismatch. No production or test changes are retained.

## Review matrix

| Case | Result |
|---|---|
| Normal/repeated lookup | The symbol remains owned by the port. A temporary 32-call fixture check returned `latency` on every call. |
| Failure, exhaustion, reconfiguration | Not applicable: this accessor returns an existing port-owned node; it does not allocate from a bounded pool or have mutable configuration. |
| Realtime | No changes. These symbol reads occur during port registration or GUI setup; they are not plugin render callbacks. |
| Supported platforms | No source change. The declaration and ownership contract come from the configured Lilv API. |

No leak regression test is retained because it would assert the opposite of Lilv's ownership contract. An exploratory source assertion requiring `AutoLilvNode` failed as expected for the correct borrowed-node implementation and was removed; it was not a valid regression test. The final existing LV2 family test passed.

## Commands and results

| Command | Exit | Result |
|---|---:|---|
| `cmake --build build --target Lv2PortsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 2 | No generated rule for the temporary target. |
| `cmake -S . -B build > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/configure-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 97 | `bunny-guard` rejected explicit configure because the parent owns configuration. |
| `ctest -R '^Lv2StateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` (run from `build/tests`) | 8 | Exploratory test failed only its invalid `AutoLilvNode` source assertion; its real fixture slot returned the symbol on all 32 calls. The assertion was removed. |
| `cmake --build build --target zene Lv2StateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/build-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 2 | The attempted wrapper did not compile because Lilv returns `const LilvNode*`; this matches the API contract above. The attempt was reverted. |
| `cmake --build build --target zene Lv2StateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/build-6.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 | Final unchanged product and existing test target built. |
| `ctest -R '^Lv2StateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` (run from `build/tests`) | 0 | 1/1 CTest test passed. |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-50 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 1 | `fork-sources` and `all-sources-reproduce` fail; all other rows pass, including complexity and file length. The existing baseline note is `tests/upstream-modifications.txt:5077`. |

The regression RED/GREEN proof is **not applicable** to this false-positive finding. The affected-family count is 1 existing CTest test, passed; no regression test was added.

## Self-check

- Regression RED/GREEN: not applicable. Lilv's contract proves the alleged leak is not a leak; the exploratory assertion was invalid and removed. Its temporary test command exited 8; the final existing test command exited 0.
- Determinism: no retained regression test. The temporary fixture check used 32 direct calls, with no sleeps, timing, or races.
- Call sites and siblings: checked `PortBase::uri()`, `Lv2Proc.cpp`, and `Lv2ViewBase.cpp`; each uses the borrowed port-owned symbol. The owned `lilv_port_get_name()` path remains wrapped.
- Collateral/data: no source, test, project-format, or saved-data changes are retained.
- False-positive evidence: `/usr/include/lilv-0/lilv/lilv.h:1266-1274` explicitly defines the symbol's borrowed ownership and forbids freeing it.

## Committed files

- `docs/qwen-fixes-2026-10/cluster-new-p18-50.md` (this report)
