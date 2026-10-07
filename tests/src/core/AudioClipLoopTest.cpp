/*
 * AudioClipLoopTest.cpp - an audio clip's content looped inside the clip (ED-029), measured on rendered PCM
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

/*! 0.5.0 ED-029 (audio): clip.set_loop on an audio clip. The source is a staircase - eight steps of eight ticks,
 *  step k at level 0.05 * (k + 1) - so a rendered frame's level names the source tick it came from, and a constant
 *  survives the resampler. Every render goes through the real export path (ProjectRenderer, the code `lmms render`
 *  uses), so SampleTrack's scheduling is exercised along with the play handle. Claims: a looped clip repeats its
 *  loop across the whole clip; a pass that begins past the loop's end starts on the folded frame; the loop is an
 *  additive attribute an unlooped clip never writes, and undo takes it off; refusals are typed; the looped render
 *  allocates nothing per period. ED-028: note.rotate on an audio clip turns its content with wrap-around - inside
 *  the clip, or inside its loop - one undo step each. */

#include <QtTest>

#include <QDomDocument>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "AllocationProbe.h"

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "OutputSettings.h"
#include "ProjectRenderer.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace lmms;

namespace
{

constexpr int kRate = 44100;
constexpr int kStepTicks = 8;
constexpr int kSteps = 8;  //!< the source is 64 ticks long

double framesPerTick() { return Engine::framesPerTick(kRate); }

float stepLevel(int step) { return 0.05f * static_cast<float>(step + 1); }

std::shared_ptr<const SampleBuffer> staircase()
{
	const auto stepFrames = static_cast<std::size_t>(std::llround(kStepTicks * framesPerTick()));
	std::vector<SampleFrame> frames;
	for (int step = 0; step < kSteps; ++step)
	{
		for (std::size_t i = 0; i < stepFrames; ++i) { frames.emplace_back(stepLevel(step), stepLevel(step)); }
	}
	return std::make_shared<const SampleBuffer>(frames.data(), frames.size(), kRate);
}

ControlResult run(const QString& id, const QJsonObject& args = {}) { return ControlRegistry::instance()->invoke(id, args); }

int dataOffset(const QByteArray& wav)
{
	int offset = 12;
	while (offset + 8 <= wav.size())
	{
		qint32 size = 0;
		std::memcpy(&size, wav.constData() + offset + 4, sizeof(size));
		if (wav.mid(offset, 4) == QByteArrayLiteral("data")) { return offset + 8; }
		offset += 8 + size + (size & 1);
	}
	return -1;
}

//! The rendered left channel at the middle of song tick @a tick.
float levelAtTick(const QByteArray& wav, double tick)
{
	const int offset = dataOffset(wav);
	const auto frame = static_cast<int>((tick + 0.5) * framesPerTick());
	if (offset < 0 || offset + frame * 4 + 2 > wav.size()) { return -1.f; }
	qint16 value = 0;
	std::memcpy(&value, wav.constData() + offset + frame * 4, sizeof(value));
	return static_cast<float>(value) / 32768.0f;
}

//! The source tick a looped clip plays at content tick @a content: itself before the loop's end, folded after it.
int folded(int content, int loopStart, int loopLength)
{
	return content < loopStart + loopLength ? content : loopStart + (content - loopStart) % loopLength;
}

} // namespace

class AudioClipLoopTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! A clip 160 ticks long over 64 ticks of audio, looping ticks 16..48: the audio plays to 48, then 16..48 again
	//! and again to the clip's end - where without the loop it fell silent at 64.
	void aLoopedClipRepeatsItsLoopAcrossTheClip()
	{
		SampleClip* subject = buildSong(160);
		const ControlResult set = run(QStringLiteral("clip.set_loop"), {{QStringLiteral("clip"), control::clipIdOf(subject)},
			{QStringLiteral("start"), 16}, {QStringLiteral("length"), 32}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		const QByteArray wav = render();
		QVERIFY2(!wav.isEmpty(), "the export produced no file");
		expectContent(wav, 0, 160, 0, 16, 32);
	}

	//! A clip whose origin lies 100 ticks before its start (trimmed on the left): the first pass begins at content
	//! tick 100, past the loop's end, so the render must start on the FOLDED frame - not on a frame past the audio.
	void aPassThatBeginsPastTheLoopStartsOnTheFoldedFrame()
	{
		SampleClip* subject = buildSong(96);
		subject->setStartTimeOffset(TimePos(-100));
		const ControlResult set = run(QStringLiteral("clip.set_loop"), {{QStringLiteral("clip"), control::clipIdOf(subject)},
			{QStringLiteral("start"), 16}, {QStringLiteral("length"), 32}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		expectContent(render(), 0, 96, 100, 16, 32);
	}

	//! Additive format: the loop is written only on a looped clip (an unlooped clip's element is what it was), read
	//! back from the attributes, and a journal undo takes it off; clearing it renders exactly as before it was set.
	void theLoopIsAnAdditiveAttributeAndUndoes()
	{
		SampleClip* subject = buildSong(160);
		const QByteArray before = render();
		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("track"));
		QDomElement plain = subject->saveState(doc, parent);
		QVERIFY(!plain.hasAttribute(QStringLiteral("loopframes")) && !plain.hasAttribute(QStringLiteral("loopfrom")));
		QVERIFY(!plain.hasAttribute(QStringLiteral("loop")) && !plain.hasAttribute(QStringLiteral("loopoffset")));

		const QString id = control::clipIdOf(subject);
		QVERIFY(run(QStringLiteral("clip.set_loop"), {{QStringLiteral("clip"), id}, {QStringLiteral("start"), 16}, {QStringLiteral("length"), 32}}).ok);
		QDomElement looped = subject->saveState(doc, parent);
		QCOMPARE(looped.attribute(QStringLiteral("loopframes")).toLongLong(), std::llround(32 * framesPerTick()));
		QCOMPARE(looped.attribute(QStringLiteral("loopfrom")).toLongLong(), std::llround(16 * framesPerTick()));
		const QByteArray loopedRender = render();
		QVERIFY(loopedRender != before);

		// A fresh clip restored from the looped element plays the loop; one restored from the plain one does not.
		subject->restoreState(plain);
		QCOMPARE(render(), before);
		subject->restoreState(looped);
		QCOMPARE(render(), loopedRender);

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!subject->saveState(doc, parent).hasAttribute(QStringLiteral("loopframes")));
		QCOMPARE(render(), before);
		QVERIFY(run(QStringLiteral("clip.set_loop"), {{QStringLiteral("clip"), id}, {QStringLiteral("length"), 32}}).ok);
		QVERIFY(run(QStringLiteral("clip.set_loop"), {{QStringLiteral("clip"), id}, {QStringLiteral("length"), 0}}).ok);
		QCOMPARE(render(), before);
	}

	void refusalsAreTyped()
	{
		SampleClip* subject = buildSong(160);
		const QString id = control::clipIdOf(subject);
		// Past the end of the audio (64 ticks of it).
		QCOMPARE(run(QStringLiteral("clip.set_loop"), {{QStringLiteral("clip"), id}, {QStringLiteral("start"), 48}, {QStringLiteral("length"), 32}}).errorKind,
			ControlErrorKind::InvalidArgs);
		subject->setWarpTempoMode(WarpTempoMode::SourceTempo);
		subject->setSourceTempo(120.f);
		QCOMPARE(run(QStringLiteral("clip.set_loop"), {{QStringLiteral("clip"), id}, {QStringLiteral("length"), 32}}).errorKind,
			ControlErrorKind::Refused);
		subject->setWarpTempoMode(WarpTempoMode::FollowProject);
		auto* instrument = Track::create(Track::Type::Instrument, Engine::getSong());
		const QString midi = control::clipIdOf(instrument->createClip(TimePos(0)));
		QCOMPARE(run(QStringLiteral("clip.set_loop"), {{QStringLiteral("clip"), midi}, {QStringLiteral("start"), 8}, {QStringLiteral("length"), 32}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QVERIFY(run(QStringLiteral("clip.set_loop"), {{QStringLiteral("clip"), midi}, {QStringLiteral("length"), 32}}).ok);
		delete instrument;
	}

	//! I8: the looped render allocates nothing per period, across many wraps. The handle is built outside the probe
	//! (what allocates on this path is the handle, not the render loop), over a pass four times the audio's length.
	void theLoopedRenderAllocatesNothing()
	{
		SampleClip* subject = buildSong(160);
		QVERIFY(run(QStringLiteral("clip.set_loop"), {{QStringLiteral("clip"), control::clipIdOf(subject)}, {QStringLiteral("start"), 8},
			{QStringLiteral("length"), 4}}).ok);
		const auto size = static_cast<f_cnt_t>(subject->sample().sampleSize());
		SamplePlayHandle handle(subject, SampleWindow(0, 4 * size));
		std::vector<SampleFrame> buffer(1024, SampleFrame(0.f, 0.f));
		const auto span = std::span<SampleFrame>(buffer);
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		for (int period = 0; period < 64; ++period) { handle.play(span); }
		test::tlCountAllocations = false;
		QCOMPARE(static_cast<qulonglong>(test::tlAllocationCount), static_cast<qulonglong>(0));
		// The 64th period lies past the audio's end: it sounds only because the loop wrapped (step 1, level 0.1).
		QVERIFY2(std::abs(buffer[512][0] - stepLevel(1)) < 0.01f, qPrintable(QString::number(buffer[512][0])));
	}

	//! ED-028: note.rotate on an audio clip turns its content with wrap-around, inside the clip: the audio 16 ticks
	//! later, what falls off the end coming round at the start - one undo step per rotation.
	void rotatingAudioTurnsItsContentWithWrap()
	{
		SampleClip* subject = buildSong(64);  // exactly as long as its audio
		const QString id = control::clipIdOf(subject);
		const ControlResult turned = run(QStringLiteral("note.rotate"), {{QStringLiteral("clip"), id}, {QStringLiteral("amount"), 16}});
		QVERIFY2(turned.ok, qPrintable(turned.errorMessage));
		QCOMPARE(turned.result.value(QStringLiteral("rotated")).toInt(), 1);
		expectContent(render(), 0, 64, 48, 0, 64);  // tick t plays source tick (t - 16) mod 64
		QVERIFY(run(QStringLiteral("note.rotate"), {{QStringLiteral("clip"), id}, {QStringLiteral("amount"), -8}}).ok);
		expectContent(render(), 0, 64, 56, 0, 64);  // (t - 8) mod 64
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		expectContent(render(), 0, 64, 48, 0, 64);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		expectContent(render(), 0, 64, 0, 0, 64);  // as it was
		// A whole number of turns is no edit at all.
		const ControlResult whole = run(QStringLiteral("note.rotate"), {{QStringLiteral("clip"), id}, {QStringLiteral("amount"), 128}});
		QVERIFY(whole.ok);
		QCOMPARE(whole.result.value(QStringLiteral("rotated")).toInt(), 0);
		QVERIFY(!subject->clipEdits().hasAudioLoop());
	}

	//! A looped clip turns inside its loop: a 32-tick loop from the in-point, turned 8 ticks later, plays source tick
	//! (t - 8) mod 32 across the whole 160-tick clip.
	void aLoopedClipTurnsInsideItsLoop()
	{
		SampleClip* subject = buildSong(160);
		const QString id = control::clipIdOf(subject);
		QVERIFY(run(QStringLiteral("clip.set_loop"), {{QStringLiteral("clip"), id}, {QStringLiteral("length"), 32}}).ok);
		QVERIFY(run(QStringLiteral("note.rotate"), {{QStringLiteral("clip"), id}, {QStringLiteral("amount"), 8}}).ok);
		expectContent(render(), 0, 160, 24, 0, 32);
	}

	void rotationRefusalsAreTyped()
	{
		SampleClip* subject = buildSong(160);
		const QString id = control::clipIdOf(subject);
		// Longer than its audio, with no loop: there is no content to wrap the silence into.
		QCOMPARE(run(QStringLiteral("note.rotate"), {{QStringLiteral("clip"), id}, {QStringLiteral("amount"), 8}}).errorKind,
			ControlErrorKind::InvalidArgs);
		// A loop that starts after the clip's first tick: the intro does not repeat, so it cannot turn.
		QVERIFY(run(QStringLiteral("clip.set_loop"), {{QStringLiteral("clip"), id}, {QStringLiteral("start"), 16}, {QStringLiteral("length"), 32}}).ok);
		QCOMPARE(run(QStringLiteral("note.rotate"), {{QStringLiteral("clip"), id}, {QStringLiteral("amount"), 8}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(subject->clipEdits().audioLoopOffset, std::int64_t{0});
	}

private:
	static Song* song() { return Engine::getSong(); }

	//! One song: a sample track carrying one staircase clip from tick 0, @a ticks long (longer than its audio).
	SampleClip* buildSong(int ticks)
	{
		song()->clearProject();
		auto* track = new SampleTrack(song());
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		clip->setSampleBuffer(staircase());
		clip->changeLength(TimePos(ticks));
		clip->setAutoResize(false);
		song()->updateLength();
		return clip;
	}

	/*! The render's song ticks [@a from, @a to) against a clip that plays content tick (tick + @a origin) folded into
	 *  the loop @a loopStart..+@a loopLength: each tick away from a step's edge has its step's level. */
	void expectContent(const QByteArray& wav, int from, int to, int origin, int loopStart, int loopLength)
	{
		int checked = 0;
		for (int tick = from; tick < to; ++tick)
		{
			const int content = folded(tick + origin, loopStart, loopLength);
			if (content % kStepTicks < 1 || content % kStepTicks > kStepTicks - 2) { continue; }  // a step's edges
			const float expected = stepLevel(content / kStepTicks);
			const float heard = levelAtTick(wav, tick);
			QVERIFY2(std::abs(heard - expected) < 0.003f, qPrintable(QStringLiteral("tick %1 (content %2): heard %3, expected %4")
				.arg(tick).arg(content).arg(heard).arg(expected)));
			++checked;
		}
		QVERIFY(checked > (to - from) / 2);
	}

	QByteArray render()
	{
		const QString path = m_dir.filePath(QStringLiteral("loop-render.wav"));
		QFile::remove(path);
		const OutputSettings settings(kRate, 192, OutputSettings::BitDepth::Depth16Bit, OutputSettings::StereoMode::Stereo);
		ProjectRenderer renderer(settings, ProjectRenderer::ExportFileFormat::Wave, path);
		if (!renderer.isReady()) { return QByteArray(); }
		renderer.startProcessing();
		renderer.wait();
		QFile file(path);
		return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
	}

	QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(AudioClipLoopTest)
#include "AudioClipLoopTest.moc"
