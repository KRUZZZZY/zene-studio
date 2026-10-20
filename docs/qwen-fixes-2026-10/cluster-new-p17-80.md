**Verdict: FIXED** — a failed theme reload no longer clears the stylesheet or reports success (cluster new-p17-80, BUG 5).

## The defect

`LmmsStyle::reloadCustomStyle()` (original `src/gui/LmmsStyle.cpp:200-206`) ignored the result of `file.open()`, installed `readAll()` regardless, and then showed "Theme updated". An unreadable `style.css` (permissions changed while the file still exists) therefore replaced the application stylesheet with an empty string and told the user it had reloaded. The constructor had the same unchecked open (original `:158-159`), so an unreadable theme at startup also installed an empty stylesheet.

Confirmed real on this branch before the fix: `anUnreadableStylesheetKeepsTheThemeInPlace` returned `qApp->styleSheet()` without `redmarker` (RED, below).

## The fix

- `src/gui/LmmsStyle.cpp:139-158` — new static helper `installCustomStyle(path, error)`. It opens the file, reads it, and checks `file.error()` after `readAll()`. It calls `qApp->setStyleSheet` only when the whole file was read; otherwise it installs nothing and returns the `errorString()`.
- `src/gui/LmmsStyle.cpp:179-184` — constructor uses the helper. On failure it logs a `qWarning` and keeps the watches, so a later readable file is still picked up. No window exists yet at construction, so a `TextFloat` message would be unsafe there.
- `src/gui/LmmsStyle.cpp:222-250` — `reloadCustomStyle()` uses the helper. Success shows "Theme updated" (`:227-233`). Failure shows "Theme not updated" with the read reason and the error icon (`:234-245`). A deleted file is still silent, as before (`if (QFile::exists(...))` at `:224`). The re-arm of the watch is unchanged.
- Sibling check: `rg -n setStyleSheet src include` shows `LmmsStyle.cpp` is the only place that loads the custom theme file into the application stylesheet. The other `setStyleSheet` calls are per-widget strings with no file read.

Startup behaviour is unchanged for the cases that work: the app-level stylesheet is empty before `LmmsStyle` is constructed (no app-level `setStyleSheet` in `src/` or `include/` outside `LmmsStyle.cpp`), so an unreadable file at startup leaves it empty, as the old code effectively did.

## Test

`tests/src/gui/LmmsStyleTest.cpp:121` `anUnreadableStylesheetKeepsTheThemeInPlace` — loads a theme, makes `style.css` unreadable with `QFile::setPermissions({})`, calls `reloadCustomStyle()` directly, expects the theme to stay; then restores read permission, rewrites the file, reloads, and expects the new content. Deterministic: no watcher event, no timing.

`tests/src/gui/LmmsStyleTest.cpp:149` `anUnreadableStylesheetAtStartupInstallsNothing` — sets a sentinel stylesheet, makes the theme unreadable, constructs `LmmsStyle`, expects the sentinel to survive.

Both tests restore read permission at the start, so a failed run cannot poison the next test (this was found during RED: an aborted test left the file at `000` and cascaded into the second test).

Test seam (not a behaviour change): `include/LmmsStyle.h:32` forward declaration and `:66` `friend class ::LmmsStyleTest;`, the same pattern as `include/AudioPortsModel.h:45` / `include/AudioJack.h:109`. It lets the test call the private `reloadCustomStyle()` directly.

Not asserted by the test: the "Theme updated" / "Theme not updated" `TextFloat` message itself. It is not observable headless here; the message choice is by code reading (`LmmsStyle.cpp:227-245`).

## Evidence table

| Step | Command (cwd = worktree, logs in LOG FOLDER) | EXIT | Result |
| --- | --- | --- | --- |
| env | `cmake --build build --target zene LmmsStyleTest -j2 > build-1.log` | 2 | ccache could not create temp file in `/run/user/1000/ccache-tmp` (Permission denied). Environment, not code. |
| env | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p17-80 cmake --build build --target zene LmmsStyleTest -j2 > build-2.log` | 2 | Compile error: `LmmsStyle.h:65` friend names an undeclared class. Fixed by the forward declaration at `LmmsStyle.h:32`. |
| env | same, `build-3.log` | 0 | Build green with the seam, before any fix. |
| **RED** | `cmake --build build --target zene LmmsStyleTest -j2 > build-4-red.log` | 0 | Build with the new tests, fix not applied. |
| **RED** | `cd build/tests && ctest -R '^LmmsStyleTest$' --output-on-failure > test-2-red.log` | **8** | `FAIL! anUnreadableStylesheetKeepsTheThemeInPlace() 'qApp->styleSheet().contains("redmarker")' returned FALSE (an unreadable stylesheet cleared the theme in place)`; `FAIL! anUnreadableStylesheetAtStartupInstallsNothing() 'qApp->styleSheet().contains("sentinel")' returned FALSE`. Totals: 4 passed, 2 failed. |
| (earlier RED, `test-1-red.log`, EXIT 8) | first run, before the permission restore was added | 8 | Same first failure; the second failed on `writeTheme` because the aborted first test had left the file at `000`. Motivated the restore lines. |
| **GREEN** | `cmake --build build --target zene LmmsStyleTest -j2 > build-5-green.log` | 0 | Build with fix. |
| **GREEN** | `cd build/tests && ctest -R '^LmmsStyleTest$' --output-on-failure > test-3-green.log` | **0** | `1/1 Test #345: LmmsStyleTest ... Passed` |
| GREEN (count) | `cd build/tests && ctest -R '^LmmsStyleTest$' -V > test-5-green-verbose.log` | 0 | `Totals: 6 passed, 0 failed, 0 skipped` (4 test functions + QTest's initTestCase/cleanupTestCase). |
| **PROOF 4b** | `git diff -- src/gui/LmmsStyle.cpp > fix.patch` (90 lines) | 0 | Fix hunks only. The header seam is kept because reverting it breaks compilation. |
| **PROOF 4b** | `git apply -R fix.patch` | 0 | Fix reverted in the worktree. |
| **PROOF 4b** | `cmake --build build --target zene LmmsStyleTest -j2 > build-6-proof-reverted.log` | 0 | Build without the fix. |
| **PROOF 4b** | `cd build/tests && ctest -R '^LmmsStyleTest$' --output-on-failure > test-4-proof-reverted.log` | **8** | Same two assertions fail as in RED. |
| **PROOF 4b** | `git apply fix.patch` | 0 | Fix restored. |
| **PROOF 4b** | `cmake --build build --target zene LmmsStyleTest -j2 > build-7-proof-applied.log` | 0 | Build with fix. |
| **PROOF 4b** | `cd build/tests && ctest -R '^LmmsStyleTest$' -V > test-5-green-verbose.log` | 0 | 6 passed, 0 failed. |
| family | `ctest -N -R "LmmsStyle\|Style"` | — | Only `LmmsStyleTest` (#345) matches; 1 test. |
| rebuild dependents | `rg -l "LmmsStyle\.h"` | — | Only `src/gui/GuiApplication.cpp` includes the header; it is in the `zene` target and was rebuilt in `build-3.log` and `build-5-green.log`. |
| gate | `bash tests/fork-sources-gate.sh > gate-fork-sources.log` | 0 | PASS |
| gate | `bash tests/all-sources-reproduce.sh > gate-all-sources.log` | 0 | PASS |
| gate | `bash tests/no-upstream-regression-gate.sh > gate-no-upstream.log` | 0 | PASS |
| gate | `tests/scripted/check-namespace > gate-check-namespace.log` | 0 | PASS |
| gate | `bash tests/no-tautology-gate.sh > gate-no-tautology.log` | 0 | PASS |
| gate | `python3 tests/doc-figures-gate.py > gate-doc-figures.log` | 0 | PASS |
| ratchet | `bash tests/complexity-gate.sh --check > gate-complexity.log` | 1 | Red on the train by design. Items listed are in other files; `grep -c LmmsStyle` over the ratchet logs = 0, so none is in a changed file. |
| ratchet | `bash tests/file-length-gate.sh --check > gate-file-length.log` | 1 | Same: red on the train by design; no item names `LmmsStyle*`. |
| full script | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > gate-feature-1.log` | 1 | Not a code result: the script creates `<worktree>-feature-check/` next to the worktree (`PermissionError`, outside the write-jail). I ran the same six simple gates and two ratchets above instead, from the worktree, with the same commands. The ratchets' base comparison (it writes a cached base checkout) could not run here. |

## Registration

- `tests/upstream-modifications.txt`: two appended lines — `src/gui/LmmsStyle.cpp` (behaviour fix) and `include/LmmsStyle.h` (test seam only). `LmmsStyleTest` was already registered (`tests/fork-sources.txt`/`tests/CMakeLists.txt`, unchanged); `LmmsStyle.cpp` and `LmmsStyle.h` were already listed as inherited files.
- No new files, no new control commands, no snapshot change.

## Self-check

- **Each new test FAILS with the product change reverted and PASSES with it:** yes. Reverted `src/gui/LmmsStyle.cpp` only: `test-4-proof-reverted.log` EXIT 8, the same two assertions as RED. Restored: `test-5-green-verbose.log` EXIT 0, 6 passed.
- **Acceptance (the finding's fix):** "check the open result" — proven by the startup test and the reload test (`LmmsStyleTest.cpp:121`, `:149`). "Keep the current stylesheet on failure" — proven by the reload test (theme in place after failed reload). "Show the read error" — implemented (`LmmsStyle.cpp:234-245`), **not asserted by a test** (the `TextFloat` message is not observable headless). The constructor shows a `qWarning` instead, because no window exists at that point.
- **Undo, commands, realtime:** no command, no undo step, no audio-thread code. The reload runs on the GUI thread, as before; it did a file read before and does one now.
- **Old projects and saved data:** the change touches only theme-file loading. Projects, saves and the default theme (`customTheme == false`) take the unchanged branch at `LmmsStyle.cpp:173-178`.
- **Portability (Qt 5.15 / MSVC / macOS):** uses `QFile`, `QByteArray`, `QString::fromUtf8`, `qWarning`, `QFile::errorString` — all available in Qt 5.15. No Qt 6-only API; no `std::filesystem`; no `windows.h`.
- **Gates:** six simple gates EXIT 0; two ratchets EXIT 1 with no item in a changed file (see table). The full `feature_check.py` cannot run inside the write-jail (its log folder is outside the worktree).
- **Open, not verified here:** whether `QFileSystemWatcher::addPath` succeeds on a file the process cannot read. If it does not, a file that is unreadable at startup and later becomes readable might not be re-armed until another reload. This predates the fix and was not changed.
