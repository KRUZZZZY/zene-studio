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

// Complete fixture method definitions; included once after the fixture declaration.

void HardwareIoTest::initTestCase() {
	Engine::init(true);
	Engine::audioEngine()->audioDev()->stopProcessing();  // the test drives the periods itself
	Mixer* mixer = Engine::mixer();
	while (mixer->numChannels() <= 2) { mixer->createChannel(); }
	mixer->mixerChannel(1)->m_volumeModel.setValue(0.5f);
	mixer->mixerChannel(2)->m_volumeModel.setValue(0.5f);
	ControlRegistry::setReady(true);
	for (int i = 0; i < 3; ++i) { period(mixer); }  // let the fader ramps settle
}
void HardwareIoTest::cleanupTestCase() {
	mixerlisten::clear();
	ControlRegistry::setReady(false);
	Engine::destroy();
}
void HardwareIoTest::aRoutedChannelPlaysOnItsPairAfterItsFader() {
	Mixer* mixer = Engine::mixer();
	QVERIFY(!period(mixer).empty());
	QVERIFY2(!bus().active(), "with nothing routed the device takes the old path");
	QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("output"), QStringLiteral("3/4")}}));
	const std::vector<SampleFrame> out = period(mixer);
	QVERIFY(bus().active());
	QVERIFY(channelAt(out, 2, 0.125f));  // 0.25 through the 0.5 fader
	QVERIFY(channelAt(out, 3, 0.125f));
	for (f_cnt_t f = 0; f < static_cast<f_cnt_t>(out.size()); ++f)
	{
		QCOMPARE(bus().sample(0, f, out[f]), out[f][0]);  // the master stays on 1/2, the send to it untouched
	}
	QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("output"), QStringLiteral("7")}}));
	QVERIFY(channelAt(period(mixer), 6, 0.125f));  // a mono output: the average of both sides
	QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("output"), QStringLiteral("default")}}));
	period(mixer);
	QVERIFY(!bus().active());
}
void HardwareIoTest::theMasterMovesToItsOwnPair() {
	Mixer* mixer = Engine::mixer();
	QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(0)}, {QStringLiteral("output"), QStringLiteral("5/6")}}));
	std::vector<SampleFrame> out = period(mixer);
	for (f_cnt_t f = 0; f < static_cast<f_cnt_t>(out.size()); ++f)
	{
		QCOMPARE(bus().sample(0, f, out[f]), 0.f);
		QCOMPARE(bus().sample(4, f, out[f]), out[f][0]);
	}
	QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(0)}, {QStringLiteral("output"), QStringLiteral("none")}}));
	period(mixer);
	QCOMPARE(bus().channelsInUse(), 0);
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	period(mixer);
	QVERIFY2(!bus().active(), "two undos put the master back on 1/2");
}
void HardwareIoTest::outOfRangeFrameReadsAsSilence() {
	// The bus holds 256 frames a period. Channel 0 carries 0.5 and channel 1 carries 0.25, so frame 256
	// of channel 0 - past its plane - reads channel 1's first sample unless the frame is bounded.
	HardwareOutputBus outputs;
	outputs.init(256);
	const std::vector<SampleFrame> half(256, SampleFrame(0.5f, 0.5f));
	const std::vector<SampleFrame> quarter(256, SampleFrame(0.25f, 0.25f));
	outputs.add(0, 1, half.data(), 256);
	outputs.add(1, 1, quarter.data(), 256);
	outputs.setMasterPlacement({-1, 2});  // the master is off the outputs
	outputs.swap();  // the period just written is the one the device reads
	const SampleFrame master(0.f, 0.f);
	QVERIFY(outputs.sample(0, 255, master) == 0.5f);
	QVERIFY(outputs.monoSample(255, master) == 0.5f);
	QVERIFY2(outputs.sample(0, 256, master) == 0.f, "a frame past the period is silence");
	QVERIFY2(outputs.monoSample(256, master) == 0.f, "a frame past the period is silence");
}
void HardwareIoTest::cueMasterFieldsRequireACue() {
	const ControlResult result = run(QStringLiteral("mixer.cue_master"), {{QStringLiteral("name"), QStringLiteral("Phones")}});
	QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
}
void HardwareIoTest::aCueMixSumsItsSendsAtItsLevel() {
	Mixer* mixer = Engine::mixer();
	QVERIFY(ok(QStringLiteral("mixer.cue_master"), {{QStringLiteral("cue"), QStringLiteral("A")}, {QStringLiteral("output"), QStringLiteral("3/4")},
		{QStringLiteral("level"), 0.5}}));
	QVERIFY(ok(QStringLiteral("mixer.cue_send"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("cue"), QStringLiteral("A")},
		{QStringLiteral("level"), 1.0}}));
	QVERIFY(channelAt(period(mixer), 2, 0.125f));  // pre-fader 0.25, at the cue's 0.5
	QVERIFY(ok(QStringLiteral("mixer.cue_send"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("cue"), QStringLiteral("A")},
		{QStringLiteral("pre_fader"), false}, {QStringLiteral("pan"), -1.0}}));
	const std::vector<SampleFrame> out = period(mixer);
	QVERIFY(channelAt(out, 2, 0.0625f));  // after the 0.5 fader
	QVERIFY(channelAt(out, 3, 0.f));      // panned hard left
	QVERIFY(ok(QStringLiteral("mixer.cue_send"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("cue"), QStringLiteral("A")},
		{QStringLiteral("pre_fader"), true}, {QStringLiteral("pan"), 0.0}}));
}
void HardwareIoTest::invalidCueNumbersUseClampedDefaultNames() {
	const int lastCue = hardwareio::MaxCues - 1;
	const hardwareio::CueMaster firstMaster = hardwareio::cueMaster(0);
	const hardwareio::CueMaster lastMaster = hardwareio::cueMaster(lastCue);
	hardwareio::setCueMaster(0, hardwareio::CueMaster{});
	hardwareio::setCueMaster(lastCue, hardwareio::CueMaster{});

	const QString belowRange = hardwareio::cueName(-1);
	const QString aboveRange = hardwareio::cueName(hardwareio::MaxCues);

	hardwareio::setCueMaster(0, firstMaster);
	hardwareio::setCueMaster(lastCue, lastMaster);
	QCOMPARE(belowRange, QStringLiteral("Cue A"));
	QCOMPARE(aboveRange, QStringLiteral("Cue D"));
}
void HardwareIoTest::aCueFollowsSoloAndListen() {
	Mixer* mixer = Engine::mixer();
	mixer->mixerChannel(2)->m_soloModel.setValue(true);  // mutes channel 1, whose send feeds the cue
	QVERIFY(channelAt(period(mixer), 2, 0.f));
	mixer->mixerChannel(2)->m_soloModel.setValue(false);
	QVERIFY(channelAt(period(mixer), 2, 0.125f));

	mixerlisten::setListening(2, true);  // AFL on channel 2: the cue carries it, at the cue's level
	const std::vector<SampleFrame> listened = period(mixer);
	for (f_cnt_t f = 0; f < static_cast<f_cnt_t>(listened.size()); ++f)
	{
		QVERIFY(std::abs(bus().sample(2, f, listened[f]) - varying(f) * 0.5f * 0.5f) < 1e-5f);
		QVERIFY(std::abs(listened[f][0] - varying(f) * 0.5f) < 1e-5f);  // listen_to main: the main output too
	}
	QVERIFY(ok(QStringLiteral("mixer.cue_master"), {{QStringLiteral("listen_to"), QStringLiteral("cue")}}));
	mixerlisten::setListening(2, false);
	const std::vector<SampleFrame> mix = period(mixer);
	mixerlisten::setListening(2, true);
	const std::vector<SampleFrame> spared = period(mixer);
	for (std::size_t f = 0; f < mix.size(); ++f) { QCOMPARE(spared[f][0], mix[f][0]); }  // the main output keeps the mix
	QVERIFY(ok(QStringLiteral("mixer.cue_master"), {{QStringLiteral("cue"), QStringLiteral("A")}, {QStringLiteral("level"), 0.0}}));
	const std::vector<SampleFrame> silentCue = period(mixer);
	bool mainCarriesListen = true;
	for (f_cnt_t f = 0; f < static_cast<f_cnt_t>(silentCue.size()); ++f)
	{
		if (std::abs(silentCue[f][0] - varying(f) * 0.5f) >= 1e-5f) { mainCarriesListen = false; break; }
	}
	QVERIFY(ok(QStringLiteral("mixer.cue_master"), {{QStringLiteral("cue"), QStringLiteral("A")}, {QStringLiteral("level"), 0.5}}));
	mixerlisten::clear();
	QVERIFY(ok(QStringLiteral("mixer.cue_master"), {{QStringLiteral("listen_to"), QStringLiteral("main")}}));
	QVERIFY2(mainCarriesListen, "a silent following cue must leave the listened signal on the main output");
}
void HardwareIoTest::theRoutedRenderAllocatesNothing() {
	Mixer* mixer = Engine::mixer();
	QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("output"), QStringLiteral("5/6")}}));
	mixerlisten::setListening(1, true);
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
	std::vector<SampleFrame> out(fpp);
	mixer->prepareMasterMix();
	test::resetAllocationCount();
	test::tlCountAllocations = true;
	mixer->masterMix(out.data());
	bus().swap();
	test::tlCountAllocations = false;
	QCOMPARE(static_cast<qulonglong>(test::tlAllocationCount), static_cast<qulonglong>(0));
	mixerlisten::clear();
	QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("output"), QStringLiteral("default")}}));
}
void HardwareIoTest::theRealtimeCaptureTrimsEndsAndAllocatesNothing() {
	RealtimeExportCapture capture;
	// 100 frames a tick: the song ends at tick 10 = frame 1000; 100 frames of compensation, 50 of tail.
	QVERIFY(capture.arm(44100, 10, 100, 50, 0.5f, 1000000));
	QVERIFY(!capture.arm(44100, 10, 100, 50, 0.5f, 1000000));  // one take at a time
	constexpr f_cnt_t Period = 256;
	std::vector<SampleFrame> period(Period);
	f_cnt_t played = 0;
	test::resetAllocationCount();
	test::tlCountAllocations = true;
	for (int n = 0; n < 8 && capture.state() != RealtimeExportCapture::State::Done; ++n)
	{
		for (f_cnt_t f = 0; f < Period; ++f)
		{
			const float at = static_cast<float>(played + f) / 10000.f;
			period[f] = SampleFrame(at, -at);
		}
		played += Period;
		capture.push(period.data(), Period, played / 100, static_cast<double>(played % 100), 100.0);
	}
	test::tlCountAllocations = false;
	QCOMPARE(static_cast<qulonglong>(test::tlAllocationCount), static_cast<qulonglong>(0));
	QCOMPARE(static_cast<int>(capture.state()), static_cast<int>(RealtimeExportCapture::State::Done));
	QCOMPARE(capture.targetFrames(), static_cast<std::uint64_t>(1000 + 50));
	QVERIFY(!capture.capped());
	const std::shared_ptr<const SampleBuffer> take = capture.takeFinished();
	QVERIFY(take != nullptr);
	QCOMPARE(static_cast<int>(take->size()), 1050);
	// File frame k is song frame k: master frame k + 100, at half the level.
	QCOMPARE(take->data()[0][0], 0.5f * 100 / 10000.f);
	QCOMPARE(take->data()[1049][1], -0.5f * 1149 / 10000.f);
	QCOMPARE(static_cast<int>(capture.state()), static_cast<int>(RealtimeExportCapture::State::Idle));
}
void HardwareIoTest::aPingMeasuresTheLoopAndFeedsDelayCompensation() {
	AudioEngine* engine = Engine::audioEngine();
	const f_cnt_t fpp = engine->framesPerPeriod();
	QCOMPARE(run(QStringLiteral("mixer.hardware_insert_ping"), {{QStringLiteral("channel"), ch(1)}}).errorKind, ControlErrorKind::Requires);
	QVERIFY(ok(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("enabled"), true},
		{QStringLiteral("send"), QStringLiteral("3/4")}, {QStringLiteral("return"), QStringLiteral("1/2")}}));
	QVERIFY(ok(QStringLiteral("mixer.hardware_insert_ping"), {{QStringLiteral("channel"), ch(1)}}));
	HardwareInsert& insert = Engine::mixer()->mixerChannel(1)->m_hardwareIo.insert;
	// A cable from outputs 3/4 to inputs 1/2: what the device plays on 3/4 is captured for the next period.
	std::vector<SampleFrame> cable(fpp);
	for (int i = 0; i < 64 && insert.pingState() != HardwareInsert::PingState::Done; ++i)
	{
		const std::span<const SampleFrame> played = engine->renderNextPeriod();
		for (f_cnt_t f = 0; f < fpp; ++f) { cable[f] = SampleFrame(bus().sample(2, f, played[f]), bus().sample(3, f, played[f])); }
		engine->pushInputFrames(cable.data(), fpp);
	}
	QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Done));
	// One period of output double buffer, one of capture staging: the loop the Dummy device has too.
	QCOMPARE(insert.settings().measured, static_cast<int>(2 * fpp));
	QCOMPARE(insert.latencyFrames(), static_cast<int>(2 * fpp));
	engine->renderNextPeriod();
	QCOMPARE(Engine::mixer()->totalLatencyFrames(), static_cast<int>(2 * fpp));
	QVERIFY(ok(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("latency_frames"), 300}}));
	QCOMPARE(insert.latencyFrames(), 300);  // a typed figure wins over the measured one
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(insert.settings().measured, -1);  // the ping's undo: no figure, as before it
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QVERIFY(!insert.enabled());
	engine->renderNextPeriod();
	QCOMPARE(Engine::mixer()->totalLatencyFrames(), 0);
}
void HardwareIoTest::masterHardwareInsertLatencyIsIncludedInTotal() {
	Mixer* mixer = Engine::mixer();
	QVERIFY(ok(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), ch(0)}, {QStringLiteral("enabled"), true},
		{QStringLiteral("send"), QStringLiteral("3/4")}, {QStringLiteral("return"), QStringLiteral("1/2")},
		{QStringLiteral("latency_frames"), 123}}));
	period(mixer);  // updateLatencyCompensation publishes the graph at the period boundary
	QCOMPARE(mixer->mixerChannel(0)->m_hardwareIo.insert.latencyFrames(), 123);
	QCOMPARE(mixer->totalLatencyFrames(), 123);
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	period(mixer);
	QCOMPARE(mixer->totalLatencyFrames(), 0);
}
void HardwareIoTest::aNonFiniteSavedMixDoesNotContaminateAudio() {
	QDomDocument doc;
	QDomElement channel = doc.createElement(QStringLiteral("channel"));
	doc.appendChild(channel);
	QDomElement element = doc.createElement(QStringLiteral("hardwareinsert"));
	element.setAttribute(QStringLiteral("on"), 1);
	element.setAttribute(QStringLiteral("mix"), QStringLiteral("nan"));
	channel.appendChild(element);

	bool parsed = false;
	QVERIFY(std::isnan(element.attribute(QStringLiteral("mix")).toFloat(&parsed)));
	QVERIFY(parsed);

	HardwareInsert insert;
	const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
	insert.load(channel, frames);
	std::vector<SampleFrame> output(static_cast<std::size_t>(frames), SampleFrame(0.25f, -0.25f));
	insert.process(output.data(), frames, *Engine::audioEngine());
	for (const SampleFrame& frame : output)
	{
		QVERIFY(std::isfinite(frame[0]));
		QVERIFY(std::isfinite(frame[1]));
	}
}
void HardwareIoTest::malformedInsertLatencyUsesTheMeasuredFigure() {
	QDomDocument doc;
	QDomElement channel = doc.createElement(QStringLiteral("channel"));
	doc.appendChild(channel);
	QDomElement element = doc.createElement(QStringLiteral("hardwareinsert"));
	element.setAttribute(QStringLiteral("on"), 1);
	element.setAttribute(QStringLiteral("latency"), QStringLiteral("abc"));
	element.setAttribute(QStringLiteral("measured"), 512);
	channel.appendChild(element);

	HardwareInsert insert;
	insert.load(channel, 64);
	QCOMPARE(insert.settings().latencyOverride, -1);
	QCOMPARE(insert.latencyFrames(), 512);
}
void HardwareIoTest::savedInsertMeasurementAtAnotherSampleRateIsDiscarded() {
	AudioEngine* engine = Engine::audioEngine();
	const sample_rate_t currentRate = engine->outputSampleRate();
	const sample_rate_t savedRate = currentRate == 1 ? 2 : 1;
	QVERIFY(currentRate != savedRate);
	HardwareInsert source;
	HardwareInsert::Settings measured;
	measured.enabled = true;
	measured.measured = 512;
	source.setSettings(measured, 64);
	QDomDocument doc;
	QDomElement channel = doc.createElement(QStringLiteral("channel"));
	doc.appendChild(channel);
	source.save(doc, channel);
	QDomElement element = channel.firstChildElement(QStringLiteral("hardwareinsert"));
	QVERIFY(!element.isNull());
	QCOMPARE(element.attribute(QStringLiteral("measured_rate")).toUInt(), currentRate);
	element.setAttribute(QStringLiteral("measured_rate"), savedRate);

	HardwareInsert insert;
	insert.load(channel, 64);
	QCOMPARE(insert.settings().measured, -1);
	QCOMPARE(insert.latencyFrames(), 0);
}
void HardwareIoTest::liveInsertMeasurementIsDiscardedWhenSampleRateChanges() {
	AudioEngine* engine = Engine::audioEngine();
	const sample_rate_t currentRate = engine->outputSampleRate();
	const sample_rate_t changedRate = currentRate == 1 ? 2 : 1;
	HardwareInsert insert;
	HardwareInsert::Settings settings;
	settings.enabled = true;
	settings.measured = 512;
	insert.setSettings(settings, engine->framesPerPeriod());
	QCOMPARE(insert.latencyFrames(), 512);

	engine->audioDev()->setSampleRateForTesting(changedRate);
	const int preProcessLatency = insert.latencyFrames();  // PDC queries this before the next insert process period
	const int preProcessMeasurement = insert.settings().measured;
	std::vector<SampleFrame> block(static_cast<std::size_t>(engine->framesPerPeriod()), SampleFrame(0.25f, -0.25f));
	insert.process(block.data(), engine->framesPerPeriod(), *engine);
	const int postProcessMeasurement = insert.settings().measured;
	engine->audioDev()->setSampleRateForTesting(currentRate);
	QCOMPARE(preProcessLatency, 0);
	QCOMPARE(preProcessMeasurement, -1);
	QCOMPARE(postProcessMeasurement, -1);
	QCOMPARE(insert.settings().measured, -1);
	QCOMPARE(insert.latencyFrames(), 0);
}
void HardwareIoTest::measuredInsertLatencyIsDiscardedWhenItsRouteChanges() {
	const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
	for (const bool changeSend : {true, false})
	{
		HardwareInsert insert;
		HardwareInsert::Settings settings;
		settings.enabled = true;
		settings.measured = 512;
		insert.setSettings(settings, frames);
		QCOMPARE(insert.latencyFrames(), 512);

		if (changeSend) { settings.send = {2, 2}; }
		else { settings.ret = {2, 2}; }
		insert.setSettings(settings, frames);
		QCOMPARE(insert.settings().measured, -1);
		QCOMPARE(insert.latencyFrames(), 0);
	}
}
void HardwareIoTest::malformedInsertMeasurementKeepsUnknownSentinel() {
	QDomDocument doc;
	QDomElement channel = doc.createElement(QStringLiteral("channel"));
	doc.appendChild(channel);
	QDomElement element = doc.createElement(QStringLiteral("hardwareinsert"));
	element.setAttribute(QStringLiteral("on"), 1);
	element.setAttribute(QStringLiteral("latency"), -1);
	element.setAttribute(QStringLiteral("measured"), QStringLiteral("abc"));
	channel.appendChild(element);

	HardwareInsert insert;
	insert.load(channel, 64);
	QCOMPARE(insert.settings().measured, -1);
	QCOMPARE(insert.latencyFrames(), 0);
}
void HardwareIoTest::unparsableSavedLevelsUseTheirDefaults() {
	// Qt's toFloat() returns 0 for "abc" and 0 is finite, so only the success flag can tell it apart.
	bool parsed = true;
	QDomDocument doc;
	QDomElement channel = doc.createElement(QStringLiteral("channel"));
	doc.appendChild(channel);
	QDomElement element = doc.createElement(QStringLiteral("hardwareinsert"));
	element.setAttribute(QStringLiteral("on"), 1);
	element.setAttribute(QStringLiteral("mix"), QStringLiteral("abc"));
	channel.appendChild(element);
	QVERIFY(element.attribute(QStringLiteral("mix")).toFloat(&parsed) == 0.f);
	QVERIFY(!parsed);

	HardwareInsert insert;
	insert.load(channel, 64);
	QCOMPARE(insert.settings().mix, 1.f);  // the insert's default: fully wet, not fully dry

	QDomDocument mixDoc;
	QDomElement mixerElement = mixDoc.createElement(QStringLiteral("mixer"));
	mixDoc.appendChild(mixerElement);
	QDomElement cuemix = mixDoc.createElement(QStringLiteral("cuemix"));
	cuemix.setAttribute(QStringLiteral("index"), 0);
	cuemix.setAttribute(QStringLiteral("level"), QStringLiteral("abc"));
	mixerElement.appendChild(cuemix);
	hardwareio::loadMixer(mixerElement);
	QCOMPARE(hardwareio::cueMaster(0).level, 1.f);  // the cuemix's default level: not silent
	hardwareio::resetMixer();
}
void HardwareIoTest::disablingAnInsertCancelsAnInProgressPing() {
	AudioEngine* engine = Engine::audioEngine();
	const f_cnt_t fpp = engine->framesPerPeriod();
	HardwareInsert insert;
	HardwareInsert::Settings settings;
	settings.enabled = true;
	settings.ret = {10000, 2};  // no engine input can accidentally complete the ping
	insert.setSettings(settings, fpp);
	insert.requestPing();
	std::vector<SampleFrame> block(fpp, SampleFrame(0.25f, 0.25f));
	insert.process(block.data(), fpp, *engine);
	QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Waiting));

	settings.enabled = false;
	insert.setSettings(settings, fpp);
	QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Idle));
	settings.enabled = true;
	insert.setSettings(settings, fpp);
	insert.process(block.data(), fpp, *engine);
	QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Idle));
}
void HardwareIoTest::changingAnInsertRouteCancelsAnInProgressPing() {
	AudioEngine* engine = Engine::audioEngine();
	const f_cnt_t frames = engine->framesPerPeriod();
	for (const bool changeSend : {true, false})
	{
		HardwareInsert insert;
		HardwareInsert::Settings settings;
		settings.enabled = true;
		settings.ret = {10000, 2};  // no engine input can accidentally complete the ping
		insert.setSettings(settings, frames);
		insert.requestPing();
		std::vector<SampleFrame> block(static_cast<std::size_t>(frames), SampleFrame(0.25f, 0.25f));
		insert.process(block.data(), frames, *engine);
		QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Waiting));

		settings = insert.settings();
		const auto waiting = insert.pingWord();
		if (changeSend) { settings.send = {2, 2}; }
		else { settings.ret = {10002, 2}; }
		insert.setSettings(settings, frames);
		QVERIFY2(!insert.publishPingResult(waiting, 1234), "a response from the old route published latency after a route change");
		QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Idle));
		insert.process(block.data(), frames, *engine);
		QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Idle));
		QCOMPARE(insert.settings().measured, -1);
	}
}
