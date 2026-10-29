/*
 * MixerStripTest.cpp - the strip's own stages and meters, and muting or soloing many channels at once
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

/*! 0.5.0 L08: trim, polarity, mono and the pan modes change exactly the samples they should and
 *  nothing at their defaults (MixerStrip.cpp, driven directly); the meters hold a pre-fader peak, an
 *  RMS and a latched clip light that mixer.clear_clips clears; mixer.set_strip is one undo step and
 *  is saved only when set; solo safe keeps a channel playing through another's solo; mixer.set_many
 *  unmutes every channel in one step; mixer.solo_mode's additive solo keeps every soloed channel on
 *  (MX-029); the pre-fader meter mode's peak ignores the fader (MX-129); mixer.rename_channel is one undo step
 *  (theme 13). */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "ControlRegistry.h"
#include "ControlMixerSupport.h"
#include "ControlVocabulary.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomationClip.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerPostInserts.h"
#include "MixerStrip.h"
#include "PhaseDMixerTestSupport.h"
#include "SampleFrame.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

MixerChannel* channel(int index) { return Engine::mixer()->mixerChannel(index); }

QString ch(int index) { return control::channelIdOf(channel(index)); }

void verifyLastUndoTransaction(const QString& command)
{
	const QJsonArray records = run(QStringLiteral("control.transactions")).result.value(QStringLiteral("transactions")).toArray();
	QVERIFY(!records.isEmpty());
	if (records.isEmpty()) { return; }
	const QJsonObject record = records.last().toObject();
	QCOMPARE(record.value(QStringLiteral("command")).toString(), command);
	QVERIFY(!record.value(QStringLiteral("before")).toObject().isEmpty());
	QCOMPARE(record.value(QStringLiteral("inverse")).toObject().value(QStringLiteral("op")).toString(), QStringLiteral("control.undo"));
	QVERIFY(record.value(QStringLiteral("reversible")).toBool());
}

//! Channel @a index's buffer filled with (left, right), @a frames frames.
void fill(int index, float left, float right, int frames = 8)
{
	for (int f = 0; f < frames; ++f) { channel(index)->m_buffer[f] = SampleFrame(left, right); }
}

//! Let @a c's pan models run out their change ramp, as the period after a change does, so a stage reads the value
//! the test set (a model's valueBuffer() ramps from the old value to the new across the first period).
void settle(MixerChannel* c)
{
	for (int period = 0; period < 2; ++period)
	{
		AutomatableModel::incrementPeriodCounter();
		c->m_panModel.valueBuffer();
		c->m_panRightModel.valueBuffer();
	}
	AutomatableModel::incrementPeriodCounter();
}

} // namespace

class MixerStripTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test reads and writes channel buffers itself
		while (Engine::mixer()->numChannels() <= 3) { Engine::mixer()->createChannel(); }
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theDefaultsChangeNothing()
	{
		fill(1, 0.3f, -0.2f);
		mixerstrip::applyInput(*channel(1), 8);
		mixerstrip::applyOutput(*channel(1), 8);
		QVERIFY(!mixerstrip::applyPanMode(*channel(1), 8));  // balance: the inherited stage pans
		QCOMPARE(channel(1)->m_buffer[0][0], 0.3f);
		QCOMPARE(channel(1)->m_buffer[0][1], -0.2f);
	}

	void trimPolarityAndMono()
	{
		QVERIFY(run(QStringLiteral("mixer.set_strip"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("trim_db"), -6.0206},
			{QStringLiteral("polarity_right"), true}}).ok);
		fill(1, 0.4f, 0.4f);
		mixerstrip::applyInput(*channel(1), 8);
		QVERIFY(std::abs(channel(1)->m_buffer[0][0] - 0.2f) < 1e-4f);  // -6 dB halves
		mixerstrip::applyOutput(*channel(1), 8);
		QVERIFY(std::abs(channel(1)->m_buffer[0][1] + 0.2f) < 1e-4f);  // the right side inverted
		QVERIFY(run(QStringLiteral("mixer.set_strip"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("mono"), true},
			{QStringLiteral("polarity_right"), false}}).ok);
		fill(1, 1.f, 0.f);
		mixerstrip::applyOutput(*channel(1), 8);
		QCOMPARE(channel(1)->m_buffer[0][0], 0.5f);
		QCOMPARE(channel(1)->m_buffer[0][1], 0.5f);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!channel(1)->m_monoModel.value());
		QVERIFY(channel(1)->m_polarityRightModel.value());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(channel(1)->m_trimModel.value(), 0.f);
	}

	void thePanModes()
	{
		QVERIFY(run(QStringLiteral("mixer.set_strip"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("pan_mode"), QStringLiteral("dual")}}).ok);
		QCOMPARE(channel(2)->m_panModel.value(), -1.f);  // dual from centred pans: hard left and hard right
		QCOMPARE(channel(2)->m_panRightModel.value(), 1.f);
		fill(2, 0.6f, 0.2f);
		settle(channel(2));
		QVERIFY(mixerstrip::applyPanMode(*channel(2), 8));
		QVERIFY(std::abs(channel(2)->m_buffer[0][0] - 0.6f) < 1e-5f && std::abs(channel(2)->m_buffer[0][1] - 0.2f) < 1e-5f);
		QVERIFY(run(QStringLiteral("mixer.set_strip"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("pan_mode"), QStringLiteral("width")},
			{QStringLiteral("width"), 0.0}, {QStringLiteral("pan"), 0.0}}).ok);
		fill(2, 0.6f, 0.2f);
		settle(channel(2));
		mixerstrip::applyPanMode(*channel(2), 8);
		QVERIFY(std::abs(channel(2)->m_buffer[0][0] - 0.4f) < 1e-5f && std::abs(channel(2)->m_buffer[0][1] - 0.4f) < 1e-5f);  // width 0: mono
		QCOMPARE(run(QStringLiteral("mixer.set_strip"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("width"), 3.0}}).errorKind,
			ControlErrorKind::InvalidArgs);
		run(QStringLiteral("mixer.set_strip"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("pan_mode"), QStringLiteral("balance")},
			{QStringLiteral("width"), 1.0}, {QStringLiteral("pan"), 0.0}, {QStringLiteral("pan_right"), 0.0}});
	}

	void rejectsNonFiniteStripSettings()
	{
		const double nan = std::numeric_limits<double>::quiet_NaN();
		QCOMPARE(run(QStringLiteral("mixer.set_strip"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("trim_db"), nan}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("mixer.set_strip"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("pan"), nan}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("mixer.set_strip"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("pan_right"), nan}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("mixer.set_strip"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("width"), nan}}).errorKind,
			ControlErrorKind::InvalidArgs);
	}

	void theMetersAndTheClipLight()
	{
		fill(3, 0.5f, -0.9f, 4);
		mixerstrip::updateMeters(*channel(3), channel(3)->m_buffer, 2.f, 4);
		QCOMPARE(channel(3)->m_prePeakRight.get(), 0.9f);           // before the fader
		QVERIFY(std::abs(channel(3)->m_rmsLeft.get() - 1.f) < 1e-5f); // after it: 0.5 x 2
		QVERIFY(channel(3)->m_clipped.load());                 // 0.9 x 2 is over full scale
		const QJsonObject meters = run(QStringLiteral("mixer.strip_get"), {{QStringLiteral("channel"), ch(3)}}).result.value(QStringLiteral("meters")).toObject();
		QVERIFY(meters.value(QStringLiteral("clipped")).toBool());
		QVERIFY(run(QStringLiteral("mixer.clear_clips")).ok);
		QVERIFY(!channel(3)->m_clipped.load());
	}

	void postFaderSendMeterReadsTheBakedSignal()
	{
		QVERIFY(control::findMixerRoute(3, 2) == nullptr);
		MixerRoute* route = Engine::mixer()->createChannelSend(3, 2, 0.5f);
		QVERIFY(route != nullptr);
		MixerChannel* sender = channel(3);
		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		sender->m_volumeModel.setValue(0.5f);
		for (f_cnt_t f = 0; f < frames; ++f) { sender->m_buffer[f] = SampleFrame(0.4f, 0.2f); }
		sender->updatePostFaderBuffer(0.5f, frames);
		auto* gain = new partd::FixedGainEffect(&sender->m_postInserts.chain(), 2.f, 2.f);
		sender->m_postInserts.chain().appendEffect(gain);
		const float meterGain = sender->m_postInserts.process(*sender, 0.5f, frames);
		const float bakedLeft = sender->m_buffer[0][0];
		mixerstrip::updateMeters(*sender, sender->m_buffer, 1.f, frames);
		float outputPeak = 0.f;
		for (f_cnt_t f = 0; f < frames; ++f) { outputPeak = std::max(outputPeak, std::abs(sender->m_buffer[f][0])); }
		const auto level = mixerstrip::sendLevel(*route);
		const float expected = outputPeak * route->amount()->value();
		sender->m_postInserts.chain().removeEffect(gain);
		delete gain;
		sender->m_postInserts.process(*sender, 0.5f, frames);
		sender->m_volumeModel.setValue(1.f);
		Engine::mixer()->deleteChannelSend(route);
		QCOMPARE(meterGain, 1.f);
		QVERIFY(bakedLeft > 0.4f);  // the after-fader gain increases the baked sample
		QVERIFY(std::abs(level[0] - expected) < 1e-5f);  // post-fader output peak x send amount
	}

	void stripSettingsAreSavedOnlyWhenSet()
	{
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		doc.appendChild(root);  // in the document, so its text holds what was saved
		Engine::mixer()->saveState(doc, root);
		QVERIFY(!doc.toString().contains(QStringLiteral("solosafe")));
		QVERIFY(run(QStringLiteral("mixer.set_strip"), {{QStringLiteral("channel"), ch(3)}, {QStringLiteral("solo_safe"), true},
			{QStringLiteral("note"), QStringLiteral("lead vox")}}).ok);
		QDomDocument second;
		QDomElement root2 = second.createElement(QStringLiteral("root"));
		second.appendChild(root2);
		const QDomElement saved = Engine::mixer()->saveState(second, root2);
		QVERIFY(second.toString().contains(QStringLiteral("lead vox")));
		channel(3)->m_note.clear();
		channel(3)->m_soloSafeModel.setValue(false);
		Engine::mixer()->restoreState(saved);
		QCOMPARE(channel(3)->m_note, QStringLiteral("lead vox"));
		QVERIFY(channel(3)->m_soloSafeModel.value());
	}

	void soloSafeAndSetMany()
	{
		channel(3)->m_soloSafeModel.setValue(true);
		channel(1)->m_soloModel.setValue(true);  // soloing channel 1 mutes the others - but not the safe one
		QVERIFY(!channel(3)->m_muteModel.value());
		QVERIFY(channel(2)->m_muteModel.value());
		QVERIFY(run(QStringLiteral("mixer.set_many"), {{QStringLiteral("all"), true}, {QStringLiteral("soloed"), false}}).ok);
		QVERIFY(run(QStringLiteral("mixer.set_many"), {{QStringLiteral("all"), true}, {QStringLiteral("muted"), false}}).ok);
		for (int i = 1; i <= 3; ++i) { QVERIFY(!channel(i)->m_muteModel.value() && !channel(i)->m_soloModel.value()); }
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the unmute is one step
		QVERIFY(run(QStringLiteral("mixer.set_many"), {{QStringLiteral("channels"), QJsonArray{ch(1), ch(2)}}, {QStringLiteral("muted"), true}}).ok);
		QVERIFY(channel(1)->m_muteModel.value() && channel(2)->m_muteModel.value());
		run(QStringLiteral("mixer.set_many"), {{QStringLiteral("all"), true}, {QStringLiteral("muted"), false}});
		channel(3)->m_soloSafeModel.setValue(false);
	}

	void setManyNoOpDoesNotEnterUndoHistory()
	{
		channel(1)->m_muteModel.setValue(false);
		const ControlResult beforeDepth = run(QStringLiteral("control.undo_depth"));
		QVERIFY(beforeDepth.ok);
		const int depth = beforeDepth.result.value(QStringLiteral("depth")).toInt();

		QVERIFY(run(QStringLiteral("mixer.set_many"), {{QStringLiteral("channels"), QJsonArray{ch(1)}},
			{QStringLiteral("muted"), false}}).ok);

		const ControlResult afterDepth = run(QStringLiteral("control.undo_depth"));
		QVERIFY(afterDepth.ok);
		QVERIFY2(afterDepth.result.value(QStringLiteral("depth")).toInt() == depth,
			"a no-op mixer.set_many must leave undo depth unchanged");
	}

	void setStripNoOpDoesNotEnterUndoHistory()
	{
		channel(1)->m_trimModel.setValue(0.f);
		const ControlResult beforeDepth = run(QStringLiteral("control.undo_depth"));
		QVERIFY(beforeDepth.ok);
		const int depth = beforeDepth.result.value(QStringLiteral("depth")).toInt();

		QVERIFY(run(QStringLiteral("mixer.set_strip"), {{QStringLiteral("channel"), ch(1)},
			{QStringLiteral("trim_db"), 0.0}}).ok);

		const ControlResult afterDepth = run(QStringLiteral("control.undo_depth"));
		QVERIFY(afterDepth.ok);
		QVERIFY2(afterDepth.result.value(QStringLiteral("depth")).toInt() == depth,
			"a no-op mixer.set_strip must leave undo depth unchanged");
	}

	void additiveSoloAddsToTheOthers()
	{
		QCOMPARE(run(QStringLiteral("mixer.solo_mode")).result.value(QStringLiteral("mode")).toString(), QStringLiteral("exclusive"));
		QVERIFY(run(QStringLiteral("mixer.solo_mode"), {{QStringLiteral("mode"), QStringLiteral("additive")}}).ok);
		QVERIFY(mixerstrip::additiveSolo());
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the mode change is one undo step
		QVERIFY(!mixerstrip::additiveSolo());
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QVERIFY(mixerstrip::additiveSolo());
		channel(1)->m_soloModel.setValue(true);
		channel(2)->m_soloModel.setValue(true);
		QVERIFY(channel(1)->m_soloModel.value());  // additive: the first solo stays on
		QVERIFY(!channel(1)->m_muteModel.value() && !channel(2)->m_muteModel.value());
		QVERIFY(channel(3)->m_muteModel.value());   // the channel nobody soloed is muted
		run(QStringLiteral("mixer.set_many"), {{QStringLiteral("all"), true}, {QStringLiteral("soloed"), false}});
		QVERIFY(!channel(3)->m_muteModel.value());  // the last solo off unmutes everyone
		QVERIFY(run(QStringLiteral("mixer.solo_mode"), {{QStringLiteral("mode"), QStringLiteral("exclusive")}}).ok);
		QVERIFY(!mixerstrip::additiveSolo());
		QCOMPARE(run(QStringLiteral("mixer.solo_mode"), {{QStringLiteral("mode"), QStringLiteral("loud")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		run(QStringLiteral("mixer.set_many"), {{QStringLiteral("all"), true}, {QStringLiteral("soloed"), false}, {QStringLiteral("muted"), false}});
	}

	//! Deleting the soloed channel gives every other channel back its pre-solo mute (activateSolo saved it), so
	//! nothing stays muted with no solo left to restore it (Mixer::deleteChannel).
	void deletingTheSoloedChannelRestoresTheMutes()
	{
		QVERIFY(!mixerstrip::additiveSolo());
		for (int i = 1; i < Engine::mixer()->numChannels(); ++i) { QVERIFY(!channel(i)->m_soloModel.value()); }
		channel(1)->m_muteModel.setValue(true);   // muted before the solo: stays muted
		channel(2)->m_muteModel.setValue(false);  // audible before the solo: must be audible again
		const int doomed = Engine::mixer()->createChannel();
		channel(doomed)->m_soloModel.setValue(true);
		QVERIFY(channel(1)->m_muteModel.value() && channel(2)->m_muteModel.value());  // the solo mutes the others
		Engine::mixer()->deleteChannel(doomed);
		QVERIFY(channel(1)->m_muteModel.value());
		QVERIFY2(!channel(2)->m_muteModel.value(), "deleting the soloed channel must restore the pre-solo mutes");
		channel(1)->m_muteModel.setValue(false);
		channel(2)->m_muteModel.setValue(false);
	}

	//! Additive solo: deleting the channel the engine names as its last solo keeps the other soloed channel heard
	//! and the unsoloed ones muted (applyAdditiveSolo runs again on the new layout).
	void deletingAnAdditiveSoloKeepsTheOthers()
	{
		QVERIFY(run(QStringLiteral("mixer.solo_mode"), {{QStringLiteral("mode"), QStringLiteral("additive")}}).ok);
		channel(1)->m_muteModel.setValue(false);
		channel(2)->m_muteModel.setValue(true);
		const int first = Engine::mixer()->createChannel();
		const int second = Engine::mixer()->createChannel();
		channel(first)->m_soloModel.setValue(true);   // the lower index is the last solo the engine names
		channel(second)->m_soloModel.setValue(true);
		QVERIFY(!channel(first)->m_muteModel.value() && !channel(second)->m_muteModel.value());
		QVERIFY(channel(1)->m_muteModel.value());
		Engine::mixer()->deleteChannel(first);
		// the survivor moved down into first's slot
		QVERIFY2(channel(first)->m_soloModel.value(), "the other soloed channel must stay soloed");
		QVERIFY2(!channel(first)->m_muteModel.value(), "the other soloed channel must still be heard");
		QVERIFY(channel(1)->m_muteModel.value());  // nobody soloed channel 1
		QVERIFY(channel(2)->m_muteModel.value());
		channel(first)->m_soloModel.setValue(false);
		// clearing the last solo gives every channel back its pre-solo mute (channel 1 audible, channel 2 still muted)
		QVERIFY2(!channel(1)->m_muteModel.value(), "clearing the last additive solo must restore channel 1's pre-solo unmuted state");
		QVERIFY2(channel(2)->m_muteModel.value(), "clearing the last additive solo must keep channel 2's pre-solo mute");
		QVERIFY(run(QStringLiteral("mixer.solo_mode"), {{QStringLiteral("mode"), QStringLiteral("exclusive")}}).ok);
		Engine::mixer()->deleteChannel(first);
		channel(1)->m_muteModel.setValue(false);
		channel(2)->m_muteModel.setValue(false);
	}

	void thePreFaderMeterIsReadOnce()
	{
		channel(3)->m_preMeterLeft.set(0.f);
		fill(3, 0.25f, 0.f, 4);
		mixerstrip::updateMeters(*channel(3), channel(3)->m_buffer, 0.5f, 4);
		QCOMPARE(channel(3)->m_preMeterLeft.get(), 0.25f);  // the fader does not touch it
		QCOMPARE(channel(3)->m_peakLeft >= 0.f, true);
	}

	//! 0.5.0 theme 13: mixer.rename_channel names a channel (the Inspector's in-place name, the strip's own field), one
	//! undo step puts the old name back and its redo the new one; a blank name is refused and a same name is no step.
	void aChannelIsRenamedAndTheUndoPutsTheNameBack()
	{
		const QString before = channel(2)->m_name;
		const ControlResult renamed = run(QStringLiteral("mixer.rename_channel"), {{QStringLiteral("channel"), ch(2)},
			{QStringLiteral("name"), QStringLiteral("  Drum Bus  ")}});
		QVERIFY2(renamed.ok, qPrintable(renamed.errorMessage));
		QCOMPARE(channel(2)->m_name, QStringLiteral("Drum Bus"));  // trimmed
		QCOMPARE(renamed.result.value(QStringLiteral("previous")).toString(), before);
		verifyLastUndoTransaction(QStringLiteral("mixer.rename_channel"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(channel(2)->m_name, before);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(channel(2)->m_name, QStringLiteral("Drum Bus"));
		QCOMPARE(run(QStringLiteral("mixer.rename_channel"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("name"), QStringLiteral("   ")}})
			.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("mixer.rename_channel"), {{QStringLiteral("channel"), QStringLiteral("ch-99999")},
			{QStringLiteral("name"), QStringLiteral("x")}}).errorKind, ControlErrorKind::NotFound);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(channel(2)->m_name, before);
	}

	//! Luna cluster 4279: Mixer::clearChannel resets volume, pan, mute and solo, but a cleared
	//! channel kept its trim, polarity, mono, swap, EQ, pan mode, width, solo safe and
	//! crossfader side - so after mixer.clear() the master still applied them to later audio.
	void clearingAChannelResetsItsStripSettings()
	{
		MixerChannel* master = channel(0);
		master->m_trimModel.setValue(-6.0206f);
		master->m_polarityLeftModel.setValue(true);
		master->m_monoModel.setValue(true);
		master->m_eqLowModel.setValue(4.f);
		master->m_panModeModel.setValue(static_cast<int>(mixerstrip::PanMode::Width));
		master->m_widthModel.setValue(0.f);
		master->m_soloSafeModel.setValue(true);
		master->m_crossfadeSideModel.setValue(1);
		QVERIFY(master->m_monoModel.value());

		Engine::mixer()->clearChannel(0);

		QCOMPARE(master->m_trimModel.value(), 0.f);
		QVERIFY(!master->m_polarityLeftModel.value());
		QVERIFY(!master->m_monoModel.value());
		QCOMPARE(master->m_eqLowModel.value(), 0.f);
		QCOMPARE(master->m_panModeModel.value(), static_cast<int>(mixerstrip::PanMode::Balance));
		QCOMPARE(master->m_widthModel.value(), 1.f);
		QVERIFY(!master->m_soloSafeModel.value());
		QCOMPARE(master->m_crossfadeSideModel.value(), 0);
	}

	//! A non-finite persisted strip float must load as the same default a missing attribute gets.
	void nonFiniteStripSettingsAreReplaced()
	{
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("mixerchannel"));
		element.setAttribute(QStringLiteral("trim"), QStringLiteral("nan"));
		element.setAttribute(QStringLiteral("eqlow"), QStringLiteral("nan"));
		element.setAttribute(QStringLiteral("eqmid"), QStringLiteral("nan"));
		element.setAttribute(QStringLiteral("eqmidf"), QStringLiteral("nan"));
		element.setAttribute(QStringLiteral("eqhigh"), QStringLiteral("nan"));
		element.setAttribute(QStringLiteral("panright"), QStringLiteral("nan"));
		element.setAttribute(QStringLiteral("width"), QStringLiteral("nan"));
		mixerstrip::load(*channel(1), element);

		QCOMPARE(channel(1)->m_trimModel.value(), 0.f);
		QCOMPARE(channel(1)->m_eqLowModel.value(), 0.f);
		QCOMPARE(channel(1)->m_eqMidModel.value(), 0.f);
		QCOMPARE(channel(1)->m_eqMidFreqModel.value(), 1000.f);
		QCOMPARE(channel(1)->m_eqHighModel.value(), 0.f);
		QCOMPARE(channel(1)->m_panRightModel.value(), 0.f);
		QCOMPARE(channel(1)->m_widthModel.value(), 1.f);
	}

	//! A non-finite persisted channel/send float must not become a model value.
	void nonFiniteMixerSettingsAreIgnored()
	{
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("mixer"));
		doc.appendChild(root);

		QDomElement master = doc.createElement(QStringLiteral("mixerchannel"));
		master.setAttribute(QStringLiteral("num"), 0);
		root.appendChild(master);

		QDomElement element = doc.createElement(QStringLiteral("mixerchannel"));
		element.setAttribute(QStringLiteral("num"), 1);
		element.setAttribute(QStringLiteral("volume"), QStringLiteral("1e999"));
		element.setAttribute(QStringLiteral("pan"), QStringLiteral("nan"));
		QDomElement sendElement = doc.createElement(QStringLiteral("send"));
		sendElement.setAttribute(QStringLiteral("channel"), 0);
		sendElement.setAttribute(QStringLiteral("amount"), QStringLiteral("nan"));
		sendElement.setAttribute(QStringLiteral("pan"), QStringLiteral("nan"));
		element.appendChild(sendElement);
		root.appendChild(element);

		Engine::mixer()->loadSettings(root);

		QVERIFY(std::isfinite(channel(1)->m_volumeModel.value()));
		QCOMPARE(channel(1)->m_volumeModel.value(), 1.f);
		QVERIFY(std::isfinite(channel(1)->m_panModel.value()));
		QCOMPARE(channel(1)->m_panModel.value(), 0.f);

		MixerRoute* masterSend = nullptr;
		for (MixerRoute* route : channel(1)->m_sends)
		{
			if (route->receiverIndex() == 0) { masterSend = route; }
		}
		QVERIFY(masterSend != nullptr);
		QVERIFY(std::isfinite(masterSend->amount()->value()));
		QCOMPARE(masterSend->amount()->value(), 1.f);
		QVERIFY(std::isfinite(masterSend->pan()->value()));
		QCOMPARE(masterSend->pan()->value(), 0.f);
	}

	//! A mismatched nodename child is still loaded by AutomatableModel when no matching sibling exists.
	void nonFiniteMixerSettingsWithMismatchedNodeNameAreIgnored()
	{
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("mixer"));
		doc.appendChild(root);
		QDomElement master = doc.createElement(QStringLiteral("mixerchannel"));
		master.setAttribute(QStringLiteral("num"), 0);
		root.appendChild(master);
		QDomElement mixerChannel = doc.createElement(QStringLiteral("mixerchannel"));
		mixerChannel.setAttribute(QStringLiteral("num"), 1);
		QDomElement volume = doc.createElement(QStringLiteral("volume"));
		volume.setAttribute(QStringLiteral("nodename"), QStringLiteral("another-model"));
		volume.setAttribute(QStringLiteral("value"), QStringLiteral("nan"));
		mixerChannel.appendChild(volume);
		root.appendChild(mixerChannel);

		Engine::mixer()->loadSettings(root);

		QVERIFY(std::isfinite(channel(1)->m_volumeModel.value()));
		QCOMPARE(channel(1)->m_volumeModel.value(), 1.f);
	}

	//! A non-finite automation bend on a persisted model must not reach the value curve.
	void nonFiniteMixerAutomationBendIsIgnored()
	{
		Mixer* mixer = Engine::mixer();
		AutomationClip* clip = AutomationClip::globalAutomationClip(&channel(1)->m_volumeModel);
		clip->clear();

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("mixer"));
		doc.appendChild(root);
		QDomElement mixerChannel = doc.createElement(QStringLiteral("mixerchannel"));
		mixerChannel.setAttribute(QStringLiteral("num"), 1);
		QDomElement automation = doc.createElement(AutomationClip::classNodeName());
		QDomElement volume = doc.createElement(QStringLiteral("volume"));
		for (int pos : {0, 100})
		{
			QDomElement point = doc.createElement(QStringLiteral("time"));
			point.setAttribute(QStringLiteral("pos"), pos);
			point.setAttribute(QStringLiteral("value"), QStringLiteral("0.5"));
			point.setAttribute(QStringLiteral("outValue"), QStringLiteral("0.5"));
			point.setAttribute(QStringLiteral("inTan"), QStringLiteral("0"));
			point.setAttribute(QStringLiteral("outTan"), QStringLiteral("0"));
			point.setAttribute(QStringLiteral("curve"), 4);  // AutomationNode::Curve::Bend
			point.setAttribute(QStringLiteral("bend"), QStringLiteral("nan"));
			volume.appendChild(point);
		}
		automation.appendChild(volume);
		mixerChannel.appendChild(automation);
		root.appendChild(mixerChannel);

		mixer->loadSettings(root);

		AutomationClip* loadedClip = AutomationClip::globalAutomationClip(&channel(1)->m_volumeModel);
		QVERIFY(std::isfinite(loadedClip->valueAt(TimePos(50))));
		QVERIFY(std::isfinite(channel(1)->m_volumeModel.value()));
	}

	//! MX-027: 'all' names every channel but the master, so a mixer left with only its master
	//! names none - Unmute All / Clear Solo is then a successful no-op, not an error, and it
	//! records no undo step.
	void unmuteAllWithOnlyTheMaster()
	{
		Engine::mixer()->clear();  // only the master channel is left
		QVERIFY(Engine::mixer()->numChannels() == 1);
		const int depthBefore = run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
		const ControlResult unmuted = run(QStringLiteral("mixer.set_many"), {{QStringLiteral("all"), true}, {QStringLiteral("muted"), false}});
		QVERIFY2(unmuted.ok, qPrintable(unmuted.errorMessage));
		QCOMPARE(unmuted.result.value(QStringLiteral("count")).toInt(), 0);
		QCOMPARE(run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(), depthBefore);
	}

	//! cluster new-p21-143: removing an effect takes it off its chain (effectChain() is nullptr and the chain no
	//! longer lists it); appending it to another chain then attaches it to that chain only.
	void removedEffectIsDetachedFromItsChain()
	{
		EffectChain chain{nullptr};
		EffectChain next{nullptr};
		auto* effect = new partd::FixedGainEffect(&chain, 1.f, 1.f);
		chain.appendEffect(effect);
		chain.removeEffect(effect);
		QVERIFY2(effect->effectChain() == nullptr, "a removed effect still names the chain it was taken off");
		QVERIFY(chain.effects().empty());
		next.appendEffect(effect);
		QCOMPARE(effect->effectChain(), &next);
		next.removeEffect(effect);
		delete effect;
	}

	//! cluster new-p21-143: removing an effect from a chain that does not hold it changes nothing; the owner keeps it.
	void removeFromWrongChainLeavesOwnerIntact()
	{
		EffectChain owner{nullptr};
		EffectChain other{nullptr};
		auto* effect = new partd::FixedGainEffect(&owner, 1.f, 1.f);
		owner.appendEffect(effect);
		other.removeEffect(effect);
		QCOMPARE(effect->effectChain(), &owner);
		owner.removeEffect(effect);
		delete effect;
	}
};

QTEST_MAIN(MixerStripTest)
#include "MixerStripTest.moc"
