/*
 * TrackOffsetTest.cpp - Q050-339: the manual delay of audio tracks, instrument tracks and mixer channels
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

/*! Q050-339: pdc.set_offset sets and reads the manual delay of an audio (sample) track, an instrument track and
 *  a mixer channel; an impulse rendered through each target lands exactly the requested number of samples
 *  later (or earlier, the other path then delayed) than an untouched second path - non-period-aligned
 *  offsets, and on top of a latent device's automatic compensation; a saved and reopened project keeps the
 *  offsets (an older one without them has none); one undo puts the previous offset back; a refused call
 *  changes nothing. Synthetic impulses: no plug-in module is loaded. */

#include <QtTest>

#include <QTemporaryDir>

#include <cmath>
#include <vector>

#include "PhaseDMixerTestSupport.h"

#include "AudioBusHandle.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "InstrumentTrack.h"
#include "PlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;
using namespace partd;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString ch(int index) { return control::channelIdOf(Engine::mixer()->mixerChannel(index)); }

QString addTrack(const char* type)
{
	return run(QStringLiteral("track.add"), {{QStringLiteral("type"), QLatin1String(type)}}).result.value(QStringLiteral("track")).toString();
}

Track* trackById(const QString& id)
{
	for (Track* track : Engine::getSong()->tracks()) { if (control::trackIdOf(track) == id) { return track; } }
	return nullptr;
}

AudioBusHandle* busOf(Track* track)
{
	if (auto* instrument = dynamic_cast<InstrumentTrack*>(track)) { return instrument->audioBusHandle(); }
	if (auto* sample = dynamic_cast<SampleTrack*>(track)) { return sample->audioBusHandle(); }
	return nullptr;
}

ControlResult setOffset(const QString& target, int samples)
{
	return run(QStringLiteral("pdc.set_offset"), {{QStringLiteral("target"), target}, {QStringLiteral("offset_samples"), samples}});
}

int offsetOf(const QString& target)
{
	return run(QStringLiteral("pdc.set_offset"), {{QStringLiteral("target"), target}}).result.value(QStringLiteral("offset_samples")).toInt(-99999);
}

//! One impulse of @a level on both sides at the first frame it plays, silence after.
class ImpulsePlayHandle : public PlayHandle
{
public:
	explicit ImpulsePlayHandle(float level) : PlayHandle{PlayHandle::Type::InstrumentPlayHandle, 0}, m_level(level) {}

	void play(std::span<SampleFrame> buffer) override
	{
		for (SampleFrame& frame : buffer) { frame = SampleFrame{}; }
		if (!m_played && !buffer.empty()) { buffer[0] = SampleFrame{m_level, m_level}; }
		m_played = true;
	}

	bool isFinished() const override { return false; }
	bool isFromTrack(const Track*) const override { return false; }

private:
	float m_level;
	bool m_played = false;
};

//! The frames of @a out whose left side is not silent, with their values.
std::vector<std::pair<int, float>> hits(const std::vector<SampleFrame>& out)
{
	std::vector<std::pair<int, float>> found;
	for (std::size_t f = 0; f < out.size(); ++f)
	{
		if (std::abs(out[f][0]) > 1e-6f) { found.emplace_back(static_cast<int>(f), out[f][0]); }
	}
	return found;
}

//! An impulse of 1 through @a bus and one of 0.5 through an untouched handle, both into channel 1, rendered as the
//! engine orders a period (alignment pass, then the handles); channel 1's input over 8 periods.
std::vector<SampleFrame> renderThroughHandles(AudioBusHandle* bus)
{
	Mixer* mixer = Engine::mixer();
	AudioBusHandle reference{QStringLiteral("q050-339-reference"), false};
	reference.setNextMixerChannel(1);
	bus->setNextMixerChannel(1);
	ImpulsePlayHandle offsetImpulse(1.f);
	ImpulsePlayHandle referenceImpulse(0.5f);
	bus->addPlayHandle(&offsetImpulse);
	reference.addPlayHandle(&referenceImpulse);
	std::vector<SampleFrame> out;
	for (int p = 0; p < 8; ++p)
	{
		offsetImpulse.doProcessing();
		referenceImpulse.doProcessing();
		mixer->prepareMasterMix();
		SampleFrame* channel = mixer->mixerChannel(1)->m_buffer;
		for (f_cnt_t f = 0; f < periodFrames(); ++f) { channel[f] = SampleFrame{}; }
		reference.doProcessing();
		bus->doProcessing();
		out.insert(out.end(), channel, channel + periodFrames());
		AutomatableModel::incrementPeriodCounter();
	}
	bus->removePlayHandle(&offsetImpulse);
	reference.removePlayHandle(&referenceImpulse);
	return out;
}

//! An impulse of 1 into channel 1 and one of 0.5 into channel 2, both routed to the master; the master over 8 periods.
std::vector<SampleFrame> renderThroughChannels()
{
	PeriodHarness harness(Engine::mixer());
	std::vector<SampleFrame> out;
	for (int p = 0; p < 8; ++p)
	{
		harness.zeroInput();
		if (p == 0) { harness.in()[0] = SampleFrame{1.f, 1.f}; }
		harness.feed(1);
		if (p == 0) { harness.in()[0] = SampleFrame{0.5f, 0.5f}; }
		harness.feed(2);
		const std::vector<SampleFrame>& period = harness.render();
		out.insert(out.end(), period.begin(), period.end());
	}
	return out;
}

} // namespace

class TrackOffsetTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		initEngine();
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		destroyEngine();
	}

	void init()
	{
		QVERIFY(run(QStringLiteral("project.new")).ok);
		while (Engine::mixer()->numChannels() < 3) { Engine::mixer()->createChannel(); }
		QVERIFY(run(QStringLiteral("control.set_undo_coalescing"), {{QStringLiteral("window_ms"), 0}}).ok);
	}

	//! Each supported target takes its own offset and reports it; the others keep theirs.
	void theAgentSetsAndReadsEachTarget()
	{
		const QString audio = addTrack("sample");
		const QString instrument = addTrack("instrument");
		QVERIFY(!audio.isEmpty() && !instrument.isEmpty());
		const ControlResult a = setOffset(audio, 37);
		QVERIFY2(a.ok, qPrintable(a.errorMessage));
		QCOMPARE(a.result.value(QStringLiteral("kind")).toString(), QStringLiteral("track"));
		QVERIFY(std::abs(a.result.value(QStringLiteral("offset_ms")).toDouble() - 37000.0 / Engine::audioEngine()->outputSampleRate()) < 1e-9);
		QVERIFY(setOffset(instrument, -12).ok);
		const ControlResult c = setOffset(ch(1), 53);
		QVERIFY2(c.ok, qPrintable(c.errorMessage));
		QCOMPARE(c.result.value(QStringLiteral("kind")).toString(), QStringLiteral("channel"));
		QCOMPARE(offsetOf(audio), 37);
		QCOMPARE(offsetOf(instrument), -12);
		QCOMPARE(offsetOf(ch(1)), 53);
		QCOMPARE(offsetOf(ch(2)), 0);
		QCOMPARE(busOf(trackById(audio))->offsetFrames(), 37);
		QCOMPARE(busOf(trackById(instrument))->offsetFrames(), -12);
		QCOMPARE(Engine::mixer()->mixerChannel(1)->offsetFrames(), 53);
		// The instrument track's note delay is a separate setting, untouched.
		QCOMPARE(run(QStringLiteral("track.set_delay"), {{QStringLiteral("track"), instrument}}).result.value(QStringLiteral("delay_ms")).toInt(), 0);
	}

	//! A bad value or target is refused, and the refusal changes nothing and records no undo step.
	void aRefusedOffsetChangesNothing()
	{
		const QString audio = addTrack("sample");
		const QString automation = addTrack("automation");
		QVERIFY(setOffset(audio, 10).ok);
		const int depth = run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
		QVERIFY(!setOffset(audio, 9000).ok);
		QVERIFY(!setOffset(audio, -9000).ok);
		QVERIFY(!run(QStringLiteral("pdc.set_offset"), {{QStringLiteral("target"), audio}, {QStringLiteral("offset_samples"), 2.5}}).ok);
		QCOMPARE(setOffset(ch(0), 10).errorKind, ControlErrorKind::Refused);  // the master
		QCOMPARE(setOffset(automation, 10).errorKind, ControlErrorKind::Refused);  // plays no audio
		QVERIFY(!setOffset(QStringLiteral("trk-999999"), 10).ok);
		QVERIFY(!setOffset(QStringLiteral("ch-999999"), 10).ok);
		QCOMPARE(offsetOf(audio), 10);
		QCOMPARE(run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(), depth);
	}

	//! An impulse through an audio or instrument track lands 'offset' samples after the untouched path - or, with a
	//! negative offset, that many before it (the untouched path is the one delayed).
	void aTrackImpulseMovesByItsOffset_data()
	{
		QTest::addColumn<QString>("type");
		QTest::addColumn<int>("offset");
		for (const char* type : {"sample", "instrument"})
		{
			for (const int offset : {37, 301, -45})
			{
				QTest::addRow("%s/%d", type, offset) << QString::fromLatin1(type) << offset;
			}
		}
	}

	void aTrackImpulseMovesByItsOffset()
	{
		QFETCH(QString, type);
		QFETCH(int, offset);
		const QString id = addTrack(type.toLatin1().constData());
		AudioBusHandle* bus = busOf(trackById(id));
		QVERIFY(bus != nullptr);
		const auto before = hits(renderThroughHandles(bus));
		QCOMPARE(static_cast<int>(before.size()), 1);  // no offset: both impulses land in frame 0
		QCOMPARE(before[0].first, 0);
		QVERIFY(setOffset(id, offset).ok);
		const auto after = hits(renderThroughHandles(bus));
		QCOMPARE(static_cast<int>(after.size()), 2);
		const int trackAt = offset > 0 ? offset : 0;
		const int referenceAt = offset > 0 ? 0 : -offset;
		const auto at = [&after](int frame) { for (const auto& hit : after) { if (hit.first == frame) { return hit.second; } } return 0.f; };
		QVERIFY2(at(referenceAt) == 0.5f, qPrintable(QStringLiteral("the untouched path is not at %1").arg(referenceAt)));
		QVERIFY2(at(trackAt) > 0.6f, qPrintable(QStringLiteral("the track's impulse is not at %1").arg(trackAt)));
	}

	//! An impulse through a mixer channel lands 'offset' samples after the other channel's at the master - also on
	//! top of the automatic compensation of a latent device on the other channel.
	void aChannelImpulseMovesByItsOffset()
	{
		QVERIFY(setOffset(ch(1), 53).ok);
		auto found = hits(renderThroughChannels());
		QCOMPARE(static_cast<int>(found.size()), 2);
		QVERIFY(found[0] == std::make_pair(0, 0.5f));  // channel 2, untouched
		QVERIFY(found[1] == std::make_pair(53, 1.f));  // channel 1, 53 samples later
		QVERIFY(setOffset(ch(1), -29).ok);
		found = hits(renderThroughChannels());
		QVERIFY(found[0] == std::make_pair(0, 1.f));  // channel 1 now first
		QVERIFY(found[1] == std::make_pair(29, 0.5f));  // channel 2 delayed to keep them 29 apart
		// A latent device on channel 2 (100 samples, reported): PDC aligns it, and the offset is added to that.
		EffectChain& chain = Engine::mixer()->mixerChannel(2)->m_fxChain;
		chain.appendEffect(new LatentDelayEffect(&chain, 100));
		QVERIFY(setOffset(ch(1), 53).ok);
		found = hits(renderThroughChannels());
		QCOMPARE(static_cast<int>(found.size()), 2);
		QVERIFY(found[0] == std::make_pair(100, 0.5f));
		QVERIFY(found[1] == std::make_pair(153, 1.f));
		chain.clear();
	}

	//! Saved and reopened, every offset is back; a target saved without one has none.
	void aSavedProjectKeepsTheOffsets()
	{
		const QString audio = addTrack("sample");
		const QString instrument = addTrack("instrument");
		const QString plain = addTrack("sample");
		QVERIFY(setOffset(audio, 211).ok);
		QVERIFY(setOffset(instrument, -64).ok);
		QVERIFY(setOffset(ch(2), 99).ok);
		QTemporaryDir dir;
		const QString path = dir.filePath(QStringLiteral("offsets.mmp"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), path}}).ok);
		QVERIFY(run(QStringLiteral("project.new")).ok);
		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), path}}).ok);
		QCOMPARE(offsetOf(audio), 211);
		QCOMPARE(offsetOf(instrument), -64);
		QCOMPARE(offsetOf(plain), 0);
		QCOMPARE(offsetOf(ch(2)), 99);
		QCOMPARE(offsetOf(ch(1)), 0);
		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		const QByteArray saved = file.readAll();
		QCOMPARE(saved.count("offsetframes=\""), 2);  // written only off the default
	}

	//! One undo puts the previous offset back on each kind of target; the redo puts the new one back.
	void oneUndoRestoresThePreviousOffset()
	{
		const QString audio = addTrack("sample");
		QVERIFY(setOffset(audio, 40).ok);
		QVERIFY(setOffset(audio, 80).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(offsetOf(audio), 40);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(offsetOf(audio), 80);
		QVERIFY(setOffset(ch(1), 7).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(offsetOf(ch(1)), 0);
	}
};

QTEST_MAIN(TrackOffsetTest)
#include "TrackOffsetTest.moc"
