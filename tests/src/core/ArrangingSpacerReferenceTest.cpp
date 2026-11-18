/*
 * ArrangingSpacerReferenceTest.cpp - track spacers, the reference project and project switching (split out of
 *                                  ArrangingCatchUpTest to keep both under the file-length ratchet)
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
/*! 0.5.0 F3 (moved unchanged from ArrangingCatchUpTest.cpp):
 *  - WF-107: track.set_spacer sets a spacer, one undo step, and a save and reload keeps it.
 *  - WF-042: reference.open parses another project; its tracks, clips and devices copy into this one, each
 *    one undo step; a clip of the wrong kind is refused; project.switch saves nothing it should not, keeps
 *    the project it left as the reference, and refuses to lose an untitled session's changes.
 */

#include <QtTest>

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryDir>

#include <cmath>
#include <memory>

#include "ArrangementEdits.h"
#include "AudioEngine.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "InstrumentPlayHandle.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "InputMonitor.h"
#include "Mixer.h"
#include "ProjectInfo.h"
#include "ReferenceProject.h"
#include "SampleClip.h"
#include "SampleFrameEdits.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TrackSpacers.h"
#include "TakeLaneTestSupport.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

int indexOfReferenceTrack(const QString& name)
{
	const QJsonArray tracks = run(QStringLiteral("reference.get_state")).result.value(QStringLiteral("tracks")).toArray();
	for (const QJsonValue& value : tracks)
	{
		if (value.toObject().value(QStringLiteral("name")).toString() == name) { return value.toObject().value(QStringLiteral("index")).toInt(); }
	}
	return -1;
}

} // namespace

class ArrangingSpacerReferenceTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // the build's effects, for the reference device copy
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_tone = taketest::writeToneWav(QDir(m_dir.path()).filePath(QStringLiteral("tone.wav")), 440.0);
		QVERIFY(!m_tone.isEmpty());
	}

	void cleanupTestCase()
	{
		ReferenceProject::instance().close();
		ControlRegistry::setReady(false);
		Engine::destroy();
	}
	void aSpacerIsOneStepAndSurvivesASave()
	{
		Track* track = Track::create(Track::Type::Instrument, Engine::getSong());
		const QString id = control::trackIdOf(track);
		const ControlResult set = run(QStringLiteral("track.set_spacer"), {{QStringLiteral("track"), id}, {QStringLiteral("height"), 32},
			{QStringLiteral("label"), QStringLiteral("Drums")}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(TrackSpacers::instance().spacer(track->id()).height, 32);
		QCOMPARE(run(QStringLiteral("track.spacers")).result.value(QStringLiteral("count")).toInt(), 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(TrackSpacers::instance().spacer(track->id()).height, 0);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(TrackSpacers::instance().spacer(track->id()).label, QStringLiteral("Drums"));
		const QString path = QDir(m_dir.path()).filePath(QStringLiteral("spacers.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(path));
		const int trackId = track->id();
		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), path}}).ok);
		QCOMPARE(TrackSpacers::instance().spacer(trackId).height, 32);
		QCOMPARE(TrackSpacers::instance().spacer(trackId).label, QStringLiteral("Drums"));
		QCOMPARE(run(QStringLiteral("track.set_spacer"), {{QStringLiteral("track"), QStringLiteral("trk-999999")}}).errorKind,
			ControlErrorKind::NotFound);
	}

	void aSpacerWithoutItsTrackIdAttachesToNothing()
	{
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("track-spacers"));
		doc.appendChild(root);
		QDomElement missing = doc.createElement(QStringLiteral("spacer")); // no track attribute at all
		missing.setAttribute(QStringLiteral("height"), 40);
		root.appendChild(missing);
		QDomElement bogus = doc.createElement(QStringLiteral("spacer")); // an id that is not a number
		bogus.setAttribute(QStringLiteral("track"), QStringLiteral("bogus"));
		bogus.setAttribute(QStringLiteral("height"), 50);
		root.appendChild(bogus);
		QDomElement good = doc.createElement(QStringLiteral("spacer")); // a well-formed spacer still loads
		good.setAttribute(QStringLiteral("track"), 7);
		good.setAttribute(QStringLiteral("height"), 30);
		good.setAttribute(QStringLiteral("label"), QStringLiteral("Drums"));
		root.appendChild(good);
		TrackSpacers::instance().load(root);
		QCOMPARE(TrackSpacers::instance().spacer(0).height, 0);  // a missing or invalid id must not land on track 0
		QCOMPARE(TrackSpacers::instance().spacer(7).height, 30);
		QCOMPARE(TrackSpacers::instance().spacer(7).label, QStringLiteral("Drums"));
		TrackSpacers::instance().clear();
	}

	void aLoadedSpacerLabelIsLimited()
	{
		QVERIFY(run(QStringLiteral("project.new")).ok);
		Track* track = Track::create(Track::Type::Instrument, Engine::getSong());
		const int trackId = track->id();
		TrackSpacers::instance().setSpacer(trackId, {32, QStringLiteral("Drums")});
		const QString path = QDir(m_dir.path()).filePath(QStringLiteral("long-spacer-label.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(path));

		QFile project(path);
		QVERIFY(project.open(QIODevice::ReadOnly));
		QDomDocument document;
		QVERIFY(document.setContent(project.readAll()));
		project.close();
		QDomElement savedSpacer = document.documentElement().firstChildElement(QStringLiteral("song"))
			.firstChildElement(QStringLiteral("track-spacers"))
			.firstChildElement(QStringLiteral("spacer"));
		QVERIFY(!savedSpacer.isNull());
		QCOMPARE(savedSpacer.attribute(QStringLiteral("track")).toInt(), trackId);
		const QString overlong = QString(TrackSpacers::MaxLabelLength + 1, QLatin1Char('x'));
		savedSpacer.setAttribute(QStringLiteral("label"), overlong);
		const QByteArray projectBytes = document.toByteArray(1);
		QVERIFY(project.open(QIODevice::WriteOnly | QIODevice::Truncate));
		QCOMPARE(project.write(projectBytes), static_cast<qint64>(projectBytes.size()));
		project.close();

		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), path}}).ok);
		const QString loaded = TrackSpacers::instance().spacer(trackId).label;
		QCOMPARE(loaded.size(), TrackSpacers::MaxLabelLength);
		QCOMPARE(loaded, overlong.left(TrackSpacers::MaxLabelLength));
		QVERIFY(Engine::getSong()->saveProjectFile(path));
		QVERIFY(project.open(QIODevice::ReadOnly));
		QVERIFY(document.setContent(project.readAll()));
		project.close();
		savedSpacer = document.documentElement().firstChildElement(QStringLiteral("song"))
			.firstChildElement(QStringLiteral("track-spacers"))
			.firstChildElement(QStringLiteral("spacer"));
		QCOMPARE(savedSpacer.attribute(QStringLiteral("label")).size(), TrackSpacers::MaxLabelLength);
	}

	void aProjectWithoutSpacersWritesNone()
	{
		QVERIFY(run(QStringLiteral("project.new")).ok);
		QVERIFY(!TrackSpacers::instance().shouldPersist());
	}

	void theReferenceCopiesTracksAndClips()
	{
		const QString source = writeSourceProject();
		QVERIFY(run(QStringLiteral("project.new")).ok);
		const ControlResult opened = run(QStringLiteral("reference.open"), {{QStringLiteral("path"), source}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		QVERIFY(opened.result.value(QStringLiteral("open")).toBool());
		const int lead = indexOfReferenceTrack(QStringLiteral("Lead"));
		const int vox = indexOfReferenceTrack(QStringLiteral("Vox"));
		QVERIFY(lead >= 0 && vox >= 0);
		const std::size_t before = Engine::getSong()->tracks().size();
		const ControlResult copied = run(QStringLiteral("reference.copy_tracks"), {{QStringLiteral("tracks"), QJsonArray{lead}}});
		QVERIFY2(copied.ok, qPrintable(copied.errorMessage));
		QCOMPARE(Engine::getSong()->tracks().size(), before + 1);
		QCOMPARE(Engine::getSong()->tracks().back()->name(), QStringLiteral("Lead"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(Engine::getSong()->tracks().size(), before);

		Track* target = Track::create(Track::Type::Sample, Engine::getSong());
		const ControlResult clips = run(QStringLiteral("reference.copy_clips"), {{QStringLiteral("track"), vox},
			{QStringLiteral("to"), control::trackIdOf(target)}, {QStringLiteral("position"), 384}});
		QVERIFY2(clips.ok, qPrintable(clips.errorMessage));
		QCOMPARE(target->getClips().size(), std::size_t{1});
		QCOMPARE(target->getClips().front()->startPosition().getTicks(), 384);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(target->getClips().size(), std::size_t{0});
		Track* instrument = Track::create(Track::Type::Instrument, Engine::getSong());
		QCOMPARE(run(QStringLiteral("reference.copy_clips"), {{QStringLiteral("track"), vox},
			{QStringLiteral("to"), control::trackIdOf(instrument)}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("reference.copy_clips"), {{QStringLiteral("track"), 999},
			{QStringLiteral("to"), control::trackIdOf(target)}}).errorKind, ControlErrorKind::NotFound);
	}

	void theReferenceCopiesDevices()
	{
		EffectChain& master = Engine::mixer()->mixerChannel(0)->m_fxChain;
		Effect* probe = Effect::instantiate(QStringLiteral("amplifier"), &master, nullptr);
		if (probe == nullptr || !probe->isOkay())
		{
			delete probe;
			QSKIP("this test binary finds no built-in effect plug-ins");
		}
		delete probe;
		master.clear();
		master.appendEffect(Effect::instantiate(QStringLiteral("amplifier"), &master, nullptr));
		const QString source = QDir(m_dir.path()).filePath(QStringLiteral("devices.mmp"));
		Track::create(Track::Type::Sample, Engine::getSong());
		QVERIFY(Engine::getSong()->saveProjectFile(source));
		QVERIFY(run(QStringLiteral("project.new")).ok);
		QVERIFY(run(QStringLiteral("reference.open"), {{QStringLiteral("path"), source}}).ok);
		const std::size_t before = Engine::mixer()->mixerChannel(0)->m_fxChain.effects().size();
		// Channels are addressed by their persistent id, which a new project need not start at 0.
		const QString masterId = QStringLiteral("ch-%1").arg(Engine::mixer()->mixerChannel(0)->id());
		const ControlResult copied = run(QStringLiteral("reference.copy_devices"), {{QStringLiteral("channel"), 0},
			{QStringLiteral("to"), masterId}});
		QVERIFY2(copied.ok, qPrintable(copied.errorMessage));
		QCOMPARE(Engine::mixer()->mixerChannel(0)->m_fxChain.effects().size(), before + 1);
		const QJsonArray transactions = run(QStringLiteral("control.transactions")).result.value(QStringLiteral("transactions")).toArray();
		QVERIFY(!transactions.isEmpty());
		const QJsonObject record = transactions.last().toObject();
		QCOMPARE(record.value(QStringLiteral("command")).toString(), QStringLiteral("reference.copy_devices"));
		QCOMPARE(record.value(QStringLiteral("reversible")).toBool(), true);
		QCOMPARE(record.value(QStringLiteral("before")).toObject().value(QStringLiteral("target")).toString(), masterId);
		QCOMPARE(record.value(QStringLiteral("inverse")).toObject().value(QStringLiteral("op")).toString(), QStringLiteral("control.undo"));
		QVERIFY(record.value(QStringLiteral("mechanism")).toString().contains(QStringLiteral("recorded undo step restores the target chain")));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(Engine::mixer()->mixerChannel(0)->m_fxChain.effects().size(), before);
	}

	void theReferenceCloses()
	{
		QVERIFY(run(QStringLiteral("reference.close")).ok);
		QVERIFY(!run(QStringLiteral("reference.get_state")).result.value(QStringLiteral("open")).toBool());
		QCOMPARE(run(QStringLiteral("reference.copy_tracks")).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("reference.open"), {{QStringLiteral("path"), QStringLiteral("/no/such.mmp")}}).errorKind,
			ControlErrorKind::NotFound);
	}

	void aSwitchKeepsTheProjectLeftAsTheReference()
	{
		const QString a = QDir(m_dir.path()).filePath(QStringLiteral("a.mmp"));
		const QString b = QDir(m_dir.path()).filePath(QStringLiteral("b.mmp"));
		QVERIFY(run(QStringLiteral("project.new")).ok);
		QVERIFY(Engine::getSong()->saveProjectFile(a));
		QVERIFY(Engine::getSong()->saveProjectFile(b));
		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), a}}).ok);
		const ControlResult switched = run(QStringLiteral("project.switch"), {{QStringLiteral("path"), b},
			{QStringLiteral("keep_as_reference"), true}});
		QVERIFY2(switched.ok, qPrintable(switched.errorMessage));
		QCOMPARE(QFileInfo(Engine::getSong()->projectFileName()).fileName(), QStringLiteral("b.mmp"));
		QCOMPARE(QFileInfo(ReferenceProject::instance().path()).fileName(), QStringLiteral("a.mmp"));
		for (const QJsonValue& value : run(QStringLiteral("project.recent")).result.value(QStringLiteral("projects")).toArray())
		{
			const QString path = value.toObject().value(QStringLiteral("path")).toString();
			QVERIFY2(QFileInfo(path).fileName() != QLatin1String("b.mmp"), "the session's own file is not a project to switch to");
			QVERIFY(!path.endsWith(QStringLiteral(".mpt")));
		}
		ReferenceProject::instance().close();
	}

	void aSwitchReportsWhenThePreviousProjectCannotBeKeptAsReference()
	{
		const QString a = QDir(m_dir.path()).filePath(QStringLiteral("reference-fails.mmp"));
		const QString b = QDir(m_dir.path()).filePath(QStringLiteral("switch-target.mmp"));
		ReferenceProject::instance().close();
		QVERIFY(run(QStringLiteral("project.new")).ok);
		QVERIFY(Engine::getSong()->saveProjectFile(a));
		QVERIFY(Engine::getSong()->saveProjectFile(b));
		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), a}}).ok);

		QFile broken(a);
		QVERIFY(broken.open(QIODevice::WriteOnly | QIODevice::Truncate));
		QVERIFY(broken.write("<lmms-project/>") > 0);
		broken.close();

		const ControlResult switched = run(QStringLiteral("project.switch"), {{QStringLiteral("path"), b},
			{QStringLiteral("keep_as_reference"), true}, {QStringLiteral("save_current"), false}});
		QVERIFY2(switched.ok, qPrintable(switched.errorMessage));
		QCOMPARE(QFileInfo(Engine::getSong()->projectFileName()).fileName(), QStringLiteral("switch-target.mmp"));
		QCOMPARE(switched.result.value(QStringLiteral("reference")).toString(), QString());
		const QString referenceError = switched.result.value(QStringLiteral("reference_error")).toString();
		QVERIFY2(referenceError.contains(QStringLiteral("not a project with song tracks")), qPrintable(referenceError));
		QVERIFY(!ReferenceProject::instance().isOpen());
	}

	void aSwitchReportsWhenTheUntitledSessionCannotBeKeptAsReference()
	{
		const QString target = QDir(m_dir.path()).filePath(QStringLiteral("untitled-switch-target.mmp"));
		ReferenceProject::instance().close();
		QVERIFY(run(QStringLiteral("project.new")).ok);
		QVERIFY(Engine::getSong()->saveProjectFile(target));
		QVERIFY(run(QStringLiteral("project.new")).ok);
		QVERIFY(Engine::getSong()->projectFileName().isEmpty());

		const ControlResult switched = run(QStringLiteral("project.switch"), {{QStringLiteral("path"), target},
			{QStringLiteral("keep_as_reference"), true}});
		QVERIFY2(switched.ok, qPrintable(switched.errorMessage));
		QCOMPARE(QFileInfo(Engine::getSong()->projectFileName()).fileName(), QStringLiteral("untitled-switch-target.mmp"));
		QCOMPARE(switched.result.value(QStringLiteral("reference")).toString(), QString());
		const QString referenceError = switched.result.value(QStringLiteral("reference_error")).toString();
		QVERIFY2(referenceError.contains(QStringLiteral("no project file")), qPrintable(referenceError));
		QVERIFY(!ReferenceProject::instance().isOpen());
	}

	void aSwitchRefusesToLoseAnUntitledSession()
	{
		const QString a = QDir(m_dir.path()).filePath(QStringLiteral("a.mmp"));
		QVERIFY(run(QStringLiteral("project.new")).ok);
		QVERIFY(run(QStringLiteral("project.metadata"), {{QStringLiteral("title"), QStringLiteral("unsaved")}}).ok);
		QVERIFY(Engine::getSong()->isModified());
		QCOMPARE(run(QStringLiteral("project.switch"), {{QStringLiteral("path"), a}}).errorKind, ControlErrorKind::Refused);
		QVERIFY(run(QStringLiteral("project.switch"), {{QStringLiteral("path"), a}, {QStringLiteral("save_current"), false}}).ok);
		QCOMPARE(run(QStringLiteral("project.switch"), {{QStringLiteral("path"), QStringLiteral("/no/such.mmp")}}).errorKind,
			ControlErrorKind::NotFound);
	}
private:
	//! A project with an instrument track "Lead" and a sample track "Vox" holding one clip.
	QString writeSourceProject()
	{
		run(QStringLiteral("project.new"));
		Track* lead = Track::create(Track::Type::Instrument, Engine::getSong());
		lead->setName(QStringLiteral("Lead"));
		Track* vox = Track::create(Track::Type::Sample, Engine::getSong());
		vox->setName(QStringLiteral("Vox"));
		auto* clip = dynamic_cast<SampleClip*>(vox->createClip(TimePos(192)));
		clip->setSampleFile(m_tone);
		const QString path = QDir(m_dir.path()).filePath(QStringLiteral("source.mmp"));
		Engine::getSong()->saveProjectFile(path);
		return path;
	}

	QTemporaryDir m_dir;
	QString m_tone;
};

QTEST_MAIN(ArrangingSpacerReferenceTest)
#include "ArrangingSpacerReferenceTest.moc"
