/*
 * DrumSynthTest.cpp - malformed .ds envelope parsing regression coverage.
 *
 * This file is part of Zene Studio (an LMMS-derived product).
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

// Complete fixture method definitions; included once after the fixture declaration.

void DrumSynthTest::rejectsOverlongEnvelopeLevel() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());

	const QString path = directory.filePath(QStringLiteral("overlong-envelope.ds"));
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly));
	QVERIFY(file.write(R"([General]
Version=DrumSynth v1.0

[Tone]
On=1
Level=128
F1=440
F2=440
Phase=90
Envelope=0,00000001 100,0
)") > 0);
	file.close();

	DrumSynth synth;
	int16_t* samples = nullptr;
	const int sampleCount = synth.GetDSFileSamples(path, samples, 1, 44100);
	const bool rendered = sampleCount > 0 && samples != nullptr;
	const bool silent = rendered
		&& std::all_of(samples, samples + sampleCount,
			[](int16_t sample) { return sample == 0; });
	delete[] samples;

	QVERIFY2(rendered, "the valid file around the malformed envelope must still render");
	QVERIFY2(silent, "an eight-character envelope level must be rejected safely");
}
void DrumSynthTest::ignoresEnvelopePointsBeyondSupportedCapacity() {
	// envpts holds 32 points per envelope and needs one slot for the end sentinel, so the 32nd
	// point must be dropped rather than written past the array and into the first level.
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString prefixPath = directory.filePath(QStringLiteral("prefix.ds"));
	const QString oversizedPath = directory.filePath(QStringLiteral("oversized.ds"));
	QVERIFY(writeToneFixture(prefixPath, envelopeText(31)));
	QVERIFY(writeToneFixture(oversizedPath, envelopeText(32)));

	const auto prefixSamples = render(prefixPath);
	const auto oversizedSamples = render(oversizedPath);
	QVERIFY(!prefixSamples.empty());
	QCOMPARE(oversizedSamples.size(), prefixSamples.size());
	QVERIFY2(oversizedSamples == prefixSamples,
		"an envelope point beyond the supported capacity changed the rendered prefix");
}
void DrumSynthTest::zeroDurationToneUsesItsStartingFrequency_data() {
	QTest::addColumn<int>("droop");
	QTest::newRow("static") << 0;
	QTest::newRow("with-droop") << 40;
}
void DrumSynthTest::zeroDurationToneUsesItsStartingFrequency() {
	QFETCH(int, droop);
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString path = directory.filePath(QStringLiteral("zero-duration.ds"));
	QVERIFY(writeZeroDurationToneFixture(path, droop));

	const auto samples = render(path);
	QCOMPARE(samples.size(), std::size_t{2400});

	// A tone without a positive envelope duration has no sweep to run, so it stays at F1.
	const float phaseIncrement = 2.f * std::numbers::pi_v<float> * 440.f / 44100.f;
	const auto expected = static_cast<int16_t>(10000.f * std::sin(phaseIncrement));
	QCOMPARE(samples.front(), expected);
}
void DrumSynthTest::rejectsNonfiniteToneFrequency_data() {
	QTest::addColumn<QString>("f1Line");
	QTest::newRow("nan") << QStringLiteral("F1=nan\n");
	QTest::newRow("inf") << QStringLiteral("F1=inf\n");
	QTest::newRow("negative-inf") << QStringLiteral("F1=-inf\n");
}
void DrumSynthTest::rejectsNonfiniteToneFrequency() {
	// A non-finite F1 must fall back to its default, so the render matches a file without the key.
	QFETCH(QString, f1Line);
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString envelope = QStringLiteral("0,100 1200,0");
	const QString nonfinitePath = directory.filePath(QStringLiteral("nonfinite-f1.ds"));
	const QString defaultPath = directory.filePath(QStringLiteral("default-f1.ds"));
	QVERIFY(writeToneFrequencyFixture(nonfinitePath, f1Line, envelope));
	QVERIFY(writeToneFrequencyFixture(defaultPath, QString(), envelope));

	const auto expected = render(defaultPath);
	QVERIFY(std::any_of(expected.begin(), expected.end(), [](int16_t sample) { return sample != 0; }));
	QVERIFY2(render(nonfinitePath) == expected, "a non-finite F1 must render as the default frequency");
}
void DrumSynthTest::rejectsNonfiniteEnvelopeLevel_data() {
	QTest::addColumn<QString>("level");
	QTest::newRow("nan") << QStringLiteral("nan");
	QTest::newRow("inf") << QStringLiteral("inf");
	QTest::newRow("negative-inf") << QStringLiteral("-inf");
}
void DrumSynthTest::rejectsNonfiniteEnvelopeLevel() {
	// A non-finite envelope level must be stored as zero, so the render matches a literal zero level.
	QFETCH(QString, level);
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString nonfinitePath = directory.filePath(QStringLiteral("nonfinite-envelope.ds"));
	const QString zeroPath = directory.filePath(QStringLiteral("zero-envelope.ds"));
	QVERIFY(writeToneFixture(nonfinitePath, QStringLiteral("0,100 600,%1 1200,0").arg(level)));
	QVERIFY(writeToneFixture(zeroPath, QStringLiteral("0,100 600,0 1200,0")));

	const auto expected = render(zeroPath);
	QVERIFY(std::any_of(expected.begin(), expected.end(), [](int16_t sample) { return sample != 0; }));
	QVERIFY2(render(nonfinitePath) == expected, "a non-finite envelope level must be stored as zero");
}
void DrumSynthTest::emptyEnvelopeFieldIsZeroNotAStaleToken_data() {
	QTest::addColumn<QString>("malformed");
	QTest::addColumn<QString>("literal");
	// The empty level after "10," must not reuse the "10" read just before it.
	QTest::newRow("empty-level") << QStringLiteral("0,100 10, 20,100")
		<< QStringLiteral("0,100 10,0 20,100");
	// The empty time before ",100" must not reuse the "50" level read just before it.
	QTest::newRow("empty-time") << QStringLiteral("0,100 10,50 ,100 30,0")
		<< QStringLiteral("0,100 10,50 0,100 30,0");
}
void DrumSynthTest::emptyEnvelopeFieldIsZeroNotAStaleToken() {
	QFETCH(QString, malformed);
	QFETCH(QString, literal);
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString malformedPath = directory.filePath(QStringLiteral("empty-field-envelope.ds"));
	const QString literalPath = directory.filePath(QStringLiteral("literal-zero-envelope.ds"));
	QVERIFY(writeToneFixture(malformedPath, malformed));
	QVERIFY(writeToneFixture(literalPath, literal));

	const auto expected = render(literalPath);
	QVERIFY(!expected.empty());
	QVERIFY2(render(malformedPath) == expected,
		"an empty envelope field must be stored as zero, not as the token before it");
}
void DrumSynthTest::emptyEnvelopeFieldIgnoresAnEarlierParse() {
	// The envelope parse state is thread local and outlives one file. The earlier file leaves a level
	// of 50 in the slot that the empty level field of the next file reaches, so the empty field must
	// overwrite it with zero rather than keep it.
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString earlierPath = directory.filePath(QStringLiteral("earlier-envelope.ds"));
	const QString emptyPath = directory.filePath(QStringLiteral("empty-level-envelope.ds"));
	const QString literalPath = directory.filePath(QStringLiteral("literal-zero-envelope.ds"));
	QVERIFY(writeToneFixture(earlierPath, QStringLiteral("0,100 10,50 20,100")));
	QVERIFY(writeToneFixture(emptyPath, QStringLiteral("0,100 10, 20,100")));
	QVERIFY(writeToneFixture(literalPath, QStringLiteral("0,100 10,0 20,100")));

	QVERIFY(!render(earlierPath).empty());
	const auto expected = render(literalPath);
	QVERIFY(!expected.empty());
	QVERIFY2(render(emptyPath) == expected,
		"an empty envelope level must be stored as zero, not kept from an earlier parse");
}
void DrumSynthTest::noiseBandStepCountStaysDefinedForOutOfRangeQuotients() {
	// The step quotient is converted to an int only when it is finite and in range; NaN and
	// out-of-range values take the defined results below instead of an undefined conversion.
	constexpr long maxStep = std::numeric_limits<int>::max();
	QCOMPARE(DrumSynth::noiseBandStepCount(41.7), 42L);
	QCOMPARE(DrumSynth::noiseBandStepCount(-3.0), 1L);
	QCOMPARE(DrumSynth::noiseBandStepCount(std::numeric_limits<double>::quiet_NaN()), 1L);
	QCOMPARE(DrumSynth::noiseBandStepCount(std::numeric_limits<double>::infinity()), maxStep);
	QCOMPARE(DrumSynth::noiseBandStepCount(1e300), maxStep);
}
void DrumSynthTest::negativeNoiseBandFrequencyRendersLikeTheBandOff() {
	// A disabled noise band with a negative frequency still derives its step; the render must match a file without the band.
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QByteArray tone = "[General]\nVersion=DrumSynth v2.0\n[Tone]\nOn=1\nLevel=100\n"
							"Envelope=0,100 2400,100 4800,0\nF1=440\nF2=220\n";
	const auto writeBytes = [](const QString& path, const QByteArray& contents) {
		QFile file(path);
		return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
	};
	const QString plainPath = directory.filePath(QStringLiteral("plain-tone.ds"));
	const QString bandPath = directory.filePath(QStringLiteral("negative-band-off.ds"));
	QVERIFY(writeBytes(plainPath, tone));
	QVERIFY(writeBytes(bandPath, tone + "[NoiseBand]\nOn=0\nF=-100\nEnvelope=0,100 4800,0\n"));

	const auto expected = render(plainPath);
	QVERIFY(std::any_of(expected.begin(), expected.end(), [](int16_t sample) { return sample != 0; }));
	QVERIFY2(render(bandPath) == expected, "a disabled band with a negative frequency must not change the render");
}
void DrumSynthTest::refusesAnEnvelopeWhoseRoundedLengthOverflows() {
	QTemporaryDir tempDir;
	QVERIFY(tempDir.isValid());
	QFile file(tempDir.filePath("oversized.ds"));
	QVERIFY(file.open(QIODevice::WriteOnly));
	const QByteArray content = "[General]\nVersion=DrumSynth v2.0\nStretch=100\n\n"
		"[Tone]\nOn=1\nEnvelope=0,0 3e9,0\n";
	QCOMPARE(file.write(content), static_cast<qint64>(content.size()));
	file.close();

	int16_t* samples = nullptr;
	int frames = -1;
	bool allocationFailed = false;
	try
	{
		DrumSynth synth;
		frames = synth.GetDSFileSamples(file.fileName(), samples, 2, 44100);
	}
	catch (const std::bad_alloc&)
	{
		allocationFailed = true;
	}
	delete[] samples;

	QVERIFY2(!allocationFailed, "An overflowing duration must be rejected before allocation");
	QCOMPARE(frames, 0);
	QVERIFY(samples == nullptr);
}
void DrumSynthTest::refusesAnEnvelopeLongerThanTheRendererHold() {
	QTemporaryDir tempDir;
	QVERIFY(tempDir.isValid());
	QFile file(tempDir.filePath("long-hold.ds"));
	QVERIFY(file.open(QIODevice::WriteOnly));
	const QByteArray content = "[General]\nVersion=DrumSynth v2.0\nStretch=1000\n\n"
		"[Tone]\nOn=1\nEnvelope=0,100 500000,0\n";
	QCOMPARE(file.write(content), static_cast<qint64>(content.size()));
	file.close();

	int16_t* samples = nullptr;
	DrumSynth synth;
	const int frames = synth.GetDSFileSamples(file.fileName(), samples, 1, 44100);
	const auto owned = std::unique_ptr<int16_t[]>{samples};

	QCOMPARE(frames, 0);
	QVERIFY(samples == nullptr);
}
void DrumSynthTest::rendersTheLongestShippedEnvelopeAtMaximumStretch() {
	QTemporaryDir tempDir;
	QVERIFY(tempDir.isValid());
	QFile file(tempDir.filePath("shipped-hold.ds"));
	QVERIFY(file.open(QIODevice::WriteOnly));
	const QByteArray content = "[General]\nVersion=DrumSynth v2.0\nStretch=1000\n\n"
		"[Tone]\nOn=1\nEnvelope=0,100 438020,0\n";
	QCOMPARE(file.write(content), static_cast<qint64>(content.size()));
	file.close();

	const auto samples = render(file.fileName());
	QVERIFY2(!samples.empty(), "an envelope under the renderer's hold must still render");
}
void DrumSynthTest::mapsAFractionalStepAboveMaxToTheSafeMaximumLength() {
	// globals declared at file scope below

	// Reset envelope state, then install a duration whose step count is
	// fractional just above maxSteps; truncation must yield maxSteps.
	for (int e = 0; e < 8; e++)
	{
		chkOn[e] = 0;
		for (int t = 0; t < 3; t++)
		{
			for (int p = 0; p < 32; p++)
			{
				envpts[e][t][p] = -1.f;
			}
		}
	}
	constexpr int blockLength = 1200;
	constexpr int baseLength = 2 * blockLength;
	constexpr int maxSteps = (std::numeric_limits<int>::max() - baseLength) / blockLength;
	const float boundarySteps = static_cast<float>(maxSteps) + 0.5f;
	envpts[1][0][0] = boundarySteps * blockLength;
	envpts[1][0][1] = -1.f;
	chkOn[0] = 1;
	timestretch = 1.f;

	DrumSynth synth;
	const int length = synth.LongestEnv();
	const int expectedLength = baseLength + (blockLength * maxSteps);
	QCOMPARE(length, expectedLength);
}
void DrumSynthTest::boundedProfileValuesKeepTheirSettings() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());

	const QByteArray base = drumSynthFixture(100, false, false);
	QByteArray padded = base;
	padded.replace("Version=DrumSynth v2.0", QByteArray("Version=DrumSynth v2.0") + QByteArray(64, 'x'));
	QByteArray missing = base;
	missing.replace("Version=DrumSynth v2.0\n", "");
	QVERIFY(padded != base);
	QVERIFY(missing != base);

	const QString basePath = directory.filePath(QStringLiteral("base-version.ds"));
	const QString paddedPath = directory.filePath(QStringLiteral("padded-version.ds"));
	const QString missingPath = directory.filePath(QStringLiteral("missing-version.ds"));
	QVERIFY(writeDrumSynthFixture(basePath, base));
	QVERIFY(writeDrumSynthFixture(paddedPath, padded));
	QVERIFY(writeDrumSynthFixture(missingPath, missing));

	const auto expected = render(basePath);
	QVERIFY(!expected.empty());
	QVERIFY2(render(paddedPath) == expected,
		"a Version value longer than its buffer must be cut to the buffer and still be read");
	QVERIFY2(render(missingPath).empty(), "a file without a Version key must not render");
}
void DrumSynthTest::longLinesDoNotStopProfileParsing() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());

	const QString plainPath = directory.filePath(QStringLiteral("plain.ds"));
	const QString leadingPath = directory.filePath(QStringLiteral("leading-long-line.ds"));
	const QString middlePath = directory.filePath(QStringLiteral("middle-long-line.ds"));
	QVERIFY(writeDrumSynthFixture(plainPath, drumSynthFixture(100, false, false)));
	QVERIFY(writeDrumSynthFixture(leadingPath, drumSynthFixture(100, true, false)));
	QVERIFY(writeDrumSynthFixture(middlePath, drumSynthFixture(100, false, true)));

	const auto plain = render(plainPath);
	QVERIFY(!plain.empty());

	const auto leading = render(leadingPath);
	QCOMPARE(leading.size(), plain.size());
	QVERIFY2(leading == plain,
		"a long line in front of the settings stopped the DrumSynth profile parser");

	const auto middle = render(middlePath);
	QCOMPARE(middle.size(), leading.size());
	QVERIFY2(middle == leading,
		"a long line between the settings stopped the DrumSynth profile parser");
}
void DrumSynthTest::concurrentParsesMatchSingleThreadedParses() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());

	// Two fixtures with different settings, each of them with a plain file
	// and a file that carries an over-long line in front of the settings.
	QString plainPaths[2];
	QString longPaths[2];
	for (int fixture = 0; fixture < 2; ++fixture)
	{
		plainPaths[fixture] = directory.filePath(QStringLiteral("plain-%1.ds").arg(fixture));
		longPaths[fixture] = directory.filePath(QStringLiteral("long-%1.ds").arg(fixture));
		const int level = fixture == 0 ? 100 : 60;
		QVERIFY(writeDrumSynthFixture(plainPaths[fixture], drumSynthFixture(level, false, false)));
		QVERIFY(writeDrumSynthFixture(longPaths[fixture], drumSynthFixture(level, true, false)));
	}

	// The reference: the same two files, parsed one after the other.
	std::vector<std::vector<int16_t>> referencePlain(2);
	std::vector<std::vector<int16_t>> referenceLong(2);
	for (int fixture = 0; fixture < 2; ++fixture)
	{
		referencePlain[static_cast<std::size_t>(fixture)] = render(plainPaths[fixture]);
		referenceLong[static_cast<std::size_t>(fixture)] = render(longPaths[fixture]);
		QVERIFY(!referencePlain[static_cast<std::size_t>(fixture)].empty());
	}

	constexpr int threadCount = 2;
	std::vector<std::vector<std::vector<int16_t>>> results(
		threadCount, std::vector<std::vector<int16_t>>(2));
	armStrtokRendezvous(threadCount);
	std::vector<std::thread> threads;
	for (int thread = 0; thread < threadCount; ++thread)
	{
		threads.emplace_back([&, thread]() {
			joinStrtokRendezvous();
			const int fixture = thread % 2;
			results[static_cast<std::size_t>(thread)][0] = render(plainPaths[fixture]);
			results[static_cast<std::size_t>(thread)][1] = render(longPaths[fixture]);
		});
	}
	for (std::thread& thread : threads) { thread.join(); }
	disarmStrtokRendezvous();

	for (int thread = 0; thread < threadCount; ++thread)
	{
		const int fixture = thread % 2;
		const auto& plain = results[static_cast<std::size_t>(thread)][0];
		const auto& longLine = results[static_cast<std::size_t>(thread)][1];
		QVERIFY2(!plain.empty(),
			"a thread that parsed a .ds file at the same time as another one read "
			"no settings at all");
		// The frames are the ones a parse on its own produces.
		QCOMPARE(plain.size(), referencePlain[static_cast<std::size_t>(fixture)].size());
		QCOMPARE(longLine.size(), referenceLong[static_cast<std::size_t>(fixture)].size());
		// The samples of the two files of this thread are the ones of a parse
		// on its own: the renderer keeps its state in the thread that runs
		// it, so the two files of a thread are the ones to compare.
		QVERIFY2(longLine == plain,
			"a thread that parsed a .ds file with a long line at the same time as "
			"another thread saw different settings");
	}
}
