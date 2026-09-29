/*
 * ControlSuccessPathsTest.cpp - the success path of every command the suite only ever refused
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

/*! R6.3: a command whose reply was never seen SUCCEED under result checks has an
 *  unverified reply contract. tests/checked-coverage.py measured 27 such commands
 *  on the release surface (gate 16's ratchet, tests/checked-coverage-unreached.txt).
 *  Their own test files hold only refusals, so the success path of each is built
 *  here: the prerequisite state first, then one call that must succeed. Every
 *  LMMS_TESTS entry runs with ZENE_CONTROL_CHECK_RESULTS, so a reply that breaks
 *  its own resultSchema comes back as a typed failure and the QVERIFY names it.
 *
 *  Each slot also asserts one fact the success claims, so a success that did
 *  nothing is not a pass. What stays refused by design (telemetry.consent needs
 *  a human, crash.upload_report does no network) is listed in the ratchet with
 *  its reason, not faked here. Three live beside their group instead: the
 *  recording verbs need a configured input (tests/control-record-inputs.py),
 *  automation.record_mode_set needs a device parameter, which only a test given
 *  the plugin directory has (ControlAutomationScriptTest), and
 *  project.audible_diff renders through the running binary as child processes
 *  (tests/control-project-audible-diff.py).
 */

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include "ControlRegistry.h"
#include "Engine.h"
#include "MidiEvent.h"
#include "MidiLearn.h"
#include "Mixer.h"
#include "ModulationTestSupport.h"
#include "RackTestSupport.h"
#include "Song.h"

using namespace lmms;
using modtest::run;
using namespace racktest;

namespace
{

//! The reply, or a QVERIFY-able failure naming the command and its error.
#define REQUIRE_OK(result, id) \
	QVERIFY2((result).ok, qPrintable(QStringLiteral("%1: %2").arg(id, (result).errorMessage)))

QString addTrack(const QString& type)
{
	const ControlResult track = run(QStringLiteral("track.add"), {{QStringLiteral("type"), type}});
	return track.ok ? track.result.value(QStringLiteral("track")).toString() : QString();
}

QString addClip(const QString& track, int position)
{
	const ControlResult clip = run(QStringLiteral("clip.add"),
		{{QStringLiteral("track"), track}, {QStringLiteral("position"), position},
			{QStringLiteral("length"), 192}});
	return clip.ok ? clip.result.value(QStringLiteral("clip")).toString() : QString();
}

//! A 12-note Scala keyboard mapping, linear, A4 = 440 Hz on key 69.
QString writeKeymap(const QTemporaryDir& dir)
{
	const QString path = dir.filePath(QStringLiteral("linear.kbm"));
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) { return QString(); }
	QTextStream out(&file);
	out << "! linear.kbm\n12\n0\n127\n60\n69\n440.0\n12\n";
	for (int degree = 0; degree < 12; ++degree) { out << degree << "\n"; }
	return path;
}

} // namespace


class ControlSuccessPathsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QString why;
		QVERIFY2(initRackFixture(&why), qPrintable(why));
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase() { teardownRackFixture(); }

	void modulatorTargetRemove()
	{
		const ControlResult created = run(QStringLiteral("modulator.create"),
			{{QStringLiteral("name"), QStringLiteral("Route")}, {QStringLiteral("rate"), 1.0}});
		REQUIRE_OK(created, "modulator.create");
		const QString modulator = created.result.value(QStringLiteral("modulator")).toString();
		REQUIRE_OK(run(QStringLiteral("modulator.target_set"),
			{{QStringLiteral("modulator"), modulator}, {QStringLiteral("channel"), underTestChannel()},
				{QStringLiteral("chain"), kDrivenChain}, {QStringLiteral("effect"), 0},
				{QStringLiteral("parameter"), QString::fromLatin1(kGainName)},
				{QStringLiteral("depth"), 0.25}}), "modulator.target_set");
		const ControlResult removed = run(QStringLiteral("modulator.target_remove"),
			{{QStringLiteral("modulator"), modulator}, {QStringLiteral("target"), 0}});
		REQUIRE_OK(removed, "modulator.target_remove");
		REQUIRE_OK(run(QStringLiteral("modulator.remove"), {{QStringLiteral("modulator"), modulator}}),
			"modulator.remove");
	}

	void rackMacrosAndSelection()
	{
		const QString channel = underTestChannel();
		const ControlResult macro = run(QStringLiteral("rack.macro_add"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("name"), QStringLiteral("Level")},
				{QStringLiteral("value"), 0.0}});
		REQUIRE_OK(macro, "rack.macro_add");
		const QString id = macro.result.value(QStringLiteral("macro")).toString();
		REQUIRE_OK(run(QStringLiteral("rack.macro_target_add"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), id},
				{QStringLiteral("chain"), kDrivenChain}, {QStringLiteral("effect"), 0},
				{QStringLiteral("parameter"), QString::fromLatin1(kGainName)},
				{QStringLiteral("low"), 0.25}, {QStringLiteral("high"), 0.75}}), "rack.macro_target_add");
		REQUIRE_OK(run(QStringLiteral("rack.macro_target_remove"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), id},
				{QStringLiteral("target"), 0}}), "rack.macro_target_remove");
		REQUIRE_OK(run(QStringLiteral("rack.macro_remove"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), id}}), "rack.macro_remove");
		REQUIRE_OK(run(QStringLiteral("rack.set_selected"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("chain"), kDrivenChain}}),
			"rack.set_selected");
		QCOMPARE(rackUnderTest().selectedChain(), kDrivenChain);
	}

	void vcaUnassign()
	{
		const ControlResult group = run(QStringLiteral("vca.create"), {{QStringLiteral("name"), QStringLiteral("Bus")}});
		REQUIRE_OK(group, "vca.create");
		const QJsonObject membership{{QStringLiteral("group"), group.result.value(QStringLiteral("group"))},
			{QStringLiteral("channel"), underTestChannel()}};
		REQUIRE_OK(run(QStringLiteral("vca.assign"), membership), "vca.assign");
		const ControlResult unassigned = run(QStringLiteral("vca.unassign"), membership);
		REQUIRE_OK(unassigned, "vca.unassign");
	}

	void clipLinkSync()
	{
		const QString track = addTrack(QStringLiteral("instrument"));
		QVERIFY(!track.isEmpty());
		const QString first = addClip(track, 0);
		const QString second = addClip(track, 384);
		QVERIFY(!first.isEmpty() && !second.isEmpty());
		REQUIRE_OK(run(QStringLiteral("clip.link_create"),
			{{QStringLiteral("clip"), first}, {QStringLiteral("clips"), QJsonArray{second}}}), "clip.link_create");
		REQUIRE_OK(run(QStringLiteral("clip.link_sync"), {{QStringLiteral("clip"), first}}), "clip.link_sync");
	}

	void visibilitySetRemove()
	{
		const QString track = addTrack(QStringLiteral("instrument"));
		REQUIRE_OK(run(QStringLiteral("track.visibility_set_save"),
			{{QStringLiteral("name"), QStringLiteral("Keys")}, {QStringLiteral("tracks"), QJsonArray{track}}}),
			"track.visibility_set_save");
		REQUIRE_OK(run(QStringLiteral("track.visibility_set_remove"), {{QStringLiteral("name"), QStringLiteral("Keys")}}),
			"track.visibility_set_remove");
	}

	void tuningNoteTableAndKeymap()
	{
		REQUIRE_OK(run(QStringLiteral("mts.set_note"),
			{{QStringLiteral("note"), 69}, {QStringLiteral("frequency"), 442.0}}), "mts.set_note");
		REQUIRE_OK(run(QStringLiteral("mts.set_tuning"),
			{{QStringLiteral("note"), 60}, {QStringLiteral("frequency"), 261.63}}), "mts.set_tuning");
		const QString keymap = writeKeymap(m_dir);
		QVERIFY(!keymap.isEmpty());
		REQUIRE_OK(run(QStringLiteral("mts.load_keymap"), {{QStringLiteral("path"), keymap}}), "mts.load_keymap");
		REQUIRE_OK(run(QStringLiteral("mts.reset")), "mts.reset");
	}

	void controllerFeedbackTakeoverAndTemplates()
	{
		// One control bound through the landed MIDI-learn path, so the group's
		// "the only bound control" default resolves without naming it.
		FloatModel& volume = Engine::mixer()->mixerChannel(kChannel)->m_volumeModel;
		MidiLearn::instance()->setFocusTarget(&volume);
		MidiLearn::instance()->setEnabled(true);
		QVERIFY(MidiLearn::instance()->handleMidiEvent(MidiEvent(MidiControlChange, 3, 21, 64)));
		REQUIRE_OK(run(QStringLiteral("controller.feedback"), {{QStringLiteral("enabled"), true}}), "controller.feedback");
		REQUIRE_OK(run(QStringLiteral("controller.soft_takeover"),
			{{QStringLiteral("enabled"), true}, {QStringLiteral("target"), 0.5}}), "controller.soft_takeover");
		const QString name = QStringLiteral("zene-success-paths-selftest");
		REQUIRE_OK(run(QStringLiteral("controller.template_save"), {{QStringLiteral("name"), name}}),
			"controller.template_save");
		REQUIRE_OK(run(QStringLiteral("controller.template_apply"), {{QStringLiteral("name"), name}}),
			"controller.template_apply");
		REQUIRE_OK(run(QStringLiteral("controller.template_delete"), {{QStringLiteral("name"), name}}),
			"controller.template_delete");
	}

	void sessionFollowActions()
	{
		REQUIRE_OK(run(QStringLiteral("session.set_grid"),
			{{QStringLiteral("tracks"), 1}, {QStringLiteral("scenes"), 2}}), "session.set_grid");
		const ControlResult follow = run(QStringLiteral("session.follow_set"),
			{{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0}, {QStringLiteral("enabled"), true},
				{QStringLiteral("actions"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("next")},
					{QStringLiteral("chance"), 1.0}}}}});
		REQUIRE_OK(follow, "session.follow_set");
	}

	void projectDiffMergeAndConflicts()
	{
		const QString a = m_dir.filePath(QStringLiteral("a.mmp"));
		const QString b = m_dir.filePath(QStringLiteral("b.mmp"));
		REQUIRE_OK(run(QStringLiteral("project.save"), {{QStringLiteral("path"), a}}), "project.save a");
		REQUIRE_OK(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), 133}}), "transport.set_tempo");
		REQUIRE_OK(run(QStringLiteral("project.save"), {{QStringLiteral("path"), b}}), "project.save b");
		const ControlResult diff = run(QStringLiteral("project.diff"),
			{{QStringLiteral("a"), a}, {QStringLiteral("b"), b}});
		REQUIRE_OK(diff, "project.diff");
		const ControlResult merged = run(QStringLiteral("project.merge"),
			{{QStringLiteral("base"), a}, {QStringLiteral("ours"), a}, {QStringLiteral("theirs"), b}});
		REQUIRE_OK(merged, "project.merge");
		REQUIRE_OK(run(QStringLiteral("project.conflicts"), {{QStringLiteral("file"), a}}), "project.conflicts");
		// project.audible_diff renders both files through the RUNNING binary as
		// child processes, which in-process is this test executable: its success
		// path is driven over the socket instead (tests/control-project-audible-diff.py).
	}

private:
	QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(ControlSuccessPathsTest)
#include "ControlSuccessPathsTest.moc"
