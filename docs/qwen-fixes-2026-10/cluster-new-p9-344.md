# Cluster new-p9-344

Verdict: **FIXED**

## Finding and fix

In `plugins/Sf2Player/PatchesDialog.cpp:221`, the `CONFIG_FLUID_BANK_OFFSET` branch passed the undeclared identifier `sfont` to `fluid_preset_get_sfont()`. It now obtains the soundfont from the selected `pPreset`.

## Evidence

The forced bank-offset compile is the regression for this compile-only defect. The same `sf2player` build command was run with `CONFIG_FLUID_BANK_OFFSET` defined:

```sh
cmake --build build --target sf2player -- 'CXX_DEFINES=-DNDEBUG -DNOMINMAX -DNO_JACK_METADATA=1 -DPLUGIN_NAME=sf2player -DQT_CORE_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_NO_DEBUG -DQT_SVG_LIB -DQT_TRANSLATIONS_DIR=\"/usr/share/qt6/translations\" -DQT_WIDGETS_LIB -DQT_XML_LIB -DUSE_WEAK_JACK=1 -Dsf2player_EXPORTS -DCONFIG_FLUID_BANK_OFFSET'
```

| Run | Exact command | Exit |
| --- | --- | --- |
+| RED, original | `cmake --build build --target sf2player -- 'CXX_DEFINES=-DNDEBUG -DNOMINMAX -DNO_JACK_METADATA=1 -DPLUGIN_NAME=sf2player -DQT_CORE_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_NO_DEBUG -DQT_SVG_LIB -DQT_TRANSLATIONS_DIR=\"/usr/share/qt6/translations\" -DQT_WIDGETS_LIB -DQT_XML_LIB -DUSE_WEAK_JACK=1 -Dsf2player_EXPORTS -DCONFIG_FLUID_BANK_OFFSET' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-344.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 2 |
| GREEN, fix applied | `cmake --build build --target sf2player -- 'CXX_DEFINES=-DNDEBUG -DNOMINMAX -DNO_JACK_METADATA=1 -DPLUGIN_NAME=sf2player -DQT_CORE_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_NO_DEBUG -DQT_SVG_LIB -DQT_TRANSLATIONS_DIR=\"/usr/share/qt6/translations\" -DQT_WIDGETS_LIB -DQT_XML_LIB -DUSE_WEAK_JACK=1 -Dsf2player_EXPORTS -DCONFIG_FLUID_BANK_OFFSET' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-344.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| Reverse-patch RED | `cmake --build build --target sf2player -- 'CXX_DEFINES=-DNDEBUG -DNOMINMAX -DNO_JACK_METADATA=1 -DPLUGIN_NAME=sf2player -DQT_CORE_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_NO_DEBUG -DQT_SVG_LIB -DQT_TRANSLATIONS_DIR=\"/usr/share/qt6/translations\" -DQT_WIDGETS_LIB -DQT_XML_LIB -DUSE_WEAK_JACK=1 -Dsf2player_EXPORTS -DCONFIG_FLUID_BANK_OFFSET' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-344.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 2 |
| Reapplied GREEN | `cmake --build build --target sf2player -- 'CXX_DEFINES=-DNDEBUG -DNOMINMAX -DNO_JACK_METADATA=1 -DPLUGIN_NAME=sf2player -DQT_CORE_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_NO_DEBUG -DQT_SVG_LIB -DQT_TRANSLATIONS_DIR=\"/usr/share/qt6/translations\" -DQT_WIDGETS_LIB -DQT_XML_LIB -DUSE_WEAK_JACK=1 -Dsf2player_EXPORTS -DCONFIG_FLUID_BANK_OFFSET' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-344.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |

The reverse-proof commands both exited 0:

```sh
git diff -- plugins/Sf2Player/PatchesDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-344.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-344.r1/logs/fix.patch
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-344.r1/logs/fix.patch
```

The configured build and affected-family test commands were:

```sh
cmake --build build --target zene sf2player PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-344.r1/logs/build-6.log 2>&1; echo EXIT=$?
ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-344.r1/logs/test-1.log 2>&1; echo EXIT=$?
```

The build exited 0 (`build-6.log`). CTest exited 0 (`test-1.log`): 1/1 `PluginPortsMigrationTest` passed. There is no standalone `PatchesDialog` CTest in this configured tree; this reference/migration test builds and exercises the Sf2Player module.

## Gates

The checks were run with their unpiped status preserved:

```sh
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-344.r1/logs/gate-1.log 2>&1
gate_rc=$?
echo EXIT=$gate_rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-344.r1/logs/gate-1.log
echo EXIT=$gate_rc
exit "$gate_rc"

bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-344.r1/logs/gate-2.log 2>&1
gate_rc=$?
echo EXIT=$gate_rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-344.r1/logs/gate-2.log
echo EXIT=$gate_rc
exit "$gate_rc"
```

Complexity gate: EXIT=0. File length gate: EXIT=0.

## Files committed

- `plugins/Sf2Player/PatchesDialog.cpp`
- `tests/upstream-modifications.txt`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-344.md`
