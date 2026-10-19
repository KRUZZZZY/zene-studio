IMPLEMENTED

# Q050-133 - Add file URLs to browser and sample-clip drags

Every acceptance criterion is met and proven by `BrowserDragUrlTest` (new, `tests/src/gui/BrowserDragUrlTest.cpp`),
plus the three re-run neighbour suites. Worktree: `zene-feat-q050-133`, branch `feat/050-Q050-133-oc`.
Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-133.r1/logs/`.

## Acceptance

| # | Criterion | Command (exact) | EXIT | Result |
| --- | --- | --- | --- | --- |
| 1 | Drag MIME data includes `text/uri-list` with a valid local file URL for browser files and sample clips | from `build/tests`: `ctest -R BrowserDragUrlTest --output-on-failure` (log `ctest-1.log`); the binary itself: `./build/tests/BrowserDragUrlTest` (log `test-binary-1.log`) | 0; 0 | 1/1 ctest passed; 8/8 slots passed. `aBrowserSampleDragCarriesTheFileUrlAlongsideItsStringPair` (real temp WAV: StringPair intact + exactly one `isLocalFile()` URL equal to `QUrl::fromLocalFile(path)`), `everyFileKeyTheBrowserDragsCarriesTheFileUrl` (all eight file-list keys), `everySearchResultKindDragsAKeyThatCarriesTheFileUrl` (the four search-result kinds through the product's own `browserresults::dragKey`) |
| 2 | An external file-manager drop and a compatible plug-in-editor drop receive the file URL | same ctest/binary runs as row 1 | 0; 0 | `aFileUrlRecipientReceivesTheFileTheBrowserDragged`: `UriListRecipient` (a `hasUrls()`-taking widget - the file-manager / plug-in-editor contract) accepts the real `QDragEnterEvent`/`QDropEvent` carrying the browser drag's MIME and receives exactly the source path. A manual smoke check against a concrete file manager / hosted plug-in editor is a known limit (offscreen CI cannot drive an OS drag) |
| 3 | The existing StringPair MIME data still drives internal LMMS drop targets | from `build/tests`: `ctest -R "ArrangerFileDropTest\|SampleEditorDialogTest\|SessionGridViewTest\|BrowserDragUrlTest" --output-on-failure` (log `ctest-2.log`) | 0 | 4/4 passed. `theStringPairPayloadStillDrivesAnInternalSampleClipDrop`: a real `SampleClipView` accepts the two-format drag and its `dropEvent` loads the file (`sampleFile()` == path); `aDragThatIsNotABrowserFileCarriesNoUrl`: `sampledata` / `clip_2` / `instrument` / `automatable_model` / `float_value` carry no URL; the three neighbour suites (arranger uri-list drops, sample-editor StringPair drops, session-grid drops) pass unchanged |

## Build

| Command | EXIT | Note |
| --- | --- | --- |
| `cmake --build build --target zene BrowserDragUrlTest -- -j4` (log `build-1.log`) | 2 | The reconfigure completed and `zene` built (`Built target zene`); in that same make invocation the not-yet-generated test target had "No rule to make target" - re-run below picks it up |
| `cmake --build build --target BrowserDragUrlTest -- -j4` (log `build-2.log`) | 0 | |
| `cmake --build build --target ArrangerFileDropTest SampleEditorDialogTest SessionGridViewTest -- -j4` (log `build-3.log`) | 0 | |

## Gates (before commit; no-upstream re-run after it)

| Gate | EXIT | Note |
| --- | --- | --- |
| `bash tests/complexity-gate.sh --check` | 1 | Red on train 5 on purpose; regression list identical to `../zene-cachyos` modulo train drift; no file of this lane newly listed (logs `gate-complexity-1.log` vs `gate-complexity-zene-cachyos.log`) |
| `bash tests/file-length-gate.sh --check` | 1 | Same rule; none of this lane's files over limit (logs `gate-filelength-1.log` vs `gate-filelength-zene-cachyos.log`) |
| `bash tests/fork-sources-gate.sh` | 0 | `tests/src/gui/BrowserDragUrlTest.cpp` registered (logs `gate-forksources-1.log`, `-2.log`) |
| `bash tests/all-sources-reproduce.sh` | 0 | `REPRODUCES` (logs `gate-allsources-1.log`, `-2.log`) |
| `bash tests/no-upstream-regression-gate.sh` | 0 | Post-commit run (`gate-no-upstream-2.log`): `StringPairDrag.h/.cpp`, `tests/CMakeLists.txt`, `data/manual/index.html` all declared with reasons |
| `tests/scripted/check-namespace` | 0 | `0 errors` (logs `gate-namespace-1.log`, `-2.log`) |
| `bash tests/no-tautology-gate.sh` | 0 | `PASS` (logs `gate-tautology-1.log`, `-2.log`) |

No command registered, no reversibility row, no snapshot/figure/histogram change; no project-format change;
`git status` clean after the docs commit.
