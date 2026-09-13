/*
 * TakeLaneTestSupport.h - the SHARED helpers of the comping acceptance tests
 *                         (take lanes + the non-destructive composite, task
 *                         #600): one definition for the two test files that
 *                         use them - the take-lane half and the composite half.
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
 */

#ifndef LMMS_TAKE_LANE_TEST_SUPPORT_H
#define LMMS_TAKE_LANE_TEST_SUPPORT_H

#include <QCryptographicHash>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QString>
#include <QVector>

#include <cmath>

#include "Engine.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "TakeLane.h"
#include "TimePos.h"
#include "Track.h"

//! Shared helpers of the comping acceptance tests, ONE definition for the two
//! test files that use them (the take-lane half and the composite half): a
//! second copy of a helper is exactly the drift the split exists to prevent.
//! Free inline functions in a namespace, not statics in each test class, so a
//! take lane's fixture is built identically by both halves.
namespace taketest
{
using namespace lmms;

constexpr double kPi = 3.14159265358979323846;
constexpr int kRate = 44100;
constexpr int kSeconds = 2;

/*! A real 16-bit mono WAV on disk. Two of these with different frequencies are
 *  the two "takes": SampleClip::loadSettings only accepts a `src` whose file
 *  exists, so a round trip through the serialiser needs real files. */
inline QString writeToneWav(const QString& path, double frequency)
{
	const auto frames = static_cast<int>(kRate) * kSeconds;
	QByteArray pcm;
	pcm.reserve(frames * 2);
	for (int i = 0; i < frames; ++i)
	{
		const auto value = static_cast<qint16>(0.5 * 32767.0 * std::sin(2.0 * kPi * frequency * i / kRate));
		pcm.append(static_cast<char>(value & 0xff));
		pcm.append(static_cast<char>((value >> 8) & 0xff));
	}

	QByteArray out;
	const auto put32 = [&out](quint32 v) {
		out.append(static_cast<char>(v & 0xff));
		out.append(static_cast<char>((v >> 8) & 0xff));
		out.append(static_cast<char>((v >> 16) & 0xff));
		out.append(static_cast<char>((v >> 24) & 0xff));
	};
	const auto put16 = [&out](quint16 v) {
		out.append(static_cast<char>(v & 0xff));
		out.append(static_cast<char>((v >> 8) & 0xff));
	};
	out.append("RIFF");
	put32(36 + pcm.size());
	out.append("WAVEfmt ");
	put32(16);
	put16(1);
	put16(1);
	put32(kRate);
	put32(kRate * 2);
	put16(2);
	put16(16);
	out.append("data");
	put32(pcm.size());
	out.append(pcm);

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return {}; }
	file.write(out);
	file.close();
	return path;
}

//! sha256 of a file's bytes, or a marker string when it cannot be read.
inline QString fileDigest(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QStringLiteral("<unreadable>"); }
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(file.readAll());
	return QString::fromLatin1(hash.result().toHex());
}

//! sha256 of the take audio a clip HOLDS IN MEMORY, over the frames it would
//! render: the buffer is the source the composite reads, so it is what must not
//! move. SampleBuffer has no operator[], and Sample::data() is the accessor.
//! (SampleClip::sample() is non-const, hence the non-const parameter.)
inline QString takeDigest(SampleClip* clip)
{
	if (clip == nullptr) { return QStringLiteral("<no clip>"); }
	const Sample& sample = clip->sample();
	QCryptographicHash hash(QCryptographicHash::Sha256);
	// The QByteArray overload, by name: the (const char*, int) one is deprecated
	// in Qt6 and this tree builds both Qt5 (linux/macos/mingw) and Qt6 (msvc).
	hash.addData(QByteArray::fromRawData(reinterpret_cast<const char*>(sample.data()),
		static_cast<int>(sample.sampleSize() * sizeof(SampleFrame))));
	return QString::fromLatin1(hash.result().toHex());
}

//! A sample track in the song holding `count` take clips, each its own tone.
inline SampleTrack* makeTakeTrack(const QString& dir, int count, QVector<SampleClip*>* clips)
{
	auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
	if (track == nullptr) { return nullptr; }
	for (int i = 0; i < count; ++i)
	{
		const QString wav = writeToneWav(dir + QStringLiteral("/take%1.wav").arg(i), 440.0 + 220.0 * i);
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		if (clip == nullptr) { return nullptr; }
		clip->setSampleFile(wav);
		clips->append(clip);
	}
	return track;
}

//! The track, saving it the way Track::clone does and loading it into a FRESH
//! track: the same path TrackContainer::loadSettings uses for a project file.
inline Track* reloadTrack(Track* source)
{
	QDomDocument doc;
	QDomElement parent = doc.createElement(QStringLiteral("track"));
	const QDomElement element = source->saveState(doc, parent);
	return Track::create(element, Engine::getSong());
}

inline QVector<int> laneIndexes(const TakeLaneModel& model)
{
	QVector<int> out;
	for (const TakeLane& lane : model.lanes()) { out.append(lane.index); }
	return out;
}

inline QVector<int> segmentBegins(const TakeLaneModel& model)
{
	QVector<int> out;
	for (const TakeLaneSegment& seg : model.segments()) { out.append(seg.beginTick); }
	return out;
}

} // namespace taketest

#endif // LMMS_TAKE_LANE_TEST_SUPPORT_H
