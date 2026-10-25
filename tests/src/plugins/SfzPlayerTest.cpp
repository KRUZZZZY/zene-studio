/*
 * SfzPlayerTest.cpp - Q050-484: the SFZ instrument plays SFZ files
 *
 * Renders the real sfzplayer module (sfizz) on an SFZ written to a temporary folder and measures what it sounds
 * like: velocity layers, a round-robin sequence, a key switch, and that the state - the file and the settings -
 * saves and restores to the same mapping. The same instrument is then reached the way an agent reaches it
 * (plugin.list, plugin.load, plugin.open_file, plugin.param_*, browser.load) and the way a user does (the window's
 * Open button), all through the registered commands.
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include <QApplication>
#include <QDir>
#include <QSignalSpy>
#include <QDomDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPushButton>
#include <QTemporaryDir>

#include <iostream>
#include <memory>
#include <sstream>

#include "AutomatableModel.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ComboBox.h"
#include "InstrumentView.h"
#include "Knob.h"
#include "LcdSpinBox.h"
#include "NotePlayHandle.h"
#include "PluginFactory.h"
#include "../core/ReversibilityTestSupport.h"
#include "SfzTestSupport.h"
#include "../../../plugins/SfzPlayer/sfizz/external/abseil-cpp/absl/random/internal/iostream_state_saver.h"

using namespace lmms;
using namespace lmms::sfz_test;

class SfzPlayerTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;
	QString m_sfz;

	static ControlResult run(const QString& id, const QJsonObject& args = {}) { return revtest::run(id, args); }

	QString deviceId() const
	{
		const ControlResult listed = run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("instrument")}});
		for (const QJsonValue& value : listed.result.value(QStringLiteral("devices")).toArray())
		{
			if (value.toObject().value(QStringLiteral("name")).toString() == QLatin1String("sfzplayer")) { return value.toObject().value(QStringLiteral("id")).toString(); }
		}
		return {};
	}

	//! The low/mid/high levels the three layers sound at, in the order of the fixture's samples.
	static constexpr float Low = 0.25f;
	static constexpr float Mid = 0.5f;
	static constexpr float High = 0.75f;

	//! What a unit of sample level sounds as: sfizz pans a mono sample into the stereo bus, so the player's output is a
	//! fixed fraction of the sample's own level. Measured once from the fixture's low sample (key 60, soft), which the
	//! round-robin, velocity and key-switch cases then compare against - a level is its sample's, in units of Low.
	float m_unit = 0.f;

	bool levelIs(float level, float expected) const { return std::abs(level / m_unit - expected) < 0.04f; }

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
		m_sfz = writeLayeredInstrument(m_dir.path());
		QVERIFY2(!m_sfz.isEmpty(), "could not write the fixture instrument");
		SfzSession calibration;
		QVERIFY(calibration.start() && calibration.open(m_sfz));
		m_unit = calibration.levelOf(60, 40) / Low;
		QVERIFY2(m_unit > 0.1f && m_unit < 1.5f, qPrintable(QStringLiteral("unit %1").arg(m_unit)));
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	//! A file with its samples loads: the regions the SFZ declares are there, and playing a key sounds.
	void loadsAnSfzWithItsSamples()
	{
		SfzSession s;
		QVERIFY(s.start());
		QCOMPARE(s.regions(), 0);
		QVERIFY(s.open(m_sfz));
		QCOMPARE(s.regions(), 7);
		QVERIFY2(s.levelOf(60, 100) > 0.1f, "key 60 sounds");
		// A key no region covers stays silent.
		QVERIFY2(s.levelOf(70, 100) < 1e-4f, "an unmapped key is silent");
	}

	//! Notes either side of the velocity boundary (63 | 64) play their own layer.
	void velocityLayersPickTheirSample()
	{
		SfzSession s;
		QVERIFY(s.start() && s.open(m_sfz));
		const float soft = s.levelOf(60, 40);
		const float softEdge = s.levelOf(60, 63);
		const float loudEdge = s.levelOf(60, 64);
		const float loud = s.levelOf(60, 120);
		QVERIFY2(levelIs(soft, Low) && levelIs(softEdge, Low), qPrintable(QStringLiteral("soft %1 / %2").arg(soft).arg(softEdge)));
		QVERIFY2(levelIs(loudEdge, Mid) && levelIs(loud, Mid), qPrintable(QStringLiteral("loud %1 / %2").arg(loudEdge).arg(loud)));
	}

	//! Repeated notes of one key walk the round-robin sequence low, mid, high and then start over.
	void roundRobinsFollowTheirSequence()
	{
		SfzSession s;
		QVERIFY(s.start() && s.open(m_sfz));
		const float expected[] = {Low, Mid, High, Low, Mid};
		for (const float level : expected)
		{
			const float got = s.levelOf(62, 90);
			QVERIFY2(levelIs(got, level), qPrintable(QStringLiteral("round robin sounded %1, expected %2").arg(got).arg(level)));
		}
	}

	//! Pressing key switch 37 changes the sample the next note of key 64 plays; 36 puts it back.
	void aKeySwitchChangesTheNextNote()
	{
		SfzSession s;
		QVERIFY(s.start() && s.open(m_sfz));
		QVERIFY2(levelIs(s.levelOf(64, 90), Low), "the default key switch plays the low sample");
		s.noteOn(37, 90);
		s.render(1);
		s.noteOff(37);
		QVERIFY2(levelIs(s.levelOf(64, 90), High), "after key switch 37 key 64 plays the high sample");
		s.noteOn(36, 90);
		s.render(1);
		s.noteOff(36);
		QVERIFY2(levelIs(s.levelOf(64, 90), Low), "key switch 36 restores the low sample");
	}

	//! A note starts at the frame its event names inside the period, not at the period's start.
	void eventsStartAtTheirFrameOffset()
	{
		SfzSession s;
		QVERIFY(s.start() && s.open(m_sfz));
		const int offset = 100;
		s.noteOn(60, 100, offset);
		const std::vector<float> sound = s.render(2);
		QVERIFY2(SfzSession::peak(sound, 0, offset - 4) < 1e-4f, "silent before the offset");
		QVERIFY2(SfzSession::peak(sound, offset + 32, offset + 200) > 0.1f, "sounding after it");
	}

	//! The file and the settings save and restore: a second instrument built from the saved XML maps the same keys to the
	//! same samples, the file is stored by a relative-or-portable path, and a missing file keeps its reference.
	void stateSavesAndRestores()
	{
		SfzSession first;
		QVERIFY(first.start() && first.open(m_sfz));
		first.instrument->parameterModel(0)->setValue(-6.f);  // Volume
		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("state"));
		doc.appendChild(parent);
		const QDomElement saved = first.instrument->saveState(doc, parent);
		QVERIFY(saved.hasAttribute(QStringLiteral("src")));
		QVERIFY(saved.attribute(QStringLiteral("src")).endsWith(QStringLiteral("layers.sfz")));
		const float soundedFirst = first.levelOf(60, 100);
		QVERIFY2(std::abs(soundedFirst / m_unit - Mid * 0.501f) < 0.03f, "the saved volume (-6 dB) halves the level");

		SfzSession second;
		QVERIFY(second.start());
		second.instrument->restoreState(saved);
		QCOMPARE(second.regions(), 7);
		QCOMPARE(second.instrument->parameterModel(0)->value<float>(), -6.f);
		QVERIFY2(std::abs(second.levelOf(60, 100) - soundedFirst) < 0.01f * soundedFirst, "the same note renders the same after the restore");
		QVERIFY2(std::abs(second.levelOf(60, 40) / m_unit - Low * 0.501f) < 0.03f, "and so does the other velocity layer");

		// A project whose SFZ went missing keeps the reference, so a later save does not forget it.
		QDomElement gone = saved.cloneNode().toElement();
		gone.setAttribute(QStringLiteral("src"), m_dir.filePath(QStringLiteral("gone.sfz")));
		SfzSession third;
		QVERIFY(third.start());
		third.instrument->restoreState(gone);
		QCOMPARE(third.regions(), 0);
		QDomDocument doc2;
		QDomElement parent2 = doc2.createElement(QStringLiteral("state"));
		doc2.appendChild(parent2);
		QVERIFY(third.instrument->saveState(doc2, parent2).attribute(QStringLiteral("src")).endsWith(QStringLiteral("gone.sfz")));
	}

	//! The non-stream no-op saver does not write a diagnostic to stderr.
	void nonStreamStateSaversAreSilent()
	{
		int value = 7;
		std::ostringstream capturedStderr;
		std::streambuf* const originalStderr = std::cerr.rdbuf(capturedStderr.rdbuf());
		{
			auto outputSaver = absl::random_internal::make_ostream_state_saver(value);
			auto inputSaver = absl::random_internal::make_istream_state_saver(value);
			(void)outputSaver;
			(void)inputSaver;
		}
		std::cerr.rdbuf(originalStderr);

		QVERIFY2(capturedStderr.str().empty(), "non-stream state saver wrote to stderr");
	}

	//! The stream overload still applies its requested formatting and restores prior stream state.
	void ostreamStateSaverRestoresFormatting()
	{
		std::ostringstream stream;
		stream.flags(std::ios_base::hex | std::ios_base::right);
		stream.fill('0');
		stream.precision(7);
		const auto originalFlags = stream.flags();
		const auto originalFill = stream.fill();
		const auto originalPrecision = stream.precision();
		{
			auto saver = absl::random_internal::make_ostream_state_saver(
				stream, std::ios_base::dec | std::ios_base::left);
			(void)saver;
			QCOMPARE(stream.flags(), std::ios_base::dec | std::ios_base::left);
			QCOMPARE(stream.fill(), ' ');
		}
		QCOMPARE(stream.flags(), originalFlags);
		QCOMPARE(stream.fill(), originalFill);
		QCOMPARE(stream.precision(), originalPrecision);
	}

	//! plugin.list shows the instrument and says it loads; plugin.load puts it on a track; plugin.param_* read and set its models.
	void theInstrumentIsReachableThroughThePluginCommands()
	{
		// The plug-in browser lists every instrument descriptor the factory has, and the file browser and browser.load open an
		// extension through the factory: the module is an instrument that declares .sfz.
		QVERIFY(!getPluginFactory()->pluginInfo("sfzplayer").isNull());
		QCOMPARE(getPluginFactory()->pluginInfo("sfzplayer").descriptor->type, Plugin::Type::Instrument);
		QCOMPARE(getPluginFactory()->pluginSupportingExtension(QStringLiteral("sfz")).info.name(), QStringLiteral("sfzplayer"));
		const QString id = deviceId();
		QVERIFY2(!id.isEmpty(), "plugin.list carries no sfzplayer");
		const ControlResult listed = run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("instrument")}, {QStringLiteral("loadable_only"), true}});
		bool loadable = false;
		for (const QJsonValue& v : listed.result.value(QStringLiteral("devices")).toArray()) { loadable |= v.toObject().value(QStringLiteral("id")).toString() == id; }
		QVERIFY2(loadable, "the SFZ player is not loadable");

		const QString track = revtest::addTrack();
		QVERIFY(!track.isEmpty());
		const ControlResult loaded = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), track}, {QStringLiteral("device"), id}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));

		QStringList names;
		for (int index = 0; index < 4; ++index)
		{
			const ControlResult got = run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), track}, {QStringLiteral("plugin"), QStringLiteral("inst")}, {QStringLiteral("index"), index}});
			QVERIFY2(got.ok, qPrintable(got.errorMessage));
			names << got.result.value(QStringLiteral("parameter")).toObject().value(QStringLiteral("name")).toString();
		}
		QCOMPARE(names, (QStringList{QStringLiteral("Volume"), QStringLiteral("Sample quality"), QStringLiteral("Polyphony"), QStringLiteral("Oversampling")}));

		const ControlResult set = run(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), track}, {QStringLiteral("plugin"), QStringLiteral("inst")},
			{QStringLiteral("index"), 0}, {QStringLiteral("value"), -12.0}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		const ControlResult again = run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), track}, {QStringLiteral("plugin"), QStringLiteral("inst")}, {QStringLiteral("index"), 0}});
		QCOMPARE(again.result.value(QStringLiteral("parameter")).toObject().value(QStringLiteral("value")).toDouble(), -12.0);
	}

	//! plugin.open_file loads a file into the instrument a track has - what the window's Open button runs - and one undo
	//! puts the previous file back; a file the instrument refuses changes nothing.
	void openFileLoadsAndUndoes()
	{
		const QString other = m_dir.filePath(QStringLiteral("other.sfz"));
		{
			QFile f(other);
			QVERIFY(f.open(QIODevice::WriteOnly));
			f.write("<region> sample=high.wav key=60 amp_veltrack=0\n");
		}
		const QString track = revtest::addTrack();
		QVERIFY(run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), track}, {QStringLiteral("device"), deviceId()}}).ok);
		auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(Engine::getSong()->tracks().back());
		QVERIFY(instrumentTrack != nullptr);
		SfzSession s;
		s.track = instrumentTrack;
		s.instrument = instrumentTrack->instrument();

		QVERIFY(run(QStringLiteral("plugin.open_file"), {{QStringLiteral("target"), track}, {QStringLiteral("path"), m_sfz}}).ok);
		QCOMPARE(s.regions(), 7);
		QVERIFY(run(QStringLiteral("plugin.open_file"), {{QStringLiteral("target"), track}, {QStringLiteral("path"), other}}).ok);
		QCOMPARE(s.regions(), 1);
		QVERIFY(levelIs(s.levelOf(60, 40), High));

		QVERIFY(run(QStringLiteral("control.undo")).ok);  // one step
		QCOMPARE(s.regions(), 7);
		QVERIFY(levelIs(s.levelOf(60, 40), Low));

		const QString text = m_dir.filePath(QStringLiteral("notes.txt"));
		{ QFile f(text); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x"); }
		QCOMPARE(run(QStringLiteral("plugin.open_file"), {{QStringLiteral("target"), track}, {QStringLiteral("path"), text}}).errorKind, ControlErrorKind::Refused);
		const QString broken = m_dir.filePath(QStringLiteral("broken.sfz"));
		{ QFile f(broken); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("// no regions in here\n<global> volume=0\n"); }
		QVERIFY(!run(QStringLiteral("plugin.open_file"), {{QStringLiteral("target"), track}, {QStringLiteral("path"), broken}}).ok);
		QCOMPARE(s.regions(), 7);  // the instrument kept what it had
		QVERIFY(levelIs(s.levelOf(60, 40), Low));
		Engine::getSong()->clearErrors();
	}

	//! Automation moves a setting on the render thread, where nothing may emit a signal: the four models hold their change
	//! signal back there. The window / socket thread still gets it - from the player's own poll timer - and that is what
	//! rebuilds the synth for a polyphony change (a setting sfizz fixes when it is built), signal-free on the render side.
	void automatedSettingsEmitNothingOnTheRenderThread()
	{
		const QString track = revtest::addTrack();
		QVERIFY(run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), track}, {QStringLiteral("device"), deviceId()}}).ok);
		auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(Engine::getSong()->tracks().back());
		Instrument* instrument = instrumentTrack->instrument();
		QVERIFY(QMetaObject::invokeMethod(instrument, "openFile", Q_ARG(QString, m_sfz), Q_ARG(bool, false)));

		AutomatableModel* polyphony = instrument->parameterModel(2);
		AutomatableModel* volume = instrument->parameterModel(0);
		QCOMPARE(instrument->parameterName(2), QStringLiteral("Polyphony"));
		for (const auto& [tick, value] : {std::pair{0, 64.0}, std::pair{24, 32.0}})
		{
			QVERIFY(run(QStringLiteral("automation.add_point"), {{QStringLiteral("track"), track}, {QStringLiteral("parameter"), QStringLiteral("inst/2")},
				{QStringLiteral("ticks"), tick}, {QStringLiteral("value"), value}}).ok);
		}
		QVERIFY(run(QStringLiteral("automation.add_point"), {{QStringLiteral("track"), track}, {QStringLiteral("parameter"), QStringLiteral("inst/0")},
			{QStringLiteral("ticks"), 0}, {QStringLiteral("value"), -3.0}}).ok);
		QSignalSpy polyphonySignals(polyphony, &AutomatableModel::dataChanged);
		QSignalSpy volumeSignals(volume, &AutomatableModel::dataChanged);
		QSignalSpy reloads(instrument, SIGNAL(fileChanged()));

		Song* song = Engine::getSong();
		song->playSong();
		song->getTimeline().setTicks(0);
		for (int i = 0; i < 400 && song->getPlayPos().getTicks() < 48; ++i) { Engine::audioEngine()->renderNextPeriod(); }
		const float polyphonyNow = polyphony->value<float>();
		const float volumeNow = volume->value<float>();
		// Counted before stop(): stopping the transport puts the automated models back to their start values on this (the
		// main) thread, which is allowed to emit.
		const int polyphonyEmitted = polyphonySignals.count();
		const int volumeEmitted = volumeSignals.count();
		const int reloadsDuringRender = reloads.count();
		song->stop();
		QCOMPARE(polyphonyNow, 32.f);
		QVERIFY2(std::abs(volumeNow + 3.f) < 0.2f, "the volume automation was applied");
		QCOMPARE(polyphonyEmitted, 0);
		QCOMPARE(volumeEmitted, 0);
		QCOMPARE(reloadsDuringRender, 0);

		// The poll timer, on this thread, emits what was held back, and the reload it asks for follows.
		QTRY_VERIFY_WITH_TIMEOUT(reloads.count() >= 1, 2000);
		QVERIFY(polyphonySignals.count() >= 1);
		SfzSession s;
		s.track = instrumentTrack;
		s.instrument = instrumentTrack->instrument();
		QCOMPARE(s.regions(), 7);
		QVERIFY2(s.levelOf(60, 40) > 0.01f, "the rebuilt synth plays");
		Engine::getSong()->clearErrors();
	}

	//! An export loads every sample whole before its render thread starts (so nothing waits for a loader inside the render),
	//! and the player goes back to streaming once it has finished.
	void anExportPreloadsWholeSamplesAndThenStreamsAgain()
	{
		SfzSession s;
		QVERIFY(s.start() && s.open(m_sfz));
		auto frames = [&] { quint32 n = 0; QMetaObject::invokeMethod(s.instrument, "preloadFrames", Q_RETURN_ARG(quint32, n)); return n; };
		const quint32 streaming = frames();
		QVERIFY2(streaming > 0 && streaming < 100000, qPrintable(QString::number(streaming)));
		emit Engine::getSong()->exportPreparing();
		QCOMPARE(frames(), quint32{1} << 30);
		QCOMPARE(s.regions(), 7);
		QVERIFY(levelIs(s.levelOf(60, 40), Low));
		emit Engine::getSong()->exportFinished();
		QTRY_COMPARE_WITH_TIMEOUT(frames(), streaming, 2000);
		QVERIFY(levelIs(s.levelOf(60, 40), Low));
	}

	//! plugin.open_file takes whatever the platform calls an absolute path - the window passes QFileInfo::absoluteFilePath(),
	//! which on Windows is C:/dir/x.sfz - and refuses a relative one.
	void openFileTakesThePlatformsAbsolutePaths()
	{
		const QString track = revtest::addTrack();
		QVERIFY(run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), track}, {QStringLiteral("device"), deviceId()}}).ok);
		for (const QString& spelled : {QFileInfo(m_sfz).absoluteFilePath(), QDir::toNativeSeparators(m_sfz)})
		{
			QVERIFY2(QFileInfo(spelled).isAbsolute(), qPrintable(spelled));
			const ControlResult opened = run(QStringLiteral("plugin.open_file"), {{QStringLiteral("target"), track}, {QStringLiteral("path"), spelled}});
			QVERIFY2(opened.ok, qPrintable(spelled + ": " + opened.errorMessage));
		}
		const ControlResult relative = run(QStringLiteral("plugin.open_file"), {{QStringLiteral("target"), track}, {QStringLiteral("path"), QStringLiteral("layers.sfz")}});
		QCOMPARE(relative.errorKind, ControlErrorKind::InvalidArgs);
	}

	//! browser.load on an .sfz makes a track with the SFZ player holding it - the file browser's Enter.
	void browserLoadOpensAnSfzOnANewTrack()
	{
		const std::size_t tracks = Engine::getSong()->tracks().size();
		const ControlResult loaded = run(QStringLiteral("browser.load"), {{QStringLiteral("path"), m_sfz}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		QCOMPARE(Engine::getSong()->tracks().size(), tracks + 1);
		auto* track = dynamic_cast<InstrumentTrack*>(Engine::getSong()->tracks().back());
		QVERIFY(track != nullptr && track->instrument() != nullptr);
		QCOMPARE(QString::fromUtf8(track->instrument()->descriptor()->name), QStringLiteral("sfzplayer"));
		SfzSession s;
		s.track = track;
		s.instrument = track->instrument();
		QCOMPARE(s.regions(), 7);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(Engine::getSong()->tracks().size(), tracks);
	}

	//! The window: it is built for the instrument, its Open button runs plugin.open_file (so the file loads and one
	//! undo reverts it), and its controls are bound to the models the agent reads.
	void theWindowOpensAFileThroughTheCommand()
	{
		SfzSession s;
		QVERIFY(s.start());
		QWidget parent;
		std::unique_ptr<gui::PluginView> view(s.instrument->createView(&parent));
		QVERIFY(view != nullptr);
		view->show();
		QApplication::processEvents();

		// The controls are the models: a knob or box of the window is bound to each parameter.
		for (int i = 0; i < s.instrument->parameterCount(); ++i)
		{
			const Model* model = s.instrument->parameterModel(i);
			bool bound = false;
			for (const gui::Knob* k : view->findChildren<gui::Knob*>()) { bound |= k->model() == model; }
			for (const gui::LcdSpinBox* b : view->findChildren<gui::LcdSpinBox*>()) { bound |= b->model() == model; }
			for (const gui::ComboBox* c : view->findChildren<gui::ComboBox*>()) { bound |= c->model() == model; }
			QVERIFY2(bound, qPrintable(s.instrument->parameterName(i) + " has a control in the window"));
		}

		view->setProperty("chosenFile", m_sfz);  // answers the file dialog
		auto* button = view->findChild<QPushButton*>(QStringLiteral("sfzOpenButton"));
		QVERIFY2(button != nullptr, "the window has an Open button");
		button->click();
		QCOMPARE(s.regions(), 7);
		QVERIFY(levelIs(s.levelOf(60, 40), Low));
		QVERIFY2(!run(QStringLiteral("control.undo")).errorMessage.contains(QStringLiteral("nothing")), "the click left one undo step");
		QCOMPARE(s.regions(), 0);

	}
};

QTEST_MAIN(SfzPlayerTest)
#include "SfzPlayerTest.moc"
