/*
 * AudioAutomationToolsTest.cpp - crop, unique copies, fade curves, asymmetric crossfades, automation switches
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
/*! 0.5.0 L05/L06 (W3): a cropped sample keeps only what the clip plays, one undo step (ED-155); a clip
 *  made unique stops sharing its buffer with a duplicate (ED-155); the fade curve starts at silence
 *  and reaches the clip's gain (ED-180); a crossfade's two sides take their own lengths and shapes
 *  (ED-176); the automation switches gate writes, punched to the region when asked, and are saved
 *  only when off their defaults (ED-200, ED-201). */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>

#include <vector>

#include "AudioEngine.h"
#include "AutomationSwitches.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

SampleClip* aClip(Track* track, tick_t at, int seconds = 2)
{
	auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(at)));
	const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	std::vector<SampleFrame> data(static_cast<std::size_t>(rate * seconds), SampleFrame(0.25f, 0.25f));
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	clip->movePosition(TimePos(at));
	return clip;
}

} // namespace

class AudioAutomationToolsTest : public QObject
{
	Q_OBJECT

private:
	SampleTrack* m_track = nullptr;

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aCropKeepsWhatTheClipPlays()
	{
		SampleClip* clip = aClip(m_track, 0);
		const QString id = control::clipIdOf(clip);
		QCOMPARE(run(QStringLiteral("sample.crop"), {{QStringLiteral("clip"), id}}).errorKind, ControlErrorKind::Refused);  // plays it all
		const auto size = static_cast<f_cnt_t>(clip->sample().sampleSize());
		clip->setSampleWindow(SampleWindow(size / 4, size / 2));
		QVERIFY(run(QStringLiteral("sample.crop"), {{QStringLiteral("clip"), id}}).ok);
		QCOMPARE(static_cast<f_cnt_t>(clip->sample().sampleSize()), size / 2 - size / 4);
		QVERIFY(clip->sampleWindow() == SampleWindow::full(size / 2 - size / 4));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(static_cast<f_cnt_t>(dynamic_cast<SampleClip*>(m_track->getClips().front())->sample().sampleSize()), size);
	}

	void aUniqueCopyStopsSharing()
	{
		SampleClip* clip = aClip(m_track, 1920);
		auto* twin = dynamic_cast<SampleClip*>(clip->clone());
		QCOMPARE(twin->sample().data(), clip->sample().data());  // a duplicate shares the buffer
		QVERIFY(run(QStringLiteral("sample.make_unique"), {{QStringLiteral("clip"), control::clipIdOf(clip)}}).ok);
		QVERIFY(twin->sample().data() != clip->sample().data());
		QCOMPARE(clip->sample().sampleSize(), twin->sample().sampleSize());
		delete twin;
	}

	void aNoOpSpeedEditDoesNotAddUndoCheckpoint()
	{
		SampleClip* clip = aClip(m_track, 2880);
		const QString id = control::clipIdOf(clip);
		const int depthBefore = Engine::projectJournal()->undoDepth();
		QVERIFY(run(QStringLiteral("clip.speed"), {{QStringLiteral("clip"), id}}).ok);
		QCOMPARE(Engine::projectJournal()->undoDepth(), depthBefore);
	}

	void theFadeCurve()
	{
		SampleClip* clip = aClip(m_track, 3840);
		QVERIFY(run(QStringLiteral("clip.set_fade"), {{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("fade_in"), 96}}).ok);
		const QJsonArray curve = run(QStringLiteral("clip.fade_curve"), {{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("points"), 9}})
			.result.value(QStringLiteral("curve")).toArray();
		QCOMPARE(curve.size(), 9);
		QCOMPARE(curve.first().toObject().value(QStringLiteral("gain")).toDouble(), 0.0);
		QCOMPARE(curve.last().toObject().value(QStringLiteral("gain")).toDouble(), 1.0);
	}

	void aCrossfadesSidesDiffer()
	{
		SampleClip* out = aClip(m_track, 7680);
		SampleClip* in = aClip(m_track, 7680 + static_cast<tick_t>(out->length().getTicks()) - 96);
		const ControlResult faded = run(QStringLiteral("clip.crossfade"), {{QStringLiteral("out"), control::clipIdOf(out)},
			{QStringLiteral("in"), control::clipIdOf(in)}, {QStringLiteral("out_length"), 96}, {QStringLiteral("in_length"), 48},
			{QStringLiteral("in_shape"), QStringLiteral("linear")}});
		QVERIFY2(faded.ok, qPrintable(faded.errorMessage));
		QCOMPARE(out->clipEdits().fadeOutTicks, 96);
		QCOMPARE(in->clipEdits().fadeInTicks, 48);
		QCOMPARE(in->clipEdits().fadeInShape, FadeShape::Linear);
		QCOMPARE(out->clipEdits().fadeOutShape, FadeShape::EqualPower);
	}

	void formantsAreKeptAndSaved()
	{
		SampleClip* clip = aClip(m_track, 11520);
		const ControlResult set = run(QStringLiteral("clip.set_formant"), {{QStringLiteral("clip"), control::clipIdOf(clip)},
			{QStringLiteral("keep"), true}, {QStringLiteral("semitones"), -3}});
		if (!set.ok)
		{
			QCOMPARE(set.errorKind, ControlErrorKind::Requires);  // a build without Rubber Band says so
			return;
		}
		QVERIFY(clip->clipEdits().keepFormants);
		QCOMPARE(clip->clipEdits().formant, -3);
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("track"));
		const QDomElement saved = clip->saveState(doc, root);
		QCOMPARE(saved.attribute(QStringLiteral("formant")), QStringLiteral("-3"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!dynamic_cast<SampleClip*>(m_track->getClips().back())->clipEdits().keepFormants);
	}

	void theAutomationSwitches()
	{
		AutomationSwitches& switches = AutomationSwitches::instance();
		QVERIFY(switches.writesAt(100) && switches.reads() && !switches.shouldPersist());
		QVERIFY(run(QStringLiteral("automation.switches"), {{QStringLiteral("write"), false}}).ok);
		QVERIFY(!switches.writesAt(100));
		QVERIFY(switches.shouldPersist());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(switches.writesAt(100));
		QVERIFY(run(QStringLiteral("automation.switches"), {{QStringLiteral("punched_writes"), true}}).ok);
		QVERIFY(!switches.writesAt(100));  // no punch region armed: nothing is inside it
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("song"));
		switches.save(doc, root);
		QCOMPARE(root.firstChildElement().attribute(QStringLiteral("punched")), QStringLiteral("1"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!switches.punchedWrites());
	}
};

QTEST_MAIN(AudioAutomationToolsTest)
#include "AudioAutomationToolsTest.moc"
