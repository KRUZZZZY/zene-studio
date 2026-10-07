/*
 * ArrangingCatchUpTest.cpp - frame-exact cuts and zero-crossing snap, track spacers, the reference project
 *                            and project switching, typed new documents (0.5.0 F3)
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
/*! 0.5.0 F3:
 *  - ED-021: clip.split cuts a sample clip at a frame between two ticks - the halves' windows meet at the
 *    frame, the right half's lead holds its audio where it was, one undo step; zero-crossing snap moves the
 *    cut onto a crossing; clip.trim moves a start to a frame and its undo resets the lead; the lead saves,
 *    loads, and is reset when absent (a clip without one writes no attribute); arrangement.modes keeps the
 *    snap toggle.
 *  - WF-107: track.set_spacer sets a spacer, one undo step, and a save and reload keeps it.
 *  - WF-042: reference.open parses another project; its tracks, clips and devices copy into this one, each
 *    one undo step; a clip of the wrong kind is refused; project.switch saves nothing it should not, keeps
 *    the project it left as the reference, and refuses to lose an untitled session's changes.
 *  - WF-048: project.new_document builds each type's template - unmodified, untitled, typed. */

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

SampleClip* sampleClip(const QString& id)
{
	ControlResult error;
	control::ClipRef ref;
	return control::resolveClip(id, &ref, &error) ? dynamic_cast<SampleClip*>(ref.clip) : nullptr;
}

float summed(SampleClip* clip, f_cnt_t frame)
{
	const SampleFrame& value = clip->sample().data()[frame];
	return value[0] + value[1];
}

QStringList trackNames()
{
	QStringList out;
	for (const Track* track : Engine::getSong()->tracks()) { out << track->name(); }
	return out;
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

class CountedInstrumentHandle : public InstrumentPlayHandle
{
public:
	CountedInstrumentHandle(InstrumentTrack* track, int& destroyed) :
		InstrumentPlayHandle(track->instrument(), track), m_destroyed(destroyed) {}
	~CountedInstrumentHandle() override { ++m_destroyed; }

private:
	int& m_destroyed;
};

} // namespace

class ArrangingCatchUpTest : public QObject
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

	void aSplitCutsAtAFrameBetweenTwoTicks()
	{
		SampleClip* clip = freshClip();
		const double framesPerTick = clip->clipFramesPerTick();
		const double outputPerTick = Engine::framesPerTick(Engine::audioEngine()->outputSampleRate());
		const ControlResult split = run(QStringLiteral("clip.split"), {{QStringLiteral("clip"), control::clipIdOf(clip)},
			{QStringLiteral("position"), 20}, {QStringLiteral("frames"), 100}, {QStringLiteral("zero_crossing"), false}});
		QVERIFY2(split.ok, qPrintable(split.errorMessage));
		const auto frame = static_cast<f_cnt_t>(split.result.value(QStringLiteral("frame")).toInteger());
		const double expected = (20.0 + 100.0 / outputPerTick) * framesPerTick;
		QVERIFY(std::abs(static_cast<double>(frame) - expected) <= 1.0);
		SampleClip* right = sampleClip(split.result.value(QStringLiteral("right")).toString());
		QVERIFY(right != nullptr);
		QCOMPARE(clip->sampleWindow().sourceOut, frame);
		QCOMPARE(right->sampleWindow().sourceIn, frame);
		QVERIFY(static_cast<double>(right->leadFrames()) < framesPerTick);
		QCOMPARE(right->startPosition().getTicks(), 20);
		QCOMPARE(clip->endPosition().getTicks(), 21);  // the halves overlap by under a tick; their audio meets at the frame
		// The cut frame still sounds when it did: the right half's start tick plus its lead.
		const double heard = right->startPosition().getTicks() * framesPerTick + static_cast<double>(right->leadFrames());
		QVERIFY(std::abs(heard - static_cast<double>(frame)) <= 1.0);
		QCOMPARE(right->sourceFrameAt(right->startPosition()), frame);
		QVERIFY(right->leadOutputFramesAt(right->startPosition()) > 0);
		QCOMPARE(right->leadOutputFramesAt(TimePos(21)), f_cnt_t{0});  // past the lead
		Track* track = clip->getTrack();
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // one step takes both halves back
		QCOMPARE(track->getClips().size(), std::size_t{1});
		QCOMPARE(dynamic_cast<SampleClip*>(track->getClips().front())->leadFrames(), f_cnt_t{0});
	}

	void zeroCrossingSnapMovesTheCutOntoACrossing()
	{
		SampleClip* clip = freshClip();
		const ControlResult split = run(QStringLiteral("clip.split"), {{QStringLiteral("clip"), control::clipIdOf(clip)},
			{QStringLiteral("position"), 30}, {QStringLiteral("frames"), 37}, {QStringLiteral("zero_crossing"), true}});
		QVERIFY2(split.ok, qPrintable(split.errorMessage));
		QVERIFY(split.result.value(QStringLiteral("zero_crossing")).toBool());
		const auto frame = static_cast<f_cnt_t>(split.result.value(QStringLiteral("frame")).toInteger());
		SampleClip* right = sampleClip(split.result.value(QStringLiteral("right")).toString());
		QVERIFY(right != nullptr);
		const bool crossesBefore = (summed(right, frame - 1) < 0.0f) != (summed(right, frame) < 0.0f);
		const bool crossesAfter = (summed(right, frame) < 0.0f) != (summed(right, frame + 1) < 0.0f);
		QVERIFY2(crossesBefore || crossesAfter || summed(right, frame) == 0.0f, "the cut is not at a zero crossing");
		// A 440 Hz tone crosses zero every ~50 frames: the snap never went far.
		const double asked = (30.0 + 37.0 / Engine::framesPerTick(Engine::audioEngine()->outputSampleRate())) * clip->clipFramesPerTick();
		QVERIFY(std::abs(static_cast<double>(frame) - asked) < 60.0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
	}

	void aFrameCutThatCannotBeIsRefused()
	{
		SampleClip* clip = freshClip();
		QString why;
		QVERIFY(!arrangement::canSplitAtFrame(*clip, 0, &why));  // at the window's first frame: nothing on the left
		QVERIFY(!why.isEmpty());
		// A clip at its own source tempo does not map frames to ticks in a straight line: refused, nothing changed.
		clip->setSourceTempo(120.0f);
		clip->setWarpTempoMode(WarpTempoMode::SourceTempo);
		QCOMPARE(run(QStringLiteral("clip.split"), {{QStringLiteral("clip"), control::clipIdOf(clip)},
			{QStringLiteral("position"), 20}, {QStringLiteral("frames"), 10}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(m_track->getClips().size(), std::size_t{1});
	}

	void aTrimMovesTheStartToAFrameAndUndoesWholly()
	{
		SampleClip* clip = freshClip();
		const ControlResult trimmed = run(QStringLiteral("clip.trim"), {{QStringLiteral("clip"), control::clipIdOf(clip)},
			{QStringLiteral("start"), 10}, {QStringLiteral("start_frames"), 50}, {QStringLiteral("zero_crossing"), false}});
		QVERIFY2(trimmed.ok, qPrintable(trimmed.errorMessage));
		QVERIFY(trimmed.result.value(QStringLiteral("source_in")).toInteger() > 0);
		QCOMPARE(trimmed.result.value(QStringLiteral("position")).toInt(), 10);
		QVERIFY(clip->leadFrames() > 0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(clip->sampleWindow().sourceIn, f_cnt_t{0});
		QCOMPARE(clip->leadFrames(), f_cnt_t{0});  // reset on absence
		QCOMPARE(clip->startPosition().getTicks(), 0);
	}

	void theLeadSavesLoadsAndIsResetWhenAbsent()
	{
		SampleClip* clip = freshClip();
		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("track"));
		QVERIFY(!clip->saveState(doc, parent).hasAttribute(QStringLiteral("lead")));  // an unedited clip writes nothing new
		clip->setLeadFrames(123);
		const QDomElement saved = clip->saveState(doc, parent);
		QCOMPARE(saved.attribute(QStringLiteral("lead")), QStringLiteral("123"));
		auto* other = dynamic_cast<SampleClip*>(m_track->createClip(TimePos(0)));
		other->restoreState(saved);
		QCOMPARE(other->leadFrames(), f_cnt_t{123});
		QDomElement without = saved.cloneNode(true).toElement();
		without.removeAttribute(QStringLiteral("lead"));
		other->restoreState(without);
		QCOMPARE(other->leadFrames(), f_cnt_t{0});
	}

	void theZeroCrossingToggleIsAMode()
	{
		const bool before = arrangement::modes().snapToZeroCrossings;
		const ControlResult on = run(QStringLiteral("arrangement.modes"), {{QStringLiteral("snap_to_zero_crossings"), true}});
		QVERIFY(on.result.value(QStringLiteral("snap_to_zero_crossings")).toBool());
		QVERIFY(arrangement::modes().snapToZeroCrossings);
		QVERIFY(run(QStringLiteral("arrangement.modes"), {{QStringLiteral("snap_to_zero_crossings"), before}}).ok);
		QCOMPARE(arrangement::modes().snapToZeroCrossings, before);
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

	void newDocumentsCarryTheirTypesTemplate()
	{
		const ControlResult mastering = run(QStringLiteral("project.new_document"), {{QStringLiteral("type"), QStringLiteral("mastering")}});
		QVERIFY2(mastering.ok, qPrintable(mastering.errorMessage));
		QCOMPARE(ProjectInfo::instance().documentType(), QStringLiteral("mastering"));
		QCOMPARE(trackNames(), (QStringList{QStringLiteral("Mix"), QStringLiteral("Reference")}));
		QVERIFY(Engine::getSong()->tracks().back()->isMuted());  // the reference must not run through the chain
		QVERIFY(Engine::getSong()->projectFileName().isEmpty());
		QVERIFY(!Engine::getSong()->isModified());
		QVERIFY(run(QStringLiteral("project.new_document"), {{QStringLiteral("type"), QStringLiteral("live")}}).ok);
		QCOMPARE(ProjectInfo::instance().documentType(), QStringLiteral("live"));
		QCOMPARE(trackNames().size(), 5);
		QVERIFY(run(QStringLiteral("project.new_document"), {{QStringLiteral("type"), QStringLiteral("production")}}).ok);
		QCOMPARE(ProjectInfo::instance().documentType(), QStringLiteral("production"));
		QCOMPARE(run(QStringLiteral("project.new_document"), {{QStringLiteral("type"), QStringLiteral("radio")}}).errorKind,
			ControlErrorKind::InvalidArgs);
	}

	void queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie()
	{
		auto* engine = Engine::audioEngine();
		const auto guard = engine->requestChangesGuard();
		auto track = std::make_unique<InstrumentTrack>(Engine::getSong());
		auto other = std::make_unique<InstrumentTrack>(Engine::getSong());
		QCOMPARE(track->loadInstrument(QStringLiteral("tripleoscillator"))->nodeName(), QStringLiteral("tripleoscillator"));
		QCOMPARE(other->loadInstrument(QStringLiteral("tripleoscillator"))->nodeName(), QStringLiteral("tripleoscillator"));
		int destroyed = 0;
		int otherDestroyed = 0;
		// Both handles are guaranteed still to be in the staging list: the
		// model guard prevents the audio device from beginning another period.
		QVERIFY(engine->addPlayHandle(new CountedInstrumentHandle(track.get(), destroyed)));
		QVERIFY(engine->addPlayHandle(new CountedInstrumentHandle(other.get(), otherDestroyed)));
		engine->removePlayHandlesOfTypes(track.get(), PlayHandle::Type::NotePlayHandle);
		const int afterNoteRemoval = destroyed;
		engine->removePlayHandlesOfTypes(track.get(), PlayHandle::Type::InstrumentPlayHandle);
		const int beforeRender = destroyed;
		const int otherBeforeRender = otherDestroyed;
		// Clean up the baseline's survivor while its instrument remains alive,
		// so a failed assertion records the omission without leaving a crash.
		engine->renderNextPeriod();
		track->silenceAllNotes(true);
		other->silenceAllNotes(true);
		QCOMPARE(afterNoteRemoval, 0);
		QCOMPARE(beforeRender, 1);
		QCOMPARE(otherBeforeRender, 0);
		QCOMPARE(destroyed, 1);
		QCOMPARE(otherDestroyed, 1);
	}

	void switchingMonitoringOffRemovesAHandleQueuedInTheSamePeriod()
	{
		auto* engine = Engine::audioEngine();
		const auto guard = engine->requestChangesGuard();
		auto track = std::make_unique<SampleTrack>(Engine::getSong());
		track->setMonitorMode(MonitorMode::In);
		// monitorHandle() is the const accessor (SampleTrack.h); the render list holds PlayHandle*.
		PlayHandle* pending = const_cast<InputMonitorHandle*>(track->monitorHandle());
		QVERIFY(pending != nullptr);
		track->setMonitorMode(MonitorMode::Off);
		QVERIFY(track->monitorHandle() == nullptr);
		engine->renderNextPeriod();
		const bool survived = engine->playHandles().contains(pending);
		// A baseline survivor is silent with monitoring Off; remove it from the
		// active list before checking, while its track still exists.
		engine->removePlayHandlesOfTypes(track.get(), PlayHandle::Type::InputMonitorHandle);
		QVERIFY2(!survived, "monitoring Off left a staged monitor in the render list");
	}

private:
	//! A new sample track holding one clip of the tone at the song's start (m_track is that track).
	SampleClip* freshClip()
	{
		m_track = Track::create(Track::Type::Sample, Engine::getSong());
		auto* clip = dynamic_cast<SampleClip*>(m_track->createClip(TimePos(0)));
		clip->setSampleFile(m_tone);
		return clip;
	}

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
	Track* m_track = nullptr;
};

QTEST_MAIN(ArrangingCatchUpTest)
#include "ArrangingCatchUpTest.moc"
