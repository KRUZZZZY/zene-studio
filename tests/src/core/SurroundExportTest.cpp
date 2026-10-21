/*
 * SurroundExportTest.cpp - the multichannel WAV export of a surround master (0.5.0 F7)
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

/*! 0.5.0 F7 (MX-060), measured on rendered files (the fixture: tests/src/core/SurroundExportTestSupport.h).
 *  - A stereo export is the default and is unchanged: the default settings and an explicit Stereo give
 *    the same bytes (the same samples at 32-bit float, whose header carries a timestamped PEAK chunk),
 *    a two-channel PCM header, and the BS.775 fold of the surround master.
 *  - A 5.1 export at 16, 24 and 32-bit writes six channels, WAVE_FORMAT_EXTENSIBLE, mask 0x3F, and every
 *    source on its own speaker and on no other one.
 *  - The stereo export of the project is the BS.775 downmix of its surround export, sample for sample.
 *  - A 7.1 master gives eight channels and mask 0x63F; the back pair (Lrs, Rrs) and the side pair apart.
 *    LCR and quad give three and four channels, masks 0x7 and 0x33.
 *  - A surround export on a stereo master, in FLAC, or with the loudness report is refused, with the
 *    reason, and opens no file.
 *  - The master mix feeding the export tap allocates nothing, and the tap holds the period the renderer
 *    is handed (the engine's one-period offset). */

#include <QtTest>
#include <QTemporaryDir>

#include <tuple>

#include "AllocationProbe.h"
#include "AudioBus.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "SampleTrack.h"
#include "Song.h"
#include "SurroundExport.h"
#include "SurroundExportTestSupport.h"

using namespace lmms;
using namespace surroundexporttest;

namespace
{

using Depth = OutputSettings::BitDepth;
using Format = ProjectRenderer::ExportFileFormat;

constexpr float Root = 0.70710678f;
//! The mixer channel of the left-surround source (sources()[5]); its panner points at (-1, -1).
constexpr int LsChannel = 6;

bool ok(const QString& id, const QJsonObject& args)
{
	const ControlResult result = ControlRegistry::instance()->invoke(id, args);
	if (!result.ok) { qWarning("%s: %s", qPrintable(id), qPrintable(result.errorMessage)); }
	return result.ok;
}

QString ch(int index)
{
	return control::channelIdOf(Engine::mixer()->mixerChannel(index));
}

bool masterFormat(const char* name)
{
	return ok(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), ch(0)}, {QStringLiteral("format"), QString::fromLatin1(name)}});
}

OutputSettings surroundSettings(Depth depth)
{
	OutputSettings s = settings(depth);
	s.setChannelMode(OutputSettings::ChannelMode::Surround);
	return s;
}

QByteArray samplesHash(const QString& path)
{
	int channels = 0;
	const std::vector<float> v = readSamples(path, &channels);
	return QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(v.data()), int(v.size() * sizeof(float))),
		QCryptographicHash::Sha256).toHex();
}

//! Every source at full level on its own speaker, and at least 60 dB down on every other channel.
QString sourcesOnTheirSpeakers(const std::vector<float>& samples, int channels)
{
	for (const Source& s : sources())
	{
		if (s.speaker < 0) { continue; }
		for (int c = 0; c < channels; ++c)
		{
			const double level = levelAt(samples, channels, c, s.frequency);
			const bool own = c == s.speaker;
			if (own && std::abs(level - Amplitude) > 0.01) { return QStringLiteral("%1 on its channel %2: %3").arg(s.name).arg(c).arg(level); }
			if (!own && level > Amplitude * 1e-3) { return QStringLiteral("%1 leaks into channel %2: %3").arg(s.name).arg(c).arg(level); }
		}
	}
	return QString();
}

} // namespace

class SurroundExportTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(m_dir.isValid());
		const QString project = writeProject(m_dir.path());
		QVERIFY(!project.isEmpty());
		Engine::getSong()->loadProject(project);
		Mixer* mixer = Engine::mixer();
		while (mixer->numChannels() <= int(sources().size())) { mixer->createChannel(); }
		int i = 0;
		for (Track* track : Engine::getSong()->tracks())
		{
			IntModel* model = dynamic_cast<SampleTrack*>(track)->mixerChannelModel();
			model->setRange(0, mixer->numChannels() - 1);  // the range was fixed when the project loaded
			model->setValue(++i);
			QCOMPARE(model->value(), i);
		}
		QVERIFY(masterFormat("5.1"));
		i = 0;
		for (const Source& s : sources())
		{
			QVERIFY(ok(QStringLiteral("mixer.surround_pan"), {{QStringLiteral("channel"), ch(++i)}, {QStringLiteral("x"), s.x},
				{QStringLiteral("y"), s.y}, {QStringLiteral("lfe"), s.lfe}}));
		}
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The default is stereo, and stereo is what it was: the same bytes as an explicit Stereo, a plain
	//! two-channel PCM header, the master folded with BS.775. The hashes are printed so a run on the code
	//! before this feature can be compared with one after it.
	void stereoIsTheDefaultAndUnchanged()
	{
		QCOMPARE(settings(Depth::Depth16Bit).channelMode(), OutputSettings::ChannelMode::Stereo);
		for (const Depth depth : {Depth::Depth16Bit, Depth::Depth24Bit, Depth::Depth32Bit})
		{
			const QString byDefault = path(QStringLiteral("default-%1.wav").arg(int(depth)));
			const QString explicitStereo = path(QStringLiteral("stereo-%1.wav").arg(int(depth)));
			render(settings(depth), Format::Wave, byDefault);
			OutputSettings stereo = settings(depth);
			stereo.setChannelMode(OutputSettings::ChannelMode::Stereo);
			render(stereo, Format::Wave, explicitStereo);
			const Header h = readHeader(byDefault);
			QCOMPARE(h.channels, 2);
			QCOMPARE(h.tag, depth == Depth::Depth32Bit ? 3 : 1);
			QCOMPARE(samplesHash(byDefault), samplesHash(explicitStereo));
			if (depth != Depth::Depth32Bit) { QCOMPARE(sha256(byDefault), sha256(explicitStereo)); }
			std::fprintf(stderr, "STEREO depth %d file %s samples %s\n", int(depth), sha256(byDefault).constData(),
				samplesHash(byDefault).constData());
		}
		int channels = 0;
		const std::vector<float> s = readSamples(path(QStringLiteral("default-0.wav")), &channels);
		QVERIFY(std::abs(levelAt(s, channels, 0, 400.0) - Amplitude) < 0.01);  // L on L
		QVERIFY(std::abs(levelAt(s, channels, 0, 800.0) - Amplitude * Root) < 0.01);  // C into L at -3 dB
		QVERIFY(std::abs(levelAt(s, channels, 1, 1200.0) - Amplitude * Root) < 0.01);  // Rs into R at -3 dB
		QVERIFY(levelAt(s, channels, 0, 100.0) < Amplitude * 1e-3);  // the LFE dropped
	}

	void a51ExportPutsEachSourceOnItsSpeaker()
	{
		for (const Depth depth : {Depth::Depth16Bit, Depth::Depth24Bit, Depth::Depth32Bit})
		{
			const QString out = path(QStringLiteral("surround-%1.wav").arg(int(depth)));
			render(surroundSettings(depth), Format::Wave, out);
			const Header h = readHeader(out);
			QCOMPARE(h.tag, 0xFFFE);
			QCOMPARE(h.channels, 6);
			QCOMPARE(h.bits, depth == Depth::Depth16Bit ? 16 : depth == Depth::Depth24Bit ? 24 : 32);
			QCOMPARE(h.mask, quint32(0x3F));
			QCOMPARE(h.subFormat, depth == Depth::Depth32Bit ? 3 : 1);
			int channels = 0;
			const std::vector<float> samples = readSamples(out, &channels);
			QCOMPARE(channels, 6);
			QVERIFY(samples.size() >= static_cast<std::size_t>(WindowStart + Window) * 6);
			const QString wrong = sourcesOnTheirSpeakers(samples, channels);
			QVERIFY2(wrong.isEmpty(), qPrintable(wrong));
		}
	}

	//! The stereo export is the BS.775 fold of the surround one, frame for frame (float, so exact up to
	//! the order of the additions).
	void theStereoExportIsTheDownmixOfTheSurroundOne()
	{
		int sc = 0;
		int mc = 0;
		const std::vector<float> st = readSamples(path(QStringLiteral("default-2.wav")), &sc);
		const std::vector<float> mx = readSamples(path(QStringLiteral("surround-2.wav")), &mc);
		QCOMPARE(sc, 2);
		QCOMPARE(mc, 6);
		QCOMPARE(st.size() / 2, mx.size() / 6);
		double worst = 0.0;
		for (std::size_t f = 0; f < st.size() / 2; ++f)
		{
			const float* m = &mx[f * 6];
			const double left = m[0] + Root * m[2] + Root * m[4];
			const double right = m[1] + Root * m[2] + Root * m[5];
			worst = std::max({worst, std::abs(st[f * 2] - left), std::abs(st[f * 2 + 1] - right)});
		}
		QVERIFY2(worst < 1e-5, qPrintable(QStringLiteral("worst frame differs by %1").arg(worst)));
	}

	void a71ExportCarriesBothSurroundPairs()
	{
		QVERIFY(masterFormat("7.1"));
		const QString out = path(QStringLiteral("surround71.wav"));
		render(surroundSettings(Depth::Depth16Bit), Format::Wave, out);
		const Header h = readHeader(out);
		QCOMPARE(h.channels, 8);
		QCOMPARE(h.mask, quint32(0x63F));
		int channels = 0;
		const std::vector<float> s = readSamples(out, &channels);
		QCOMPARE(channels, 8);
		QVERIFY(std::abs(levelAt(s, 8, 0, 400.0) - Amplitude) < 0.01);  // L
		QVERIFY(std::abs(levelAt(s, 8, 3, 100.0) - Amplitude) < 0.01);  // LFE
		QVERIFY(std::abs(levelAt(s, 8, 4, 1000.0) - Amplitude) < 0.01);  // (-1, -1) is 7.1's Lrs
		QVERIFY(std::abs(levelAt(s, 8, 5, 1200.0) - Amplitude) < 0.01);  // (1, -1) is Rrs
		QVERIFY(peakOf(s, 8, 6) < 1e-4 && peakOf(s, 8, 7) < 1e-4);  // nothing panned to the sides
		QVERIFY(masterFormat("5.1"));
	}

	//! The two smaller layouts get their own masks: LCR 0x7 (L R C), quad 0x33 (L R and the back pair).
	void lcrAndQuadCarryTheirMasks()
	{
		const std::array<std::tuple<const char*, int, quint32>, 2> layouts{{{"lcr", 3, 0x7}, {"quad", 4, 0x33}}};
		for (const auto& [name, count, mask] : layouts)
		{
			QVERIFY(masterFormat(name));
			const QString out = path(QStringLiteral("surround-%1.wav").arg(QString::fromLatin1(name)));
			render(surroundSettings(Depth::Depth16Bit), Format::Wave, out);
			const Header h = readHeader(out);
			QCOMPARE(h.tag, 0xFFFE);
			QCOMPARE(h.channels, count);
			QCOMPARE(h.mask, mask);
			int channels = 0;
			const std::vector<float> s = readSamples(out, &channels);
			QVERIFY(std::abs(levelAt(s, channels, 0, 400.0) - Amplitude) < 0.01);  // L on L in every layout
		}
		QVERIFY(masterFormat("5.1"));
	}

	void aDiscreteMasterWritesItsChannelCountWithoutSpeakerMask()
	{
		QVERIFY(masterFormat("discrete-5"));
		const QString out = path(QStringLiteral("discrete-master.wav"));
		render(surroundSettings(Depth::Depth16Bit), Format::Wave, out);
		const Header h = readHeader(out);
		QCOMPARE(h.tag, 0xFFFE);
		QCOMPARE(h.channels, 5);
		QCOMPARE(h.mask, quint32(0));  // no speaker positions
		int channels = 0;
		const std::vector<float> samples = readSamples(out, &channels);
		QCOMPARE(channels, 5);
		QVERIFY(!samples.empty());
		QVERIFY(masterFormat("5.1"));
	}

	void invalidCombinationsAreRefused()
	{
		const bool wave = true;
		QVERIFY(surroundexport::refusal(surroundSettings(Depth::Depth16Bit), wave).isEmpty());
		QVERIFY(!surroundexport::refusal(surroundSettings(Depth::Depth16Bit), false).isEmpty());  // FLAC/OGG/MP3
		OutputSettings loudness = surroundSettings(Depth::Depth16Bit);
		loudness.setLoudnessReport(true);
		QVERIFY(surroundexport::refusal(loudness, wave).contains(QStringLiteral("loudness")));
		expectRefusedRender(surroundSettings(Depth::Depth16Bit), Format::Flac, QStringLiteral("WAV"));

		QVERIFY(masterFormat("stereo"));
		const QString stereoMaster = surroundexport::refusal(surroundSettings(Depth::Depth16Bit), wave);
		QVERIFY2(stereoMaster.contains(QStringLiteral("master is stereo")), qPrintable(stereoMaster));
		expectRefusedRender(surroundSettings(Depth::Depth16Bit), Format::Wave, QStringLiteral("master is stereo"));
		// A stereo export is never refused, whatever else is chosen.
		OutputSettings stereo = settings(Depth::Depth16Bit);
		stereo.setLoudnessReport(true);
		QVERIFY(surroundexport::refusal(stereo, false).isEmpty());
		QVERIFY(masterFormat("5.1"));

		ProjectRenderer accepted(surroundSettings(Depth::Depth24Bit), Format::Wave, path(QStringLiteral("accepted.wav")));
		QVERIFY(accepted.isReady() && accepted.refusal().isEmpty());
		QCOMPARE(accepted.channels(), 6);
	}

	//! The master mix with the tap armed during an export allocates nothing, and the tap's read side is
	//! the period before the one being mixed - the offset the engine's stereo output has.
	void theTapAllocatesNothingAndKeepsTheOffset()
	{
		Mixer* mixer = Engine::mixer();
		if (auto* device = Engine::audioEngine()->audioDev()) { device->stopProcessing(); }
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> out(fpp);
		std::vector<SampleFrame> loud(fpp, SampleFrame(0.5f, 0.5f));
		std::vector<SampleFrame> quiet(fpp);
		surroundexport::arm(fpp);
		Engine::getSong()->startExport();
		mixPeriod(*mixer, out, quiet);  // the fader ramps settle
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		mixPeriod(*mixer, out, loud);
		test::tlCountAllocations = false;
		const auto allocations = static_cast<qulonglong>(test::tlAllocationCount);
		const float mixed = surroundexport::writePlane(4)[0];
		mixPeriod(*mixer, out, quiet);
		const float handed = surroundexport::readPlane(4)[0];
		const float now = surroundexport::writePlane(4)[0];
		Engine::getSong()->stopExport();
		surroundexport::disarm();
		QCOMPARE(allocations, static_cast<qulonglong>(0));
		QVERIFY2(std::abs(mixed - 0.5f) < 1e-5f, qPrintable(QString::number(mixed)));  // Ls of the period mixed
		QVERIFY2(std::abs(handed - 0.5f) < 1e-5f, qPrintable(QString::number(handed)));  // ...read one period on
		QCOMPARE(now, 0.f);
	}

private:
	QString path(const QString& name) const { return QDir(m_dir.path()).filePath(name); }

	void expectRefusedRender(const OutputSettings& settings, Format format, const QString& reason)
	{
		const QString out = path(QStringLiteral("refused.out"));
		ProjectRenderer renderer(settings, format, out);
		QVERIFY(!renderer.isReady());
		QVERIFY2(renderer.refusal().contains(reason), qPrintable(renderer.refusal()));
		QVERIFY2(!QFileInfo::exists(out), "a refused render opened no file");
	}

	//! One master mix by hand, the left-surround source's channel fed @a in.
	static void mixPeriod(Mixer& mixer, std::vector<SampleFrame>& out, std::vector<SampleFrame>& in)
	{
		const auto fpp = static_cast<f_cnt_t>(out.size());
		SampleFrame* planes[1] = {in.data()};
		mixer.prepareMasterMix();
		mixer.mixToChannel(AudioBus{planes, 1, fpp}, LsChannel);
		zeroSampleFrames(out.data(), fpp);
		mixer.masterMix(out.data());
		AutomatableModel::incrementPeriodCounter();
	}

	QTemporaryDir m_dir;
};

QTEST_MAIN(SurroundExportTest)
#include "SurroundExportTest.moc"
