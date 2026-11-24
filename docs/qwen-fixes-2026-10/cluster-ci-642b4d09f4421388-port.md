FIXED

# Cluster ci-642b4d09f4421388-port: CI regression, build / linux-x86_64 (Qt 5)

Verdict: FIXED (REAL). The hosted `build / linux-x86_64` job builds against Qt 5.15
(`.github/workflows/build.yml` sources `/opt/qt5*/bin/qt5*-env.sh` and does not pass
`-DWANT_QT6=ON`). The original cluster ci-642b4d09f4421388 was fixed by commit
50f8ea2b46. Its four product changes are already on this branch:
`git diff --stat 50f8ea2b46 HEAD` over `include/TelemetryConsentDialog.h`,
`src/core/ControlCommandsScript.cpp` and `src/core/OscBridge.cpp` is empty.
Train 051 then added new test code that assumes Qt 6, and the same job fails on it again.

## Reproduction (Qt 5.15.19 build tree `build-qt5`, git-ignored)

Configure: `/usr/bin/cmake -S . -B build-qt5 -DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
-DTARGET_UARCH=none -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON -DWANT_VST3=ON -DWANT_CLAP=ON
-DWANT_VST3_TEST_INSTRUMENT=ON -DWANT_QT6=OFF -DZENE_TEST_STRIP_DEBUG=ON
-DQt5Svg_DIR=$TMPDIR/qt5svg-dl/prefix/usr/lib/cmake/Qt5Svg ...`. EXIT=0, log
`qt5-1-configure.log` ("Experimental Qt6 support: Disabled"). Qt5Svg comes from the Arch
`qt5-svg-5.15.19` package, extracted under `$TMPDIR`.

Full keep-going build: `/usr/bin/make -j4 -k` in build-qt5. EXIT=2, log `qt5-3-build.log`.
These errors come from the source:

| File:line | Qt 5 error | Cause |
|---|---|---|
| `tests/src/core/ControlOscServerTest.cpp:113` | `call of overloaded 'bind(QHostAddress::SpecialAddress, int)' is ambiguous` | Qt 5's `QAbstractSocket` has both `bind(const QHostAddress&, quint16, BindMode)` and `bind(quint16, BindMode)`. An unscoped enum plus a literal `0` matches both. Qt 6 no longer has the ambiguity. |
| `tests/src/core/LinkAudioStreamTestSupport.h:358` (in LinkAudioStreamTest) | same | same |
| `tests/src/gui/CollabPanelTest.cpp:130,131,134,139` | `invalid use of incomplete type 'class QAction'` | Qt 5 QtTest/QtGui only forward-declare `QAction` (`qevent.h:62`). Qt 6 moved `QAction` into QtGui, so it was reachable there by accident. |

## Fix (test code only, no product change)

- `ControlOscServerTest.cpp:113` and `LinkAudioStreamTestSupport.h:358` now call
  `bind(QHostAddress(QHostAddress::LocalHost), 0)`. Only the first overload accepts a
  `QHostAddress`, and the address is the same. Siblings at `ControlOscServerTest.cpp:145,154`
  pass a `quint16 m_port`, which is not ambiguous, and they compiled cleanly in the Qt 5 build.
  They are left unchanged.
- `CollabPanelTest.cpp:34`: `#include <QAction>`. This is a test file of upstream origin, so
  it is registered in `tests/upstream-modifications.txt`.

## Regression proof (the Qt 5 build of the three affected targets is the regression test)

| Step | Command | EXIT | Log |
|---|---|---:|---|
| save the patch | `git diff -- tests/src/core/ControlOscServerTest.cpp tests/src/core/LinkAudioStreamTestSupport.h tests/src/gui/CollabPanelTest.cpp > logs/fix.patch` | 0 | fix.patch |
| revert | `git apply -R logs/fix.patch` | 0 | |
| RED | `cd build-qt5 && /usr/bin/make -k -j4 ControlOscServerTest LinkAudioStreamTest CollabPanelTest` | **2** | qt5-4-red.log (all three errors above) |
| reapply | `git apply logs/fix.patch` | 0 | |
| GREEN | same make | **0** | qt5-5-green.log |
| Qt 6 build | `cmake --build build --target zene ControlOscServerTest LinkAudioStreamTest CollabPanelTest -j4` | 0 | qt6-7-build.log |
| Qt 6 run | `cd build/tests && ctest -R '^(ControlOscServerTest\|LinkAudioStreamTest\|CollabPanelTest)$' --output-on-failure` | 0 | qt6-8-ctest.log: "100% tests passed out of 3" |
| gates | `python3 .../qwen/feature_check.py <worktree>` | 0 | gates-1.log: RESULT: PASS (all 8 PASS) |

Not verified: running ctest on the Qt 5 tree. The lane guard refused it with EXIT=97
("bunny-guard: run ctest from <lane>/build/tests", qt5-6-ctest.log), and I did not try to
get around it. The change has no runtime effect. It selects the overload Qt 6 already
uses, with the same address, and adds an include.

## Self-check

- Siblings: every `bind(QHostAddress::...)` call in the tree was searched (fff
  `bind(QHostAddress::`): ControlOscServerTest.cpp:113,145,154 and
  LinkAudioStreamTestSupport.h:358. Only the two calls with a literal `0` were ambiguous, and
  both are fixed.
- Realtime, undo and commands: not touched. No control command was added.
- Old projects and saved data: not affected, because only test sources changed.
- No src/gui, Song, Record, Undo, Transport, Take or AudioEngine file changed, so no
  screenshot or state sequence is needed.

## Other findings

- `zynaddsubfx_gui` fails on this box's Qt 5 build with
  `/usr/include/FL/Fl_Cairo.H:40: fatal error: cairo.h: No such file or directory`. The
  system FLTK here is built with cairo and its include path is not propagated. This is an
  environment artifact, not a source defect, and nothing here changes it.

Delegation: Sonnet set up the Qt 5 tree (downloaded qt5-svg, configured, ran the full -k
build) and listed the errors. Haiku ran the patch revert/reapply RED/GREEN builds and the
Qt 6 build and ctest runs. I designed and made the three one-line edits and the
registration line, and wrote this report.
