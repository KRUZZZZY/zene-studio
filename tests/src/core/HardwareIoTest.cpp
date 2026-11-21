/*
 * HardwareIoTest.cpp - hardware outputs, cue mixes and hardware inserts (0.5.0 F1)
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

/*! 0.5.0 F1 (MX-040, MX-041, MX-036, MX-061). Channel 1 carries a constant 0.25 and channel 2 a
 *  varying signal, both at fader 0.5 into the master. Driven through the commands, as the menus drive
 *  them, and read back from the engine's hardware bus (HardwareOutputBus):
 *  - a routed channel plays on its pair after its fader; the master moves to its own pair or nowhere;
 *    a mono output takes the average; with nothing routed the bus is inactive (the old device path);
 *  - a cue mix sums its sends at its level, pre- or post-fader, follows solo and listen, and with
 *    listen_to cue the main output keeps the mix;
 *  - the routed render allocates nothing;
 *  - a hardware insert's ping, through a loop from its send pair back to its return, measures the round
 *    trip exactly, and delay compensation then counts it;
 *  - everything survives a save and load, and a project that uses none of it saves none of it;
 *  - destinations parse ("3/4", "3", a bus name, "none") and refuse what is not one;
 *  - audio.io_setup names the interface's channels and buses, a route can use a bus name, and one undo
 *    puts the setup back. */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>
#include <cmath>
#include <span>
#include <vector>

#include "AllocationProbe.h"
#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AudioIoSetup.h"
#include "AutomatableModel.h"
#include "ControlRegistry.h"
#include "ConfigManager.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "HardwareInsert.h"
#include "HardwareIo.h"
#include "Mixer.h"
#include "ProjectJournal.h"
#include "RealtimeExport.h"
#include "SampleBuffer.h"
#include "MixerListen.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString ch(int index)
{
	return control::channelIdOf(Engine::mixer()->mixerChannel(index));
}

HardwareOutputBus& bus()
{
	return Engine::audioEngine()->hardwareOutputs();
}

float varying(int f)
{
	return static_cast<float>(((f * 131) % 2048) - 1024) / 2048.f;
}

//! One mixer period (channel 1 constant 0.25, channel 2 varying), then the bus flipped so it can be read.
std::vector<SampleFrame> period(Mixer* mixer)
{
	// Serial model prepass, as AudioEngine runs it once per period before mixing (consumes setter ramps).
	{
		auto changeGuard = Engine::audioEngine()->requestChangesGuard();
		Engine::audioEngine()->prepareAutomatableModelBuffers();
	}
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
	std::vector<SampleFrame> out(fpp);
	std::vector<SampleFrame> one(fpp, SampleFrame(0.25f, 0.25f));
	std::vector<SampleFrame> two(fpp);
	for (f_cnt_t f = 0; f < fpp; ++f) { two[f] = SampleFrame(varying(f), -varying(f)); }
	SampleFrame* a[1] = {one.data()};
	SampleFrame* b[1] = {two.data()};
	bus().swap();  // a clean write side
	mixer->mixToChannel(AudioBus{a, 1, fpp}, 1);
	mixer->mixToChannel(AudioBus{b, 1, fpp}, 2);
	mixer->prepareMasterMix();
	zeroSampleFrames(out.data(), fpp);
	mixer->masterMix(out.data());
	AutomatableModel::incrementPeriodCounter();
	bus().swap();  // the period just mixed is the one the device would be fed
	return out;
}

//! Whether device channel @a channel carries @a level on every frame (with @a out the period's master).
bool channelAt(const std::vector<SampleFrame>& out, int channel, float level)
{
	for (f_cnt_t f = 0; f < static_cast<f_cnt_t>(out.size()); ++f)
	{
		if (std::abs(bus().sample(channel, f, out[f]) - level) > 1e-5f) { return false; }
	}
	return true;
}

bool ok(const QString& id, const QJsonObject& args)
{
	const ControlResult result = run(id, args);
	if (!result.ok) { qWarning("%s: %s", qPrintable(id), qPrintable(result.errorMessage)); }
	return result.ok;
}

} // namespace

class HardwareIoTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();

	void cleanupTestCase();

	void aRoutedChannelPlaysOnItsPairAfterItsFader();

	void theMasterMovesToItsOwnPair();

	void outOfRangeFrameReadsAsSilence();

	void cueMasterFieldsRequireACue();

	void aCueMixSumsItsSendsAtItsLevel();

	void invalidCueNumbersUseClampedDefaultNames();

	void aCueFollowsSoloAndListen();

	void theRoutedRenderAllocatesNothing();

	//! Q050-089.2: render.realtime's capture keeps the master from the song's first period, drops the delay
	//! compensation's head, ends at the end tick plus the tail and scales by the master gain - and the period
	//! that feeds it allocates nothing.
	void theRealtimeCaptureTrimsEndsAndAllocatesNothing();

	void aPingMeasuresTheLoopAndFeedsDelayCompensation();

	void masterHardwareInsertLatencyIsIncludedInTotal();

	void aNonFiniteSavedMixDoesNotContaminateAudio();

	void malformedInsertLatencyUsesTheMeasuredFigure();

	void savedInsertMeasurementAtAnotherSampleRateIsDiscarded();

	void liveInsertMeasurementIsDiscardedWhenSampleRateChanges();

	void measuredInsertLatencyIsDiscardedWhenItsRouteChanges();

	void malformedInsertMeasurementKeepsUnknownSentinel();

	void unparsableSavedLevelsUseTheirDefaults();

	void disablingAnInsertCancelsAnInProgressPing();

	void changingAnInsertRouteCancelsAnInProgressPing();

	void changingReturnRouteDropsQueuedFrames();

	void destinationsParse();

	void theInterfaceIsNamedPerDeviceAndUndone();

	/*! A store that is set but is not a JSON object holds the setups of every device, and it is kept as it is:
	 *  a write or an undo refuses rather than read it as empty and store that empty object with its one new
	 *  entry, which would drop every other device's setup. */
	void aMalformedSetupStoreIsNeverReplaced();

	/*! A restart is required whenever the CONFIGURED output count and the RUNNING
	 *  device's granted count differ - not only when this very call changed the
	 *  count. A saved setup that already asks for four outputs, met by a device
	 *  that granted a stereo pair, still needs a restart: reporting "none" while
	 *  the counts disagree tells the caller the running device matches what it
	 *  asked for, and it does not. */
	void aStillMismatchedOutputCountAsksForARestart();

	/*! Undo takes the ping back with it. The measurement lands a few periods later,
	 *  on the audio thread; an undo that restored the settings but left the ping
	 *  running would let it finish afterwards and write a measured latency into
	 *  the project the undo just put back. */
	void anUndoCancelsThePingItRequested();

	void anEmptyIoSetupAddsNoUndoStep();

	void settingHardwareOutputToItsCurrentRouteAddsNoUndoStep();

	/*! The undo calls the ping off, but a period that had already read the request can still
	 *  move the ping on afterwards: the ping comes back and the answer it hears lands a
	 *  measured latency in the project the undo just put back. The ping's state machine must
	 *  be a compare-and-swap, so a cancel wins against a move that is already on its way.
	 *
	 *  The two halves of the race are driven one after the other, without a thread and
	 *  without a clock: the period reads the state, the interface thread cancels, the period
	 *  makes its move. */
	void aCancelledPingIsNotRevivedByAPeriodThatHadReadTheRequest();

	void everythingSurvivesASaveAndLoad();

	void nonFiniteCueSettingsSanitizeToDefaults();

	/*! A saved route that does not parse leaves the default in force (cluster new-p14-120-port):
	 *  a hand-edited project can carry output="3/5" (a pair that is not two consecutive channels),
	 *  and the master must not be silenced by it - it keeps its default route, the outputs 1/2.
	 *  An explicit "none" does parse: it is the user's own route nowhere, and it stays. */
	void malformedOutputKeepsTheDefaultRoute();

	void malformedDuplicateCueOutputKeepsItsPreviousDefaultSemantics();

	void channelCueLoadsRequireExplicitIndices();

	void cueMasterLoadsRequireExplicitIndices();

	void malformedPrefaderKeepsItsDefault();

	void malformedListenKeepsItsDefault();

private:
	//! What everythingSurvivesASaveAndLoad routed, read back after the load.
	static void verifyLoaded(Mixer& mixer)
	{
		const ChannelHardwareIo& io = mixer.mixerChannel(2)->m_hardwareIo;
		QVERIFY(io.outputSet && io.output == (audioio::Destination{4, 2}));
		QVERIFY(std::abs(io.cues[1].level - 0.7f) < 1e-6f && std::abs(io.cues[1].pan - 0.25f) < 1e-6f && !io.cues[1].preFader);
		const hardwareio::CueMaster& cue = hardwareio::cueMaster(1);
		QCOMPARE(cue.name, QStringLiteral("Drummer"));
		QVERIFY(cue.output == (audioio::Destination{6, 2}) && std::abs(cue.level - 0.8f) < 1e-6f && !cue.followListen);
		QVERIFY(hardwareio::listenToCue());
		const HardwareInsert::Settings insert = io.insert.settings();
		QVERIFY(insert.enabled && insert.send == (audioio::Destination{8, 2}) && insert.ret == (audioio::Destination{2, 1}));
		QVERIFY(std::abs(insert.mix - 0.5f) < 1e-6f && insert.latencyOverride == 123);
	}
};

#include "HardwareIoTestParts1.h"
#include "HardwareIoTestParts2.h"

QTEST_MAIN(HardwareIoTest)
#include "HardwareIoTest.moc"
