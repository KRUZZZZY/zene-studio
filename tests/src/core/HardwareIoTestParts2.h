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

void HardwareIoTest::changingReturnRouteDropsQueuedFrames() {
	AudioEngine* engine = Engine::audioEngine();
	const f_cnt_t fpp = engine->framesPerPeriod();
	QVERIFY(fpp > 0);
	const f_cnt_t capturedFrames = 2 * fpp;
	MixerChannel* channel = Engine::mixer()->mixerChannel(1);
	HardwareInsert& insert = channel->m_hardwareIo.insert;
	insert.setSettings(HardwareInsert::Settings{}, fpp);

	std::vector<SampleFrame> captured(static_cast<std::size_t>(capturedFrames), SampleFrame(0.25f, 0.5f));
	engine->pushInputFrames(captured.data(), capturedFrames);
	engine->renderNextPeriod();
	QCOMPARE(engine->inputBufferFrames(), capturedFrames);

	HardwareInsert::Settings settings;
	settings.enabled = true;
	settings.ret = {0, 2};
	settings.mix = 1.f;
	settings.latencyOverride = 0;
	insert.setSettings(settings, fpp);

	std::vector<SampleFrame> first(static_cast<std::size_t>(fpp), SampleFrame{});
	insert.process(first.data(), fpp, *engine);
	bool firstMatches = true;
	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		firstMatches = firstMatches && first[static_cast<std::size_t>(f)][0] == 0.25f
			&& first[static_cast<std::size_t>(f)][1] == 0.5f;
	}

	std::vector<SampleFrame> newCapture(static_cast<std::size_t>(capturedFrames), SampleFrame(-0.75f, 0.125f));
	settings.ret = {1, 1};
	insert.setSettings(settings, fpp);
	channel->m_muteModel.setValue(true);
	engine->pushInputFrames(newCapture.data(), capturedFrames);
	engine->renderNextPeriod();
	channel->m_muteModel.setValue(false);
	const bool newCaptureReady = engine->inputBufferFrames() == capturedFrames;

	std::vector<SampleFrame> afterRouteChange(static_cast<std::size_t>(fpp), SampleFrame{});
	insert.process(afterRouteChange.data(), fpp, *engine);
	bool newRouteMatches = true;
	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		newRouteMatches = newRouteMatches && afterRouteChange[static_cast<std::size_t>(f)][0] == 0.125f
			&& afterRouteChange[static_cast<std::size_t>(f)][1] == 0.125f;
	}
	insert.setSettings(HardwareInsert::Settings{}, fpp);
	QVERIFY(newCaptureReady);
	QVERIFY(firstMatches);
	QVERIFY(newRouteMatches);
}
void HardwareIoTest::destinationsParse() {
	audioio::Side outputs;
	outputs.channels = 6;
	outputs.buses = {{QStringLiteral("Phones"), 2, 2}};
	audioio::Destination d;
	QVERIFY(audioio::parseDestination(QStringLiteral("3/4"), outputs, &d, nullptr) && d == (audioio::Destination{2, 2}));
	QVERIFY(audioio::parseDestination(QStringLiteral("5"), outputs, &d, nullptr) && d == (audioio::Destination{4, 1}));
	QVERIFY(audioio::parseDestination(QStringLiteral("phones"), outputs, &d, nullptr) && d == (audioio::Destination{2, 2}));
	QVERIFY(audioio::parseDestination(QStringLiteral("none"), outputs, &d, nullptr) && d.none());
	QString error;
	QVERIFY(!audioio::parseDestination(QStringLiteral("3/5"), outputs, &d, &error) && !error.isEmpty());
	QVERIFY(!audioio::parseDestination(QStringLiteral("0"), outputs, &d, nullptr));
	QVERIFY(!audioio::parseDestination(QStringLiteral("Talkback"), outputs, &d, nullptr));
	// A number names a channel of the side it is read on: it must be one of its channels
	// (cluster new-p8-269: a destination beyond the side's count was accepted and stored).
	audioio::Side twoOutputs;
	twoOutputs.channels = 2;
	QVERIFY(!audioio::parseDestination(QStringLiteral("3"), twoOutputs, &d, &error));
	QVERIFY(!error.isEmpty());
	error.clear();
	QVERIFY(!audioio::parseDestination(QStringLiteral("2/3"), twoOutputs, &d, &error));
	QVERIFY(!error.isEmpty());
	error.clear();
	QVERIFY(!audioio::parseDestination(QStringLiteral("127"), twoOutputs, &d, &error));
	QVERIFY(!error.isEmpty());
	QCOMPARE(audioio::destinationLabel({2, 2}, outputs), QStringLiteral("Phones"));
	QCOMPARE(audioio::channelLabel({4, 1}), QStringLiteral("5"));
}
void HardwareIoTest::theInterfaceIsNamedPerDeviceAndUndone() {
	const QString key = audioio::currentDeviceKey();
	const QJsonObject before = audioio::storedEntry(key);
	const QJsonObject outputsSetup{{QStringLiteral("channels"), 4},
		{QStringLiteral("names"), QJsonArray{QStringLiteral("Main L"), QStringLiteral("Main R"), QStringLiteral("HP L"), QStringLiteral("HP R")}},
		{QStringLiteral("buses"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("Main")}, {QStringLiteral("channels"), QStringLiteral("1/2")}},
			QJsonObject{{QStringLiteral("name"), QStringLiteral("Phones")}, {QStringLiteral("channels"), QStringLiteral("3/4")}}}}};
	const QJsonArray inputNames{QStringLiteral("Input 1"), QStringLiteral("Input 2"), QStringLiteral("Input 3"), QStringLiteral("Input 4"),
		QStringLiteral("Input 5"), QStringLiteral("Input 6"), QStringLiteral("Input 7"), QStringLiteral("Input 8")};
	const QJsonObject setupArgs{{QStringLiteral("outputs"), outputsSetup},
		{QStringLiteral("inputs"), QJsonObject{{QStringLiteral("names"), inputNames}}}};
	QVERIFY(ok(QStringLiteral("audio.io_setup"), setupArgs));
	const ControlResult list = run(QStringLiteral("audio.io_list"));
	QVERIFY(list.ok);
	const QJsonObject outputs = list.result.value(QStringLiteral("outputs")).toObject();
	const QJsonObject inputs = list.result.value(QStringLiteral("inputs")).toObject();
	QCOMPARE(outputs.value(QStringLiteral("requested")).toInt(), 4);
	QCOMPARE(outputs.value(QStringLiteral("granted")).toInt(), 2);  // the Dummy opens a stereo pair
	QCOMPARE(inputs.value(QStringLiteral("channels")).toArray().size(), inputs.value(QStringLiteral("requested")).toInt());
	QCOMPARE(outputs.value(QStringLiteral("buses")).toArray().at(1).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Phones"));
	QCOMPARE(outputs.value(QStringLiteral("channels")).toArray().at(2).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("HP L"));
	const ControlResult routed = run(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("output"), QStringLiteral("Phones")}});
	QVERIFY(routed.ok);
	const QJsonObject output = routed.result.value(QStringLiteral("output")).toObject();
	QCOMPARE(output.value(QStringLiteral("channels")).toString(), QStringLiteral("3/4"));
	QCOMPARE(output.value(QStringLiteral("status")).toString(), QStringLiteral("stereo_backend"));  // routed nowhere, and said so
	QCOMPARE(run(QStringLiteral("audio.io_setup"), {{QStringLiteral("outputs"), QJsonObject{{QStringLiteral("buses"), QJsonArray{
		QJsonObject{{QStringLiteral("name"), QStringLiteral("A")}, {QStringLiteral("channels"), QStringLiteral("1/2")}},
		QJsonObject{{QStringLiteral("name"), QStringLiteral("B")}, {QStringLiteral("channels"), QStringLiteral("2")}}}}}}}).errorKind,
		ControlErrorKind::InvalidArgs);  // overlapping buses
	QVERIFY(run(QStringLiteral("control.undo")).ok);  // the route
	QVERIFY(run(QStringLiteral("control.undo")).ok);  // the setup
	QVERIFY(audioio::storedEntry(key) == before);
}
void HardwareIoTest::aMalformedSetupStoreIsNeverReplaced() {
	ConfigManager* config = ConfigManager::inst();
	const QString section = QStringLiteral("audioio");
	const QString attribute = QStringLiteral("setups");
	const QString stored = config->value(section, attribute);
	const QString key = audioio::currentDeviceKey();
	const QStringList unreadable{QStringLiteral("{\"Other device\": {\"outputs\": {"), QStringLiteral("[1, 2]")};
	for (const QString& text : unreadable)
	{
		config->setValue(section, attribute, text);
		QString error;
		const bool written = audioio::writeSetup(key, audioio::setupFor(key, 2), &error);
		audioio::restoreEntry(key, QJsonObject{});
		const QString after = config->value(section, attribute);
		config->setValue(section, attribute, stored);  // put back what the config held
		QVERIFY2(!written, qPrintable(text));
		QVERIFY2(!error.isEmpty(), qPrintable(text));
		QCOMPARE(after, text);
	}
}
void HardwareIoTest::aStillMismatchedOutputCountAsksForARestart() {
	const QString key = audioio::currentDeviceKey();
	const QJsonObject before = audioio::storedEntry(key);
	const QJsonObject listed = run(QStringLiteral("audio.io_list")).result.value(QStringLiteral("outputs")).toObject();
	const int granted = listed.value(QStringLiteral("granted")).toInt();
	QVERIFY2(granted > 0, "the running device granted no outputs to compare against");

	QVERIFY(ok(QStringLiteral("audio.io_setup"), {{QStringLiteral("outputs"),
		QJsonObject{{QStringLiteral("channels"), granted == 4 ? 6 : 4}}}}));
	// The count now differs from what the running device granted, and it differs
	// whatever this next call does: it changes only the NAMES.
	const ControlResult renamed = run(QStringLiteral("audio.io_setup"), {{QStringLiteral("outputs"),
		QJsonObject{{QStringLiteral("names"), QJsonArray{QStringLiteral("Renamed L"), QStringLiteral("Renamed R")}}}}});
	QVERIFY2(renamed.ok, qPrintable(renamed.errorMessage));
	const QJsonObject outputs = renamed.result.value(QStringLiteral("outputs")).toObject();
	QCOMPARE(outputs.value(QStringLiteral("requested")).toInt(), granted == 4 ? 6 : 4);
	QCOMPARE(outputs.value(QStringLiteral("granted")).toInt(), granted);
	QVERIFY2(renamed.result.value(QStringLiteral("restart_required")).toBool(),
		"the configured count still differs from the running device's, so a restart is still required");
	// The honest report is the mismatch itself: this call changed no count, so the
	// names are what took effect now, while the count still waits for the next start.
	QCOMPARE(renamed.result.value(QStringLiteral("applied")).toString(), QStringLiteral("now"));

	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QVERIFY(audioio::storedEntry(key) == before);
}
void HardwareIoTest::anUndoCancelsThePingItRequested() {
	QVERIFY(ok(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("enabled"), true},
		{QStringLiteral("send"), QStringLiteral("9/10")}, {QStringLiteral("return"), QStringLiteral("3")}}));
	QVERIFY(ok(QStringLiteral("mixer.hardware_insert_ping"), {{QStringLiteral("channel"), ch(1)}}));
	auto& insert = Engine::mixer()->mixerChannel(1)->m_hardwareIo.insert;
	QVERIFY(insert.pingState() != HardwareInsert::PingState::Idle);  // the click is in flight

	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QVERIFY2(insert.pingState() == HardwareInsert::PingState::Idle,
		"the undone ping can still land a measured latency after the undo");

	QVERIFY(run(QStringLiteral("control.undo")).ok);  // the insert itself
	QVERIFY(!Engine::mixer()->mixerChannel(1)->m_hardwareIo.insert.enabled());
}
void HardwareIoTest::anEmptyIoSetupAddsNoUndoStep() {
	const int depthBefore = Engine::projectJournal()->undoDepth();
	QVERIFY(ok(QStringLiteral("audio.io_setup"), QJsonObject{}));
	QCOMPARE(Engine::projectJournal()->undoDepth(), depthBefore);
}
void HardwareIoTest::settingHardwareOutputToItsCurrentRouteAddsNoUndoStep() {
	const QString id = ch(1);
	QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), id}, {QStringLiteral("output"), QStringLiteral("default")}}));
	const int depthBefore = Engine::projectJournal()->undoDepth();
	QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), id}, {QStringLiteral("output"), QStringLiteral("default")}}));
	QCOMPARE(Engine::projectJournal()->undoDepth(), depthBefore);

	QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), id}, {QStringLiteral("output"), QStringLiteral("3/4")}}));
	const int depthBefore2 = Engine::projectJournal()->undoDepth();
	QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), id}, {QStringLiteral("output"), QStringLiteral("3/4")}}));
	QCOMPARE(Engine::projectJournal()->undoDepth(), depthBefore2);
}
void HardwareIoTest::aCancelledPingIsNotRevivedByAPeriodThatHadReadTheRequest() {
	HardwareInsert insert;

	// The period read the request; the undo calls the ping off; the period claims the
	// request anyway: the claim must not bring the ping back.
	insert.requestPing();
	const auto requested = insert.pingWord();
	insert.cancelPing();
	QVERIFY2(!insert.claimPing(requested), "a period that had read the request put the ping back after the cancel");
	QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Idle));
	QCOMPARE(insert.settings().measured, -1);

	// The click is on its way; the undo calls the ping off; the period hears the click and
	// publishes what it measured: the answer must not land either.
	insert.requestPing();
	QVERIFY(insert.claimPing(insert.pingWord()));
	QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Waiting));
	const auto waiting = insert.pingWord();
	insert.cancelPing();
	QVERIFY2(!insert.publishPingResult(waiting, 1234), "a ping that was called off published its measurement");
	QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Idle));
	QCOMPARE(insert.settings().measured, -1);
}
void HardwareIoTest::everythingSurvivesASaveAndLoad() {
	Mixer* mixer = Engine::mixer();
	mixer->clear();  // a project with nothing routed
	while (mixer->numChannels() <= 2) { mixer->createChannel(); }
	QDomDocument plain;
	QDomElement plainMixer = plain.createElement(QStringLiteral("mixer"));
	plain.appendChild(plainMixer);
	mixer->saveSettings(plain, plainMixer);
	QVERIFY2(!plain.toString().contains(QStringLiteral("hardware")) && !plain.toString().contains(QStringLiteral("cue")),
		"a project that routes nothing to the hardware saves none of it");

	QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("output"), QStringLiteral("5/6")}}));
	QVERIFY(ok(QStringLiteral("mixer.cue_send"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("cue"), QStringLiteral("B")},
		{QStringLiteral("level"), 0.7}, {QStringLiteral("pan"), 0.25}, {QStringLiteral("pre_fader"), false}}));
	QVERIFY(ok(QStringLiteral("mixer.cue_master"), {{QStringLiteral("cue"), QStringLiteral("B")}, {QStringLiteral("name"), QStringLiteral("Drummer")},
		{QStringLiteral("output"), QStringLiteral("7/8")}, {QStringLiteral("level"), 0.8}, {QStringLiteral("follow_listen"), false},
		{QStringLiteral("listen_to"), QStringLiteral("cue")}}));
	QVERIFY(ok(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("enabled"), true},
		{QStringLiteral("send"), QStringLiteral("9/10")}, {QStringLiteral("return"), QStringLiteral("3")}, {QStringLiteral("mix"), 0.5},
		{QStringLiteral("latency_frames"), 123}}));
	QDomDocument doc;
	QDomElement element = doc.createElement(QStringLiteral("mixer"));
	doc.appendChild(element);
	mixer->saveSettings(doc, element);
	mixer->clear();
	QVERIFY(hardwareio::cueMaster(1) == hardwareio::CueMaster{});
	mixer->loadSettings(element);

	verifyLoaded(*mixer);
	mixer->clear();
}
void HardwareIoTest::nonFiniteCueSettingsSanitizeToDefaults() {
	// A hand-edited project can carry level="nan" / pan="nan": std::clamp lets NaN through,
	// and it contaminates the cue's output samples. Loading must sanitize to the defaults.
	QDomDocument doc;
	QDomElement channelElement = doc.createElement(QStringLiteral("mixerchannel"));
	doc.appendChild(channelElement);
 QDomElement element = doc.createElement(QStringLiteral("hardwareio"));
 channelElement.appendChild(element);
 QDomElement cue = doc.createElement(QStringLiteral("cue"));
 cue.setAttribute(QStringLiteral("index"), 0);
 cue.setAttribute(QStringLiteral("level"), QStringLiteral("nan"));
 cue.setAttribute(QStringLiteral("pan"), QStringLiteral("nan"));
 element.appendChild(cue);
 ChannelHardwareIo loaded;
	 loaded.load(channelElement, Engine::audioEngine()->framesPerPeriod());
 QVERIFY(std::isfinite(loaded.cues[0].level) && std::isfinite(loaded.cues[0].pan));
 QCOMPARE(loaded.cues[0].level, 0.f);  // the attribute's own default: no send
 QCOMPARE(loaded.cues[0].pan, 0.f);    // centred

 QDomDocument mixDoc;
 QDomElement mixerElement = mixDoc.createElement(QStringLiteral("mixer"));
 mixDoc.appendChild(mixerElement);
 QDomElement cuemix = mixDoc.createElement(QStringLiteral("cuemix"));
 cuemix.setAttribute(QStringLiteral("index"), 0);
 cuemix.setAttribute(QStringLiteral("level"), QStringLiteral("nan"));
 mixerElement.appendChild(cuemix);
 hardwareio::loadMixer(mixerElement);
 QVERIFY(std::isfinite(hardwareio::cueMaster(0).level));
 QCOMPARE(hardwareio::cueMaster(0).level, 1.f);  // the cuemix's default level
 hardwareio::resetMixer();
}
void HardwareIoTest::malformedOutputKeepsTheDefaultRoute() {
	QDomDocument doc;
	QDomElement channelElement = doc.createElement(QStringLiteral("mixerchannel"));
	doc.appendChild(channelElement);
	QDomElement element = doc.createElement(QStringLiteral("hardwareio"));
	element.setAttribute(QStringLiteral("output"), QStringLiteral("3/5"));
	channelElement.appendChild(element);

	ChannelHardwareIo io;
	io.load(channelElement, Engine::audioEngine()->framesPerPeriod());
	QVERIFY2(!io.outputSet, "a route that does not parse must not replace the default");
	QVERIFY2(io.effectiveOutput(true) == (audioio::Destination{0, 2}),
		"the master must keep its default route (the outputs 1/2)");

	QDomElement noneChannel = doc.createElement(QStringLiteral("mixerchannel"));
	doc.appendChild(noneChannel);
	QDomElement noneElement = doc.createElement(QStringLiteral("hardwareio"));
	noneElement.setAttribute(QStringLiteral("output"), QStringLiteral("none"));
	noneChannel.appendChild(noneElement);

	ChannelHardwareIo none;
	none.load(noneChannel, Engine::audioEngine()->framesPerPeriod());
	QVERIFY(none.outputSet);
	QVERIFY(none.effectiveOutput(true).none());
}
void HardwareIoTest::malformedDuplicateCueOutputKeepsItsPreviousDefaultSemantics() {
	QDomDocument doc;
	QDomElement mixerElement = doc.createElement(QStringLiteral("mixer"));
	doc.appendChild(mixerElement);
	for (const QString& output : {QStringLiteral("3"), QStringLiteral("3/5")})
	{
		QDomElement cue = doc.createElement(QStringLiteral("cuemix"));
		cue.setAttribute(QStringLiteral("index"), 0);
		cue.setAttribute(QStringLiteral("output"), output);
		mixerElement.appendChild(cue);
	}

	// Before the shared parser refactor, the later malformed record set the cue back to its
	// default (nowhere) instead of retaining the earlier record's route.
	hardwareio::loadMixer(mixerElement);
	QVERIFY(hardwareio::cueMaster(0).output.none());
	hardwareio::resetMixer();
}
void HardwareIoTest::channelCueLoadsRequireExplicitIndices() {
	bool ignored = true;
	for (const bool omitIndex : {true, false})
	{
		QDomDocument doc;
		QDomElement channel = doc.createElement(QStringLiteral("channel"));
		doc.appendChild(channel);
		QDomElement hardware = doc.createElement(QStringLiteral("hardwareio"));
		channel.appendChild(hardware);
		QDomElement cue = doc.createElement(QStringLiteral("cue"));
		if (!omitIndex) { cue.setAttribute(QStringLiteral("index"), QStringLiteral("broken")); }
		cue.setAttribute(QStringLiteral("level"), QStringLiteral("0.75"));
		hardware.appendChild(cue);

		ChannelHardwareIo io;
		io.load(channel, Engine::audioEngine()->framesPerPeriod());
		ignored = ignored && io.cues[0] == hardwareio::CueSend{};
	}
	QVERIFY(ignored);
}
void HardwareIoTest::cueMasterLoadsRequireExplicitIndices() {
	bool ignored = true;
	for (const bool omitIndex : {true, false})
	{
		QDomDocument doc;
		QDomElement mixer = doc.createElement(QStringLiteral("mixer"));
		doc.appendChild(mixer);
		QDomElement cue = doc.createElement(QStringLiteral("cuemix"));
		if (!omitIndex) { cue.setAttribute(QStringLiteral("index"), QStringLiteral("broken")); }
		cue.setAttribute(QStringLiteral("name"), QStringLiteral("Invalid cue"));
		mixer.appendChild(cue);

		hardwareio::loadMixer(mixer);
		ignored = ignored && hardwareio::cueMaster(0) == hardwareio::CueMaster{};
	}
	QVERIFY(ignored);
}
void HardwareIoTest::malformedPrefaderKeepsItsDefault() {
	// "prefader" defaults to true. A malformed value ("abc") is not a zero: it must keep the default,
	// while a well-formed "0" still switches the send to post-fader.
	QDomDocument doc;
	QDomElement channelElement = doc.createElement(QStringLiteral("mixerchannel"));
	doc.appendChild(channelElement);
	QDomElement element = doc.createElement(QStringLiteral("hardwareio"));
	channelElement.appendChild(element);
	QDomElement malformed = doc.createElement(QStringLiteral("cue"));
	malformed.setAttribute(QStringLiteral("index"), 0);
	malformed.setAttribute(QStringLiteral("level"), QStringLiteral("0.5"));
	malformed.setAttribute(QStringLiteral("prefader"), QStringLiteral("abc"));
	element.appendChild(malformed);
	QDomElement zero = doc.createElement(QStringLiteral("cue"));
	zero.setAttribute(QStringLiteral("index"), 1);
	zero.setAttribute(QStringLiteral("level"), QStringLiteral("0.5"));
	zero.setAttribute(QStringLiteral("prefader"), QStringLiteral("0"));
	element.appendChild(zero);
	ChannelHardwareIo loaded;
	loaded.load(channelElement, Engine::audioEngine()->framesPerPeriod());
	QVERIFY(loaded.cues[0].preFader);   // malformed: the default, pre-fader
	QVERIFY(!loaded.cues[1].preFader);  // "0" is a real value: post-fader
}
void HardwareIoTest::malformedListenKeepsItsDefault() {
	// A cue master's "listen" defaults to true. A malformed value ("abc") must keep the default,
	// while a well-formed "0" still stops the cue following the listen.
	QDomDocument mixDoc;
	QDomElement mixerElement = mixDoc.createElement(QStringLiteral("mixer"));
	mixDoc.appendChild(mixerElement);
	QDomElement malformedMix = mixDoc.createElement(QStringLiteral("cuemix"));
	malformedMix.setAttribute(QStringLiteral("index"), 0);
	malformedMix.setAttribute(QStringLiteral("listen"), QStringLiteral("abc"));
	mixerElement.appendChild(malformedMix);
	QDomElement zeroMix = mixDoc.createElement(QStringLiteral("cuemix"));
	zeroMix.setAttribute(QStringLiteral("index"), 1);
	zeroMix.setAttribute(QStringLiteral("listen"), QStringLiteral("0"));
	mixerElement.appendChild(zeroMix);
	hardwareio::loadMixer(mixerElement);
	QVERIFY(hardwareio::cueMaster(0).followListen);   // malformed: the default, follows listen
	QVERIFY(!hardwareio::cueMaster(1).followListen);  // "0" switches it off
	hardwareio::resetMixer();
}
