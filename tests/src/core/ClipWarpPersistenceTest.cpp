/*
 * ClipWarpPersistenceTest.cpp - task #597: the warp map survives a save, and a
 *                               project that predates it still loads.
 *
 * Copyright (c) 2026 Zene Studio developers
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

// The three persistence claims of docs/CLIP-CAPTURE-DESIGN.md 2.4/#597, split
// out of SampleClipWarpTest.cpp so both files stay inside the 500-line gate:
//
//   I9            a clip with NO warp writes the attribute set task #611 left
//                 behind and no <warp> element at all;
//   round trip    the markers, the tempo mode and the declared source tempo come
//                 back from the project file, and a second save is stable;
//   old projects  an element written exactly as #611 wrote it - no <warp> child
//                 - loads with every #597 default and the linear mapping.
//
// The fixture is the same literal two-marker set the mapping tests use.

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QDomNode>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

#include <cmath>
#include <memory>
#include <vector>

#include "Engine.h"
#include "LmmsTypes.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"
#include "WarpMarkers.h"

using namespace lmms;

namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr int kSourceSeconds = 8;

const WarpMarker kM1{ 44100, 24 };
const WarpMarker kM2{ 176400, 96 };

template <typename T>
qulonglong N(T value) { return static_cast<qulonglong>(value); }

//! A deterministic 440 Hz tone, so a reloaded clip has a real source buffer.
std::vector<SampleFrame> makeTone(int rate)
{
	const auto frames = static_cast<f_cnt_t>(kSourceSeconds) * rate;
	const double period = rate / 440.0;
	std::vector<SampleFrame> data(frames);
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		const auto value = static_cast<sample_t>(0.5 * std::sin(2.0 * kPi * f / period));
		data[f] = SampleFrame(value, value);
	}
	return data;
}

SampleClip* makeClip(SampleTrack& track, int rate)
{
	auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
	Q_ASSERT(clip != nullptr);
	auto data = makeTone(rate);
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	return clip;
}

void applyWarp(SampleClip& clip)
{
	const std::array<WarpMarker, 2> markers{ kM1, kM2 };
	QVERIFY(clip.setWarpMarkers(std::span<const WarpMarker>(markers.data(), markers.size())));
}

TimePos atOffset(const SampleClip& clip, tick_t offset)
{
	return TimePos(clip.startPosition().getTicks() + clip.startTimeOffset().getTicks() + offset);
}

QString nodeToString(const QDomNode& node)
{
	QString out;
	QTextStream stream(&out);
	node.save(stream, 2);
	stream.flush();
	return out;
}

//! A serialised node without the per-object journalling id, which no two objects
//! can share (the ClipSerialisationTest pattern).
QString withoutJournallingIds(const QString& text)
{
	static const QRegularExpression idPattern("id=\"[0-9]+\"");
	QString out = text;
	out.replace(idPattern, "id=\"N\"");
	return out;
}

QStringList attributeNames(const QDomElement& element)
{
	QStringList names;
	const QDomNamedNodeMap attributes = element.attributes();
	for (int i = 0; i < attributes.length(); ++i) { names << attributes.item(i).nodeName(); }
	names.sort();
	return names;
}

} // namespace


class ClipWarpPersistenceTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); m_rate = Engine::audioEngine()->outputSampleRate(); }
	void cleanupTestCase() { Engine::destroy(); }

	//! I9 (additive serialisation): a clip with no warp writes no <warp>
	//! element and the exact attribute set task #611 left behind.
	void aClipWithNoWarpSerialisesExactlyAsBefore()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);

		QDomDocument doc;
		QDomElement parent = doc.createElement("track");
		const QDomElement element = clip->saveState(doc, parent);
		QCOMPARE(element.nodeName(), QString("sampleclip"));
		QVERIFY(element.firstChildElement("warp").isNull());
		QCOMPARE(attributeNames(element),
			QStringList({ "autoresize", "data", "len", "muted", "off", "pos", "sample_rate", "src" }));

		// load and re-save: identical authored state, byte for byte once the
		// per-object journalling id is neutralised
		SampleTrack reloadedTrack(Engine::getSong());
		auto* reloaded = makeClip(reloadedTrack, m_rate);
		reloaded->restoreState(element);
		QVERIFY(reloaded->warpMarkers().empty());

		QDomDocument doc2;
		QDomElement parent2 = doc2.createElement("track");
		const QDomElement element2 = reloaded->saveState(doc2, parent2);
		QCOMPARE(attributeNames(element2), attributeNames(element));
		QCOMPARE(withoutJournallingIds(nodeToString(element2)), withoutJournallingIds(nodeToString(element)));
	}

	//! ACCEPTANCE (round trip). The markers, the mode and the declared tempo
	//! come back from the project file, and a second save is stable.
	void warpRoundTripsThroughTheProjectFile()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);
		applyWarp(*clip);
		clip->setWarpTempoMode(WarpTempoMode::SourceTempo);
		clip->setSourceTempo(128.5f);
		clip->movePosition(TimePos(96));

		QDomDocument doc;
		QDomElement parent = doc.createElement("track");
		const QDomElement element = clip->saveState(doc, parent);
		const auto warpNode = element.firstChildElement("warp");
		QVERIFY(!warpNode.isNull());
		QCOMPARE(warpNode.attribute("mode"), QString("source"));
		QCOMPARE(warpNode.attribute("tempo"), QString("128.5"));
		QCOMPARE(warpNode.firstChildElement("marker").attribute("src"), QString("44100"));
		QCOMPARE(warpNode.firstChildElement("marker").attribute("pos"), QString("24"));

		SampleTrack reloadedTrack(Engine::getSong());
		auto* reloaded = makeClip(reloadedTrack, m_rate);
		reloaded->movePosition(TimePos(96));
		reloaded->restoreState(element);

		QCOMPARE(reloaded->warpMarkers().size(), 2);
		QVERIFY(reloaded->warpMarkers() == clip->warpMarkers());
		QCOMPARE(static_cast<int>(reloaded->warpTempoMode()), static_cast<int>(WarpTempoMode::SourceTempo));
		QCOMPARE(reloaded->sourceTempo(), 128.5f);
		QCOMPARE(N(reloaded->sourceFrameAt(atOffset(*reloaded, 96))), N(176400));
		QCOMPARE(reloaded->timelinePosAt(176400).getTicks(), 96 + 96);

		QDomDocument doc2;
		QDomElement parent2 = doc2.createElement("track");
		const QDomElement element2 = reloaded->saveState(doc2, parent2);
		QCOMPARE(withoutJournallingIds(nodeToString(element2)), withoutJournallingIds(nodeToString(element)));
	}

	//! ACCEPTANCE (old projects). An element written exactly as task #611
	//! wrote it - no <warp> child at all - loads with every #597 default.
	void anOldProjectWithoutAWarpElementLoads()
	{
		SampleTrack track(Engine::getSong());
		auto* source = makeClip(track, m_rate);
		source->setSampleStartFrame(44100);
		source->setSamplePlayLength(264600);

		QDomDocument doc;
		QDomElement parent = doc.createElement("track");
		const QDomElement old = source->saveState(doc, parent);
		QVERIFY(old.firstChildElement("warp").isNull());
		const auto xml = nodeToString(old);

		SampleTrack other(Engine::getSong());
		auto* clip = makeClip(other, m_rate);
		QDomDocument loaded;
		QVERIFY(loaded.setContent(xml));
		clip->restoreState(loaded.documentElement());

		// #597 defaults: no markers, project-following tempo, no source tempo
		QVERIFY(clip->warpMarkers().empty());
		QCOMPARE(static_cast<int>(clip->warpTempoMode()), static_cast<int>(WarpTempoMode::FollowProject));
		QCOMPARE(clip->sourceTempo(), 0.0f);
		QVERIFY(clip->rendersLinearly());

		// the window #611 persisted is still the window
		QCOMPARE(N(clip->sampleWindow().sourceIn), N(44100));
		QCOMPARE(N(clip->sampleWindow().sourceOut), N(264600));

		// and the mapping is the linear one: 44100 + framesPerTick * ticks
		const auto framesPerTick = Engine::framesPerTick(m_rate);
		QCOMPARE(N(clip->sourceFrameAt(TimePos(48))),
			N(44100 + static_cast<f_cnt_t>(48 * framesPerTick)));
		QCOMPARE(clip->timelinePosAt(44100).getTicks(), 0);
	}

private:
	int m_rate = 44100;
};

QTEST_GUILESS_MAIN(ClipWarpPersistenceTest)
#include "ClipWarpPersistenceTest.moc"
