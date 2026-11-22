FALSE-POSITIVE

# Cluster gui-1105-main-pop-out-the-note-editor-panel — "Pop out Note Editor button leaves the panel docked"

## Verdict

The pop-out works: from a fresh Arrange shell, clicking the Note Editor's pop-out button detaches `detail`
into its own top-level window and `window.get_state` reports `floating: true`. The sweep's failure is an
occlusion artifact: the sweep had already popped out the Browser (its control #8), and that floating Browser
window covers the Note Editor's pop-out button. The sweep's live AT-SPI inventory still reports the covered
button at (1539,712); the blind coordinate click lands on the floating Browser window instead, so the Note
Editor never receives the click and stays docked.

The finding's proposed root cause ("the click dispatches window.detach but ignores its result; a runtime
refusal would leave the panel docked") does not match what happens: no refusal occurs. The command is never
reached because the click goes to another window.

## Evidence

Product path (current tree):

- `src/gui/shell/Shell.cpp:217-222` creates a `ShellPanel` and calls `addFloatButton` for every panel, including
  `detail`.
- `src/gui/shell/ShellLayout.cpp:79` maps `detail` to "Note Editor"; `ShellLayout.cpp:57-58` opens it in the
  bottom region of the first-run Arrange view.
- `src/gui/shell/ShellFloating.cpp:424` connects the button to `dispatchFloat(id, !isDetached(id))`.
- `src/gui/shell/ShellFloating.cpp:220-231` dispatches `window.detach`; `src/gui/ControlCommandsLayouts.cpp:228-244`
  runs `detachPanel`; `ShellFloating.cpp:237-265` creates the `Qt::Window`, inserts it into `m_floating`, shows it,
  and returns `true`. `Shell::apply()` (`Shell.cpp:319-346`) skips panels in `m_floating`, so the layout does not
  pull it back.
- `ShellFloating.cpp:429-446` then makes the same button say "Dock" (`window.attach`), which is the visible
  confirmation that the panel left the shell.

Sweep run `m0-train-25` (the evidence the finding cites):

- `results.jsonl` control #8 popped out the Browser; its `after` screenshot shows the Browser floating.
- By control #20 the Browser is still floating, so the main window's bottom region spans the full width and the
  Note Editor's pop-out button is at the live AT-SPI point (1539,712).
- The floating Browser window occupies that point (it starts at x=1327), so the click at (1539,712) hits the
  Browser's plugin list, not the Note Editor button. `results.jsonl` records that click as the Note Editor
  control; the after screenshot is unchanged.

Reproduction on this worktree (binary `build/zene`, fresh temporary HOME, Dummy audio, 1600x1000):

| Step | Command | EXIT | `window.get_state` floating panels |
| --- | --- | --- | --- |
| fresh shell | `gui.py start build/zene --size 1600x1000` | 0 | `{}` |
| click Note Editor pop-out at its fresh location (1257,712) | `gui.py click 1257 712` | 0 | `{'detail': True}` |
| dock it again | `window.attach {"panel":"detail"}` | — | `{}` |
| pop out the Browser (1539,99) | `gui.py click 1539 99` | 0 | `{'browser': True}` |
| click the Note Editor button at its live covered point (1539,712) | `gui.py click 1539 712` | 0 | `{'browser': True}` (detail stays docked) |
| dock the Browser again | `window.attach {"panel":"browser"}` | — | `{}` |
| click the Note Editor button where it now is (1257,712) | `gui.py click 1257 712` | 0 | `{'detail': True}` |

The full transcript is `01-gui-repro.log` in the log folder; the matching screenshots are
`02-fresh.png` through `06-note-editor-floating-after-dock.png`.

The panel's floating window is placed exactly where the panel was (`ShellFloating.cpp:246-260`), and the
headless sweep has no window manager to draw a title bar, so a successfully floated panel can look "still
docked" in a screenshot. The button changing to "Dock" and `window.get_state` are the distinguishing evidence;
both show the detach succeeded.

## Commands run and exit codes

| Command | EXIT | Log |
| --- | --- | --- |
| `env -u LMMS_PLUGIN_DIR .../venv/bin/python repro.py` (the seven-step reproduction above) | 0 | `01-gui-repro.log` |
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `02-feature-check.log` (RESULT: PASS) |
| `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> gui-1105-main-pop-out-the-note-editor-panel` | 0 | `03-submit-check.log` (RESULT: PASS) |

No product code was changed, so there is no RED/GREEN pair to claim.

## Self-check

- **Regression test FAILS with the product change reverted and PASSES with it (exit codes):** not applicable.
  The verdict is FALSE-POSITIVE and no product file changed. The reproduction above is the evidence: the same
  build detaches `detail` when the click reaches the button and does not when the click is covered by the
  floating Browser window.
- **Deterministic (no wall-clock timing, sleeps or races):** the reproduction uses fixed coordinates for the
  fresh 1600x1000 layout and asserts `window.get_state` after each click through the control socket. The short
  sleeps only let the frame settle; no assertion depends on elapsed time.
- **Fix at the cause and complete:** no fix was needed. Every caller of the same path was checked and stays
  consistent: the header button (`ShellFloating.cpp:424`), the tear-off (`ShellTearOff.cpp:235,340-342,395`), the
  Window menu (`ShellCommandMenus.cpp:570`), the placeholder's Dock button (`ShellFloating.cpp:184`) and the
  keyboard action (`ShellFloating.cpp:215`) all go through `dispatchFloat` / `window.detach` and the same
  `detachPanel`.
- **No collateral damage:** no product, data or test file changed; no limit or check was added. Old layouts still
  restore floating panels through the existing `restoreFloating` / `floatOne` path (`ShellFloating.cpp:382-415`).
- **FALSE-POSITIVE evidence:** the button is live at `ShellFloating.cpp:424`; `detachPanel` returns `true` and
  sets `m_floating` at `ShellFloating.cpp:237-265`; `window.get_state` reports `floating: true` after the fresh
  click; the sweep's click at (1539,712) lands on the previously floated Browser window, not on the button.
- **Sibling commands:** no command file changed and no command was added; `window.detach`, `window.attach` and
  their siblings in `ControlCommandsLayouts.cpp` are untouched.
