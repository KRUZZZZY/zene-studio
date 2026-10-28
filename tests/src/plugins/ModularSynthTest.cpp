/*
 * ModularSynthTest.cpp - Q050-059.1: the modular instrument
 *
 * A note played through the starter graph (oscillator -> filter -> output, an envelope on the level and an LFO on
 * the cutoff) sounds, and the filter and the wiring are really in its path. The instrument's state - modules,
 * wiring and every module parameter - saves and restores, through a state and through project.save/project.open;
 * a state without a graph (what an instrument saved before the graph existed has) loads as the starter graph, and a
 * damaged one falls back to it instead of playing half-wired. Agents reach it through plugin.list / plugin.load /
 * plugin.param_*, and the render path allocates nothing. (The window: tests/src/gui/ModularSynthViewTest.cpp.)
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include <QDomDocument>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <vector>

#include "../core/AllocationProbe.h"
#include "ModularSynthTestSupport.h"
#include "PluginFactory.h"

using namespace lmms;
using namespace lmms::modular_test;


class ModularSynthTest : public ModularSynthFixture
{
	Q_OBJECT

private slots:
	void initTestCase() { initEngine(); }
	void cleanupTestCase() { cleanupEngine(); }

	//! ACCEPTANCE 1: a new modular instrument carries the starter graph, and a note through it sounds.
	void aNoteThroughTheStarterGraphSounds()
	{
		fresh();
		QCOMPARE(wiring(), StarterWiring);
		QCOMPARE(m_instrument->parameterCount(), 16); // 4 oscillator + 4 envelope + 4 filter + 3 LFO + 1 output
		const std::vector<float> sound = playNote();
		QVERIFY2(sound.size() > static_cast<std::size_t>(m_fpp) * 8, "the note was rendered past its length");
		const float peak = peakOf(sound);
		QVERIFY2(peak > 0.05f, qPrintable(QStringLiteral("the note is audible (peak %1)").arg(peak)));
		QVERIFY2(peak < 2.f, "and is not blowing up");
		// The envelope's release takes the note to silence at its end.
		QVERIFY2(std::abs(sound.back()) < 1e-3f, "the released note dies away");
	}

	//! ACCEPTANCE 1: the filter and the wiring are what the note goes through - the cutoff shapes the sound, a
	//! filter not wired to the output is silent, and wiring it sounds.
	void theFilterAndTheWiringAreInTheNotesPath()
	{
		fresh();
		setValue(QStringLiteral("LFO 1 Depth"), 0.f);
		setValue(QStringLiteral("Env 1 Attack"), 0.f);
		setValue(QStringLiteral("Filter 1 Cutoff"), 12000.f);
		const float open = brightness(playNote(45), static_cast<std::size_t>(m_fpp));
		setValue(QStringLiteral("Filter 1 Cutoff"), 150.f);
		const float closed = brightness(playNote(45), static_cast<std::size_t>(m_fpp));
		QVERIFY2(open > 0.f && closed < 0.25f * open,
			qPrintable(QStringLiteral("a closed low-pass darkens the saw: %1 vs %2").arg(closed).arg(open)));
		setValue(QStringLiteral("Filter 1 Mode"), 1.f); // high-pass at 150 Hz: brighter than the low-pass
		const float highPassed = brightness(playNote(45), static_cast<std::size_t>(m_fpp));
		QVERIFY2(highPassed > 4.f * closed, qPrintable(QStringLiteral("high-pass %1 vs low-pass %2").arg(highPassed).arg(closed)));

		const QString modules = QStringLiteral(
			"<module id=\"osc1\" type=\"oscillator\"/><module id=\"filter1\" type=\"filter\"/><module id=\"out\" type=\"output\"/>");
		QDomDocument unwired;
		restore(stateElement(unwired, modules + QStringLiteral("<wire from=\"osc1\" to=\"filter1\" port=\"in\"/>")));
		QCOMPARE(loadProblem(), QString());
		QCOMPARE(wiring(), QStringLiteral("osc1 -> filter1.in"));
		QCOMPARE(peakOf(playNote()), 0.f);

		QDomDocument wired;
		restore(stateElement(wired, modules + QStringLiteral(
			"<wire from=\"osc1\" to=\"filter1\" port=\"in\"/><wire from=\"filter1\" to=\"out\" port=\"in\"/>")));
		QCOMPARE(wiring(), QStringLiteral("osc1 -> filter1.in\nfilter1 -> out.in"));
		QVERIFY2(peakOf(playNote()) > 0.05f, "oscillator -> filter -> output sounds once the filter reaches the output");
	}

	//! ACCEPTANCE 2: the modules, the wiring and every parameter save and restore; the restored instrument renders the
	//! same audio, sample for sample.
	void theStateRoundTripsModulesWiringAndParameters()
	{
		fresh();
		QDomDocument authored;
		restore(stateElement(authored, QStringLiteral(
			"<module id=\"osc1\" type=\"oscillator\" wave=\"2\"/><module id=\"osc2\" type=\"oscillator\" wave=\"0\" coarse=\"7\"/>"
			"<module id=\"vib\" type=\"lfo\" rate=\"5\" depth=\"0.1\"/><module id=\"filter1\" type=\"filter\" cutoff=\"900\"/>"
			"<module id=\"env1\" type=\"envelope\" attack=\"20\" release=\"60\"/><module id=\"out\" type=\"output\" gain=\"0.5\"/>"
			"<wire from=\"osc1\" to=\"filter1\" port=\"in\"/><wire from=\"osc2\" to=\"filter1\" port=\"in\"/>"
			"<wire from=\"vib\" to=\"osc2\" port=\"pitch\"/><wire from=\"filter1\" to=\"out\" port=\"in\"/>"
			"<wire from=\"env1\" to=\"out\" port=\"amp\"/>")));
		QCOMPARE(loadProblem(), QString());
		setValue(QStringLiteral("Filter 1 Resonance"), 0.7f);
		setValue(QStringLiteral("Osc 2 Fine"), -12.f);
		const QString authoredWiring = wiring();
		const std::vector<float> before = playNote();
		QVERIFY(peakOf(before) > 0.05f);

		QDomDocument doc;
		QDomElement saved = doc.createElement(QStringLiteral("state"));
		m_instrument->saveState(doc, saved);
		doc.appendChild(saved);
		const QDomElement settings = saved.firstChildElement();
		QCOMPARE(settings.tagName(), QStringLiteral("modularsynth"));
		QVERIFY2(!settings.firstChildElement(QStringLiteral("graph")).isNull(), "the graph is part of the state");

		fresh();
		QCOMPARE(wiring(), StarterWiring);
		restore(settings);
		QCOMPARE(wiring(), authoredWiring);
		QCOMPARE(model(QStringLiteral("Osc 1 Wave"))->value<float>(), 2.f);
		QCOMPARE(model(QStringLiteral("Osc 2 Coarse"))->value<float>(), 7.f);
		QCOMPARE(model(QStringLiteral("Osc 2 Fine"))->value<float>(), -12.f);
		QCOMPARE(model(QStringLiteral("LFO 1 Rate"))->value<float>(), 5.f);
		QCOMPARE(model(QStringLiteral("Filter 1 Cutoff"))->value<float>(), 900.f);
		QVERIFY(std::abs(model(QStringLiteral("Filter 1 Resonance"))->value<float>() - 0.7f) < 1e-6f);
		QCOMPARE(model(QStringLiteral("Env 1 Release"))->value<float>(), 60.f);
		QCOMPARE(model(QStringLiteral("Output Gain"))->value<float>(), 0.5f);
		QCOMPARE(m_instrument->desiredReleaseTimeMs(), 60.f);
		const std::vector<float> after = playNote();
		QCOMPARE(after.size(), before.size());
		for (std::size_t i = 0; i < before.size(); ++i)
		{
			if (std::abs(after[i] - before[i]) > 1e-6f)
			{
				QFAIL(qPrintable(QStringLiteral("frame %1 differs: %2 vs %3").arg(i).arg(after[i]).arg(before[i])));
			}
		}
	}

	//! ACCEPTANCE 2: a state with no graph - an instrument saved before the graph existed - loads as the starter graph
	//! at its defaults, with no error, and plays.
	void aStateWithoutAGraphLoadsTheStarterGraph()
	{
		fresh();
		QDomDocument other;
		restore(stateElement(other, QStringLiteral(
			"<module id=\"o\" type=\"oscillator\"/><module id=\"out\" type=\"output\"/><wire from=\"o\" to=\"out\" port=\"in\"/>")));
		QCOMPARE(wiring(), QStringLiteral("o -> out.in"));

		QDomDocument old;
		restore(stateElement(old, {}, false));
		QCOMPARE(wiring(), StarterWiring);
		QCOMPARE(loadProblem(), QString());
		QCOMPARE(model(QStringLiteral("Filter 1 Cutoff"))->value<float>(), 1200.f);
		QCOMPARE(model(QStringLiteral("Osc 1 Wave"))->value<float>(), 1.f);
		QVERIFY(peakOf(playNote()) > 0.05f);
	}

	//! ACCEPTANCE 2: a graph that cannot be played is never installed: the starter graph is, the reason is kept, and
	//! the instrument still plays.
	void aDamagedGraphFallsBackToTheStarterGraph_data()
	{
		QTest::addColumn<QString>("graph");
		QTest::addColumn<QString>("reason");
		const QString oscOut = QStringLiteral("<module id=\"osc1\" type=\"oscillator\"/><module id=\"out\" type=\"output\"/>");
		QTest::newRow("unknown module") << QStringLiteral("<module id=\"x\" type=\"granular\"/><module id=\"out\" type=\"output\"/>")
			<< QStringLiteral("unknown module type");
		QTest::newRow("no output") << QStringLiteral("<module id=\"osc1\" type=\"oscillator\"/>") << QStringLiteral("output");
		QTest::newRow("two outputs") << oscOut + QStringLiteral("<module id=\"out2\" type=\"output\"/>") << QStringLiteral("output");
		QTest::newRow("duplicate id") << oscOut + QStringLiteral("<module id=\"osc1\" type=\"lfo\"/>") << QStringLiteral("twice");
		QTest::newRow("unknown port") << oscOut + QStringLiteral("<wire from=\"osc1\" to=\"out\" port=\"sidechain\"/>")
			<< QStringLiteral("no input");
		QTest::newRow("audio into control") << oscOut + QStringLiteral("<wire from=\"osc1\" to=\"out\" port=\"amp\"/>")
			<< QStringLiteral("cannot feed");
		QTest::newRow("missing module") << oscOut + QStringLiteral("<wire from=\"ghost\" to=\"out\" port=\"in\"/>")
			<< QStringLiteral("unknown module");
		QTest::newRow("cycle") << QStringLiteral(
			"<module id=\"a\" type=\"filter\"/><module id=\"b\" type=\"filter\"/><module id=\"out\" type=\"output\"/>"
			"<wire from=\"a\" to=\"b\" port=\"in\"/><wire from=\"b\" to=\"a\" port=\"in\"/><wire from=\"b\" to=\"out\" port=\"in\"/>")
			<< QStringLiteral("cycle");
	}

	void aDamagedGraphFallsBackToTheStarterGraph()
	{
		QFETCH(QString, graph);
		QFETCH(QString, reason);
		fresh();
		QDomDocument doc;
		restore(stateElement(doc, graph));
		QCOMPARE(wiring(), StarterWiring);
		QVERIFY2(loadProblem().contains(reason), qPrintable(loadProblem()));
		QVERIFY(peakOf(playNote()) > 0.05f);
	}

	//! ACCEPTANCE 2, through the files and commands a user and an agent use: project.save writes the graph,
	//! project.open brings it back; a project whose instrument has no <graph> opens with the starter graph.
	void projectSaveAndOpenKeepTheGraph()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		fresh();
		QDomDocument authored;
		restore(stateElement(authored, QStringLiteral(
			"<module id=\"osc1\" type=\"oscillator\" coarse=\"-5\"/><module id=\"filter1\" type=\"filter\" mode=\"2\"/>"
			"<module id=\"out\" type=\"output\"/>"
			"<wire from=\"osc1\" to=\"filter1\" port=\"in\"/><wire from=\"filter1\" to=\"out\" port=\"in\"/>")));
		setValue(QStringLiteral("Filter 1 Cutoff"), 640.f);
		const std::vector<float> before = playNote();

		const QString project = dir.filePath(QStringLiteral("modular.mmp"));
		const ControlResult saved = run(QStringLiteral("project.save"), {{QStringLiteral("path"), project}});
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));

		Engine::getSong()->clearProject();
		Engine::getSong()->setLoadOnLaunch(false);
		const ControlResult opened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), project}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		QVERIFY2(adoptModularTrack(), "the opened project carries the modular instrument");
		QCOMPARE(wiring(), QStringLiteral("osc1 -> filter1.in\nfilter1 -> out.in"));
		QCOMPARE(model(QStringLiteral("Osc 1 Coarse"))->value<float>(), -5.f);
		QCOMPARE(model(QStringLiteral("Filter 1 Mode"))->value<float>(), 2.f);
		QCOMPARE(model(QStringLiteral("Filter 1 Cutoff"))->value<float>(), 640.f);
		const std::vector<float> after = playNote();
		QCOMPARE(after.size(), before.size());
		float worst = 0.f;
		for (std::size_t i = 0; i < before.size(); ++i) { worst = std::max(worst, std::abs(after[i] - before[i])); }
		QVERIFY2(worst < 1e-6f, qPrintable(QStringLiteral("the reopened instrument sounds the same (%1)").arg(worst)));

		// The same project with the graph taken out of the instrument's state: what an older save holds.
		QFile file(project);
		QVERIFY(file.open(QIODevice::ReadOnly));
		QDomDocument doc;
		QVERIFY(doc.setContent(&file));
		file.close();
		const QDomNodeList graphs = doc.elementsByTagName(QStringLiteral("graph"));
		QCOMPARE(graphs.size(), 1);
		const QDomElement graph = graphs.at(0).toElement();
		QCOMPARE(graph.parentNode().nodeName(), QStringLiteral("modularsynth"));
		graph.parentNode().removeChild(graph);
		const QString older = dir.filePath(QStringLiteral("older.mmp"));
		QFile out(older);
		QVERIFY(out.open(QIODevice::WriteOnly));
		out.write(doc.toByteArray());
		out.close();

		Engine::getSong()->clearProject();
		const ControlResult reopened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), older}});
		QVERIFY2(reopened.ok, qPrintable(reopened.errorMessage));
		QVERIFY(adoptModularTrack());
		QCOMPARE(wiring(), StarterWiring);
		QCOMPARE(loadProblem(), QString());
		QVERIFY(peakOf(playNote()) > 0.05f);
	}

	//! Agents reach the instrument with the existing plug-in commands: listed, loadable, every module parameter
	//! readable and writable by name, and a write changes the sound.
	void theInstrumentIsReachableThroughThePluginCommands()
	{
		Engine::getSong()->clearProject();
		QVERIFY(!getPluginFactory()->pluginInfo("modularsynth").isNull());
		QCOMPARE(getPluginFactory()->pluginInfo("modularsynth").descriptor->type, Plugin::Type::Instrument);
		QString id;
		const ControlResult listed = run(QStringLiteral("plugin.list"),
			{{QStringLiteral("kind"), QStringLiteral("instrument")}, {QStringLiteral("loadable_only"), true}});
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		for (const QJsonValue& v : listed.result.value(QStringLiteral("devices")).toArray())
		{
			if (v.toObject().value(QStringLiteral("name")).toString() == QLatin1String("modularsynth"))
			{
				id = v.toObject().value(QStringLiteral("id")).toString();
			}
		}
		QVERIFY2(!id.isEmpty(), "plugin.list carries modularsynth as loadable");

		const QString track = revtest::addTrack();
		QVERIFY(!track.isEmpty());
		const ControlResult loaded = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), track}, {QStringLiteral("device"), id}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));

		QStringList names;
		for (int index = 0; index < 16; ++index)
		{
			const ControlResult got = run(QStringLiteral("plugin.param_get"),
				{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), QStringLiteral("inst")}, {QStringLiteral("index"), index}});
			QVERIFY2(got.ok, qPrintable(got.errorMessage));
			names << got.result.value(QStringLiteral("parameter")).toObject().value(QStringLiteral("name")).toString();
		}
		QCOMPARE(names, (QStringList{
			QStringLiteral("Osc 1 Wave"), QStringLiteral("Osc 1 Coarse"), QStringLiteral("Osc 1 Fine"), QStringLiteral("Osc 1 Level"),
			QStringLiteral("Env 1 Attack"), QStringLiteral("Env 1 Decay"), QStringLiteral("Env 1 Sustain"), QStringLiteral("Env 1 Release"),
			QStringLiteral("Filter 1 Mode"), QStringLiteral("Filter 1 Cutoff"), QStringLiteral("Filter 1 Resonance"), QStringLiteral("Filter 1 Mod depth"),
			QStringLiteral("LFO 1 Wave"), QStringLiteral("LFO 1 Rate"), QStringLiteral("LFO 1 Depth"), QStringLiteral("Output Gain")}));

		QVERIFY(adoptModularTrack());
		setValue(QStringLiteral("LFO 1 Depth"), 0.f);
		const float before = peakOf(playNote());
		const ControlResult set = run(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), track},
			{QStringLiteral("plugin"), QStringLiteral("inst")}, {QStringLiteral("name"), QStringLiteral("Output Gain")},
			{QStringLiteral("value"), 0.2}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QVERIFY(std::abs(model(QStringLiteral("Output Gain"))->value<float>() - 0.2f) < 1e-6f);
		const float after = peakOf(playNote());
		QVERIFY2(std::abs(after / before - 0.25f) < 0.02f,
			qPrintable(QStringLiteral("gain 0.8 -> 0.2 quarters the level: %1 -> %2").arg(before).arg(after)));
		// The write is one undo step.
		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QVERIFY(std::abs(model(QStringLiteral("Output Gain"))->value<float>() - 0.8f) < 1e-6f);
	}

	//! A graph replaced while a note sounds: the note goes on with the new graph, nothing reads the freed one.
	void aGraphReplacedUnderASoundingNoteKeepsPlaying()
	{
		fresh();
		const Note note(TimePos(384), TimePos(0), 57);
		NotePlayHandle* nph = NotePlayHandleManager::acquire(m_track, 0, m_fpp * 40, note);
		std::vector<SampleFrame> buffer(static_cast<std::size_t>(m_fpp));
		std::vector<float> left;
		const auto period = [&] {
			std::fill(buffer.begin(), buffer.end(), SampleFrame{});
			nph->play(std::span<SampleFrame>{buffer});
			for (const SampleFrame& frame : buffer) { left.push_back(frame[0]); }
		};
		for (int i = 0; i < 4; ++i) { period(); }
		QDomDocument doc;
		restore(stateElement(doc, QStringLiteral(
			"<module id=\"o\" type=\"oscillator\" wave=\"0\"/><module id=\"out\" type=\"output\"/><wire from=\"o\" to=\"out\" port=\"in\"/>")));
		const std::size_t replacedAt = left.size();
		for (int i = 0; i < 4; ++i) { period(); }
		NotePlayHandleManager::release(nph);
		QVERIFY2(peakOf(left, replacedAt + static_cast<std::size_t>(m_fpp)) > 0.3f, "the note sounds through the new graph");
	}

	//! Realtime: notes through the starter graph - their first period (the voice is made in the note's own storage),
	//! their sustain, their release and their end - allocate and free nothing on the render path.
	void theRenderPathAllocatesNothing()
	{
		fresh();
		std::vector<NotePlayHandle*> notes;
		for (int key : {48, 55, 60, 64}) { notes.push_back(NotePlayHandleManager::acquire(m_track, 0, m_fpp * 6, Note(TimePos(384), TimePos(0), key))); }
		std::vector<SampleFrame> buffer(static_cast<std::size_t>(m_fpp));
		std::uint64_t allocations = 0;
		std::uint64_t frees = 0;
		float peak = 0.f;
		bool finished = false;
		for (int period = 0; period < 400 && !finished; ++period)
		{
			finished = true;
			for (NotePlayHandle* nph : notes)
			{
				if (nph->isFinished()) { continue; }
				finished = false;
				std::fill(buffer.begin(), buffer.end(), SampleFrame{});
				test::resetAllocationCount();
				test::tlCountAllocations = true;
				nph->play(std::span<SampleFrame>{buffer}); // the note's whole play path: track, instrument, release
				test::tlCountAllocations = false;
				allocations += test::tlAllocationCount;
				frees += test::tlFreeCount;
				for (const SampleFrame& frame : buffer) { peak = std::max(peak, std::abs(frame[0])); }
			}
		}
		QVERIFY2(finished, "every note was released and finished");
		QVERIFY(peak > 0.05f);
		for (NotePlayHandle* nph : notes) { NotePlayHandleManager::release(nph); }
		QCOMPARE(static_cast<qulonglong>(allocations), 0ull);
		QCOMPARE(static_cast<qulonglong>(frees), 0ull);
	}
};

QTEST_MAIN(ModularSynthTest)
#include "ModularSynthTest.moc"
