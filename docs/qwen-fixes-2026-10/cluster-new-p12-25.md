# Cluster new-p12-25

## Verdict: FALSE-POSITIVE

The reported unbounded read of the existing library file is absent from the current save path. `targetPath` only checks whether a destination exists (`src/core/ControlCommandsLibraryClips.cpp:174-178`). `writeClip` sends sample clips to `ClipConsolidate::render` and writes MIDI clips with `QSaveFile` opened in `WriteOnly` mode (`src/core/ControlCommandsLibraryClips.cpp:136-153`). The undo callback removes the destination; redo renders or writes the clip again (`src/core/ControlCommandsLibraryClips.cpp:209-218`). There is no existing-file backup or `readAll()` call in this file.

The WAV writer opens the destination with `SFM_WRITE` (`src/core/ClipConsolidate.cpp:84-109`). It allocates buffers for the current clip render (`src/core/ClipConsolidate.cpp:121-139`), but does not load the previous destination file as an undo backup. The finding's stated memory mechanism therefore does not apply to this branch.

## Evidence and validation

| Check | Command | Exit | Result |
|---|---|---:|---|
| Search for the alleged read | `rg -n "readAll" src/core/ControlCommandsLibraryClips.cpp` | 1 | No matches. |
| Inspect save and undo path | `nl -ba src/core/ControlCommandsLibraryClips.cpp \| sed -n '136,153p;203,218p'` | 0 | Writer and undo callbacks confirmed. |
| Inspect WAV writer | `nl -ba src/core/ClipConsolidate.cpp \| sed -n '84,109p;121,145p'` | 0 | Destination is opened for writing; render buffers are for current audio. |
| RED regression | Not run | N/A | No regression was added because the reported path is absent. |
| GREEN regression | Not run | N/A | No regression was added because the reported path is absent. |
| Affected-family tests | Not run; `LibraryBrowserTest` | 0 executed | No test result is claimed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-25.r1/logs/complexity-2.log 2>&1` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-25.r1/logs/file-length-2.log 2>&1` | 0 | Passed. |

The available tools did not include the requested `fff` log grep tool, so the two gate logs were searched with `rg -n -i` for `error`, `FAILED`, and `Assertion`, one term per search. All six searches returned exit 1 (no matches). An initial complexity-gate invocation yielded before its status was surfaced; the recorded rerun above completed with exit 0.

Build and test commands were not run because the finding is a false positive; therefore no RED/GREEN build or test logs were produced.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p12-25.md`
- No product or test files changed.
