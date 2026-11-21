FIXED

# cluster ci-bbdf307254017268

## Classification

Test defect, Qt5-only. `tests/src/gui/TakeLanesPanelTest.cpp` calls
`QTouchEventSequence::press/move/release(touchId, QPoint)` without the optional
widget argument. Under Qt5 the same class exposes both a `QWindow*` overload and,
with `QT_WIDGETS_LIB`, a `QWidget*` overload, each with a `nullptr` default:

- `/usr/include/qt/QtTest/qtesttouch.h:78` `press(int, const QPoint&, QWindow* = nullptr)`
- `/usr/include/qt/QtTest/qtesttouch.h:85` `move(int, const QPoint&, QWindow* = nullptr)`
- `/usr/include/qt/QtTest/qtesttouch.h:92` `release(int, const QPoint&, QWindow* = nullptr)`
- `/usr/include/qt/QtTest/qtesttouch.h:107` `press(int, const QPoint&, QWidget* = nullptr)`
- `/usr/include/qt/QtTest/qtesttouch.h:114` `move(int, const QPoint&, QWidget* = nullptr)`
- `/usr/include/qt/QtTest/qtesttouch.h:121` `release(int, const QPoint&, QWidget* = nullptr)`

A two-argument call is therefore ambiguous in Qt5. Qt6 removed the ambiguity by
placement: `QTest::touchEvent(QWidget*, ...)` returns `QTouchEventWidgetSequence`,
whose overrides take `QWidget*` explicitly
(`/usr/include/qt6/QtWidgets/qtestsupport_widgets.h:33-35`); those overrides hide
the base class's `QWindow*` overloads, so the two-argument call resolves.

The six Qt5-failing calls were at `TakeLanesPanelTest.cpp:374,376,383,385,387,389`.
The fix passes `strip` explicitly at each call. Sibling tests already use the
explicit-widget form: `TouchInstrumentSurfaceTest.cpp:132`
(`sequence.press(0, atFirst, m_view)`) and `TouchLayoutTest.cpp:525`
(`...press(0, localPos, target)`), so they stay consistent.

## Reproduction on the exact commit

The worktree is a descendant of the CI commit: `git merge-base --is-ancestor
4b29f8a94716 HEAD` returned EXIT=0, and the diff of the test file against
`4b29f8a94716` was empty (`00-diff-vs-ci-commit.log`, 0 bytes). So the failing
source is the CI source.

Note: the CI log path named in the brief does not exist on disk here. The claim
that `TakeLanesPanelTest.cpp` was the sole failing object therefore rests on the
brief's excerpt, in which it is the only `FAILED` line before ninja stopped.

## Qt5 harness

The Qt5 syntax check is `/home/kruzzzzy/.config/kads/zene/state/runs/ci-bbdf307254017268.r1/logs/qt5-syntax-check.sh`:

```bash
#!/usr/bin/env bash
# Qt5 compile check for one test .cpp.
# Usage: bash qt5-syntax-check.sh tests/src/gui/FooTest.cpp
# Reads the Qt6 compile command from build/compile_commands.json, swaps the
# Qt6 include paths for Qt5 ones, generates the file's .moc with Qt5 moc,
# and runs clang++ -fsyntax-only.
set -u

if [ $# -ne 1 ]; then
	echo "usage: $0 <test-cpp-relative-to-worktree>" >&2
	exit 2
fi

SRC_REL="$1"
WORKTREE="$(pwd)"
SRC_ABS="$WORKTREE/$SRC_REL"
LOG_DIR="$(cd "$(dirname "$0")" && pwd)"
MOC_DIR="$LOG_DIR/qt5moc"
mkdir -p "$MOC_DIR"

if [ ! -f "$SRC_ABS" ]; then
	echo "no such file: $SRC_ABS" >&2
	exit 2
fi

# --- Qt5 moc ---------------------------------------------------------------
MOC=""
for cand in /usr/lib/qt/bin/moc "$(qmake-qt5 -query QT_INSTALL_BINS 2>/dev/null)/moc-qt5" "$(qmake-qt5 -query QT_INSTALL_BINS 2>/dev/null)/moc"; do
	if [ -n "$cand" ] && [ -x "$cand" ]; then MOC="$cand"; break; fi
done
if [ -z "$MOC" ]; then
	echo "Qt5 moc not found" >&2
	exit 2
fi

# --- extract flags from compile_commands.json ------------------------------
mapfile -t RAW < <(python3 -I - "$WORKTREE/build/compile_commands.json" "$SRC_ABS" <<'PY'
import json, shlex, sys
db, src = sys.argv[1], sys.argv[2]
entries = json.load(open(db))
entry = None
for e in entries:
    if e.get("file") == src:
        entry = e
        break
if entry is None:
    sys.exit("no compile command for %s" % src)
args = shlex.split(entry.get("command") or entry["arguments"])
skip_next = False
for a in args:
    if skip_next:
        skip_next = False
        continue
    if a in ("-o", "-MF", "-MT", "-MQ", "-c"):
        skip_next = a == "-o" or a.startswith("-M")
        continue
    if a.startswith("-M") or a == "-c":
        continue
    if a.startswith("-o") and len(a) > 2:
        continue
    print(a)
PY
) || { echo "failed to read compile command" >&2; exit 2; }

KEPT=()
MOCFLAGS=()
add_inc() { KEPT+=("-I" "$1"); MOCFLAGS+=("-I" "$1"); }
add_def() { KEPT+=("$1"); MOCFLAGS+=("$1"); }
i=0
n=${#RAW[@]}
while [ "$i" -lt "$n" ]; do
	f="${RAW[$i]}"
	case "$f" in
		# drop Qt6 include dirs (both joined and separate forms)
		-I/usr/include/qt6*|-isystem/usr/include/qt6*)
			i=$((i + 1)); continue ;;
		-isystem)
			nxt="${RAW[$((i + 1))]:-}"
			case "$nxt" in
				/usr/include/qt6*|/usr/lib/qt6/*) i=$((i + 2)); continue ;;
			esac
			add_inc "$nxt"; i=$((i + 2)); continue ;;
		-I)
			nxt="${RAW[$((i + 1))]:-}"
			case "$nxt" in
				/usr/include/qt6*|/usr/lib/qt6/*) i=$((i + 2)); continue ;;
			esac
			add_inc "$nxt"; i=$((i + 2)); continue ;;
		-I*)
			case "$f" in
				-I/usr/include/qt6*|/usr/lib/qt6/*) ;;
				-I*) add_inc "${f#-I}" ;;
			esac
			i=$((i + 1)); continue ;;
		-D*)
			add_def "$f"
			i=$((i + 1)); continue ;;
		-std=*)
			KEPT+=("-std=gnu++20")
			i=$((i + 1)); continue ;;
		"$SRC_ABS")
			i=$((i + 1)); continue ;;
		*)
			i=$((i + 1)); continue ;;
	esac
done

case " ${KEPT[*]:-} " in
	*" -std=gnu++20 "*) ;;
	*) KEPT+=("-std=gnu++20") ;;
esac

QT5_INC_DIRS=(
	/usr/include/qt
	/usr/include/qt/QtCore
	/usr/include/qt/QtGui
	/usr/include/qt/QtWidgets
	/usr/include/qt/QtTest
	/usr/include/qt/QtXml
	/usr/include/qt/QtNetwork
	/usr/include/qt/QtSvg
)
QT5_INCLUDES=()
for d in "${QT5_INC_DIRS[@]}"; do
	QT5_INCLUDES+=(-isystem "$d")
	MOCFLAGS+=(-I "$d")
done
QT5_INCLUDES+=(-fPIC)

# --- generate the test's .moc with Qt5 moc ---------------------------------
base="$(basename "$SRC_REL" .cpp)"
"$MOC" -I"$MOC_DIR" "${MOCFLAGS[@]}" "$SRC_ABS" -o "$MOC_DIR/$base.moc" || exit $?

# --- compile --------------------------------------------------------------
CXX="$(command -v clang++ || command -v c++)"
[ -n "$CXX" ] || { echo "no clang++ or c++" >&2; exit 2; }
echo "$CXX -fsyntax-only $SRC_REL (Qt5)"
"$CXX" -fsyntax-only -I"$MOC_DIR" "${KEPT[@]}" "${QT5_INCLUDES[@]}" "$SRC_ABS"
exit $?
```

## Commands and exits

Copied from logs 00-12 and 14 under
`/home/kruzzzzy/.config/kads/zene/state/runs/ci-bbdf307254017268.r1/logs`:

| Step | Command (abbreviated) | EXIT | Source |
| --- | --- | --- | --- |
| 00 | ancestry `4b29f8a94716` -> HEAD; diff test file vs `4b29f8a94716` | 0 | `00-diff-vs-ci-commit.log` (empty) |
| 01 | Qt5 control: `clang++ -fsyntax-only TouchInstrumentSurfaceTest.cpp` | 0 | `01-qt5-control.log` |
| 02 | Qt5 red: `clang++ -fsyntax-only TakeLanesPanelTest.cpp` before fix | 1 | `02-qt5-red.log` |
| 03 | Qt5 green: same, after fix | 0 | `03-qt5-green.log` |
| 04 | Qt6 build `TakeLanesPanelTest` | 0 | `04-build-qt6.log` |
| 05 | `ctest -R '^TakeLanesPanelTest$'` | 0 | `05-ctest-takelanes.log`, 1/1 passed |
| 06 | Qt6 build touch family (+ tripleoscillator) | 0 | `06-build-family.log` |
| 07 | `ctest -R` touch family | 0 | `07-ctest-family.log`, 5/5 passed |
| - | `git apply -R fix.patch` (revert the test fix) | 0 | run log |
| 08 | Qt5 red again on reverted tree | 1 | `08-qt5-reverted-red.log` |
| - | `git apply fix.patch` (restore) | 0 | run log |
| 09 | Qt5 green after restore | 0 | `09-qt5-reapplied-green.log` |
| 10 | Qt6 build after restore | 0 | `10-build-qt6-reapplied.log` |
| 11 | `ctest -R '^TakeLanesPanelTest$'` after restore | 0 | `11-ctest-reapplied.log`, 1/1 passed |
| 14 | `feature_check.py <worktree>` | 0 | `14-feature-check.log`, RESULT: PASS |

In log 08 exactly six `error: call to member function ... is ambiguous` are
reported, at `TakeLanesPanelTest.cpp:374,376,383,385,387,389` (press, release,
press, move, move, release). An earlier worker line list was wrong; these are the
six lines the log actually cites.

## Limit

The windows-arm64 MSYS build was not run. Portability was checked only by
compiling against the same Qt5 header set with clang++ (`-fsyntax-only`), not by
an actual Qt5 link or test run.

## Other findings

- clang reports `-Wunused-lambda-capture` at `TakeLanesPanelTest.cpp:381` (not
  introduced by this fix; the build does not fail on it).
- clang reports `-Wunused-result` for `QTest::qWaitForWindowExposed` at
  `ShortcutsPageTest.cpp:58` and `ShortcutsCommandsTest.cpp:130`; the Qt6
  declaration is `[[nodiscard]]`
  (`/usr/include/qt6/QtWidgets/qtestsupport_widgets.h:25-27`).

## Self-check

- **Regression test fails with the product change reverted, passes restored:**
  reverted Qt5 compile 08 EXIT=1 (six ambiguous-call errors at 374,376,383,385,387,389);
  restored Qt5 compile 09 EXIT=0. Same at the initial pair: 02 EXIT=1, 03 EXIT=0.
  The Qt6 build and ctest pass with the fix (04/05 EXIT=0, 1/1; 11 EXIT=0, 1/1).
- **Determinism:** the test is the existing `TakeLanesPanelTest` touch case; the
  change only adds an explicit `strip` argument. It adds no sleep, wall-clock
  wait or new race; delivery still goes through `commitTouchSequence`.
- **Cause and completeness:** the cause is the omitted optional widget argument
  (`qtesttouch.h` overload pair). All six calls in the swipe test were updated
  (374,376,383,385,387,389). Siblings already pass the widget explicitly
  (`TouchInstrumentSurfaceTest.cpp:132`, `TouchLayoutTest.cpp:525`) and are
  unchanged; the touch-family build/ctest (06/07, 5/5) stays green.
- **No collateral damage:** only test source plus registration/doc changed; no
  product, save/load or command code is touched, so old projects and saved data
  load and render as before.
- **ALREADY-FIXED / FALSE-POSITIVE:** not applicable; verdict is FIXED, with the
  evidence above (red 02/08 vs green 03/09).
- **Sibling commands (`app.about`, `app.ask`, `app.help`, `app.online_help`,
  `app.task_cancel`, `app.tasks`, `app.tour`, `app.tours`,
  `app.update_check_get`, `app.update_check_run`, `app.update_check_set`,
  `app.version`, `app.whats_new`, `arrangement.fit_tracks`,
  `arrangement.get_state`):** not affected. This cluster changes only a test
  file, its upstream-modifications registration and this report; no command or
  control code is in this commit, so none of these commands changes behaviour.
