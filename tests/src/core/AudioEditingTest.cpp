/*
 * AudioEditingTest.cpp - the L05 audio edits: fade shapes, handles, automatic fades, range fades, splits, slices, beat mapping
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

/*! 0.5.0 L05: the two new fade shapes (gain, name, the clip file), the fade handles' geometry, the
 *  automatic fade rendering on an unedited clip (and only while it is on), range.fade (from the range's
 *  edges, and batch lengths; one undo step), clip.transients and clip.split_at (beats, markers,
 *  transients; one undo step), clip.slice_to_instrument (skipped without SlicerT) and
 *  transport.tempo_map_from_clip. */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>

#include "ArrangementEdits.h"
#include "AudioBurstSupport.h"
#include "ClipEdits.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "ProjectMarkers.h"
#include "SampleClipOverlays.h"
#include "SamplePlayHandle.h"
#include "TempoMap.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

int clipsOn(const Track* track) { return static_cast<int>(track->getClips().size()); }

} // namespace

class AudioEditingTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		run(QStringLiteral("arrangement.modes"), {{QStringLiteral("auto_fade_ms"), 0}, {QStringLiteral("show_transients"), false}});
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	void theNewFadeShapes()
	{
		QCOMPARE(fadeShapeGain(FadeShape::Logarithmic, 0.5), 0.75f);
		QCOMPARE(fadeShapeGain(FadeShape::SCurve, 0.5), 0.5f);
		QCOMPARE(fadeShapeGain(FadeShape::SCurve, 0.25), 0.15625f);
		FadeShape parsed = FadeShape::Linear;
		QVERIFY(fadeShapeFromName(QStringLiteral("s_curve"), &parsed) && parsed == FadeShape::SCurve);
		QCOMPARE(fadeShapeName(FadeShape::Logarithmic), QStringLiteral("logarithmic"));
		SampleClip* clip = test::sampleClipOf(test::burstAudio(192, {}));
		QVERIFY(run(QStringLiteral("clip.set_fade"), {{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("fade_in"), 24},
			{QStringLiteral("fade_in_shape"), QStringLiteral("s_curve")}}).ok);
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		const QDomElement saved = clip->saveState(doc, root);
		auto* other = dynamic_cast<SampleClip*>(clip->getTrack()->createClip(TimePos(384)));
		other->restoreState(saved);
		QCOMPARE(other->clipEdits().fadeInShape, FadeShape::SCurve);
	}

	void theFadeHandlesGeometry()
	{
		ClipEdits edits;
		edits.fadeInTicks = 24;
		edits.fadeOutTicks = 48;
		const gui::ClipOverlayFrame frame{1.f, 0, 192, 2, 60};
		QCOMPARE(gui::fadeHandleAt(edits, 192, frame, QPoint(26, 4)), gui::FadeHandle::In);
		QCOMPARE(gui::fadeHandleAt(edits, 192, frame, QPoint(143, 4)), gui::FadeHandle::Out);
		QCOMPARE(gui::fadeHandleAt(edits, 192, frame, QPoint(80, 4)), gui::FadeHandle::None);
		QCOMPARE(gui::fadeHandleAt(edits, 192, frame, QPoint(24, 50)), gui::FadeHandle::None);  // below the handles
		QCOMPARE(gui::fadeTicksAt(gui::FadeHandle::In, 60, 192, frame), 60);
		QCOMPARE(gui::fadeTicksAt(gui::FadeHandle::Out, 150, 192, frame), 42);
		QCOMPARE(gui::fadeTicksAt(gui::FadeHandle::In, 500, 192, frame), 192);  // clamped to the clip
	}

	void anAutomaticFadeRampsAnUneditedClip()
	{
		const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		std::vector<SampleFrame> level(static_cast<std::size_t>(rate), SampleFrame(0.5f, 0.5f));
		SampleClip* clip = test::sampleClipOf(level);
		QVERIFY(clip->clipEdits().isNeutral());
		QVERIFY(run(QStringLiteral("arrangement.modes"), {{QStringLiteral("auto_fade_ms"), 10}}).ok);
		QCOMPARE(arrangement::autoFadeMs(), 10);
		std::vector<SampleFrame> buffer(1024, SampleFrame(0.f, 0.f));
		{
			SamplePlayHandle handle(clip);
			handle.play(std::span<SampleFrame>(buffer));
		}
		const auto fadeFrames = static_cast<std::size_t>(rate / 100);
		QVERIFY2(std::abs(buffer[0].left()) < 0.01f, "the clip starts silent");
		QVERIFY(buffer[fadeFrames / 2].left() > 0.1f && buffer[fadeFrames / 2].left() < 0.4f);
		QVERIFY(std::abs(buffer[fadeFrames + 10].left() - 0.5f) < 0.01f);
		QVERIFY(run(QStringLiteral("arrangement.modes"), {{QStringLiteral("auto_fade_ms"), 0}}).ok);
		std::fill(buffer.begin(), buffer.end(), SampleFrame(0.f, 0.f));
		{
			SamplePlayHandle handle(clip);
			handle.play(std::span<SampleFrame>(buffer));
		}
		QVERIFY2(std::abs(buffer[0].left() - 0.5f) < 0.01f, "with the automatic fade off an unedited clip is untouched");
	}

	void aRangeFadesTheClipsItTouches()
	{
		SampleClip* a = test::sampleClipOf(test::burstAudio(192, {}));
		a->changeLength(TimePos(192));
		a->setAutoResize(false);
		auto* b = dynamic_cast<SampleClip*>(a->getTrack()->createClip(TimePos(192)));
		b->changeLength(TimePos(192));
		const QString track = control::trackIdOf(a->getTrack());
		QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 144}, {QStringLiteral("end"), 240},
			{QStringLiteral("tracks"), QJsonArray{track}}}).ok);
		QVERIFY(run(QStringLiteral("range.fade"), {}).ok);
		QCOMPARE(a->clipEdits().fadeOutTicks, 48);  // its end is inside: out over 144..192
		QCOMPARE(b->clipEdits().fadeInTicks, 48);   // its start is inside: in over 192..240
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(a->getTrack()->getClips().size(), std::size_t{2});
		for (const Clip* clip : a->getTrack()->getClips()) { QVERIFY(!clip->clipEdits().hasFade()); }
		// A clip wholly inside the range has no edge at it: nothing is faded (review L05-L09-L08-1 #11).
		QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 960},
			{QStringLiteral("tracks"), QJsonArray{track}}}).ok);
		run(QStringLiteral("range.fade"), {});
		for (const Clip* clip : a->getTrack()->getClips()) { QVERIFY(!clip->clipEdits().hasFade()); }
		QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 144}, {QStringLiteral("end"), 240},
			{QStringLiteral("tracks"), QJsonArray{track}}}).ok);
		QVERIFY(run(QStringLiteral("range.fade"), {{QStringLiteral("fade_in"), 12}, {QStringLiteral("shape"), QStringLiteral("logarithmic")}}).ok);
		for (const Clip* clip : a->getTrack()->getClips())
		{
			QCOMPARE(clip->clipEdits().fadeInTicks, 12);
			QCOMPARE(clip->clipEdits().fadeInShape, FadeShape::Logarithmic);
		}
		run(QStringLiteral("range.clear"));
	}

	void splittingAtBeatsMarkersAndTransients()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		clip->changeLength(TimePos(192));
		clip->setAutoResize(false);
		const QString id = control::clipIdOf(clip);
		const ControlResult beats = run(QStringLiteral("clip.split_at"), {{QStringLiteral("clip"), id}, {QStringLiteral("at"), QStringLiteral("beats")}});
		QVERIFY2(beats.ok, qPrintable(beats.errorMessage));
		QCOMPARE(clipsOn(track), 4);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(clipsOn(track), 1);
		QVERIFY(run(QStringLiteral("marker.add"), {{QStringLiteral("tick"), 100}}).ok);
		QVERIFY(run(QStringLiteral("clip.split_at"), {{QStringLiteral("clip"), control::clipIdOf(track->getClips().front())},
			{QStringLiteral("at"), QStringLiteral("markers")}}).ok);
		QCOMPARE(clipsOn(track), 2);
		QCOMPARE(run(QStringLiteral("clip.split_at"), {{QStringLiteral("clip"), control::clipIdOf(track->getClips().front())},
			{QStringLiteral("at"), QStringLiteral("transients")}}).errorKind, ControlErrorKind::Refused);  // a MIDI clip has no audio

		SampleClip* audio = test::sampleClipOf(test::burstAudio(200, {10, 58, 106, 154}));
		const QJsonArray found = run(QStringLiteral("clip.transients"), {{QStringLiteral("clip"), control::clipIdOf(audio)}})
			.result.value(QStringLiteral("transients")).toArray();
		QCOMPARE(found.size(), 4);
		QVERIFY(std::abs(found.at(1).toObject().value(QStringLiteral("tick")).toInt() - 58) <= 6);
		QVERIFY(run(QStringLiteral("clip.split_at"), {{QStringLiteral("clip"), control::clipIdOf(audio)}, {QStringLiteral("at"), QStringLiteral("transients")}}).ok);
		QCOMPARE(clipsOn(audio->getTrack()), 4);  // the first hit is at the clip's start side: three cuts inside
	}

	void slicingToASamplerTrack()
	{
		SampleClip* audio = test::sampleClipOf(test::burstAudio(200, {10, 58, 106, 154}));
		const int before = static_cast<int>(Engine::getSong()->tracks().size());
		const ControlResult sliced = run(QStringLiteral("clip.slice_to_instrument"), {{QStringLiteral("clip"), control::clipIdOf(audio)}});
		if (sliced.errorKind == ControlErrorKind::Requires) { QSKIP("this build's plug-in folder has no SlicerT"); }
		QVERIFY2(sliced.ok, qPrintable(sliced.errorMessage));
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), before + 1);
		auto* slices = dynamic_cast<InstrumentTrack*>(Engine::getSong()->tracks().back());
		QVERIFY(slices != nullptr);
		auto* notes = dynamic_cast<MidiClip*>(slices->getClips().front());
		QCOMPARE(notes->notes().size(), std::size_t{4});
		QCOMPARE(notes->notes().front()->key(), slices->baseNote() + 2);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), before);
	}

	void aTempoMapFromAFreePerformance()
	{
		SampleClip* audio = test::sampleClipOf(test::burstAudio(200, {10, 58, 106, 154}));
		const ControlResult mapped = run(QStringLiteral("transport.tempo_map_from_clip"), {{QStringLiteral("clip"), control::clipIdOf(audio)}});
		QVERIFY2(mapped.ok, qPrintable(mapped.errorMessage));
		const TempoMap& map = Engine::getSong()->tempoMap().map();
		QVERIFY(map.active());
		QCOMPARE(map.size(), 3);  // one event per gap between four hits
		const int tempo = Engine::getSong()->getTempo();
		for (const TempoMapEvent& event : map.all()) { QVERIFY(std::abs(event.tempo - tempo) <= 3); }  // a beat apart at the song tempo
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(Engine::getSong()->tempoMap().map().size(), 0);
	}
};

QTEST_MAIN(AudioEditingTest)
#include "AudioEditingTest.moc"
