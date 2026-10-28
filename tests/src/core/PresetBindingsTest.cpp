/*
 * PresetBindingsTest.cpp - controller bindings saved with track and chain presets (Q050-331)
 *                                       reversibility contract (0.5.0 F6)
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

/*! Q050-331, through the registered commands an agent uses (track.preset_save / track.preset_load,
 *  chain.save / chain.apply) on real devices (the built-in TripleOscillator and Amplifier modules):
 *   1. a track preset and a chain preset bring their MIDI controller bindings back on the LOADED devices'
 *      own parameters - channel, CC, soft takeover and feedback - and the load reports each one restored; one
 *      undo takes the load, bindings included, back;
 *   2. a binding whose parameter the loaded device no longer has is reported as skipped (target, channel, CC)
 *      and no connection is made for it; a preset older than the manifest loads exactly as before. */

#include <QtTest>

#include <QDir>
#include <QJsonDocument>
#include <QDomDocument>
#include <QFile>
#include <QTemporaryDir>

#include "AudioBusHandle.h"
#include "AutomatableModel.h"
#include "ConfigManager.h"
#include "ControlChainPresetSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControllerConnection.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MidiController.h"
#include "MidiLearn.h"
#include "PresetBindings.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The catalogue id of the loadable device whose name is @a name (case-insensitive), or empty.
QString deviceNamed(const QString& name)
{
	const QJsonArray devices = run(QStringLiteral("plugin.list"), {{QStringLiteral("loadable_only"), true}})
		.result.value(QStringLiteral("devices")).toArray();
	for (const QJsonValue& device : devices)
	{
		if (device.toObject().value(QStringLiteral("name")).toString().compare(name, Qt::CaseInsensitive) == 0)
		{
			return device.toObject().value(QStringLiteral("id")).toString();
		}
	}
	return QString();
}

//! A new instrument track with a name of its own, so a parameter's display name names one parameter.
QString addTrack()
{
	static int made = 0;
	const QString track = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")}})
		.result.value(QStringLiteral("track")).toString();
	run(QStringLiteral("track.rename"), {{QStringLiteral("track"), track}, {QStringLiteral("name"),
		QStringLiteral("Preset Bindings %1").arg(++made)}});
	return track;
}

InstrumentTrack* trackOf(const QString& id)
{
	ControlResult ignored;
	return dynamic_cast<InstrumentTrack*>(control::resolveTrack(id, &ignored));
}

Effect* firstEffectOf(InstrumentTrack* track)
{
	EffectChain* chain = track->audioBusHandle()->effects();
	return chain->effects().empty() ? nullptr : chain->effects().front();
}

//! The first float parameter of @a device whose name contains @a hint, else its first float parameter.
FloatModel* parameterOf(QObject* device, const QString& hint = {})
{
	const QList<FloatModel*> models = device->findChildren<FloatModel*>();
	for (FloatModel* model : models)
	{
		if (!hint.isEmpty() && model->displayName().contains(hint, Qt::CaseInsensitive)) { return model; }
	}
	return models.isEmpty() ? nullptr : models.first();
}

const MidiController* midiOf(const AutomatableModel* model)
{
	ControllerConnection* connection = model != nullptr ? model->controllerConnection() : nullptr;
	return connection != nullptr ? dynamic_cast<const MidiController*>(connection->getController()) : nullptr;
}

//! MIDI controller connections that exist anywhere. A load that made a connection for a skipped binding - a
//! phantom mapping - would show here as one more than the bindings it restored.
int midiConnections()
{
	int count = 0;
	for (ControllerConnection* connection : ControllerConnection::connections())
	{
		if (dynamic_cast<MidiController*>(connection->getController()) != nullptr) { ++count; }
	}
	return count;
}

//! Binds @a model the way controller.map does (bindMidiController, then the two surface flags).
bool map(AutomatableModel* model, int channel, int cc, bool soft, bool feedback)
{
	MidiController* bound = bindMidiController(model, channel, cc);
	if (bound == nullptr) { return false; }
	bound->setSoftTakeoverEnabled(soft);
	bound->setFeedbackEnabled(feedback);
	return true;
}

//! Rewrites @a path so the FIRST <connection> child element becomes one no parameter reads - the preset's
//! binding now names a parameter the device does not have (a plug-in update that renamed it).
bool orphanFirstConnection(const QString& path, const QString& into)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return false; }
	QDomDocument doc;
	if (!doc.setContent(file.readAll())) { return false; }
	file.close();
	const QDomNodeList connections = doc.elementsByTagName(QStringLiteral("connection"));
	if (connections.isEmpty()) { return false; }
	QDomElement first = connections.at(0).firstChildElement();
	if (first.isNull()) { return false; }
	first.setTagName(QStringLiteral("zeneNoSuchParameter"));
	QFile out(into);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
	out.write(doc.toByteArray());
	return true;
}

//! @a path without its binding manifest (a preset written before Q050-331).
bool dropManifest(const QString& path, const QString& into)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return false; }
	QByteArray bytes = file.readAll();
	const int from = bytes.indexOf("<zenebindings");
	const int to = bytes.indexOf("</zenebindings>");
	if (from < 0 || to < 0) { return false; }
	bytes.remove(from, to + static_cast<int>(qstrlen("</zenebindings>")) - from);
	QFile out(into);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
	out.write(bytes);
	return true;
}

} // namespace

class PresetBindingsTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_world;
	QString m_synth;
	QString m_amp;

	//! A track holding TripleOscillator and an Amplifier, with one binding on each device.
	QString boundSource(FloatModel** synthParam, FloatModel** ampParam)
	{
		const QString track = addTrack();
		for (const QString& device : {m_synth, m_amp})
		{
			const ControlResult loaded = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), track},
				{QStringLiteral("device"), device}});
			if (!loaded.ok) { qWarning("plugin.load %s: %s", qPrintable(device), qPrintable(loaded.errorMessage)); return {}; }
		}
		InstrumentTrack* source = trackOf(track);
		*synthParam = parameterOf(source->instrument(), QStringLiteral("volume"));
		*ampParam = parameterOf(firstEffectOf(source), QStringLiteral("volume"));
		if (*synthParam == nullptr || *ampParam == nullptr) { qWarning("no parameter to bind"); return {}; }
		if (!map(*synthParam, 2, 21, true, true) || !map(*ampParam, 3, 22, false, false)) { qWarning("controller.map refused"); return {}; }
		return track;
	}

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(m_world.isValid());
		ConfigManager::inst()->setWorkingDir(m_world.path());
		m_synth = deviceNamed(QStringLiteral("tripleoscillator"));
		m_amp = deviceNamed(QStringLiteral("amplifier"));
		QVERIFY2(!m_synth.isEmpty() && !m_amp.isEmpty(), "the build's tripleoscillator and amplifier modules are needed");
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Criterion 1 (track): the bindings come back on the loaded devices' own parameters, reported, one undo step.
	void trackPresetRestoresBindingsOnTheLoadedDevices()
	{
		FloatModel* synthParam = nullptr;
		FloatModel* ampParam = nullptr;
		const QString sourceId = boundSource(&synthParam, &ampParam);
		QVERIFY(!sourceId.isEmpty());
		FloatModel* trackVolume = parameterOf(trackOf(sourceId), QStringLiteral("volume"));
		QVERIFY(trackVolume != nullptr && map(trackVolume, 1, 7, false, true));
		const QString path = m_world.filePath(QStringLiteral("bound.xpf"));
		const ControlResult saved = run(QStringLiteral("track.preset_save"), {{QStringLiteral("track"), sourceId},
			{QStringLiteral("path"), path}});
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		QCOMPARE(saved.result.value(QStringLiteral("bindings")).toInt(), 3);

		// The destination has an instrument of its own, so its checkpoint names one to put back.
		const QString destinationId = addTrack();
		QVERIFY(run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), destinationId}, {QStringLiteral("device"), m_synth}}).ok);
		const int before = midiConnections();
		const ControlResult loaded = run(QStringLiteral("track.preset_load"), {{QStringLiteral("track"), destinationId},
			{QStringLiteral("path"), path}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		const QJsonObject report = loaded.result.value(QStringLiteral("bindings")).toObject();
		QCOMPARE(report.value(QStringLiteral("saved")).toInt(), 3);
		QCOMPARE(report.value(QStringLiteral("restored")).toArray().size(), 3);
		QCOMPARE(report.value(QStringLiteral("skipped")).toArray().size(), 0);
		QVERIFY(loaded.result.value(QStringLiteral("bindings_summary")).toString().startsWith(QStringLiteral("3 of 3")));

		// On the DESTINATION's own new devices, not the source's, with every flag.
		InstrumentTrack* destination = trackOf(destinationId);
		FloatModel* newSynth = parameterOf(destination->instrument(), QStringLiteral("volume"));
		FloatModel* newAmp = parameterOf(firstEffectOf(destination), QStringLiteral("volume"));
		QVERIFY(newSynth != nullptr && newAmp != nullptr);
		QVERIFY(newSynth != synthParam && newAmp != ampParam);
		const MidiController* synthMidi = midiOf(newSynth);
		const MidiController* ampMidi = midiOf(newAmp);
		QVERIFY2(synthMidi != nullptr && ampMidi != nullptr, "a binding did not come back on the loaded device");
		QCOMPARE(synthMidi->midiPort().inputChannel(), 2);
		QCOMPARE(synthMidi->midiPort().inputController(), 21);
		QVERIFY(synthMidi->softTakeoverEnabled());
		QVERIFY(synthMidi->feedbackEnabled());
		QCOMPARE(ampMidi->midiPort().inputChannel(), 3);
		QCOMPARE(ampMidi->midiPort().inputController(), 22);
		QVERIFY(!ampMidi->softTakeoverEnabled());
		QVERIFY(midiOf(synthParam) != nullptr && midiOf(ampParam) != nullptr);  // the source keeps its own
		QCOMPARE(midiConnections(), before + 3);
		const MidiController* volumeMidi = midiOf(parameterOf(destination, QStringLiteral("volume")));
		QVERIFY(volumeMidi != nullptr && volumeMidi->feedbackEnabled());
		QCOMPARE(volumeMidi->midiPort().inputController(), 7);
		const QJsonObject first = report.value(QStringLiteral("restored")).toArray().at(0).toObject();
		QVERIFY(first.contains(QStringLiteral("target")) && first.contains(QStringLiteral("channel")));

		// One undo takes the load back, its bindings with it.
		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY(undone.ok);
		// The track's own Volume is the same model before and after: the undo has to take the preset's connection off
		// a parameter its checkpoint reuses (ConnectionRestoreScope.h).
		QVERIFY(presetbindings::collectTrack(trackOf(destinationId)).isEmpty());
		QCOMPARE(midiConnections(), before);
	}

	//! Criterion 1 (chain): chain.apply rebinds to the newly created effect, reported by its fx id; undo restores.
	void chainPresetRebindsToTheNewEffect()
	{
		FloatModel* synthParam = nullptr;
		FloatModel* ampParam = nullptr;
		const QString sourceId = boundSource(&synthParam, &ampParam);
		QVERIFY(!sourceId.isEmpty());
		QVERIFY(run(QStringLiteral("chain.save"), {{QStringLiteral("target"), sourceId},
			{QStringLiteral("name"), QStringLiteral("bound-chain")}, {QStringLiteral("overwrite"), true}}).ok);
		const QString destinationId = addTrack();
		const int before = midiConnections();
		const ControlResult applied = run(QStringLiteral("chain.apply"), {{QStringLiteral("name"), QStringLiteral("bound-chain")},
			{QStringLiteral("target"), destinationId}});
		QVERIFY2(applied.ok, qPrintable(applied.errorMessage));
		const QJsonObject report = applied.result.value(QStringLiteral("bindings")).toObject();
		QCOMPARE(report.value(QStringLiteral("saved")).toInt(), 1);
		const QJsonArray restored = report.value(QStringLiteral("restored")).toArray();
		QCOMPARE(restored.size(), 1);
		Effect* fresh = firstEffectOf(trackOf(destinationId));
		QVERIFY(fresh != nullptr && fresh != firstEffectOf(trackOf(sourceId)));
		QCOMPARE(restored.at(0).toObject().value(QStringLiteral("device")).toString(), control::effectIdOf(fresh));
		const MidiController* midi = midiOf(parameterOf(fresh, QStringLiteral("volume")));
		QVERIFY(midi != nullptr);
		QCOMPARE(midi->midiPort().inputChannel(), 3);
		QCOMPARE(midi->midiPort().inputController(), 22);
		QCOMPARE(midiConnections(), before + 1);

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QVERIFY(firstEffectOf(trackOf(destinationId)) == nullptr);
		QCOMPARE(midiConnections(), before);
		run(QStringLiteral("chain.remove"), {{QStringLiteral("name"), QStringLiteral("bound-chain")}});
	}

	//! Criterion 2: a binding whose parameter is gone is reported and skipped, nothing is attached for it.
	void aMissingTargetIsReportedAndSkipped()
	{
		FloatModel* synthParam = nullptr;
		FloatModel* ampParam = nullptr;
		const QString sourceId = boundSource(&synthParam, &ampParam);
		QVERIFY(!sourceId.isEmpty());

		// The chain: its one binding names a parameter the Amplifier no longer reads.
		QVERIFY(run(QStringLiteral("chain.save"), {{QStringLiteral("target"), sourceId},
			{QStringLiteral("name"), QStringLiteral("orphan-src")}, {QStringLiteral("overwrite"), true}}).ok);
		QString stored;
		ControlResult error;
		QVERIFY(controlChainPresetPath(QStringLiteral("orphan-src"), &stored, &error));
		QString orphan;
		QVERIFY(controlChainPresetPath(QStringLiteral("orphan"), &orphan, &error));
		QVERIFY(orphanFirstConnection(stored, orphan));
		const QString chainTarget = addTrack();
		const int before = midiConnections();
		const ControlResult applied = run(QStringLiteral("chain.apply"), {{QStringLiteral("name"), QStringLiteral("orphan")},
			{QStringLiteral("target"), chainTarget}});
		QVERIFY2(applied.ok, qPrintable(applied.errorMessage));
		QJsonObject report = applied.result.value(QStringLiteral("bindings")).toObject();
		QCOMPARE(report.value(QStringLiteral("restored")).toArray().size(), 0);
		QJsonArray skipped = report.value(QStringLiteral("skipped")).toArray();
		QCOMPARE(skipped.size(), 1);
		QJsonObject entry = skipped.at(0).toObject();
		QCOMPARE(entry.value(QStringLiteral("target")).toString(), presetbindings::collect(firstEffectOf(trackOf(sourceId))).at(0).target);
		QCOMPARE(entry.value(QStringLiteral("channel")).toInt(), 3);
		QCOMPARE(entry.value(QStringLiteral("controller")).toInt(), 22);
		QVERIFY(!entry.value(QStringLiteral("reason")).toString().isEmpty());
		QVERIFY(report.value(QStringLiteral("summary")).toString().contains(QStringLiteral("skipped")));
		QVERIFY(presetbindings::collect(firstEffectOf(trackOf(chainTarget))).isEmpty());
		QCOMPARE(midiConnections(), before);  // nothing attached anywhere for it

		// The track: the first connection in the file (the instrument's) is orphaned; the effect's still lands.
		const QString path = m_world.filePath(QStringLiteral("orphan-src.xpf"));
		QVERIFY(run(QStringLiteral("track.preset_save"), {{QStringLiteral("track"), sourceId}, {QStringLiteral("path"), path}}).ok);
		const QString orphanPath = m_world.filePath(QStringLiteral("orphan.xpf"));
		QVERIFY(orphanFirstConnection(path, orphanPath));
		const QString trackTarget = addTrack();
		const int beforeTrack = midiConnections();
		const ControlResult loaded = run(QStringLiteral("track.preset_load"), {{QStringLiteral("track"), trackTarget},
			{QStringLiteral("path"), orphanPath}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		report = loaded.result.value(QStringLiteral("bindings")).toObject();
		QCOMPARE(report.value(QStringLiteral("saved")).toInt(), 2);
		QCOMPARE(report.value(QStringLiteral("restored")).toArray().size(), 1);
		skipped = report.value(QStringLiteral("skipped")).toArray();
		QCOMPARE(skipped.size(), 1);
		QCOMPARE(midiConnections(), beforeTrack + 1);
		run(QStringLiteral("chain.remove"), {{QStringLiteral("name"), QStringLiteral("orphan-src")}});
		run(QStringLiteral("chain.remove"), {{QStringLiteral("name"), QStringLiteral("orphan")}});
	}

	//! A preset written before the manifest loads exactly as before: its bindings still land, nothing is reported.
	void anOlderPresetLoadsAsBefore()
	{
		FloatModel* synthParam = nullptr;
		FloatModel* ampParam = nullptr;
		const QString sourceId = boundSource(&synthParam, &ampParam);
		QVERIFY(!sourceId.isEmpty());
		const QString path = m_world.filePath(QStringLiteral("new.xpf"));
		QVERIFY(run(QStringLiteral("track.preset_save"), {{QStringLiteral("track"), sourceId}, {QStringLiteral("path"), path}}).ok);
		const QString old = m_world.filePath(QStringLiteral("old.xpf"));
		QVERIFY(dropManifest(path, old));
		const QString destinationId = addTrack();
		const ControlResult loaded = run(QStringLiteral("track.preset_load"), {{QStringLiteral("track"), destinationId},
			{QStringLiteral("path"), old}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		const QJsonObject report = loaded.result.value(QStringLiteral("bindings")).toObject();
		QCOMPARE(report.value(QStringLiteral("saved")).toInt(), 0);
		QCOMPARE(report.value(QStringLiteral("restored")).toArray().size(), 0);
		QCOMPARE(presetbindings::collectTrack(trackOf(destinationId)).size(), 2);  // the inline bindings, as always
	}
};

QTEST_GUILESS_MAIN(PresetBindingsTest)
#include "PresetBindingsTest.moc"
