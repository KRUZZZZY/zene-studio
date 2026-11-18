/*
 * Vst3EditorTest.cpp - a hosted VST3 plug-in's own editor, against the in-tree VST3 fixture
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*! Q050-490: the editor cases of the VST3 host (R4.2, MX-094, Q050-490), in an executable of their
 *  own so that they run on every hosted job that builds the fixture - Linux (X11), Windows (HWND)
 *  and macOS (NSView) - while the rest of the fixture's suite (Vst3InstrumentTest: the MIDI path,
 *  allocation-free process, state) stays where it has always run. The cases were moved here from
 *  Vst3InstrumentTest.cpp unchanged, except the repaint-timer wait (see that case). The default-path
 *  cases use the running platform's window system; the ...OnEveryWindowSystem cases choose each one
 *  explicitly, so all three run on every job. */

#include <QtTest>

#include <QElapsedTimer>
#include <QFileInfo>
#include <QString>

#include <cstdint>
#include <cstdio>

#include "PluginWindowSystem.h"
#include "Vst3Host.h"

#ifndef VST3_TEST_INSTRUMENT_BUNDLE
#define VST3_TEST_INSTRUMENT_BUNDLE ""
#endif

namespace lmms::vst3
{

namespace
{
constexpr double TestSampleRate = 44100.0;
constexpr int TestBlockSize = 512;
} // namespace

class Vst3EditorTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();
	void cleanup();

	void testTheEditorAttachesResizesAndDetaches();
	void testTheEditorOpensAtTheDeviceScale();
	void testTheEditorEmbedsOnEveryWindowSystem();
	void testAnEditorThatCannotEmbedIsRefusedByName();
	void testTheDeviceScaleOnEveryWindowSystem();

private:
	HostedPlugin m_plugin;
};

void Vst3EditorTest::initTestCase()
{
	const QString bundle = QStringLiteral(VST3_TEST_INSTRUMENT_BUNDLE);
	if (bundle.isEmpty() || !QFileInfo::exists(bundle))
	{
		QSKIP("no VST3 instrument fixture available (configure with "
			"-DWANT_VST3_TEST_INSTRUMENT=ON and a VST3 SDK checkout)");
	}
	QString error;
	QVERIFY2(m_plugin.load(bundle, QString{}, &error), qPrintable(error));
	QVERIFY(m_plugin.isLoaded());
	QVERIFY2(m_plugin.prepare(TestSampleRate, TestBlockSize, &error), qPrintable(error));
}

void Vst3EditorTest::cleanupTestCase()
{
	m_plugin.release();
}

//! Q050-490: a failed editor case must not leave its fixture switches or window system to the next one.
void Vst3EditorTest::cleanup()
{
	m_plugin.closeEditor();
	qunsetenv("VST3_TEST_EDITOR_PLATFORM");
	qunsetenv("VST3_TEST_EDITOR_REQUEST_RESIZE");
	qunsetenv("VST3_TEST_EDITOR_NO_CONTENT_SCALE");
	m_plugin.setEditorScale(1.0);
	m_plugin.setEditorWindowSystem(currentPluginWindowSystem());
}

/*! R4.2's acceptance, offscreen: the fixture's editor attaches, resizes (honouring the view's
 *  own minimum), runs its repaint timer on the host's loop through Linux::IRunLoop, detaches,
 *  and attaches again - no leak, no double release (a release too many is a crash here). The
 *  test runs without a display server, so the parent is a stand-in id the fixture does not draw
 *  into; the manual check with a real plug-in is what proves the embedding itself. The window
 *  system is the running platform's: X11 on Linux, HWND on Windows, NSView on macOS. */
void Vst3EditorTest::testTheEditorAttachesResizesAndDetaches()
{
	void* const standInWindow = reinterpret_cast<void*>(std::uintptr_t{0x2a});
	QString error;
	QVERIFY2(m_plugin.openEditor(standInWindow, &error), qPrintable(error));
	QVERIFY(m_plugin.editorOpen());
	int width = 0;
	int height = 0;
	m_plugin.editorSize(&width, &height);
	QCOMPARE(width, 400);
	QCOMPARE(height, 300);
	QVERIFY(m_plugin.resizeEditor(640, 480));
	m_plugin.editorSize(&width, &height);
	QCOMPARE(width, 640);
	QCOMPARE(height, 480);
	QVERIFY(m_plugin.resizeEditor(10, 10));  // the view's constraint raises it to its minimum
	m_plugin.editorSize(&width, &height);
	QCOMPARE(width, 100);
	QCOMPARE(height, 80);

	QCOMPARE(m_plugin.editorTimerCount(), 1);
	const std::uint32_t before = m_plugin.editorTimerDeliveries();
	QElapsedTimer clock;
	clock.start();
	// Five calls however long a loaded runner's loop takes (as ClapHostTest's timer case: the hosted
	// macOS VM delivered 2-4 in a fixed 150 ms), and never faster than the 10 ms the editor asked for.
	QTRY_VERIFY_WITH_TIMEOUT(m_plugin.editorTimerDeliveries() - before >= 5, 3000);
	const std::uint32_t delivered = m_plugin.editorTimerDeliveries() - before;
	const qint64 elapsed = clock.elapsed();
	std::printf("VST3_EDITOR_EVIDENCE 10 ms repaint timer: %u onTimer calls in %lld ms\n", delivered,
		static_cast<long long>(elapsed));
	QVERIFY2(delivered <= elapsed / 10 + 1,
		qPrintable(QStringLiteral("%1 onTimer calls in %2 ms: faster than 10 ms").arg(delivered).arg(elapsed)));

	m_plugin.closeEditor();
	QVERIFY(!m_plugin.editorOpen());
	QCOMPARE(m_plugin.editorTimerCount(), 0);
	QVERIFY2(m_plugin.openEditor(standInWindow, &error), qPrintable(error));
	QCOMPARE(m_plugin.editorTimerCount(), 1);
	m_plugin.closeEditor();
}

/*! 0.5.0 MX-094: the editor opens at the device's zoom. A view that scales itself
 *  (IPlugViewContentScaleSupport) is told the factor and resizes through the frame; a view without
 *  it is given the scaled size by the host, through its own size constraint; at 1 nothing is asked
 *  of the view at all. The fixture's view is 400 x 300 and never smaller than 100 x 80. */
void Vst3EditorTest::testTheEditorOpensAtTheDeviceScale()
{
	void* const standInWindow = reinterpret_cast<void*>(std::uintptr_t{0x2a});
	QString error;
	int width = 0;
	int height = 0;

	m_plugin.setEditorScale(1.5);
	QVERIFY2(m_plugin.openEditor(standInWindow, &error), qPrintable(error));
	m_plugin.editorSize(&width, &height);
	QCOMPARE(width, 600);
	QCOMPARE(height, 450);
	QVERIFY(m_plugin.editorScaling() == EditorScaling::ByView);
	m_plugin.closeEditor();

	qputenv("VST3_TEST_EDITOR_NO_CONTENT_SCALE", "1");
	m_plugin.setEditorScale(0.5);
	QVERIFY2(m_plugin.openEditor(standInWindow, &error), qPrintable(error));
	m_plugin.editorSize(&width, &height);
	QCOMPARE(width, 200);
	QCOMPARE(height, 150);
	QVERIFY(m_plugin.editorScaling() == EditorScaling::ByWindow);
	m_plugin.closeEditor();
	qunsetenv("VST3_TEST_EDITOR_NO_CONTENT_SCALE");

	m_plugin.setEditorScale(1.0);
	QVERIFY2(m_plugin.openEditor(standInWindow, &error), qPrintable(error));
	m_plugin.editorSize(&width, &height);
	QCOMPARE(width, 400);
	QCOMPARE(height, 300);
	QVERIFY(m_plugin.editorScaling() == EditorScaling::Unscaled);
	m_plugin.closeEditor();
	std::printf("VST3_EDITOR_SCALE_EVIDENCE 1.5 by the view -> 600x450, 0.5 by the window -> 200x150, 1 -> 400x300\n");
}

namespace
{
//! Q050-490: the three window systems, with the name and IPlugView platform type a refusal carries.
struct WindowCase
{
	PluginWindowSystem system;
	const char* name;
	const char* type;
};
constexpr WindowCase WindowCases[] = {
	{PluginWindowSystem::X11, "X11", "X11EmbedWindowID"},
	{PluginWindowSystem::Win32, "Windows", "HWND"},
	{PluginWindowSystem::Cocoa, "macOS", "NSView"},
};
} // namespace

/*! Q050-490: the editor on each window system - the fixture takes ONLY that platform type (and is
 *  attached with it, or refuses), so an editor that opens was asked for and attached with it. Open,
 *  host resize and the view's minimum, a resize the plug-in asks for through the frame
 *  (IPlugFrame::resizeView), close and reopen. Chosen explicitly so all three run on any machine. */
void Vst3EditorTest::testTheEditorEmbedsOnEveryWindowSystem()
{
	void* const standInWindow = reinterpret_cast<void*>(std::uintptr_t{0x2a});
	for (const WindowCase& each : WindowCases)
	{
		qputenv("VST3_TEST_EDITOR_PLATFORM", each.type);
		m_plugin.setEditorWindowSystem(each.system);
		QString error;
		QVERIFY2(m_plugin.openEditor(standInWindow, &error), qPrintable(QStringLiteral("%1: %2").arg(QLatin1String(each.name), error)));
		int width = 0;
		int height = 0;
		m_plugin.editorSize(&width, &height);
		QCOMPARE(width, 400);
		QCOMPARE(height, 300);
		QVERIFY(m_plugin.resizeEditor(640, 480));
		m_plugin.editorSize(&width, &height);
		QCOMPARE(width, 640);
		QCOMPARE(height, 480);
		QVERIFY(m_plugin.resizeEditor(10, 10));
		m_plugin.editorSize(&width, &height);
		QCOMPARE(width, 100);
		QCOMPARE(height, 80);
		m_plugin.closeEditor();
		QVERIFY(!m_plugin.editorOpen());

		qputenv("VST3_TEST_EDITOR_REQUEST_RESIZE", "520x390");
		QVERIFY2(m_plugin.openEditor(standInWindow, &error), qPrintable(QStringLiteral("%1 reopen: %2").arg(QLatin1String(each.name), error)));
		qunsetenv("VST3_TEST_EDITOR_REQUEST_RESIZE");
		m_plugin.editorSize(&width, &height);
		QCOMPARE(width, 520);
		QCOMPARE(height, 390);
		m_plugin.closeEditor();
		std::printf("VST3_EDITOR_PLATFORM_EVIDENCE %s (%s): open 400x300, resize 640x480, min 100x80, close, "
			"reopen, plug-in resize 520x390\n", each.name, each.type);
	}
	qunsetenv("VST3_TEST_EDITOR_PLATFORM");
	m_plugin.setEditorWindowSystem(currentPluginWindowSystem());
}

/*! Q050-490: a view that cannot embed in this platform's window is refused with the platform and
 *  the platform type named, and nothing stays open; a session with no window to embed in (native
 *  Wayland) is refused before a view is created. */
void Vst3EditorTest::testAnEditorThatCannotEmbedIsRefusedByName()
{
	void* const standInWindow = reinterpret_cast<void*>(std::uintptr_t{0x2a});
	for (const WindowCase& each : WindowCases)
	{
		qputenv("VST3_TEST_EDITOR_PLATFORM", each.system == PluginWindowSystem::X11 ? "HWND" : "X11EmbedWindowID");
		m_plugin.setEditorWindowSystem(each.system);
		QString error;
		QVERIFY2(!m_plugin.openEditor(standInWindow, &error), each.name);
		QVERIFY(!m_plugin.editorOpen());
		const QString expected = QStringLiteral("cannot embed on %1 (VST3 platform type %2)").arg(QLatin1String(each.name),
			QLatin1String(each.type));
		QVERIFY2(error.contains(expected), qPrintable(error));
		std::printf("VST3_EDITOR_REFUSAL_EVIDENCE %s\n", qPrintable(error));
	}
	qunsetenv("VST3_TEST_EDITOR_PLATFORM");
	m_plugin.setEditorWindowSystem(PluginWindowSystem::None);
	QString error;
	QVERIFY(!m_plugin.openEditor(standInWindow, &error));
	QVERIFY(!m_plugin.editorOpen());
	QVERIFY2(error.contains(QLatin1String("no native window a plug-in editor can embed in")), qPrintable(error));
	m_plugin.setEditorWindowSystem(currentPluginWindowSystem());
}

/*! Q050-490 (MX-094 per platform): on X11 and Windows a view that scales itself is told the zoom
 *  (IPlugViewContentScaleSupport - the SDK's interface for physical-pixel platforms); on macOS the
 *  SDK keeps it out (NSView sizes are logical), so the host gives the view the scaled size instead. */
void Vst3EditorTest::testTheDeviceScaleOnEveryWindowSystem()
{
	void* const standInWindow = reinterpret_cast<void*>(std::uintptr_t{0x2a});
	m_plugin.setEditorScale(1.5);
	for (const WindowCase& each : WindowCases)
	{
		m_plugin.setEditorWindowSystem(each.system);
		QString error;
		QVERIFY2(m_plugin.openEditor(standInWindow, &error), qPrintable(QStringLiteral("%1: %2").arg(QLatin1String(each.name), error)));
		int width = 0;
		int height = 0;
		m_plugin.editorSize(&width, &height);
		QCOMPARE(width, 600);
		QCOMPARE(height, 450);
		const EditorScaling expected = each.system == PluginWindowSystem::Cocoa ? EditorScaling::ByWindow : EditorScaling::ByView;
		QVERIFY2(m_plugin.editorScaling() == expected, each.name);
		m_plugin.closeEditor();
	}
	m_plugin.setEditorScale(1.0);
	m_plugin.setEditorWindowSystem(currentPluginWindowSystem());
}

} // namespace lmms::vst3

QTEST_GUILESS_MAIN(lmms::vst3::Vst3EditorTest)
#include "Vst3EditorTest.moc"
