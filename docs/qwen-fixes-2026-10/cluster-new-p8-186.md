# Cluster new-p8-186 — PluginDescWidget Q_OBJECT

## Verdict: FALSE-POSITIVE

The finding claims `PluginDescWidget` lacks `Q_OBJECT` despite declaring the
`organised()` signal and being the target of `qobject_cast`. In this checkout
(and in every historical branch inspected) the macro is already present, so
there is nothing to fix and no behaviour to regress.

## Evidence

- `include/PluginBrowser.h:70` — `class PluginDescWidget : public QWidget`
- `include/PluginBrowser.h:72` — `Q_OBJECT` (immediately inside the class body)
- `include/PluginBrowser.h:85-87` — `signals:` section with `void organised();`
- `src/gui/PluginBrowser.cpp:131` and `src/gui/PluginBrowser.cpp:135` —
  `qobject_cast<PluginDescWidget*>(...)` targets; both compile, which is only
  possible because moc generated the meta-object code for the class.
- `git show <ref>:include/PluginBrowser.h` for every other local branch
  (e.g. `0.4.0/train-w1`, `0.5.0/ui-train`, `030/*`) shows `Q_OBJECT` at line
  69/72 too — the macro was never missing on any branch, so this is not an
  already-fixed-on-this-branch case either.
- The pre-existing build tree contains
  `build/src/CMakeFiles/lmmsobjs.dir/gui/PluginBrowser.cpp.o`, i.e. the
  translation unit with the `qobject_cast` calls already compiled.

## Commands run (exact, with exit codes)

| Step | Command | EXIT |
|---|---|---|
| Build (no-op, zene already built) | `cmake --build build --target zene > LOGS/build-1.log 2>&1` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > LOGS/gate-2.log 2>&1` | 0 |

`LOGS` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-186.r1/logs`.
`fff grep` for `error` (case-insensitive) in the log folder returned 0 matches.

No regression test was written: no production code was changed, so there is no
behaviour to flip RED→GREEN (a test would pass both before and after, which is
not regression proof). No files other than this report were touched; no
`fork-sources.txt` / `upstream-modifications.txt` registration is needed.
