# Cluster new-p12-20

## Verdict: FALSE-POSITIVE

The finding says `device.mpe_set` returns early when the requested MPE state already matches, before adding `note_expressions_unchanged`. The current handler has no such early return. It always builds the success result and inserts `note_expressions_unchanged: true` at `src/core/ControlCommandsDevice.cpp:156`; the result schema declares the field at line 237. This is true for both changed and no-op calls.

`git blame` shows the insertion at line 156 was part of the original handler implementation (`c1282f6c7e`), rather than a later fix. No product code or tests were changed.

## Evidence

| Check | Exact command | Exit code / result |
|---|---|---|
| Handler and schema | `awk 'NR >= 133 && NR <= 168 || NR >= 230 && NR <= 252 { printf "%5d\t%s\n", NR, $0 }' src/core/ControlCommandsDevice.cpp` | 0; confirms the unconditional field insertion and schema declaration |
| Finding term and surrounding handler | `rg -n -C 14 'note_expressions_unchanged' src/core/ControlCommandsDevice.cpp` | 0; shows the handler continues from calculating `changed` through field insertion and return |
| Line history | `git blame -L 133,168 -- src/core/ControlCommandsDevice.cpp` | 0; field insertion is from original handler commit `c1282f6c7e` |
| RED regression | Not run; the finding is false-positive and procedure step 2 says to skip to reporting | N/A |
| GREEN regression | Not run; no regression or product change was needed | N/A |
| Affected-family test count | Not run | N/A |
| Complexity gate | Not run; no product code changed | N/A |
| File-length gate | Not run; no product code changed | N/A |

## Files in commit

- `docs/qwen-fixes-2026-10/cluster-new-p12-20.md`
