/*
 * CarlaVst2HostingTest.cpp - VST2 instruments hosted through Carla, native and
 *                            through Carla's Wine bridge (Q050-481.1).
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

/*
 * The subject is the product's own path, end to end: the build's carlavst2
 * module, a REAL Carla runtime (tools/build-carla-runtime.sh, found through
 * LD_LIBRARY_PATH from tests/CMakeLists.txt), and the in-tree VST2 test
 * instrument (tests/data/vst2-test-instrument) built twice - a native .so
 * Carla loads in process and a Windows .dll Carla can only run through Wine.
 *
 * The fixture makes every host feature visible from outside: while a key is
 * held it outputs (left, right) = (Level, Tone), its two parameters; it counts
 * note-ons in its chunk only; its chunk says whether its editor is open.
 *
 * Everything goes through what a user or an agent uses - plugin.list,
 * plugin.load, plugin.param_*, plugin.preset_*, plugin.editor_*, automation.*,
 * the device card's "Replace Instrument..." and the instrument view's editor
 * button - and sound is read from the engine's own render (renderNextPeriod).
 * Without a Carla runtime (an ordinary build: the weak stand-in hosts nothing)
 * the cases report Skipped, naming what is missing.
 */

#include <QtTest>

#include <QApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>

#include "CarlaVst2TestSupport.h"

class CarlaVst2HostingTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		QVERIFY(m_plugins.isValid());
#if defined(LMMS_TEST_PLUGIN_DIR) && defined(_WIN32)
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // no runtime is built there: every case skips
#elif defined(LMMS_TEST_PLUGIN_DIR)
		// The build's modules, minus the weak Carla stand-in that sits beside them (plugins/CarlaBase builds it
		// there when no system Carla is found; an installed product keeps it in optional/, which is not scanned).
		// The factory would dlopen it as a module candidate first, and the Carla modules would then bind to it
		// by its soname instead of to the real runtime on the loader path.
		for (const QFileInfo& module : QDir(QStringLiteral(LMMS_TEST_PLUGIN_DIR)).entryInfoList(QDir::Files))
		{
			if (module.fileName().startsWith(QLatin1String("libcarla_native-plugin"))) { continue; }
			QFile::link(module.absoluteFilePath(), QDir(m_plugins.path()).filePath(module.fileName()));
		}
		qputenv("LMMS_PLUGIN_DIR", m_plugins.path().toLocal8Bit());
#endif
		for (const char* name : {"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"}) { qputenv(name, m_home.path().toLocal8Bit()); }
		// Exactly the two fixtures, nothing the machine has installed.
		qputenv("ZENE_VST2_PATH", (QStringLiteral(CARLA_VST2_NATIVE_FIXTURE_DIR) + QDir::listSeparator()
			+ QStringLiteral(CARLA_VST2_WINE_FIXTURE_DIR)).toLocal8Bit());
		// Wine: a prefix of the build's own (kept between runs: creating one takes minutes), and no Mono/Gecko
		// installer prompt on a fresh one.
		if (!QStringLiteral(ZENE_TEST_WINEPREFIX).isEmpty()) { qputenv("WINEPREFIX", ZENE_TEST_WINEPREFIX); }
		qputenv("WINEDLLOVERRIDES", "mscoree,mshtml=");
		qputenv("WINEDEBUG", "-all");
		bootWinePrefix();
		keepWineServer();
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the cases render periods themselves
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ShellSelection::instance()->setTrack(nullptr);
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! AC1: plugin.list offers the native fixture as a loadable VST2 instrument, with its vendor and unique id, and
	//! the bridged one as a Windows binary Carla runs through Wine; VST2 comes after every other format.
	void vst2InstrumentsAreListed()
	{
		if (const QString why = missingRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		const QJsonObject native = vst2Device(NativeFile);
		QCOMPARE(native.value(QStringLiteral("format")).toString(), QStringLiteral("vst2"));
		QCOMPARE(native.value(QStringLiteral("kind")).toString(), QStringLiteral("instrument"));
		QVERIFY(native.value(QStringLiteral("loadable")).toBool());
		QCOMPARE(native.value(QStringLiteral("display_name")).toString(), QStringLiteral("Zene VST2 Test Instrument"));
		QCOMPARE(native.value(QStringLiteral("vendor")).toString(), QStringLiteral("Zene Studio Tests"));
		QCOMPARE(native.value(QStringLiteral("unique_id")).toString(), QStringLiteral("1517180466"));  // 'ZnV2'
		QCOMPARE(native.value(QStringLiteral("binary")).toString(), QStringLiteral("native"));
		QCOMPARE(native.value(QStringLiteral("bridged")).toBool(), false);
		if (wineAvailable())
		{
			const QJsonObject bridged = vst2Device(WineFile);
			QCOMPARE(bridged.value(QStringLiteral("binary")).toString(), QStringLiteral("win64"));
			QCOMPARE(bridged.value(QStringLiteral("bridged")).toBool(), true);
			QCOMPARE(bridged.value(QStringLiteral("kind")).toString(), QStringLiteral("instrument"));
		}
		const QJsonArray all = run(QStringLiteral("plugin.list")).result.value(QStringLiteral("devices")).toArray();
		QVERIFY(!all.isEmpty());
		QCOMPARE(all.last().toObject().value(QStringLiteral("format")).toString(), QStringLiteral("vst2"));
	}

	//! AC1: the GUI - the device card's "Replace Instrument..." opens the device picker, which shows the VST2 row
	//! (format VST2); accepting it loads the instrument through plugin.load.
	void theDeviceCardReplacesTheInstrumentWithAVst2()
	{
		if (const QString why = missingRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		InstrumentTrack* track = newTrack();
		track->loadInstrument(QStringLiteral("tripleoscillator"));
		QVERIFY(track->instrument() != nullptr);
		DevicePanel panel;
		QMenu* menu = cardMenu(&panel, track);
		QVERIFY2(menu != nullptr, "the instrument has no device card with an action menu");
		QAction* replace = lmms::test::menuAction(menu, {QStringLiteral("Replace Instrument...")});
		QVERIFY(replace != nullptr);
		QString row;
		QString picked;
		onceModal([&row, &picked](QWidget* modal) {
			auto* picker = qobject_cast<DevicePicker*>(modal);
			if (picker == nullptr) { if (modal != nullptr) { modal->close(); } return; }
			picker->setQuery(QStringLiteral("Zene VST2 Test Instrument VST2"));
			row = picker->shownRowText(0);
			picked = picker->selectedDevice();
			if (picked.isEmpty()) { picker->reject(); return; }
			static_cast<QDialog*>(picker)->accept();
		});
		replace->trigger();
		QVERIFY2(row.contains(QStringLiteral("VST2")), qPrintable(row));
		QCOMPARE(picked, vst2Device(NativeFile).value(QStringLiteral("id")).toString());
		QCoreApplication::processEvents();
		QVERIFY(track->instrument() != nullptr);
		QCOMPARE(QString::fromLatin1(track->instrument()->descriptor()->name), QStringLiteral("carlavst2"));
		QCOMPARE(track->instrument()->displayName(), QStringLiteral("Zene VST2 Test Instrument"));
		ShellSelection::instance()->setTrack(nullptr);
		delete track;
	}

	//! AC1/AC2/AC3 native: plugin.load hosts the native fixture through Carla; the engine renders its sound with
	//! the parameters plugin.param_* reads and writes; plugin.param_set is one undo step.
	void pluginLoadHostsTheNativeInstrumentAndItPlays()
	{
		if (const QString why = missingRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		InstrumentTrack* track = loadOnNewTrack(NativeFile);
		QVERIFY(track != nullptr);
		QCOMPARE(track->instrument()->parameterCount(), 2);
		QCOMPARE(track->instrument()->parameterName(0), QStringLiteral("Level"));
		QCOMPARE(track->instrument()->parameterName(1), QStringLiteral("Tone"));
		QVERIFY(std::abs(paramValue(track, 0) - DefaultLevel) < Tolerance);

		Rendered sound = renderNote(track, NativeRenderMs);
		QVERIFY2(sound.sounded, "the hosted VST2 rendered nothing");
		const float defaultRatio = sound.left / sound.right;
		QVERIFY2(std::abs(defaultRatio - DefaultLevel / DefaultTone) < 1e-3f, qPrintable(QString::number(defaultRatio)));

		QJsonObject set = address(track);
		set.insert(QStringLiteral("index"), 0);
		set.insert(QStringLiteral("value"), 0.75);
		const ControlResult written = run(QStringLiteral("plugin.param_set"), set);
		QVERIFY2(written.ok, qPrintable(written.errorMessage));
		sound = renderNote(track, NativeRenderMs);
		QVERIFY(std::abs(sound.left / sound.right - 0.75f / DefaultTone) < 1e-3f);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(std::abs(paramValue(track, 0) - DefaultLevel) < Tolerance);
		sound = renderNote(track, NativeRenderMs);
		QVERIFY(std::abs(sound.left / sound.right - defaultRatio) < 1e-3f);
		delete track;
	}

	void automationDrivesAVst2Parameter_data()
	{
		QTest::addColumn<QString>("file");
		QTest::newRow("native") << NativeFile;
		QTest::newRow("wine-bridged") << WineFile;
	}

	//! AC2 + realtime (review r1): automation written on a VST2 parameter (automation.add_point) drives the plug-in
	//! during playback, natively and through the Wine bridge, and the render thread never emits the parameter's
	//! change signal - the only path into Carla's UI callbacks (for this fixture, which has its own editor, the
	//! bridge's mutex-taking uiParameterChange). The change is emitted afterwards on this (the GUI) thread.
	void automationDrivesAVst2Parameter()
	{
		QFETCH(QString, file);
		if (const QString why = missingRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		const bool bridged = file == WineFile;
		if (bridged && !wineAvailable()) { QSKIP("no Wine, no Windows PE fixture or no Carla Wine bridge in the runtime"); }
		InstrumentTrack* track = loadOnNewTrack(file);
		QVERIFY(track != nullptr);
		QJsonObject point{{QStringLiteral("track"), control::trackIdOf(track)}, {QStringLiteral("parameter"), QStringLiteral("inst/1")},
			{QStringLiteral("ticks"), 0}, {QStringLiteral("value"), 0.125}};
		const ControlResult added = run(QStringLiteral("automation.add_point"), point);
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		AutomatableModel* tone = track->instrument()->parameterModel(1);
		QVERIFY(tone != nullptr);
		int onRenderThread = 0;
		int elsewhere = 0;
		const QMetaObject::Connection probe = connect(tone, &Model::dataChanged, this, [&onRenderThread, &elsewhere] {
			++(Engine::audioEngine()->isRenderThread() ? onRenderThread : elsewhere);
		}, Qt::DirectConnection);
		const Rendered sound = renderNote(track, bridged ? BridgedRenderMs : NativeRenderMs);
		QVERIFY(sound.sounded);
		// Tone (the right channel) follows the automation; Level stays.
		QVERIFY2(std::abs(sound.right - sound.left * (0.125f / DefaultLevel)) < 1e-3f,
			qPrintable(QStringLiteral("%1 %2").arg(sound.left).arg(sound.right)));
		QCOMPARE(onRenderThread, 0);
		QTRY_VERIFY_WITH_TIMEOUT(elsewhere > 0, 2000);  // the deferred change, emitted by the instrument's GUI timer
		disconnect(probe);
		// The automation lives on a track of its own: it goes with the instrument, so no later case renders a
		// clip whose parameter is gone.
		const TrackContainer::TrackList tracks = Engine::getSong()->tracks();
		for (Track* other : tracks)
		{
			if (other->type() == Track::Type::Automation) { delete other; }
		}
		delete track;
	}

	void theAudioThreadNeverWaitsForThePlugin_data() { automationDrivesAVst2Parameter_data(); }

	//! Realtime (review r1): the engine's render period never waits for Carla - not for a native plug-in, and not
	//! for the Wine bridge, whose process call waits on the bridge's reply. The fixture is made to stall 800 ms in
	//! every process call (ZENE_VST2_FIXTURE_STALL_MS); while a note plays, every period of the engine still
	//! renders in a fraction of that.
	void theAudioThreadNeverWaitsForThePlugin()
	{
		QFETCH(QString, file);
		if (const QString why = missingRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		if (file == WineFile && !wineAvailable()) { QSKIP("no Wine, no Windows PE fixture or no Carla Wine bridge in the runtime"); }
		qputenv("ZENE_VST2_FIXTURE_STALL_MS", QByteArray::number(StallMs));
		InstrumentTrack* track = loadOnNewTrack(file);
		qunsetenv("ZENE_VST2_FIXTURE_STALL_MS");
		QVERIFY(track != nullptr);
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		clip->addNote(Note(TimePos(16 * DefaultTicksPerBar), TimePos(0), 60, 100), false);
		Song* song = Engine::getSong();
		song->setPlayPos(0, Song::PlayMode::Song);
		MidiPlayback::instance().stopped();
		song->playSong();
		qint64 worst = 0;
		for (int period = 0; period < 12; ++period)
		{
			QElapsedTimer timer;
			timer.start();
			Engine::audioEngine()->renderNextPeriod();
			worst = std::max(worst, timer.elapsed());
			QTest::qWait(5);
		}
		song->stop();
		for (int period = 0; period < 8; ++period) { Engine::audioEngine()->renderNextPeriod(); }
		delete clip;
		delete track;
		qInfo("worst render period with the plug-in stalling %lld ms per process call: %lld ms", StallMs, worst);
		QVERIFY2(worst < StallMs / 2, qPrintable(QStringLiteral("a render period took %1 ms").arg(worst)));
	}

	//! AC2: the plug-in's own editor, from the agent's plugin.editor_open/close and from the instrument view's
	//! button: Carla embeds it (the fixture's effEditOpen, seen in its chunk) in a window of its reported size.
	void theInstrumentsOwnEditorOpensThroughCarla()
	{
		if (const QString why = missingRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		InstrumentTrack* track = loadOnNewTrack(NativeFile);
		QVERIFY(track != nullptr);
		Instrument* instrument = track->instrument();
		const bool headless = ControlRegistry::instance()->isHeadless();
		ControlRegistry::instance()->setHeadless(false);
		const ControlResult opened = run(QStringLiteral("plugin.editor_open"), address(track));
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		QVERIFY(opened.result.value(QStringLiteral("open")).toBool());
		QCOMPARE(chunkField(instrument, QStringLiteral("editor")), 1);
		QWidget* window = nullptr;
		for (QWidget* top : QApplication::topLevelWidgets())
		{
			if (top->isVisible() && top->windowTitle() == instrument->displayName()) { window = top; }
		}
		QVERIFY2(window != nullptr, "no window titled after the instrument is showing");
		QVERIFY(window->width() >= 240);
		QVERIFY(window->height() >= 120);
		const ControlResult closed = run(QStringLiteral("plugin.editor_close"), address(track));
		QVERIFY(closed.ok);
		QVERIFY(closed.result.value(QStringLiteral("was_open")).toBool());
		QCOMPARE(chunkField(instrument, QStringLiteral("editor")), 0);

		// The GUI: the instrument view's "Show plugin editor" button sends the same command.
		QWidget parent;
		PluginView* view = instrument->createView(&parent);
		QVERIFY(view != nullptr);
		QToolButton* show = nullptr;
		for (QToolButton* button : view->findChildren<QToolButton*>())
		{
			if (button->property("controlCommand").toString() == QLatin1String("plugin.editor_open")) { show = button; }
		}
		QVERIFY2(show != nullptr, "the VST2 instrument's view has no plugin-editor button");
		QTest::mouseClick(show, Qt::LeftButton);
		QVERIFY2(instrument->nativeEditorOpen(), "the view's button did not open the editor");
		QCOMPARE(chunkField(instrument, QStringLiteral("editor")), 1);
		instrument->closeNativeEditor();
		QCOMPARE(chunkField(instrument, QStringLiteral("editor")), 0);
		delete view;
		ControlRegistry::instance()->setHeadless(headless);
		delete track;
	}

	//! AC2: project state - the track saved and loaded into a new track brings back the same plug-in with its
	//! parameters AND its chunk (the note-on count no parameter carries), and it renders the same.
	void projectStateRoundTripsThroughCarla()
	{
		if (const QString why = missingRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		roundTrip(NativeFile, NativeRenderMs);
	}

	//! AC2: presets - plugin.preset_save, a change, plugin.preset_load: the saved values come back.
	void presetsSaveAndLoad()
	{
		if (const QString why = missingRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		InstrumentTrack* track = loadOnNewTrack(NativeFile);
		QVERIFY(track != nullptr);
		QJsonObject set = address(track);
		set.insert(QStringLiteral("index"), 1);
		set.insert(QStringLiteral("value"), 0.625);
		QVERIFY(run(QStringLiteral("plugin.param_set"), set).ok);
		QJsonObject save = address(track);
		save.insert(QStringLiteral("name"), QStringLiteral("vst2-tone"));
		const ControlResult saved = run(QStringLiteral("plugin.preset_save"), save);
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		set.insert(QStringLiteral("value"), 0.0625);
		QVERIFY(run(QStringLiteral("plugin.param_set"), set).ok);
		QVERIFY(std::abs(paramValue(track, 1) - 0.0625) < Tolerance);
		const ControlResult loaded = run(QStringLiteral("plugin.preset_load"), save);
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		QVERIFY2(std::abs(paramValue(track, 1) - 0.625) < Tolerance, qPrintable(QString::number(paramValue(track, 1))));
		const Rendered sound = renderNote(track, NativeRenderMs);
		QVERIFY(std::abs(sound.right / sound.left - 0.625f / DefaultLevel) < 1e-3f);
		delete track;
	}

	//! AC3 bridged: the Windows fixture loads through Carla's Wine bridge, renders with its parameters, and its
	//! state (parameters and chunk) round-trips through a project like the native one.
	void theWindowsFixtureRunsThroughTheWineBridge()
	{
		if (const QString why = missingRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		if (!wineAvailable()) { QSKIP("no Wine, no Windows PE fixture or no Carla Wine bridge in the runtime: the bridged path cannot run here"); }
		InstrumentTrack* track = loadOnNewTrack(WineFile);
		QVERIFY(track != nullptr);
		QCOMPARE(track->instrument()->parameterName(0), QStringLiteral("Level"));
		QJsonObject set = address(track);
		set.insert(QStringLiteral("index"), 0);
		set.insert(QStringLiteral("value"), 0.875);
		QVERIFY(run(QStringLiteral("plugin.param_set"), set).ok);
		const Rendered sound = renderNote(track, BridgedRenderMs);
		QVERIFY2(sound.sounded, "the bridged VST2 rendered nothing");
		QVERIFY2(std::abs(sound.left / sound.right - 0.875f / DefaultTone) < 1e-3f,
			qPrintable(QStringLiteral("%1 %2").arg(sound.left).arg(sound.right)));
		delete track;
		roundTrip(WineFile, BridgedRenderMs);
	}

private:
	//! A fresh prefix is created on Wine's first start, which can take minutes: do it once, here, rather
	//! than inside Carla's bridge start-up timeout.
	static void bootWinePrefix()
	{
		const QString prefix = QString::fromLocal8Bit(qgetenv("WINEPREFIX"));
		const QString wineboot = QStandardPaths::findExecutable(QStringLiteral("wineboot"));
		if (prefix.isEmpty() || wineboot.isEmpty() || !QFileInfo::exists(wineFixture())
			|| QFileInfo::exists(QDir(prefix).filePath(QStringLiteral("system.reg"))))
		{
			return;
		}
		QProcess boot;
		boot.start(wineboot, {QStringLiteral("-u")});
		if (!boot.waitForFinished(600000)) { boot.kill(); }
	}

	//! A wineserver that outlives each bridge by a minute, so a bridge's start-up does not include starting
	//! the server too (measured: on a loaded machine that start can outlast Carla's first exchange).
	static void keepWineServer()
	{
		const QString wineserver = QStandardPaths::findExecutable(QStringLiteral("wineserver"));
		if (wineserver.isEmpty() || !QFileInfo::exists(wineFixture())) { return; }
		QProcess::execute(wineserver, {QStringLiteral("-p60")});
	}

	//! Why the hosted cases cannot run here, or empty when they can: only what the PRODUCT does not provide - a
	//! Carla runtime and the fixture. A build that has both but no working VST2 host fails the cases instead.
	static QString missingRuntime()
	{
		const QString runtime = QDir(QStringLiteral(ZENE_CARLA_RUNTIME_DIR)).filePath(QStringLiteral("libcarla_native-plugin.so"));
		if (QStringLiteral(ZENE_CARLA_RUNTIME_DIR).isEmpty() || !QFileInfo::exists(runtime))
		{
			return QStringLiteral("no Carla runtime at '%1': build one with tools/build-carla-runtime.sh <build>/carla-runtime")
				.arg(QStringLiteral(ZENE_CARLA_RUNTIME_DIR));
		}
		if (!QFileInfo::exists(nativeFixture())) { return QStringLiteral("the native VST2 fixture was not built"); }
		return QString();
	}

	//! Whether a file can be created where Carla's water library puts the chunk it hands a bridge.
	static bool carlaTempDirWritable()
	{
		const QString dir = QFileInfo(QStringLiteral("/var/tmp")).isDir() ? QStringLiteral("/var/tmp") : QStringLiteral("/tmp");
		QFile probe(QDir(dir).filePath(QStringLiteral(".zene-carla-probe-%1").arg(QCoreApplication::applicationPid())));
		const bool writable = probe.open(QIODevice::WriteOnly);
		if (writable) { probe.remove(); }
		return writable;
	}

	//! Whether this machine can run the bridged path at all: Wine, the PE fixture and Carla's Wine bridge in the
	//! runtime. Only the environment - whether the PRODUCT lists and loads it is what the case checks.
	static bool wineAvailable()
	{
		const QDir runtime(QStringLiteral(ZENE_CARLA_RUNTIME_DIR));
		return QFileInfo::exists(wineFixture()) && !QStandardPaths::findExecutable(QStringLiteral("wine")).isEmpty()
			&& QFileInfo::exists(runtime.filePath(QStringLiteral("carla-bridge-win64.exe")))
			&& QFileInfo::exists(runtime.filePath(QStringLiteral("carla-discovery-win64.exe")));
	}

	InstrumentTrack* loadOnNewTrack(const QString& fileName)
	{
		const QJsonObject device = vst2Device(fileName);
		if (device.isEmpty()) { return nullptr; }
		InstrumentTrack* track = newTrack();
		const ControlResult loaded = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), control::trackIdOf(track)},
			{QStringLiteral("device"), device.value(QStringLiteral("id")).toString()}});
		if (!loaded.ok || track->instrument() == nullptr
			|| QString::fromLatin1(track->instrument()->descriptor()->name) != QLatin1String("carlavst2"))
		{
			qWarning("plugin.load failed: %s", qPrintable(loaded.errorMessage));
			delete track;
			return nullptr;
		}
		return track;
	}

	void roundTrip(const QString& fileName, int renderMs)
	{
		InstrumentTrack* track = loadOnNewTrack(fileName);
		QVERIFY(track != nullptr);
		QJsonObject set = address(track);
		set.insert(QStringLiteral("index"), 0);
		set.insert(QStringLiteral("value"), 0.3125);
		QVERIFY(run(QStringLiteral("plugin.param_set"), set).ok);
		const Rendered before = renderNote(track, renderMs);
		QVERIFY(before.sounded);
		const int noteOns = chunkField(track->instrument(), QStringLiteral("noteons"));
		QVERIFY2(noteOns >= 1, qPrintable(chunkOf(track->instrument())));

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("project"));
		doc.appendChild(root);
		track->saveState(doc, root);
		const QDomElement saved = root.firstChildElement();
		QVERIFY(!saved.isNull());
		delete track;

		InstrumentTrack* restored = newTrack();
		restored->restoreState(saved);
		QCoreApplication::processEvents();
		QVERIFY(restored->instrument() != nullptr);
		QCOMPARE(QString::fromLatin1(restored->instrument()->descriptor()->name), QStringLiteral("carlavst2"));
		QCOMPARE(QFileInfo(restored->instrument()->key().attributes.value(QStringLiteral("file"))).fileName(), fileName);
		// A bridged plug-in restores its chunk in its own process: wait for its reply.
		QTRY_VERIFY_WITH_TIMEOUT(std::abs(paramValue(restored, 0) - 0.3125) < Tolerance, 20000);
		if (fileName == WineFile && !carlaTempDirWritable())
		{
			// Carla hands a chunk to its bridge through a file in /var/tmp (water's tempDirectory, hard-coded); where
			// that is not writable (a sandbox) the bridge never receives it. Everything else is still checked.
			qWarning("bridged chunk restore not checked: Carla's temp directory (/var/tmp) is not writable here");
		}
		else
		{
			QTRY_COMPARE_WITH_TIMEOUT(chunkField(restored->instrument(), QStringLiteral("noteons")), noteOns, 20000);
		}
		const Rendered after = renderNote(restored, renderMs);
		QVERIFY(after.sounded);
		QVERIFY2(std::abs(after.left - before.left) < Tolerance && std::abs(after.right - before.right) < Tolerance,
			qPrintable(QStringLiteral("before %1/%2 after %3/%4").arg(before.left).arg(before.right).arg(after.left).arg(after.right)));
		delete restored;
	}

	QTemporaryDir m_home;
	QTemporaryDir m_plugins;
};

QTEST_MAIN(CarlaVst2HostingTest)
#include "CarlaVst2HostingTest.moc"
