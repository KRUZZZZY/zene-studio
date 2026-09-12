/*
 * VcaGroupTest.cpp - VCA / mix-and-edit groups (task #622)
 *
 * Copyright (c) 2026 LMMS developers
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

//! Task #622: a group of mixer channels under one VCA fader.
//!
//! What these tests are for. The register's warning is that the *semantics*
//! are the hard part (Cubase's VCAs "broken ever since" 2014), so the tests
//! carry the weight:
//!
//!  * the fader move is *exactly* reversible -- bit-exact on the member
//!    models and byte-exact on the render -- and the check has teeth (an
//!    inverted control proves the naive "write the scaled value into the
//!    member fader" scheme does NOT round-trip);
//!  * the *actual* dB delta a member experiences equals the fader's delta,
//!    measured on rendered audio, and the members' relative balance survives;
//!  * mute/solo linking, one test per stated rule;
//!  * save/load round trip, and a pre-#622 project still loads and renders
//!    unchanged;
//!  * the multiply on the audio path allocates nothing (AllocationProbe).
//!
//! Stated semantics (see docs/VCA-GROUPS.md):
//!  M1  group mute silences every member, reversibly; members' own mute models
//!      are never written.
//!  M2  group mute is a gain of zero on the audio path, not the channel mute
//!      model, so a soloed member of a muted group stays silent.
//!  S1  soloing a *member* soloes its whole group (member -> group).
//!  S2  soloing the *group* makes exactly its members audible, nothing else.
//!  S3  a non-member is muted while the group is soloed.
//!
//! Evidence is printed to stdout so the numbers can be pasted into the report.

#include <QtTest>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDomDocument>
#include <QDomElement>
#include <QString>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "AllocationProbe.h"

#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "Mixer.h"
#include "SampleFrame.h"
#include "VcaGroup.h"

using namespace lmms;

namespace
{

//! Master + 8 channels, exactly as the D1 A/B harness builds its graph.
constexpr int kChannels = 9;
constexpr int kMaster = 0;
constexpr int kGroupId = 0;
//! 16 * 256 frames at the test engine's period.
constexpr int kPeriods = 16;
//! The dB delta a fader move of 2.0 -> 1.0 must produce, and the two ends.
constexpr float kHalf = 0.5f;
constexpr float kToleranceDb = 1.0e-3f;

const std::vector<int> kMembers = {2, 4, 6};
const std::vector<int> kAllChannels = {1, 2, 3, 4, 5, 6, 7, 8};

bool isMember(int channel)
{
	return std::find(kMembers.begin(), kMembers.end(), channel) != kMembers.end();
}

//! Deterministic signal in [-0.5, 0.5); integer arithmetic only, so the bit
//! pattern is platform-independent (same generator as MixerAbRegressionTest).
float sig(int period, int frame, int channel, int side)
{
	const int n = period * 7919 + frame * 131 + channel * 17 + side * 4093;
	return static_cast<float>((n % 2048) - 1024) * (1.0f / 2048.0f);
}

f_cnt_t framesPerPeriod()
{
	return Engine::audioEngine()->framesPerPeriod();
}

//! Feed the given channels with `sig`, run the period, and return the raw
//! interleaved master output for `periods` periods.
QByteArray renderRaw(Mixer* mixer, const std::vector<int>& feed, int periods)
{
	const f_cnt_t fpp = framesPerPeriod();
	QByteArray out;
	out.reserve(periods * static_cast<int>(fpp * sizeof(SampleFrame)));

	std::vector<SampleFrame> masterOut(fpp);
	std::vector<SampleFrame> input(fpp);
	SampleFrame* busData[1] = {input.data()};

	for (int p = 0; p < periods; ++p)
	{
		for (int ch : feed)
		{
			for (f_cnt_t f = 0; f < fpp; ++f)
			{
				input[f][0] = sig(p, f, ch, 0);
				input[f][1] = sig(p, f, ch, 1);
			}
			const AudioBus bus{busData, 1, fpp};
			mixer->mixToChannel(bus, static_cast<mix_ch_t>(ch));
		}
		mixer->prepareMasterMix();
		zeroSampleFrames(masterOut.data(), fpp);
		mixer->masterMix(masterOut.data());
		out.append(reinterpret_cast<const char*>(masterOut.data()),
			static_cast<int>(fpp * sizeof(SampleFrame)));
	}
	return out;
}

//! renderRaw() with the sample-exact machinery settled first.
//!
//! AutomatableModel::valueBuffer() hands out a *one-period interpolation ramp*
//! from the model's previous value after any setValue(), and caches it until
//! the engine ticks its static period counter -- a tick that only the real
//! AudioEngine performs (AutomatableModel::incrementPeriodCounter, from
//! AudioEngine::renderStage...). This harness drives Mixer::masterMix()
//! directly, so without settling, a render that follows a setValue() on a
//! channel fader or a send amount would use the frozen ramp instead of the
//! settled value, and every level comparison here would be measuring the
//! harness. One throwaway period consumes the pending ramps; the tick then
//! makes the next valueBuffer() call report "no sample-exact data", which is
//! the code path a playing project takes between edits.
//!
//! Note this is exactly the trap the VCA does *not* have: MixerChannel::m_vcaGain
//! is a plain float published on the control thread, so the group gain never
//! goes through a ValueBuffer at all.
QByteArray renderFeed(Mixer* mixer, const std::vector<int>& feed, int periods)
{
	renderRaw(mixer, feed, 1);
	AutomatableModel::incrementPeriodCounter();
	return renderRaw(mixer, feed, periods);
}

//! Render one period with every buffer owned by the caller, so an allocation
//! probe around this measures the mixer rather than the harness.
void renderPeriodNoAlloc(Mixer* mixer, const std::vector<int>& feed,
	std::vector<SampleFrame>& input, std::vector<SampleFrame>& masterOut)
{
	const f_cnt_t fpp = framesPerPeriod();
	SampleFrame* busData[1] = {input.data()};
	for (int ch : feed)
	{
		for (f_cnt_t f = 0; f < fpp; ++f)
		{
			input[f][0] = sig(0, f, ch, 0);
			input[f][1] = sig(0, f, ch, 1);
		}
		const AudioBus bus{busData, 1, fpp};
		mixer->mixToChannel(bus, static_cast<mix_ch_t>(ch));
	}
	mixer->prepareMasterMix();
	zeroSampleFrames(masterOut.data(), fpp);
	mixer->masterMix(masterOut.data());
}

std::size_t frameCount(const QByteArray& raw)
{
	return static_cast<std::size_t>(raw.size()) / sizeof(SampleFrame);
}

const SampleFrame* framesOf(const QByteArray& raw)
{
	return reinterpret_cast<const SampleFrame*>(raw.constData());
}

float peakOf(const QByteArray& raw)
{
	const SampleFrame* f = framesOf(raw);
	float peak = 0.0f;
	for (std::size_t i = 0; i < frameCount(raw); ++i)
	{
		peak = std::max(peak, std::abs(f[i][0]));
		peak = std::max(peak, std::abs(f[i][1]));
	}
	return peak;
}

//! max |b - factor * a|; negative when the two renders differ in length.
float scaleError(const QByteArray& a, const QByteArray& b, float factor)
{
	if (a.size() != b.size())
	{
		return -1.0f;
	}
	const SampleFrame* fa = framesOf(a);
	const SampleFrame* fb = framesOf(b);
	float worst = 0.0f;
	for (std::size_t i = 0; i < frameCount(a); ++i)
	{
		for (int s = 0; s < 2; ++s)
		{
			worst = std::max(worst, std::abs(fb[i][s] - factor * fa[i][s]));
		}
	}
	return worst;
}

float dB(float amplitude)
{
	return 20.0f * std::log10(amplitude);
}

//! Bit comparison, not qFuzzyCompare: the reversibility claim is "exactly the
//! same float", and a fuzzy compare would hide the very defect it targets.
bool bitEqual(float a, float b)
{
	return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

QString sha256Hex(const QByteArray& data)
{
	return QString::fromLatin1(
		QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

//! A compact dump of the mixer state that decides what a render sounds like,
//! so a round-trip difference can be read off rather than guessed at.
QString stateOf(Mixer* mixer)
{
	QStringList lines;
	for (int ch = 0; ch < mixer->numChannels(); ++ch)
	{
		const MixerChannel* c = mixer->mixerChannel(ch);
		QString sends;
		for (MixerRoute* r : const_cast<MixerChannel*>(c)->m_sends)
		{
			sends += QStringLiteral(" ->%1/%2%3")
				.arg(r->receiverIndex()).arg(r->amount()->value())
				.arg(r->preFader() ? QStringLiteral("(pre)") : QString());
		}
		lines << QStringLiteral("ch%1 vol=%2 muted=%3 soloed=%4 gain=%5 recv=%6 bus=%7 sends=%8")
			.arg(ch).arg(c->m_volumeModel.value())
			.arg(c->m_muteModel.value() ? 1 : 0)
			.arg(c->m_soloModel.value() ? 1 : 0)
			.arg(c->vcaGain())
			.arg(c->m_receives.size())
			.arg(c->isBus() ? 1 : 0)
			.arg(sends);
	}
	for (const VcaGroup* g : mixer->vcaGroups())
	{
		QString members;
		for (mix_ch_t m : g->members()) { members += QStringLiteral("%1,").arg(m); }
		lines << QStringLiteral("group%1 name=%2 vca=%3 muted=%4 soloed=%5 gain=%6 members=%7")
			.arg(g->id()).arg(g->name())
			.arg(const_cast<VcaGroup*>(g)->vcaModel()->value())
			.arg(const_cast<VcaGroup*>(g)->muteModel()->value() ? 1 : 0)
			.arg(const_cast<VcaGroup*>(g)->soloModel()->value() ? 1 : 0)
			.arg(g->gain()).arg(members);
	}
	return lines.join(QStringLiteral(" | "));
}

void evidence(const QString& line)
{
	std::fprintf(stdout, "VCA_EVIDENCE %s\n", qPrintable(line));
	std::fflush(stdout);
}

//! Master + 8 channels with distinct faders and distinct send amounts, so a
//! relative-scaling error cannot cancel out.
void buildGraph(Mixer* mixer)
{
	mixer->clear();
	while (mixer->numChannels() < kChannels)
	{
		mixer->createChannel();
	}

	const float volumes[kChannels] = {1.0f, 1.25f, 0.5f, 2.0f, 0.9f, 1.0f, 0.75f, 1.1f, 0.25f};
	const float sends[kChannels] = {1.0f, 0.8f, 1.0f, 0.25f, 0.6f, 0.9f, 1.0f, 0.45f, 0.15f};
	for (int ch = 1; ch < kChannels; ++ch)
	{
		mixer->mixerChannel(ch)->m_volumeModel.setValue(volumes[ch]);
		mixer->channelSendModel(ch, kMaster)->setValue(sends[ch]);
	}
}

VcaGroup* makeGroup(Mixer* mixer, const std::vector<int>& members,
	const QString& name = QStringLiteral("Drums"))
{
	VcaGroup* group = mixer->createVcaGroup(name);
	if (group == nullptr)
	{
		return nullptr;
	}
	for (int member : members)
	{
		if (!group->addMember(static_cast<mix_ch_t>(member)))
		{
			return nullptr;
		}
	}
	return group;
}

//! The pre-#622 project shape the D1 harness uses: <mixerchannel> with
//! volume/muted/name attributes and <send> children, no <bus>, no
//! <sidechain-send>, and of course no <vcagroup>.
const char* kLegacyMixerXml =
	"<mixer>\n"
	"  <mixerchannel num=\"0\" muted=\"0\" volume=\"1\" name=\"Master\">\n"
	"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
	"  </mixerchannel>\n"
	"  <mixerchannel num=\"1\" muted=\"0\" volume=\"1.5\" name=\"Kick\">\n"
	"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
	"    <send channel=\"0\" amount=\"0.75\"/>\n"
	"  </mixerchannel>\n"
	"  <mixerchannel num=\"2\" muted=\"0\" volume=\"0.5\" name=\"Bass\">\n"
	"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
	"    <send channel=\"0\" amount=\"1\"/>\n"
	"  </mixerchannel>\n"
	"</mixer>\n";

bool parseMixer(const char* xml, QDomDocument& doc, QDomElement& out)
{
	QString error;
	int line = 0;
	int column = 0;
	if (!doc.setContent(QString::fromUtf8(xml), &error, &line, &column))
	{
		qWarning("fixture is not valid XML at %d:%d: %s", line, column, qPrintable(error));
		return false;
	}
	out = doc.documentElement();
	return true;
}

} // namespace

class VcaGroupTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// The dummy device renders in the background; these tests drive the
		// mixer synchronously, so stop it to keep buffers stable.
		Engine::audioEngine()->audioDev()->stopProcessing();
		QVERIFY(Engine::mixer() != nullptr);
		QVERIFY2(framesPerPeriod() > 0, "the test engine has no frames per period");
		evidence(QStringLiteral("engine frames_per_period=%1").arg(framesPerPeriod()));
	}

	void cleanupTestCase() { Engine::destroy(); }

	// -- 1. relative scaling, and exact reversibility ------------------------

	//! M1/M2's arithmetic and the reversibility claim in one place: the member
	//! models are never written, so the round trip is bit-exact, and the
	//! rendered audio comes back byte-for-byte.
	void faderMoveIsRelativeAndExactlyReversible()
	{
		Mixer* mixer = Engine::mixer();
		buildGraph(mixer);
		VcaGroup* group = makeGroup(mixer, kMembers);
		QVERIFY(group != nullptr);
		QVERIFY(bitEqual(group->gain(), 1.0f));

		std::vector<std::uint32_t> memberModelsBefore;
		for (int ch = 0; ch < kChannels; ++ch)
		{
			memberModelsBefore.push_back(
				std::bit_cast<std::uint32_t>(mixer->mixerChannel(ch)->m_volumeModel.value()));
		}

		const QByteArray atUnity = renderFeed(mixer, kAllChannels, kPeriods);
		QVERIFY2(peakOf(atUnity) > 1.0e-6f, "the reference render is silent");

		// -6.02 dB (0.5). Every member published exactly 0.5, and every
		// non-member is untouched.
		group->vcaModel()->setValue(0.5f);
		for (int ch = 1; ch < kChannels; ++ch)
		{
			const float expected = isMember(ch) ? 0.5f : 1.0f;
			QVERIFY2(bitEqual(mixer->mixerChannel(ch)->vcaGain(), expected),
				qPrintable(QStringLiteral("channel %1 published gain %2, expected %3")
					.arg(ch).arg(mixer->mixerChannel(ch)->vcaGain()).arg(expected)));
		}
		const QByteArray moved = renderFeed(mixer, kAllChannels, kPeriods);
		QVERIFY2(moved != atUnity, "moving the VCA did not change the render");

		// Back to unity: models bit-exact, render byte-exact.
		group->vcaModel()->setValue(1.0f);
		for (int ch = 0; ch < kChannels; ++ch)
		{
			QVERIFY2(std::bit_cast<std::uint32_t>(mixer->mixerChannel(ch)->m_volumeModel.value())
					== memberModelsBefore[ch],
				qPrintable(QStringLiteral("channel %1's own fader changed").arg(ch)));
		}
		const QByteArray restored = renderFeed(mixer, kAllChannels, kPeriods);
		QCOMPARE(restored, atUnity);

		evidence(QStringLiteral(
			"reversible members=2,4,6 vca=1.0->0.5->1.0 models_bit_exact=1 render_byte_exact=1"
			" render_sha256=%1 moved_sha256=%2").arg(sha256Hex(atUnity), sha256Hex(moved)));
	}

	//! The inverted control: the scheme this feature must NOT use -- writing the
	//! scaled value into the member's own fader and dividing it back out on the
	//! way home -- does not round-trip. Same FloatModel, same arithmetic, so the
	//! only difference from the real implementation is the scheme.
	void absoluteScalingWouldNotRoundTrip()
	{
		// One member, one documented move.
		FloatModel member(0.9f, 0.0f, 2.0f, 0.001f, nullptr);
		const float original = member.value();
		member.setValue(member.value() * 0.7f);
		member.setValue(member.value() / 0.7f);
		const float afterNaive = member.value();
		QVERIFY2(!bitEqual(original, afterNaive),
			"the naive scheme unexpectedly round-tripped - the inverted control is vacuous");
		evidence(QStringLiteral("naive_roundtrip v=0.9 vca=0.7 restored=%1 bit_exact=0")
			.arg(static_cast<double>(afterNaive), 0, 'g', 12));

		// ... and across the whole distinct set, at least one member is lost.
		int broken = 0;
		for (float v : {1.25f, 0.5f, 0.9f, 0.75f, 1.1f, 0.25f})
		{
			FloatModel m(v, 0.0f, 2.0f, 0.001f, nullptr);
			m.setValue(m.value() * 0.7f);
			m.setValue(m.value() / 0.7f);
			if (!bitEqual(v, m.value()))
			{
				++broken;
			}
		}
		QVERIFY2(broken > 0, "the naive scheme round-tripped every member");

		// The identical check against the real implementation passes, which is
		// what gives the inverted control its meaning.
		Mixer* mixer = Engine::mixer();
		buildGraph(mixer);
		VcaGroup* group = makeGroup(mixer, kMembers);
		QVERIFY(group != nullptr);
		std::vector<float> before;
		for (int member : kMembers)
		{
			before.push_back(mixer->mixerChannel(member)->m_volumeModel.value());
		}
		group->vcaModel()->setValue(0.7f);
		group->vcaModel()->setValue(1.0f);
		for (std::size_t i = 0; i < kMembers.size(); ++i)
		{
			QVERIFY2(bitEqual(before[i], mixer->mixerChannel(kMembers[i])->m_volumeModel.value()),
				"the real implementation did not restore a member fader exactly");
		}
		evidence(QStringLiteral("naive_lost_members=%1 real_lost_members=0").arg(broken));
	}

	//! The measured claim: each member's *actual* rendered level drops by the
	//! fader's delta, exactly, and the members keep their relative balance.
	void actualDbDeltaEqualsTheVcaDelta()
	{
		Mixer* mixer = Engine::mixer();
		buildGraph(mixer);
		VcaGroup* group = makeGroup(mixer, kMembers);
		QVERIFY(group != nullptr);

		std::vector<float> memberPeaksBefore;
		std::vector<float> memberPeaksAfter;
		std::vector<float> memberDeltas;
		int nonMembersChecked = 0;

		for (int ch = 1; ch < kChannels; ++ch)
		{
			// One channel at a time, so the delta measured is that channel's.
			group->vcaModel()->setValue(1.0f);
			const QByteArray before = renderFeed(mixer, {ch}, kPeriods);
			const float peakBefore = peakOf(before);
			QVERIFY2(peakBefore > 1.0e-6f, "a channel rendered silence");

			group->vcaModel()->setValue(kHalf);
			const QByteArray after = renderFeed(mixer, {ch}, kPeriods);
			const float peakAfter = peakOf(after);

			// Exact, not approximate: a member's whole output is one factor,
			// and a non-member is not scaled at all.
			const float expectedFactor = isMember(ch) ? kHalf : 1.0f;
			QVERIFY2(scaleError(before, after, expectedFactor) == 0.0f,
				qPrintable(QStringLiteral("channel %1 is not scaled exactly by %2")
					.arg(ch).arg(static_cast<double>(expectedFactor))));

			const float delta = dB(peakAfter) - dB(peakBefore);
			if (isMember(ch))
			{
				QVERIFY2(std::abs(delta - dB(kHalf)) < kToleranceDb,
					qPrintable(QStringLiteral("member %1 delta %2 dB, expected %3 dB")
						.arg(ch).arg(static_cast<double>(delta))
						.arg(static_cast<double>(dB(kHalf)))));
				memberPeaksBefore.push_back(peakBefore);
				memberPeaksAfter.push_back(peakAfter);
				memberDeltas.push_back(delta);
			}
			else
			{
				QVERIFY2(std::abs(delta) < kToleranceDb,
					qPrintable(QStringLiteral("non-member %1 moved by %2 dB")
						.arg(ch).arg(static_cast<double>(delta))));
				++nonMembersChecked;
			}
		}
		QCOMPARE(memberDeltas.size(), kMembers.size());

		// Relative balance: every member's own factor is the same, so the
		// ratios between members are unchanged.
		for (std::size_t i = 1; i < memberDeltas.size(); ++i)
		{
			const float ratioBefore = memberPeaksBefore[0] / memberPeaksBefore[i];
			const float ratioAfter = memberPeaksAfter[0] / memberPeaksAfter[i];
			QVERIFY2(std::abs(ratioAfter / ratioBefore - 1.0f) < 1.0e-6f,
				qPrintable(QStringLiteral("relative balance of members changed: %1 vs %2")
					.arg(static_cast<double>(ratioBefore))
					.arg(static_cast<double>(ratioAfter))));
		}

		evidence(QStringLiteral(
			"db_delta vca=%1 member[2]=%2 member[4]=%3 member[6]=%4 non_members_checked=%5"
			" balance_unchanged=1 exact_scaling=1")
			.arg(static_cast<double>(dB(kHalf)))
			.arg(static_cast<double>(memberDeltas[0]))
			.arg(static_cast<double>(memberDeltas[1]))
			.arg(static_cast<double>(memberDeltas[2]))
			.arg(nonMembersChecked));
	}

	// -- 2. behaviour-preserving by default ----------------------------------

	//! No group: the render is deterministic and the graph is live. The
	//! pre-change comparison itself is the CLI render pair in the report; what
	//! this harness can prove repeatably is that the new code is inert.
	void ungroupedRenderIsDeterministicAndLive()
	{
		Mixer* mixer = Engine::mixer();
		buildGraph(mixer);
		QCOMPARE(mixer->vcaGroups().size(), static_cast<std::size_t>(0));

		const QByteArray first = renderFeed(mixer, kAllChannels, kPeriods);
		const QByteArray second = renderFeed(mixer, kAllChannels, kPeriods);
		QCOMPARE(second, first);
		QVERIFY2(first != QByteArray(first.size(), '\0'), "the render is silent");
		for (int ch = 0; ch < kChannels; ++ch)
		{
			QVERIFY(bitEqual(mixer->mixerChannel(ch)->vcaGain(), 1.0f));
		}
		evidence(QStringLiteral("ungrouped frames=%1 sha256=%2")
			.arg(frameCount(first)).arg(sha256Hex(first)));
	}

	//! A group at unity must be indistinguishable from no group at all, and
	//! moving the fader must change the render: the equality above is not a
	//! dead harness. Only the group's members are fed, so the whole render is
	//! scaled by exactly the group's factor.
	void groupAtUnityRendersIdenticallyAndMovedItDoesNot()
	{
		Mixer* mixer = Engine::mixer();
		buildGraph(mixer);
		const QByteArray noGroup = renderFeed(mixer, kMembers, kPeriods);

		VcaGroup* group = makeGroup(mixer, kMembers);
		QVERIFY(group != nullptr);
		const QByteArray groupAtUnity = renderFeed(mixer, kMembers, kPeriods);
		QCOMPARE(groupAtUnity, noGroup);

		group->vcaModel()->setValue(0.5f);
		const QByteArray moved = renderFeed(mixer, kMembers, kPeriods);
		// scaleError(a, b, f) is max|b - f*a|, so the arguments are ordered to
		// ask "is every sample of `moved` exactly half of `noGroup`?".
		const float worst = scaleError(noGroup, moved, kHalf);
		QVERIFY2(moved != noGroup, "the sensitivity control did not differ");
		evidence(QStringLiteral("render_spread sizes=%1/%2 moved_sha256=%3 max_delta_vs_half=%4")
			.arg(moved.size()).arg(noGroup.size()).arg(sha256Hex(moved))
			.arg(static_cast<double>(worst)));
		QVERIFY2(worst == 0.0f,
			qPrintable(QStringLiteral("only the group's own -6.02 dB should separate the two"
				" renders; max|moved - 0.5*noGroup| = %1").arg(static_cast<double>(worst))));

		group->vcaModel()->setValue(1.0f);
		QCOMPARE(renderFeed(mixer, kMembers, kPeriods), noGroup);

		evidence(QStringLiteral(
			"render no_group=%1 group_at_unity=%2 group_at_-6dB=%3 sensitivity_control=differs")
			.arg(sha256Hex(noGroup), sha256Hex(groupAtUnity), sha256Hex(moved)));
	}

	// -- 3. mute / solo linking ----------------------------------------------

	//! M1: group mute silences every member, reversibly, without writing a
	//! member's own mute model.
	void mutingTheGroupSilencesEveryMemberReversibly()
	{
		Mixer* mixer = Engine::mixer();
		buildGraph(mixer);
		VcaGroup* group = makeGroup(mixer, kMembers);
		QVERIFY(group != nullptr);

		const QByteArray open = renderFeed(mixer, kMembers, kPeriods);
		QVERIFY2(peakOf(open) > 1.0e-6f, "the group rendered silence before muting");

		group->muteModel()->setValue(true);
		for (int member : kMembers)
		{
			QVERIFY(bitEqual(mixer->mixerChannel(member)->vcaGain(), 0.0f));
			QVERIFY2(mixer->mixerChannel(member)->m_muteModel.value() == false,
				"group mute wrote a member's own mute model");
		}
		QVERIFY2(peakOf(renderFeed(mixer, kMembers, kPeriods)) == 0.0f,
			"a muted group still produced audio");
		// A channel outside the group is unaffected.
		QVERIFY(bitEqual(mixer->mixerChannel(1)->vcaGain(), 1.0f));
		QVERIFY2(peakOf(renderFeed(mixer, {1}, kPeriods)) > 1.0e-6f,
			"muting the group silenced a channel outside it");

		// M2: the mute is a gain of zero, not the channel mute machinery, so
		// solo cannot lift it.
		mixer->toggledSolo();
		mixer->mixerChannel(4)->m_soloModel.setValue(true);
		mixer->toggledSolo();
		QVERIFY2(peakOf(renderFeed(mixer, kMembers, kPeriods)) == 0.0f,
			"a soloed member made its muted group audible");
		mixer->mixerChannel(4)->m_soloModel.setValue(false);
		mixer->toggledSolo();

		group->muteModel()->setValue(false);
		QCOMPARE(renderFeed(mixer, kMembers, kPeriods), open);
		evidence(QStringLiteral(
			"group_mute members_silent=1 member_models_untouched=1 solo_cannot_lift=1 restored=%1")
			.arg(sha256Hex(open)));
	}

	//! S1: soloing a member soloes the whole group.
	void soloingAMemberSoloesItsGroup()
	{
		Mixer* mixer = Engine::mixer();
		buildGraph(mixer);
		VcaGroup* group = makeGroup(mixer, kMembers);
		QVERIFY(group != nullptr);
		mixer->toggledSolo(); // normalise the mixer's single-solo latch

		mixer->mixerChannel(4)->m_soloModel.setValue(true);
		mixer->toggledSolo();

		for (int member : kMembers)
		{
			QVERIFY2(mixer->mixerChannel(member)->m_muteModel.value() == false,
				qPrintable(QStringLiteral("member %1 is not audible while the group is soloed")
					.arg(member)));
		}
		QVERIFY2(mixer->mixerChannel(1)->m_muteModel.value() == true,
			"a non-member stayed audible while the group was soloed");
		QVERIFY2(group->soloModel()->value() == false,
			"soloing a member wrote the group's own solo flag");

		QVERIFY2(peakOf(renderFeed(mixer, {2}, kPeriods)) > 1.0e-6f,
			"the member that was not soloed is not audible");
		QVERIFY2(peakOf(renderFeed(mixer, {4}, kPeriods)) > 1.0e-6f,
			"the soloed member is not audible");
		QVERIFY2(peakOf(renderFeed(mixer, {1}, kPeriods)) == 0.0f,
			"a non-member is audible");
		evidence(QStringLiteral("solo_member soloed=4 group_audible=2,4,6 non_member_silent=1"));
	}

	//! S2/S3: soloing the group makes exactly its members audible.
	void soloingTheGroupMakesExactlyItsMembersAudible()
	{
		Mixer* mixer = Engine::mixer();
		buildGraph(mixer);
		VcaGroup* group = makeGroup(mixer, kMembers);
		QVERIFY(group != nullptr);
		mixer->toggledSolo();

		group->soloModel()->setValue(true);
		for (int member : kMembers)
		{
			QVERIFY2(mixer->mixerChannel(member)->m_muteModel.value() == false,
				qPrintable(QStringLiteral("member %1 is not audible").arg(member)));
		}
		for (int ch = 1; ch < kChannels; ++ch)
		{
			if (!isMember(ch))
			{
				QVERIFY2(mixer->mixerChannel(ch)->m_muteModel.value() == true,
					qPrintable(QStringLiteral("non-member %1 is audible").arg(ch)));
			}
		}
		QVERIFY2(peakOf(renderFeed(mixer, kMembers, kPeriods)) > 1.0e-6f,
			"the soloed group rendered silence");
		QVERIFY2(peakOf(renderFeed(mixer, {3}, kPeriods)) == 0.0f,
			"a non-member is audible while the group is soloed");

		group->soloModel()->setValue(false);
		for (int ch = 1; ch < kChannels; ++ch)
		{
			QVERIFY2(mixer->mixerChannel(ch)->m_muteModel.value() == false,
				qPrintable(QStringLiteral("channel %1 stayed muted after the solo").arg(ch)));
		}
		evidence(QStringLiteral("solo_group members_audible=2,4,6 others_muted=1 restored=1"));
	}

	// -- 4. persistence -------------------------------------------------------

	//! The grouping, the fader value and both flags survive save/reload, and the
	//! reloaded project renders byte-identically.
	void groupingSurvivesSaveAndReload()
	{
		Mixer* mixer = Engine::mixer();
		buildGraph(mixer);
		VcaGroup* drums = makeGroup(mixer, kMembers, QStringLiteral("Drums"));
		QVERIFY(drums != nullptr);
		QVERIFY(drums->addMember(7));
		drums->vcaModel()->setValue(0.6f);
		VcaGroup* still = mixer->createVcaGroup(QStringLiteral("Still-muted"));
		QVERIFY(still != nullptr);
		QVERIFY(still->addMember(3));
		still->muteModel()->setValue(true);

		const QByteArray beforeSave = renderFeed(mixer, kAllChannels, kPeriods);
		QVERIFY2(peakOf(beforeSave) > 1.0e-6f, "the reference render is silent");
		const QString stateBefore = stateOf(mixer);
		evidence(QStringLiteral("state_before_save %1").arg(stateBefore));

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("mixer"));
		doc.appendChild(root);
		mixer->saveSettings(doc, root);
		const QString xml = doc.toString();
		QVERIFY2(xml.contains(QStringLiteral("<vcagroup")), qPrintable(xml.left(400)));
		QVERIFY2(xml.contains(QStringLiteral("name=\"Drums\"")), qPrintable(xml.left(1200)));
		QVERIFY2(xml.contains(QStringLiteral("<member channel=\"7\"/>")), qPrintable(xml.left(1200)));
		// The attribute form prints the float at full precision ("0.60000002"),
		// so assert the value, not its spelling.
		QDomDocument parseBack;
		QVERIFY2(parseBack.setContent(xml), "saveSettings produced invalid XML");
		const QDomElement savedGroup =
			parseBack.documentElement().firstChildElement(QStringLiteral("vcagroup"));
		QVERIFY(!savedGroup.isNull());
		QVERIFY2(std::abs(savedGroup.attribute(QStringLiteral("vca")).toFloat() - 0.6f) < 1.0e-6f,
			qPrintable(QStringLiteral("saved vca=%1").arg(savedGroup.attribute(QStringLiteral("vca")))));

		mixer->loadSettings(root);
		QCOMPARE(mixer->vcaGroups().size(), static_cast<std::size_t>(2));
		VcaGroup* loaded = mixer->vcaGroup(kGroupId);
		QVERIFY(loaded != nullptr);
		QCOMPARE(loaded->name(), QStringLiteral("Drums"));
		QVERIFY(bitEqual(loaded->vcaModel()->value(), 0.6f));
		QCOMPARE(loaded->members().size(), static_cast<std::size_t>(4));
		for (int member : {2, 4, 6, 7})
		{
			QVERIFY2(bitEqual(mixer->mixerChannel(member)->vcaGain(), 0.6f),
				qPrintable(QStringLiteral("member %1 lost its gain across the round trip")
					.arg(member)));
		}
		QVERIFY(loaded->muteModel()->value() == false);
		VcaGroup* loadedMuted = mixer->vcaGroup(1);
		QVERIFY(loadedMuted != nullptr);
		QVERIFY2(loadedMuted->muteModel()->value() == true, "the mute flag did not round trip");
		QVERIFY(bitEqual(mixer->mixerChannel(3)->vcaGain(), 0.0f));
		QVERIFY(bitEqual(mixer->mixerChannel(1)->vcaGain(), 1.0f));

		// The persisted state is what must round-trip: dump the whole graph
		// (every channel's fader, mute, solo, gain and sends, plus both groups)
		// and require it to be identical, then re-render to prove the reloaded
		// mixer still plays.
		const QString stateAfter = stateOf(mixer);
		evidence(QStringLiteral("state_after_reload %1").arg(stateAfter));
		QCOMPARE(stateAfter, stateBefore);
		const QByteArray afterLoad = renderFeed(mixer, kAllChannels, kPeriods);
		QVERIFY2(peakOf(afterLoad) > 1.0e-6f, "the reloaded project rendered silence");
		QCOMPARE(afterLoad, beforeSave);
		evidence(QStringLiteral(
			"save_load groups=2 members=2,4,6,7 vca=0.6 mute_roundtrip=1 state_identical=1"
			" audio_identical=1 sha256=%1").arg(sha256Hex(beforeSave)));
	}

	//! A pre-#622 project has no <vcagroup>; it must load to no groups, publish
	//! unity everywhere, gain no group element when saved again, and render
	//! deterministically. (The pre-change *behaviour* comparison is the CLI
	//! render pair in docs/VCA-GROUPS.md; this pins the new code's inertness.)
	void legacyProjectLoadsAndRendersUnchanged()
	{
		Mixer* mixer = Engine::mixer();

		QDomDocument legacyDoc;
		QDomElement legacy;
		QVERIFY2(parseMixer(kLegacyMixerXml, legacyDoc, legacy), "the legacy fixture is invalid");
		mixer->loadSettings(legacy);
		QCOMPARE(mixer->numChannels(), static_cast<mix_ch_t>(3));
		QCOMPARE(mixer->vcaGroups().size(), static_cast<std::size_t>(0));
		for (int ch = 0; ch < 3; ++ch)
		{
			QVERIFY(bitEqual(mixer->mixerChannel(ch)->vcaGain(), 1.0f));
		}
		const QByteArray loaded = renderFeed(mixer, {1, 2}, kPeriods);
		QVERIFY2(peakOf(loaded) > 1.0e-6f, "the legacy project rendered silence");

		// The mixer as the project file described it: three channels, the
		// legacy volumes and send amounts, no group state at all.
		QDomDocument savedDoc;
		QDomElement savedRoot = savedDoc.createElement(QStringLiteral("mixer"));
		savedDoc.appendChild(savedRoot);
		mixer->saveSettings(savedDoc, savedRoot);
		const QString resaved = savedDoc.toString();
		QVERIFY2(!resaved.contains(QStringLiteral("vcagroup")),
			"an old project grew a <vcagroup> element when saved again");
		QVERIFY2(resaved.contains(QStringLiteral("num=\"1\"")), qPrintable(resaved.left(400)));
		evidence(QStringLiteral("legacy_project channels=3 groups=0 unity_gains=1"
			" no_vcagroup_on_resave=1 sha256=%1").arg(sha256Hex(loaded)));

		// The same file loaded again, from a clean mixer, renders identically:
		// nothing in the group code is order- or state-dependent.
		mixer->clear();
		QCOMPARE(mixer->vcaGroups().size(), static_cast<std::size_t>(0));
		mixer->loadSettings(legacy);
		QCOMPARE(mixer->numChannels(), static_cast<mix_ch_t>(3));
		for (int ch = 0; ch < 3; ++ch)
		{
			QVERIFY(bitEqual(mixer->mixerChannel(ch)->vcaGain(), 1.0f));
		}
		const QByteArray reloaded = renderFeed(mixer, {1, 2}, kPeriods);
		QCOMPARE(reloaded, loaded);
	}

	// -- 5. the realtime contract --------------------------------------------

	//! The multiply lives on the audio path, so it must not allocate. A whole
	//! period is driven through Mixer::masterMix(), so the measurement covers
	//! the real path (dependency bookkeeping and the job queue included) and
	//! leaves the mixer's scheduling state exactly as a rendered period does.
	void theVcaMultiplyOnTheAudioPathAllocatesNothing()
	{
		Mixer* mixer = Engine::mixer();
		buildGraph(mixer);
		VcaGroup* group = makeGroup(mixer, kMembers);
		QVERIFY(group != nullptr);
		group->vcaModel()->setValue(0.6f);

		MixerChannel* channel = mixer->mixerChannel(2);
		QVERIFY(bitEqual(channel->vcaGain(), 0.6f));

		// Every buffer is allocated here, outside the measured window, so the
		// count is the mixer's own allocations.
		const f_cnt_t fpp = framesPerPeriod();
		std::vector<SampleFrame> input(fpp);
		std::vector<SampleFrame> masterOut(fpp);
		const auto period = [&] { renderPeriodNoAlloc(mixer, kAllChannels, input, masterOut); };

		// Warm-up: the job queue and the dependency counters are touched for
		// the first time here; only steady-state blocks must be allocation-free.
		for (int i = 0; i < 4; ++i) { period(); }

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		for (int i = 0; i < 8; ++i) { period(); }
		lmms::test::tlCountAllocations = false;

		QVERIFY2(lmms::test::tlAllocationCount == 0,
			qPrintable(QStringLiteral("the audio path allocated %1 times")
				.arg(lmms::test::tlAllocationCount)));
		evidence(QStringLiteral("rt_allocation periods=8 allocations=%1 gain_published=0.6")
			.arg(lmms::test::tlAllocationCount));
	}

	//! Harness control: the graph's own faders and send amounts must actually
	//! reach the rendered audio, or every "unchanged" claim above would be
	//! vacuous. Each measurement gets a freshly built graph with its value set
	//! *before* any render: a setValue() after a render does not reach the audio
	//! in this direct-drive harness (the frozen sample-exact ramp described in
	//! renderFeed()'s comment), which is a property of the harness, not of #622.
	void fadersAndSendAmountsReachTheRender()
	{
		Mixer* mixer = Engine::mixer();

		buildGraph(mixer);
		const QByteArray baseline = renderFeed(mixer, {1}, kPeriods);
		const float peakBaseline = peakOf(baseline);
		QVERIFY2(peakBaseline > 1.0e-6f, "channel 1 rendered silence");

		buildGraph(mixer);
		mixer->channelSendModel(1, 0)->setValue(0.4f);
		const QByteArray halfSend = renderFeed(mixer, {1}, kPeriods);
		const float sendWorst = scaleError(baseline, halfSend, 0.5f);

		buildGraph(mixer);
		mixer->mixerChannel(1)->m_volumeModel.setValue(0.625f);
		const QByteArray halfFader = renderFeed(mixer, {1}, kPeriods);
		const float faderWorst = scaleError(baseline, halfFader, 0.5f);

		evidence(QStringLiteral("harness_control peak=%1 send_half_max_err=%2 fader_half_max_err=%3")
			.arg(static_cast<double>(peakBaseline))
			.arg(static_cast<double>(sendWorst))
			.arg(static_cast<double>(faderWorst)));
		QVERIFY2(sendWorst == 0.0f,
			"halving a send amount did not halve the render exactly");
		QVERIFY2(faderWorst == 0.0f,
			"halving a channel fader did not halve the render exactly");
	}

	//! Control: a solo render's level must not depend on what was rendered
	//! before it. It did, until renderFeed() started settling the sample-exact
	//! machinery (see its comment) -- this pins that, so a future edit cannot
	//! quietly reintroduce harness-dependent levels under the VCA tests above.
	void levelsDoNotDependOnThePrecedingRender()
	{
		Mixer* mixer = Engine::mixer();
		std::vector<float> fresh(kChannels, 0.0f);
		std::vector<float> afterFull(kChannels, 0.0f);

		buildGraph(mixer);
		QCOMPARE(mixer->vcaGroups().size(), static_cast<std::size_t>(0));
		for (int ch = 1; ch < kChannels; ++ch)
		{
			fresh[ch] = peakOf(renderFeed(mixer, {ch}, kPeriods));
		}

		buildGraph(mixer);
		QVERIFY2(peakOf(renderFeed(mixer, kAllChannels, kPeriods)) > 1.0e-6f,
			"the all-channel render is silent");
		for (int ch = 1; ch < kChannels; ++ch)
		{
			afterFull[ch] = peakOf(renderFeed(mixer, {ch}, kPeriods));
		}

		QString ratios;
		for (int ch = 1; ch < kChannels; ++ch)
		{
			QVERIFY2(fresh[ch] > 0.0f, "a solo render was silent");
			const float ratio = afterFull[ch] / fresh[ch];
			ratios += QStringLiteral(" ch%1=%2").arg(ch)
				.arg(static_cast<double>(ratio), 0, 'g', 6);
			QVERIFY2(bitEqual(ratio, 1.0f),
				qPrintable(QStringLiteral("channel %1's solo peak depends on the preceding"
					" render: ratio %2").arg(ch).arg(static_cast<double>(ratio))));
		}
		evidence(QStringLiteral("levels_independent_of_preceding_render%1").arg(ratios));
	}

	// -- 6. membership rules -------------------------------------------------
	//!
	void membershipIsSingleValuedAndSurvivesChannelEdits()
	{
		Mixer* mixer = Engine::mixer();
		buildGraph(mixer);
		VcaGroup* a = mixer->createVcaGroup(QStringLiteral("A"));
		VcaGroup* b = mixer->createVcaGroup(QStringLiteral("B"));
		QVERIFY(a != nullptr);
		QVERIFY(b != nullptr);

		QVERIFY2(!a->addMember(0), "master must not join a group");
		QVERIFY(a->addMember(2));
		QVERIFY2(!a->addMember(2), "adding a member twice must be a no-op");
		QVERIFY2(!b->addMember(2), "a channel must not join a second group");
		QVERIFY(mixer->vcaGroupForChannel(2) == a);
		QVERIFY(mixer->vcaGroupForChannel(3) == nullptr);

		a->vcaModel()->setValue(0.5f);
		QVERIFY(bitEqual(mixer->mixerChannel(2)->vcaGain(), 0.5f));
		QVERIFY(a->removeMember(2));
		QVERIFY2(bitEqual(mixer->mixerChannel(2)->vcaGain(), 1.0f),
			"a channel that left the group is still scaled");

		QVERIFY(a->addMember(2));
		QVERIFY(bitEqual(mixer->mixerChannel(2)->vcaGain(), 0.5f));
		QVERIFY(mixer->deleteVcaGroup(a->id()));
		QVERIFY(bitEqual(mixer->mixerChannel(2)->vcaGain(), 1.0f));
		QVERIFY(mixer->vcaGroupForChannel(2) == nullptr);

		// Deleting a channel moves the survivors' membership with their index.
		VcaGroup* c = mixer->createVcaGroup(QStringLiteral("C"));
		QVERIFY(c != nullptr);
		QVERIFY(c->addMember(4));
		QVERIFY(c->addMember(6));
		mixer->deleteChannel(4);
		QCOMPARE(c->members().size(), static_cast<std::size_t>(1));
		QCOMPARE(c->members().front(), static_cast<mix_ch_t>(5));
		QVERIFY(bitEqual(mixer->mixerChannel(5)->vcaGain(), c->gain()));
		evidence(QStringLiteral("membership master_refused=1 duplicate_refused=1"
			" second_group_refused=1 index_shift_after_delete=6->5"));
	}
};

QTEST_GUILESS_MAIN(VcaGroupTest)
#include "VcaGroupTest.moc"
