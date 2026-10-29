/*
 * AuHostingTest.cpp - Audio Unit (AUv2) hosting through the product, its commands and its GUI (Q050-491)
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

/*! Every build: plugin.list offers the "au" format, and the device organiser an "au" preference, exactly when
 *  the build hosts Audio Units (macOS), and an AU device that does not instantiate is refused with a typed
 *  error - on macOS the component's own reason, elsewhere "built on macOS only".
 *
 *  macOS (the hosted macos jobs, against Apple's built-in components AUBandpass, AUDistortion and
 *  DLSMusicDevice): the registry's components are listed and recorded in the scan cache, and a quarantined one
 *  is hidden; an AU effect processes audio on a track's chain; its parameters are reachable through
 *  plugin.param_get / param_set and automation; plugin.state_save / state_load and a project save and reopen
 *  round-trip its ClassInfo state; an AU instrument plays MIDI; the device picker lists AU rows; and the
 *  effect's "Show plugin editor" button opens its Cocoa view (under Qt's cocoa platform) or names why not. */

#include <QtTest>

#include <cmath>
#include <span>
#include <vector>

#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QToolButton>

#include "AudioBus.h"
#include "AudioEngine.h"
#include "AutomationClip.h"
#include "ControlAutomationSupport.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DevicePicker.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MidiEvent.h"
#include "Mixer.h"
#include "PluginFactory.h"
#include "PluginNativeEditor.h"
#include "PluginOrganiser.h"
#include "PluginScanCache.h"
#include "SampleFrame.h"
#include "Song.h"
#include "lmmsconfig.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

const QString BandpassKey = QStringLiteral("aufx:bpas:appl");
const QString DistortionKey = QStringLiteral("aufx:dist:appl");
const QString DlsKey = QStringLiteral("aumu:dls :appl");

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

InstrumentTrack* newTrack()
{
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	QCoreApplication::processEvents();
	return track;
}

//! plugin.list's "au" entry for @a component, or an empty object.
QJsonObject auDevice(const QString& component)
{
	const ControlResult listed = run(QStringLiteral("plugin.list"), {{QStringLiteral("format"), QStringLiteral("au")}});
	for (const QJsonValue& value : listed.result.value(QStringLiteral("devices")).toArray())
	{
		if (value.toObject().value(QStringLiteral("component")).toString() == component) { return value.toObject(); }
	}
	return {};
}

//! Left and right channels of noise, the same each call.
std::vector<SampleFrame> noise(f_cnt_t frames)
{
	std::vector<SampleFrame> out(frames);
	std::uint32_t state = 12345;
	for (SampleFrame& frame : out)
	{
		for (int c = 0; c < 2; ++c)
		{
			state = state * 1664525u + 1013904223u;
			frame[c] = static_cast<float>(state >> 8) / static_cast<float>(1u << 24) - 0.5f;
		}
	}
	return out;
}

float peakOf(const std::vector<SampleFrame>& frames)
{
	float peak = 0.f;
	for (const SampleFrame& frame : frames) { peak = std::max({peak, std::fabs(frame[0]), std::fabs(frame[1])}); }
	return peak;
}

} // namespace

class AuHostingTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		QVERIFY(m_home.isValid());
		// A persistent scan cache of the test's own, so the quarantine commands can write it.
		qputenv("LMMS_PLUGIN_SCAN_CACHE", m_home.filePath(QStringLiteral("plugin-scan-cache.json")).toUtf8());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		ControlRegistry::setReady(true);
		while (Engine::mixer()->numChannels() < 2) { Engine::mixer()->createChannel(); }
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Every build: the "au" format exists exactly where Audio Units are hosted.
	void theAuFormatIsOfferedOnlyWhereAudioUnitsAreHosted()
	{
#ifdef LMMS_BUILD_APPLE
		const bool apple = true;
#else
		const bool apple = false;
#endif
		QCOMPARE(controlAuHostingBuilt(), apple);
		const ControlCommand* list = ControlRegistry::instance()->command(QStringLiteral("plugin.list"));
		QVERIFY(list != nullptr);
		const QJsonArray formats = list->argsSchema.value(QStringLiteral("properties")).toObject()
			.value(QStringLiteral("format")).toObject().value(QStringLiteral("enum")).toArray();
		QVERIFY(formats.contains(QStringLiteral("clap")));
		QCOMPARE(formats.contains(QStringLiteral("au")), apple);
		QCOMPARE(pluginorganiser::formatNames().contains(QStringLiteral("au")), apple);

		const ControlResult all = run(QStringLiteral("plugin.list"));
		QVERIFY2(all.ok, qPrintable(all.errorMessage));
		const bool listsAu = all.result.value(QStringLiteral("counts_by_format")).toObject().contains(QStringLiteral("au"));
		QVERIFY(!listsAu || apple);  // never off macOS; on macOS the registry always has Apple's units
		QCOMPARE(listsAu, apple);
		// Off macOS the filter is refused by the schema rather than answering an empty list.
		const ControlResult filtered = run(QStringLiteral("plugin.list"), {{QStringLiteral("format"), QStringLiteral("au")}});
		QCOMPARE(filtered.ok, apple);
	}

	//! Every build: an AU device that cannot instantiate is refused with a typed error naming why.
	void aComponentThatDoesNotInstantiateIsRefusedTyped()
	{
		ControlDeviceEntry entry;
		entry.name = QStringLiteral("Missing");
		entry.displayName = entry.name;
		entry.format = QStringLiteral("au");
		entry.kind = QStringLiteral("effect");
		entry.component = QStringLiteral("aufx:zzzz:zene");
		entry.loadable = true;
		EffectChain& chain = Engine::mixer()->mixerChannel(1)->m_fxChain;
		const auto before = chain.effects().size();
		ControlResult error;
		QVERIFY(controlInstantiateDevice(entry, &chain, &error) == nullptr);
		QCOMPARE(error.errorKind, ControlErrorKind::Refused);
		QCOMPARE(chain.effects().size(), before);
#ifdef LMMS_BUILD_APPLE
		QVERIFY2(error.errorMessage.contains(QStringLiteral("the Audio Unit aufx:zzzz:zene could not be instantiated: the "
			"system component registry has no such component")), qPrintable(error.errorMessage));
#else
		QVERIFY2(error.errorMessage.contains(QStringLiteral("AU hosting is built on macOS only")), qPrintable(error.errorMessage));
#endif
		// A key that names no component at all is refused before any host is asked.
		entry.component = QStringLiteral("not-a-component");
		QVERIFY(controlInstantiateDevice(entry, &chain, &error) == nullptr);
		QVERIFY(error.errorKind == ControlErrorKind::InvalidArgs || error.errorKind == ControlErrorKind::Refused);
	}

	//! macOS: the registry's components are listed and recorded in the scan cache; a quarantined one is hidden.
	void theRegistryIsListedRecordedAndQuarantined()
	{
		if (!controlAuHostingBuilt()) { QSKIP("Audio Units are hosted on macOS only"); }
		const QJsonObject bandpass = auDevice(BandpassKey);
		const QJsonObject distortion = auDevice(DistortionKey);
		const QJsonObject dls = auDevice(DlsKey);
		QVERIFY2(!bandpass.isEmpty() && !distortion.isEmpty() && !dls.isEmpty(), "Apple's built-in units are not listed");
		QCOMPARE(bandpass.value(QStringLiteral("kind")).toString(), QStringLiteral("effect"));
		QCOMPARE(dls.value(QStringLiteral("kind")).toString(), QStringLiteral("instrument"));
		QCOMPARE(bandpass.value(QStringLiteral("vendor")).toString(), QStringLiteral("Apple"));
		const QString path = distortion.value(QStringLiteral("scan_cache_path")).toString();
		QCOMPARE(path, QStringLiteral("/AudioUnits/aufx/dist/appl"));
		QVERIFY(getPluginFactory()->scanCache().record(path) != nullptr);

		const ControlResult hidden = run(QStringLiteral("plugin.scan_cache_quarantine_add"),
			{{QStringLiteral("path"), path}, {QStringLiteral("reason"), QStringLiteral("test")}});
		QVERIFY2(hidden.ok, qPrintable(hidden.errorMessage));
		QVERIFY(auDevice(DistortionKey).isEmpty());
		QVERIFY(!auDevice(BandpassKey).isEmpty());
		QVERIFY(run(QStringLiteral("plugin.scan_cache_quarantine_remove"), {{QStringLiteral("path"), path}}).ok);
		QVERIFY(!auDevice(DistortionKey).isEmpty());
	}

	//! macOS: AUBandpass on a track's chain changes the audio; its parameter is set and read back through the
	//! commands, reaches the unit (read back from it by the controls' poll) and takes an automation point.
	void anEffectProcessesAudioAndItsParametersAreReachable()
	{
		if (!controlAuHostingBuilt()) { QSKIP("Audio Units are hosted on macOS only"); }
		InstrumentTrack* track = nullptr;
		const QString fx = loadBandpass(&track);
		QVERIFY(!fx.isEmpty());
		const QString target = control::trackIdOf(track);
		const int index = auParameterIndex(track);
		QVERIFY2(index >= 0, "the unit's parameters are not among the device's");

		const ControlResult got = run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), target},
			{QStringLiteral("plugin"), fx}, {QStringLiteral("index"), index}});
		QVERIFY2(got.ok, qPrintable(got.errorMessage));
		const QJsonObject parameter = got.result.value(QStringLiteral("parameter")).toObject();
		const double low = parameter.value(QStringLiteral("min")).toDouble();
		const double high = parameter.value(QStringLiteral("max")).toDouble();
		const double wanted = low + (high - low) * 0.25;
		const ControlResult set = run(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), target},
			{QStringLiteral("plugin"), fx}, {QStringLiteral("index"), index}, {QStringLiteral("value"), wanted}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));

		EffectChain* chain = track->audioBusHandle()->effects();
		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		const std::vector<SampleFrame> dry = noise(frames);
		std::vector<SampleFrame> wet = dry;
		SampleFrame* busData[1] = {wet.data()};
		AudioBus bus{busData, 1, frames};
		QVERIFY(chain->processAudioBuffer(bus));
		QVERIFY2(peakOf(wet) > 1e-4f, "the band-pass output is silent");
		float difference = 0.f;
		for (f_cnt_t f = 0; f < frames; ++f) { difference = std::max(difference, std::fabs(wet[f][0] - dry[f][0])); }
		QVERIFY2(difference > 1e-3f, "the band-pass did not change the signal");

		// The value went to the unit with that block; the controls' poll reads it back from the unit.
		QTest::qWait(350);
		const ControlResult again = run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), target},
			{QStringLiteral("plugin"), fx}, {QStringLiteral("index"), index}});
		QVERIFY(std::fabs(again.result.value(QStringLiteral("parameter")).toObject().value(QStringLiteral("value")).toDouble()
			- wanted) <= (high - low) * 1e-3);

		// Automation: the same parameter takes a point through automation.add_point.
		const QJsonArray parameters = run(QStringLiteral("automation.get_state"), {{QStringLiteral("track"), target}}).result
			.value(QStringLiteral("tracks")).toArray().first().toObject().value(QStringLiteral("parameters")).toArray();
		QString automated;
		AutomatableModel* model = nullptr;
		for (const QJsonValue& value : parameters)
		{
			const QString id = value.toObject().value(QStringLiteral("id")).toString();
			ControlTarget resolved;
			control::AutomationParameter found;
			ControlResult error;
			if (control::resolveAutomationParameter({{QStringLiteral("track"), target}, {QStringLiteral("parameter"), id}},
					&resolved, &found, &error) && found.model != nullptr && found.model->objectName().startsWith(QStringLiteral("au-")))
			{
				automated = id;
				model = found.model;
				break;
			}
		}
		QVERIFY2(model != nullptr, "no Audio Unit parameter is offered to automation");
		const ControlResult point = run(QStringLiteral("automation.add_point"), {{QStringLiteral("track"), target},
			{QStringLiteral("parameter"), automated}, {QStringLiteral("ticks"), 0}, {QStringLiteral("value"), wanted}});
		QVERIFY2(point.ok, qPrintable(point.errorMessage));
		QVERIFY(control::existingAutomationClip(model) != nullptr);
	}

	//! macOS: the unit's ClassInfo state round-trips through plugin.state_save / state_load and a project file.
	void theStateRoundTripsThroughAFileAndAProject()
	{
		if (!controlAuHostingBuilt()) { QSKIP("Audio Units are hosted on macOS only"); }
		InstrumentTrack* track = nullptr;
		QString fx = loadBandpass(&track);
		QVERIFY(!fx.isEmpty());
		track->setName(QStringLiteral("AU state"));
		const QString target = control::trackIdOf(track);
		const int index = auParameterIndex(track);
		QVERIFY(index >= 0);
		auto valueOf = [&] {
			return run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), control::trackIdOf(track)},
				{QStringLiteral("plugin"), fx}, {QStringLiteral("index"), auParameterIndex(track)}}).result
				.value(QStringLiteral("parameter")).toObject().value(QStringLiteral("value")).toDouble();
		};
		const QJsonObject info = run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), target},
			{QStringLiteral("plugin"), fx}, {QStringLiteral("index"), index}}).result.value(QStringLiteral("parameter")).toObject();
		const double low = info.value(QStringLiteral("min")).toDouble();
		const double high = info.value(QStringLiteral("max")).toDouble();
		const double saved = low + (high - low) * 0.6;
		const double other = low + (high - low) * 0.1;
		const double tolerance = (high - low) * 1e-3;

		QVERIFY(run(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), target}, {QStringLiteral("plugin"), fx},
			{QStringLiteral("index"), index}, {QStringLiteral("value"), saved}}).ok);
		processOnce(track);  // the unit takes the value with a block, so its ClassInfo carries it
		const QString file = m_home.filePath(QStringLiteral("bandpass.xpf"));
		const ControlResult stored = run(QStringLiteral("plugin.state_save"), {{QStringLiteral("target"), target},
			{QStringLiteral("plugin"), fx}, {QStringLiteral("path"), file}, {QStringLiteral("overwrite"), true}});
		QVERIFY2(stored.ok, qPrintable(stored.errorMessage));
		QVERIFY(run(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), target}, {QStringLiteral("plugin"), fx},
			{QStringLiteral("index"), index}, {QStringLiteral("value"), other}}).ok);
		processOnce(track);
		const ControlResult loaded = run(QStringLiteral("plugin.state_load"), {{QStringLiteral("target"), target},
			{QStringLiteral("plugin"), fx}, {QStringLiteral("path"), file}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		QVERIFY2(std::fabs(valueOf() - saved) <= tolerance, qPrintable(QString::number(valueOf())));

		// A project save and reopen: the reopened track's AU effect has the saved value.
		const QString project = m_home.filePath(QStringLiteral("au.mmp"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), project}}).ok);
		QVERIFY(run(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), target}, {QStringLiteral("plugin"), fx},
			{QStringLiteral("index"), index}, {QStringLiteral("value"), other}}).ok);
		const ControlResult reopened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), project}});
		QVERIFY2(reopened.ok, qPrintable(reopened.errorMessage));
		track = nullptr;
		for (Track* candidate : Engine::getSong()->tracks())
		{
			if (candidate->name() == QLatin1String("AU state")) { track = dynamic_cast<InstrumentTrack*>(candidate); }
		}
		QVERIFY2(track != nullptr, "the reopened project lost the track");
		const auto& effects = track->audioBusHandle()->effects()->effects();
		QVERIFY2(!effects.empty() && QString::fromUtf8(effects.back()->descriptor()->name) == QLatin1String("aueffect"),
			"the reopened track has no Audio Unit effect");
		fx = control::effectIdOf(effects.back());  // fx-<n> ids are renumbered by a reopen
		QVERIFY2(std::fabs(valueOf() - saved) <= tolerance, qPrintable(QString::number(valueOf())));
	}

	//! macOS: DLSMusicDevice on an instrument track sounds after a MIDI note-on, and is silent before it.
	void anInstrumentPlaysMidi()
	{
		if (!controlAuHostingBuilt()) { QSKIP("Audio Units are hosted on macOS only"); }
		const QJsonObject dls = auDevice(DlsKey);
		QVERIFY(!dls.isEmpty());
		InstrumentTrack* track = newTrack();
		const ControlResult loaded = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), control::trackIdOf(track)},
			{QStringLiteral("device"), dls.value(QStringLiteral("id")).toString()}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		Instrument* instrument = track->instrument();
		QCOMPARE(QString::fromUtf8(instrument->descriptor()->name), QStringLiteral("auinstrument"));
		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> block(frames);
		instrument->play(std::span<SampleFrame>{block});
		QVERIFY2(peakOf(block) < 1e-4f, "the instrument sounds with no note");
		QVERIFY(instrument->handleMidiEvent(MidiEvent(MidiNoteOn, 0, 60, 110), TimePos(), 0));
		float peak = 0.f;
		for (int i = 0; i < 20 && peak < 1e-4f; ++i)
		{
			std::fill(block.begin(), block.end(), SampleFrame());
			instrument->play(std::span<SampleFrame>{block});
			peak = std::max(peak, peakOf(block));
		}
		QVERIFY2(peak > 1e-4f, "the note-on did not sound");
		QVERIFY(instrument->handleMidiEvent(MidiEvent(MidiNoteOff, 0, 60, 0), TimePos(), 0));
	}

	//! macOS, the GUI: typing in the device picker finds the Audio Unit row by its format.
	void theDevicePickerListsAudioUnits()
	{
		if (!controlAuHostingBuilt()) { QSKIP("Audio Units are hosted on macOS only"); }
		DevicePicker picker(DevicePicker::Kind::Effect, QStringLiteral("Add effect"));
		picker.show();
		auto* search = picker.findChild<QLineEdit*>();
		QVERIFY(search != nullptr);
		QTest::keyClicks(search, QStringLiteral("AUBandpass"));
		QVERIFY(picker.shownRows() >= 1);
		QVERIFY2(picker.shownRowText(0).contains(QStringLiteral("Audio Unit")), qPrintable(picker.shownRowText(0)));
		QCOMPARE(picker.selectedDevice(), auDevice(BandpassKey).value(QStringLiteral("id")).toString());
	}

	//! macOS, the GUI: the effect's "Show plugin editor" button opens the unit's Cocoa view under Qt's cocoa
	//! platform, and under any other names why (no NSView), with no window left; plugin.editor_open agrees.
	void theEditorButtonOpensTheCocoaViewOrNamesWhyNot()
	{
		if (!controlAuHostingBuilt()) { QSKIP("Audio Units are hosted on macOS only"); }
		InstrumentTrack* track = nullptr;
		const QString fx = loadBandpass(&track);
		QVERIFY(!fx.isEmpty());
		Effect* effect = track->audioBusHandle()->effects()->effects().back();
		const bool headless = ControlRegistry::instance()->isHeadless();
		ControlRegistry::instance()->setHeadless(false);
		QWidget* box = makeNativeEditorButton(effect, nullptr);
		box->show();
		box->findChild<QToolButton*>()->click();
		const bool opened = effect->nativeEditorOpen();
		const QLabel* reason = box->findChild<QLabel*>();
		const QString why = reason->isVisible() ? reason->text() : QString();
		effect->closeNativeEditor();
		delete box;
		const ControlResult command = run(QStringLiteral("plugin.editor_open"), {{QStringLiteral("target"),
			control::trackIdOf(track)}, {QStringLiteral("plugin"), fx}});
		effect->closeNativeEditor();
		ControlRegistry::instance()->setHeadless(headless);

		if (QGuiApplication::platformName() == QLatin1String("cocoa"))
		{
			QVERIFY2(opened, qPrintable(why));
			QVERIFY2(command.ok, qPrintable(command.errorMessage));
			return;
		}
		QVERIFY(!opened);
		QVERIFY2(why.contains(QStringLiteral("has no NSView (window API cocoa)")), qPrintable(why));
		QVERIFY(!command.ok);
		QVERIFY2(command.errorMessage.contains(QStringLiteral("has no NSView")), qPrintable(command.errorMessage));
	}

private:
	//! The plugin.param_* index of the first Audio Unit parameter of @a track's last effect (the effect's own
	//! models - enabled, wet/dry - come first), or -1.
	static int auParameterIndex(InstrumentTrack* track)
	{
		const auto& effects = track->audioBusHandle()->effects()->effects();
		if (effects.empty()) { return -1; }
		const QList<AutomatableModel*> models = controlEffectParameters(effects.back());
		for (int i = 0; i < models.size(); ++i)
		{
			if (models.at(i)->objectName().startsWith(QStringLiteral("au-"))) { return i; }
		}
		return -1;
	}

	//! AUBandpass loaded by plugin.load onto a new track; its fx-<n>, or empty.
	QString loadBandpass(InstrumentTrack** track)
	{
		const QJsonObject bandpass = auDevice(BandpassKey);
		if (bandpass.isEmpty()) { return {}; }
		*track = newTrack();
		const ControlResult loaded = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), control::trackIdOf(*track)},
			{QStringLiteral("device"), bandpass.value(QStringLiteral("id")).toString()}});
		if (!loaded.ok) { qWarning() << loaded.errorMessage; }
		return loaded.ok ? loaded.result.value(QStringLiteral("id")).toString() : QString();
	}

	void processOnce(InstrumentTrack* track)
	{
		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> block = noise(frames);
		SampleFrame* busData[1] = {block.data()};
		AudioBus bus{busData, 1, frames};
		track->audioBusHandle()->effects()->processAudioBuffer(bus);
	}

	QTemporaryDir m_home;
};

QTEST_MAIN(AuHostingTest)
#include "AuHostingTest.moc"
