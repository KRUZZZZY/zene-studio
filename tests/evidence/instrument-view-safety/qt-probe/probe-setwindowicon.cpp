// Standalone probe: characterise Qt 6.4's QWidget::setWindowIcon behaviour on
// the shapes InstrumentView::setModel produces. Not part of the build; a
// scratch experiment whose output is quoted in docs/INSTRUMENT-VIEW-SAFETY.md.
//
//   g++ -fPIC probe-setwindowicon.cpp -o probe-setwindowicon $(pkg-config --cflags --libs Qt6Widgets)
//   QT_QPA_PLATFORM=offscreen ./probe-setwindowicon        # case: <which>
//
// Exit 0 = no crash. SIGSEGV = the crash under investigation.

#include <QApplication>
#include <QIcon>
#include <QPixmap>
#include <QWidget>
#include <QDebug>

int main(int argc, char** argv)
{
	QApplication app(argc, argv);

	const auto which = argc > 1 ? QString::fromLatin1(argv[1]) : QStringLiteral("all");
	const QIcon icon{QPixmap{1, 1}};   // what loadPixmap() returns on a miss

	if (which == QStringLiteral("toplevel") || which == QStringLiteral("all"))
	{
		qInfo() << "case toplevel: setWindowIcon on a top-level QWidget";
		auto* w = new QWidget;
		w->setWindowIcon(icon);
		qInfo() << "case toplevel: ok";
	}

	if (which == QStringLiteral("child") || which == QStringLiteral("all"))
	{
		qInfo() << "case child: setWindowIcon on a NON-top-level child widget";
		auto* w = new QWidget;
		auto* c = new QWidget(w);
		c->setWindowIcon(icon);
		qInfo() << "case child: ok";
	}

	if (which == QStringLiteral("stacked") || which == QStringLiteral("all"))
	{
		// the InstrumentTrackWindow shape: a widget inside a widget inside a
		// window (the real window becomes a QMdiSubWindow child, i.e. is itself
		// non-top-level by the time setWindowIcon() runs)
		qInfo() << "case stacked: setWindowIcon two levels down";
		auto* mid = new QWidget;
		auto* tab = new QWidget(mid);
		tab->setWindowIcon(icon);
		qInfo() << "case stacked: ok";
	}

	if (which == QStringLiteral("null") || which == QStringLiteral("all"))
	{
		qInfo() << "case null: dynamic_cast of a widget that is not an InstrumentTrackWindow";
		auto* w = new QWidget;
		auto* c = new QWidget(w);
		// this is literally instrumentTrackWindow() when the view's parent is
		// one level below a widget that is not an InstrumentTrackWindow:
		//   c->parentWidget()          == w
		//   w->parentWidget()          == nullptr   -> used to be dynamic_cast'd
		QWidget* window = c->parentWidget()->parentWidget();
		qInfo() << "case null: derived window pointer =" << window;
		window->setWindowIcon(icon);   // null deref, exactly as InstrumentView does
		qInfo() << "case null: ok (this line is unreachable if the crash reproduces)";
	}

	qInfo() << "probe finished without a crash";
	return 0;
}
