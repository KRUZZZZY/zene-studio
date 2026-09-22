Before/after counts for UI precondition #1 (branch 040/ui-precond).
BEFORE = git grep on HEAD = base ab9462d56 (nothing committed yet); AFTER = worktree.

1) accessible name/description calls, whole tree (src/ include/ tests/, *.cpp *.h):
   before: 0
   after:  5
   after, via the reusable helper lmms::a11y call sites: 23
   files carrying those call sites:
     src/gui/FocusDesk.cpp
     src/gui/FocusDeskPane.cpp
     src/gui/FocusDeskPlacement.cpp
     src/gui/MainWindow.cpp
     src/gui/widgets/ToolButton.cpp

2) hardcoded QColor( literals in src/gui/LmmsStyle.cpp:
   before: HEAD:src/gui/LmmsStyle.cpp:13
   after:  0
   before breakdown by line: src/gui/LmmsStyle.cpp:199:					QColor( 255, 255, 255 ) : src/gui/LmmsStyle.cpp:200:						QColor( 192, 192, 192 ) ); src/gui/LmmsStyle.cpp:202:							QColor( 64, 64, 64 ) ); src/gui/LmmsStyle.cpp:211:		so.palette.setColor(QPalette::Button, QColor(223, 228, 236)); src/gui/LmmsStyle.cpp:237:		QColor black = QColor( 0, 0, 0 ); src/gui/LmmsStyle.cpp:383:			color = QColor( 75, 75, 75 ); src/gui/LmmsStyle.cpp:384:			blend = QColor( 65, 65, 65 ); src/gui/LmmsStyle.cpp:388:			color = QColor( 100, 100, 100 ); src/gui/LmmsStyle.cpp:389:			blend = QColor( 75, 75, 75 ); src/gui/LmmsStyle.cpp:393:			color = QColor( 21, 21, 21 ); src/gui/LmmsStyle.cpp:394:			blend = QColor( 33, 33, 33 ); src/gui/LmmsStyle.cpp:399:		color = QColor( 21, 21, 21 ); src/gui/LmmsStyle.cpp:400:		blend = QColor( 33, 33, 33 ); 
   spec's claimed region 197-211 held 4 of them (lines 199,200,202,211); spec figure '~26 in 197-211' not reproducible. src/gui-wide CONTEXT (measured): before 49 occurrences / 20 files; after 36 / 19.

3) .darker(132):
   before: HEAD:src/gui/widgets/TabWidget.cpp:64:	QColor bg_color = QApplication::palette().color(QPalette::Active, QPalette::Window).darker(132); 
   after (code): 0

4) setFocusPolicy in src/gui (files / lines):
   before: 30
        files: 21
   after:  37
        files: 24
   setTabOrder call sites in src/gui before: 0
   setTabOrder call sites in src/gui after:  5

5) QStyleHints colourScheme wiring:
   53:#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
   153:#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
   155:	// QStyleHints::colorSchemeChanged(Qt::ColorScheme), whose notifier this
   161:	connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
