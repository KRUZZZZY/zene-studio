/*
 * DeviceDspTest.cpp - the de-esser, transient shaper, vocoder and console devices, and the console across the mix
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

void DeviceDspTest::aHighRateImpulseIsResampledWithoutAliasing() {
	const QString reverb = loadByName(ch(1), QStringLiteral("convolutionreverb"));
	if (reverb.isEmpty()) { QSKIP("this build has no convolution reverb module"); }
	QTemporaryDir dir;
	const QString file = dir.filePath(QStringLiteral("high-rate.wav"));
	std::vector<qint16> samples(2048, 0);
	samples[0] = 16384;
	for (std::size_t n = 0; n < samples.size(); ++n)  // 0.375 of the file's rate: 0.75 of the engine's
	{
		samples[n] = static_cast<qint16>(samples[n] + 2000.0 * std::sin(kTwoPi * 0.375 * static_cast<double>(n)));
	}
	QVERIFY(writeWav(file, samples, static_cast<quint32>(2 * rate())));
	const ControlResult loaded = run(QStringLiteral("plugin.impulse_load"),
		{{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), reverb}, {QStringLiteral("path"), file}});
	QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
	long at = 0;
	const float low = runBlocks(chainOf(1), sine(1000.0, 0.25f), 80, &at)[0];
	const float folded = runBlocks(chainOf(1), sine(0.99 * rate() / 4.0, 0.25f), 80, &at)[0];
	QVERIFY2(low > 0.01f, qPrintable(QString::number(low)));
	QVERIFY2(folded / low < 1.5f && folded / low > 0.67f,
		qPrintable(QStringLiteral("a quarter of the rate %1, 1 kHz %2").arg(folded).arg(low)));
	chainOf(1).clear();
}
//! The 'impulse' attribute the convolution reverb on @a chain would save; empty when there is none.
QString savedReverbImpulse(EffectChain& chain)
{
	for (Effect* effect : chain.effects())
	{
		if (QString::fromUtf8(effect->descriptor()->name) != QLatin1String("convolutionreverb")) { continue; }
		QDomDocument doc;
		if (!doc.setContent(controlEffectStateXml(effect))) { return QString(); }
		return doc.elementsByTagName(QStringLiteral("ConvolutionReverbControls")).item(0).toElement()
			.attribute(QStringLiteral("impulse"));
	}
	return QString();
}

void DeviceDspTest::anUnresamplableImpulseIsRefusedThroughTheCommand() {
	const QString reverb = loadByName(ch(1), QStringLiteral("convolutionreverb"));
	if (reverb.isEmpty()) { QSKIP("this build has no convolution reverb module"); }
	QTemporaryDir dir;
	const QString click = dir.filePath(QStringLiteral("click.wav"));
	std::vector<qint16> samples(64, 0);
	samples[0] = 16384;
	QVERIFY(writeWav(click, samples));
	const QJsonObject device{{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), reverb}};
	QJsonObject load = device;
	load.insert(QStringLiteral("path"), click);
	const ControlResult loaded = run(QStringLiteral("plugin.impulse_load"), load);
	QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
	const QString clickPath = savedReverbImpulse(chainOf(1));
	QVERIFY2(clickPath == QFileInfo(click).absoluteFilePath(), qPrintable(clickPath));
	long at = 0;
	const float before = runBlocks(chainOf(1), sine(1000.0, 0.5f), 80, &at)[0];
	QVERIFY2(before > 0.1f, qPrintable(QString::number(before)));
	// A successful save is the public way to a clean modified flag (setModified(bool) is private).
	const ControlResult saved = run(QStringLiteral("project.save"),
		{{QStringLiteral("path"), dir.filePath(QStringLiteral("session.mmp"))}});
	QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
	QVERIFY2(!Engine::getSong()->isModified(), "a successful save left the project modified");
	const int undoDepthBefore = Engine::projectJournal()->undoDepth();

	const QString oneHertz = dir.filePath(QStringLiteral("one-hertz.wav"));
	std::vector<qint16> badSamples(8, 0);
	badSamples[0] = 16384;
	QVERIFY(writeWav(oneHertz, badSamples, 1));
	QJsonObject bad = device;
	bad.insert(QStringLiteral("path"), oneHertz);
	const ControlResult refused = run(QStringLiteral("plugin.impulse_load"), bad);
	QVERIFY2(!refused.ok, "an impulse the resampler cannot convert was accepted");
	QVERIFY2(refused.errorKind == ControlErrorKind::Refused, qPrintable(refused.errorMessage));
	QVERIFY2(!refused.errorMessage.isEmpty(), "the refusal has no message");
	QVERIFY2(savedReverbImpulse(chainOf(1)) == clickPath, "the refused impulse replaced the saved one");
	const float after = runBlocks(chainOf(1), sine(1000.0, 0.5f), 80, &at)[0];
	QVERIFY2(std::abs(after - before) < 0.03f, qPrintable(QStringLiteral("before %1, after %2").arg(before).arg(after)));
	QVERIFY2(Engine::projectJournal()->undoDepth() == undoDepthBefore,
		qPrintable(QStringLiteral("the refused load added undo steps: depth %1 -> %2")
			.arg(undoDepthBefore).arg(Engine::projectJournal()->undoDepth())));
	QVERIFY2(!Engine::getSong()->isModified(), "the refused load marked the project modified");

	const ControlResult again = run(QStringLiteral("plugin.impulse_load"), load);
	QVERIFY2(again.ok, qPrintable(again.errorMessage));
	chainOf(1).clear();
}
void DeviceDspTest::theConvolutionReverbDropsOldRateStateOnASampleRateChange() {
	const QString reverb = loadByName(ch(1), QStringLiteral("convolutionreverb"));
	if (reverb.isEmpty()) { QSKIP("this build has no convolution reverb module"); }
	AudioDevice* const device = Engine::audioEngine()->audioDev();
	QVERIFY(device != nullptr);
	const sample_rate_t originalRate = device->sampleRate();
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	// Put the engine's rate back, with the signal a real device setup would send, however this test leaves.
	// Declared after the directory so it is destroyed before it.
	struct RestoreRate
	{
		AudioDevice* device;
		sample_rate_t rate;
		~RestoreRate()
		{
			device->setSampleRateForTesting(rate);
			emit Engine::audioEngine()->sampleRateChanged();
		}
	} restore{device, originalRate};
	const QString click = dir.filePath(QStringLiteral("click.wav"));
	std::vector<qint16> samples(64, 0);
	samples[0] = 16384;
	QVERIFY(writeWav(click, samples, 48000));
	device->setSampleRateForTesting(48000);
	emit Engine::audioEngine()->sampleRateChanged();
	const QString target = ch(1);
	QJsonObject load{{QStringLiteral("target"), target}, {QStringLiteral("plugin"), reverb},
		{QStringLiteral("path"), click}};
	const ControlResult loaded = run(QStringLiteral("plugin.impulse_load"), load);
	QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
	set(target, reverb, "Pre-delay", 100.0);
	const Signal loud = sine(1000.0, 0.5f);
	const Signal quiet = [](long) { return SampleFrame(); };
	long at = 0;
	// 0.32 s at 48 kHz fills the whole 100 ms pre-delay line with the old-rate tone.
	const SampleFrame before = runBlocks(chainOf(1), loud, 60, &at);
	QVERIFY2(before[0] > 0.1f, qPrintable(QString::number(before[0])));
	// The same 100 ms is twice as many frames at 96 kHz. The line, the queued blocks and the filter
	// histories still hold the 48 kHz tone, which belongs 200 ms back at the new scale: not silence.
	device->setSampleRateForTesting(96000);
	emit Engine::audioEngine()->sampleRateChanged();
	const SampleFrame stale = runBlocks(chainOf(1), quiet, 8, &at);
	QVERIFY2(stale[0] < 1.0e-6f && stale[1] < 1.0e-6f,
		qPrintable(QStringLiteral("old-rate audio survived the rate change: %1, %2").arg(stale[0]).arg(stale[1])));
	// The reverb still plays a signal arriving at the new rate, one new-rate pre-delay later.
	const SampleFrame fresh = runBlocks(chainOf(1), loud, 60, &at);
	QVERIFY2(fresh[0] > 0.1f, qPrintable(QStringLiteral("no audio after the rate change: %1").arg(fresh[0])));
	// A rate change the impulse cannot follow: the file is unreadable, so the rebuild fails and the
	// old kernel stays installed. Its overlap and frequency-domain history hold the 96 kHz tone and
	// must not play it back as a 48 kHz one.
	QVERIFY(QFile::remove(click));
	device->setSampleRateForTesting(48000);
	emit Engine::audioEngine()->sampleRateChanged();
	const SampleFrame unreadable = runBlocks(chainOf(1), quiet, 8, &at);
	QVERIFY2(unreadable[0] < 1.0e-6f && unreadable[1] < 1.0e-6f,
		qPrintable(QStringLiteral("old-rate audio survived a failed rebuild: %1, %2")
			.arg(unreadable[0]).arg(unreadable[1])));
	chainOf(1).clear();
}
void DeviceDspTest::recorderLandRefusesWithoutSong() {
	const QString recorder = loadByName(ch(1), QStringLiteral("insertrecorder"));
	if (recorder.isEmpty()) { QSKIP("this build has no recorder module"); }
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QJsonObject device{{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), recorder}};
	QJsonObject start = device;
	start.insert(QStringLiteral("on"), true);
	start.insert(QStringLiteral("file"), dir.filePath(QStringLiteral("take.wav")));
	const ControlResult started = run(QStringLiteral("plugin.record"), start);
	QVERIFY2(started.ok, qPrintable(started.errorMessage));
	QJsonObject stop = device;
	stop.insert(QStringLiteral("on"), false);
	QVERIFY2(run(QStringLiteral("plugin.record"), stop).ok, "could not finish the test take");

	QVERIFY(Engine::s_song != nullptr);
	const ControlCommand* command = ControlRegistry::instance()->command(QStringLiteral("plugin.recorder_land"));
	QVERIFY(command != nullptr);
	const ControlResult landed = [&]()
	{
		Song* song = Engine::s_song;
		Engine::s_song = nullptr;
		// The public registry rejects a missing song before calling the handler; test the handler's
		// own no-song safety directly so this failure path is covered too.
		const ControlResult result = command->handler(device);
		Engine::s_song = song;
		return result;
	}();
	QCOMPARE(landed.errorKind, ControlErrorKind::Refused);
	QVERIFY(landed.errorMessage.contains(QStringLiteral("song"), Qt::CaseInsensitive));
}
void DeviceDspTest::emptyRecorderFileUsesDefaultTakePath() {
	const QString recorder = loadByName(ch(1), QStringLiteral("insertrecorder"));
	if (recorder.isEmpty()) { QSKIP("this build has no recorder module"); }
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	ConfigManager* config = ConfigManager::inst();
	struct RestoreWorkingDir
	{
		QString path;
		~RestoreWorkingDir() { ConfigManager::inst()->setWorkingDir(path); }
	} restore{config->workingDir()};
	config->setWorkingDir(dir.path());

	const QJsonObject device{{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), recorder}};
	QJsonObject start = device;
	start.insert(QStringLiteral("on"), true);
	start.insert(QStringLiteral("file"), QString());
	const ControlResult started = run(QStringLiteral("plugin.record"), start);
	QVERIFY2(started.ok, qPrintable(started.errorMessage));
	const QString take = started.result.value(QStringLiteral("take")).toString();
	QVERIFY(!take.isEmpty());
	QCOMPARE(QFileInfo(take).absolutePath(), QDir(config->userSamplesDir()).filePath(QStringLiteral("Recordings")));
	QVERIFY(QFileInfo(take).fileName().startsWith(QStringLiteral("Take ")));

	QJsonObject stop = device;
	stop.insert(QStringLiteral("on"), false);
	const ControlResult stopped = run(QStringLiteral("plugin.record"), stop);
	QVERIFY2(stopped.ok, qPrintable(stopped.errorMessage));
	QVERIFY(!stopped.result.value(QStringLiteral("recording")).toBool());
	QVERIFY(QFileInfo::exists(take));
}
void DeviceDspTest::theRecorderRecordsWhatPassesIt() {
	const QString recorder = loadByName(ch(1), QStringLiteral("insertrecorder"));
	if (recorder.isEmpty()) { QSKIP("this build has no recorder module"); }
	QTemporaryDir dir;
	const QString file = dir.filePath(QStringLiteral("take.wav"));
	const QJsonObject device{{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), recorder}};
	QJsonObject start = device;
	start.insert(QStringLiteral("on"), true);
	start.insert(QStringLiteral("file"), file);
	const ControlResult started = run(QStringLiteral("plugin.record"), start);
	QVERIFY2(started.ok, qPrintable(started.errorMessage));
	QVERIFY(started.result.value(QStringLiteral("recording")).toBool());
	long at = 0;
	const int blocks = 20;
	QVERIFY(std::abs(runBlocks(chainOf(1), sine(440.0, 0.5f), blocks, &at)[0] - 0.5f) < 0.01f);  // passes unchanged
	QJsonObject stop = device;
	stop.insert(QStringLiteral("on"), false);
	const ControlResult stopped = run(QStringLiteral("plugin.record"), stop);
	QVERIFY(stopped.ok);
	QCOMPARE(stopped.result.value(QStringLiteral("frames")).toDouble(), static_cast<double>(blocks) * static_cast<double>(block().size()));
	QCOMPARE(stopped.result.value(QStringLiteral("dropped")).toDouble(), 0.0);
	QVERIFY(QFileInfo(file).size() > 44);
	const int tracks = static_cast<int>(Engine::getSong()->tracks().size());
	const ControlResult landed = run(QStringLiteral("plugin.recorder_land"), device);
	QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
	QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracks + 1);
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracks);
	const QString console = loadByName(ch(1), QStringLiteral("console"));
	QVERIFY(!console.isEmpty());
	QCOMPARE(run(QStringLiteral("plugin.record"), {{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), console}}).errorKind,
		ControlErrorKind::Refused);
}
void DeviceDspTest::theConsoleGoesOnEveryChannel() {
	const QString console = loadByName(ch(1), QStringLiteral("console"));
	if (console.isEmpty()) { QSKIP("this build has no console module"); }
	set(ch(1), console, "Drive", 0.0);
	long at = 0;
	QVERIFY(std::abs(runBlocks(chainOf(1), sine(1000.0, 0.05f), 60, &at)[0] - 0.05f) < 0.002f);
	set(ch(1), console, "Oversampling", 2.0);  // MX-100: 4x, still transparent when quiet
	QVERIFY(std::abs(runBlocks(chainOf(1), sine(1000.0, 0.05f), 60, &at)[0] - 0.05f) < 0.003f);
	chainOf(1).clear();
	const int channels = Engine::mixer()->numChannels();
	const ControlResult applied = run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("drive"), 50.0},
		{QStringLiteral("character"), QStringLiteral("warm")}});
	QVERIFY2(applied.ok, qPrintable(applied.errorMessage));
	QCOMPARE(applied.result.value(QStringLiteral("added")).toInt(), channels);
	QCOMPARE(consolesOn(2), 1);
	const QString onTwo = control::effectIdOf(chainOf(2).effects().back());
	QCOMPARE(parameterValue(ch(2), onTwo, QStringLiteral("Drive")), 50.0);
	QVERIFY(run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("drive"), 80.0}}).ok);
	QCOMPARE(consolesOn(2), 1);  // kept, not added again
	QCOMPARE(parameterValue(ch(2), onTwo, QStringLiteral("Drive")), 80.0);
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(parameterValue(ch(2), onTwo, QStringLiteral("Drive")), 50.0);
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(consolesOn(2), 0);
	QCOMPARE(run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("drive"), 150.0}}).errorKind, ControlErrorKind::InvalidArgs);
	QVERIFY(run(QStringLiteral("mixer.console_apply")).ok);
	QVERIFY(run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("remove"), true}}).ok);
	QCOMPARE(consolesOn(0), 0);
}
void DeviceDspTest::removingNoConsoleLeavesTheProjectClean() {
	const QString console = loadByName(ch(1), QStringLiteral("console"));
	if (console.isEmpty()) { QSKIP("this build has no console module"); }
	QVERIFY(run(QStringLiteral("project.new")).ok);
	while (Engine::mixer()->numChannels() <= 3) { Engine::mixer()->createChannel(); }
	for (int i = 0; i < Engine::mixer()->numChannels(); ++i) { chainOf(i).clear(); }
	QVERIFY(!Engine::getSong()->isModified());

	const ControlResult removed = run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("remove"), true}});
	QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
	QCOMPARE(removed.result.value(QStringLiteral("removed")).toInt(), 0);
	QVERIFY2(!Engine::getSong()->isModified(),
		"removing no console dirtied a project whose content did not change");
}
void DeviceDspTest::applyingTheValueAlreadyThereReportsNoChange() {
	const QString console = loadByName(ch(1), QStringLiteral("console"));
	if (console.isEmpty()) { QSKIP("this build has no console module"); }
	QVERIFY(run(QStringLiteral("project.new")).ok);
	while (Engine::mixer()->numChannels() <= 3) { Engine::mixer()->createChannel(); }
	for (int i = 0; i < Engine::mixer()->numChannels(); ++i) { chainOf(i).clear(); }
	QVERIFY(run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("drive"), 40.0}}).ok);
	const QString onTwo = control::effectIdOf(chainOf(2).effects().back());

	const ControlResult again = run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("drive"), 40.0}});
	QVERIFY2(again.ok, qPrintable(again.errorMessage));
	QCOMPARE(again.result.value(QStringLiteral("added")).toInt(), 0);
	QCOMPARE(parameterValue(ch(2), onTwo, QStringLiteral("Drive")), 40.0);
	QVERIFY2(again.result.value(QStringLiteral("changed")).toInt() == 0,
		"a console already carrying the requested drive was reported as changed");
}
void DeviceDspTest::applyingTheSameSettingsTakesNoUndoStepAndLeavesTheProjectClean() {
	const QString console = loadByName(ch(1), QStringLiteral("console"));
	if (console.isEmpty()) { QSKIP("this build has no console module"); }
	QVERIFY(run(QStringLiteral("project.new")).ok);
	while (Engine::mixer()->numChannels() <= 3) { Engine::mixer()->createChannel(); }
	for (int i = 0; i < Engine::mixer()->numChannels(); ++i) { chainOf(i).clear(); }
	QVERIFY(run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("drive"), 40.0}}).ok);

	Song* song = Engine::getSong();
	ProjectJournal* journal = Engine::projectJournal();
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString fixtureFile = directory.filePath(QStringLiteral("console-same.mmp"));
	QVERIFY(song->saveProjectFile(fixtureFile, false));
	song->noteProjectSaved(fixtureFile);
	QVERIFY(!song->isModified());
	const int undoDepth = journal->undoDepth();

	const ControlResult same = run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("drive"), 40.0}});
	QVERIFY2(same.ok, qPrintable(same.errorMessage));
	QCOMPARE(same.result.value(QStringLiteral("added")).toInt(), 0);
	QCOMPARE(same.result.value(QStringLiteral("changed")).toInt(), 0);
	QCOMPARE(journal->undoDepth(), undoDepth);
	QVERIFY2(!song->isModified(), "applying unchanged console settings dirtied the project");
}
void DeviceDspTest::oneConsoleApplyIsOneUndoStep() {
	const QString console = loadByName(ch(1), QStringLiteral("console"));
	if (console.isEmpty()) { QSKIP("this build has no console module"); }
	QVERIFY(run(QStringLiteral("project.new")).ok);
	while (Engine::mixer()->numChannels() <= 3) { Engine::mixer()->createChannel(); }
	const int channels = Engine::mixer()->numChannels();
	for (int i = 0; i < channels; ++i) { chainOf(i).clear(); }
	QVERIFY(run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("drive"), 30.0}}).ok);
	QCOMPARE(consolesOn(2), 1);

	QVERIFY(run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("remove"), true}}).ok);
	QCOMPARE(consolesOn(0), 0);
	QVERIFY2(run(QStringLiteral("control.undo")).ok, "one control.undo after the removal failed");
	for (int i = 0; i < channels; ++i)
	{
		QVERIFY2(consolesOn(i) == 1,
			qPrintable(QStringLiteral("one undo put the console back on only some of the %1 channels").arg(channels)));
	}
}
void DeviceDspTest::aNaNDriveIsRefusedAndChangesNothing() {
	const QString console = loadByName(ch(1), QStringLiteral("console"));
	if (console.isEmpty()) { QSKIP("this build has no console module"); }
	QVERIFY(run(QStringLiteral("project.new")).ok);
	while (Engine::mixer()->numChannels() <= 3) { Engine::mixer()->createChannel(); }
	const int channels = Engine::mixer()->numChannels();
	for (int i = 0; i < channels; ++i) { chainOf(i).clear(); }
	QVERIFY(run(QStringLiteral("mixer.console_apply"), {{QStringLiteral("drive"), 50.0}}).ok);
	QCOMPARE(consolesOn(2), 1);
	const QString onTwo = control::effectIdOf(chainOf(2).effects().back());

	const ControlResult refused = run(QStringLiteral("mixer.console_apply"),
		{{QStringLiteral("drive"), std::numeric_limits<double>::quiet_NaN()}});
	QVERIFY(!refused.ok);
	QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
	QCOMPARE(parameterValue(ch(2), onTwo, QStringLiteral("Drive")), 50.0);
	QCOMPARE(consolesOn(2), 1);

	QVERIFY(run(QStringLiteral("project.new")).ok);
	// project.new replaces the session with the default template, whose mixer can be smaller than this
	// fixture's, so index the channels that exist NOW. (mixerChannel() refuses an index past the list in
	// this LMMS_TESTING build, so the pre-new count would dereference the refusal's nullptr.)
	while (Engine::mixer()->numChannels() <= 3) { Engine::mixer()->createChannel(); }
	const int fresh = Engine::mixer()->numChannels();
	for (int i = 0; i < fresh; ++i) { QCOMPARE(consolesOn(i), 0); }
	QVERIFY(!run(QStringLiteral("mixer.console_apply"),
		{{QStringLiteral("drive"), std::numeric_limits<double>::quiet_NaN()}}).ok);
	for (int i = 0; i < fresh; ++i) { QCOMPARE(consolesOn(i), 0); }
}
