#include <limits>
#include <memory>
#include <vector>

#include <QJsonObject>
#include <QString>
#include <QTest>

#include "ClipEdits.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "ReversibilityTestSupport.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SampleWindow.h"

using namespace lmms;
using namespace revtest;

namespace
{

//! A copy of the clip's source frames, read from the MODEL.
struct Capture
{
	std::vector<SampleFrame> frames;
	int rate = 0;
};

Capture captureOf(const QString& id)
{
	control::ClipRef ref;
	ControlResult error;
	Capture out;
	if (!control::resolveClip(id, &ref, &error)) { return out; }
	auto* clip = dynamic_cast<SampleClip*>(ref.clip);
	if (clip == nullptr) { return out; }
	const Sample& source = clip->sample();
	out.frames.assign(source.data(), source.data() + source.sampleSize());
	out.rate = static_cast<int>(source.sampleRate());
	return out;
}

//! Bit-exact frame equality: a refused call must leave the source untouched.
bool sameFrames(const Capture& a, const Capture& b)
{
	if (a.frames.size() != b.frames.size()) { return false; }
	for (size_t i = 0; i < a.frames.size(); ++i)
	{
		if (a.frames[i].left() != b.frames[i].left()) { return false; }
		if (a.frames[i].right() != b.frames[i].right()) { return false; }
	}
	return true;
}

//! A tone clip on a fresh sample track, created through sample.generate.
QString makeGeneratedClip()
{
	const QString track = addTrack(QStringLiteral("sample"));
	if (track.isEmpty()) { return QString(); }
	const ControlResult generated = run(QStringLiteral("sample.generate"),
		QJsonObject{{QStringLiteral("track"), track},
			{QStringLiteral("position"), 0}, {QStringLiteral("length"), 480},
			{QStringLiteral("kind"), QStringLiteral("tone")},
			{QStringLiteral("frequency"), 440.0},
			{QStringLiteral("amplitude"), 0.8}});
	if (!generated.ok) { return QString(); }
	return generated.result.value(QStringLiteral("clip")).toString();
}

//! Replace the clip's source with \a frames, the way a bake would, but without a
//! checkpoint: this is fixture setup, not a command under test.
void setSource(const QString& id, std::vector<SampleFrame> frames, int rate)
{
	control::ClipRef ref;
	ControlResult error;
	QVERIFY(control::resolveClip(id, &ref, &error));
	auto* sampleClip = dynamic_cast<SampleClip*>(ref.clip);
	QVERIFY(sampleClip != nullptr);
	const auto count = static_cast<f_cnt_t>(frames.size());
	sampleClip->replaceSample(std::make_shared<SampleBuffer>(std::move(frames), rate),
		SampleWindow::full(count));
}

} // namespace

class ControlSampleBakeGuardTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! A NON-FINITE level is refused before anything is checkpointed. NaN passes
	//! every range test (`x < lo || x > hi` is false for NaN), so the range check
	//! alone let a NaN gain or target through and baked a NaN buffer. A finite
	//! target can reach the same place: a subnormal peak makes the scale 1/peak
	//! overflow float, and an infinite sample makes the peak itself non-finite.
	//! Each refusal writes nothing and pushes no undo step.
	void nonFiniteLevelsAreRefusedAndWriteNothing()
	{
		const QString clip = makeGeneratedClip();
		QVERIFY(!clip.isEmpty());
		const Capture before = captureOf(clip);
		QVERIFY(before.frames.size() > 32);

		const double nan = std::numeric_limits<double>::quiet_NaN();
		const ControlResult nanGain = run(QStringLiteral("sample.amplify"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("gain_db"), nan}});
		QCOMPARE(nanGain.ok, false);
		QCOMPARE(nanGain.errorKind, ControlErrorKind::InvalidArgs);
		const ControlResult nanTarget = run(QStringLiteral("sample.normalize"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("target_db"), nan}});
		QCOMPARE(nanTarget.ok, false);
		QCOMPARE(nanTarget.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(sameFrames(captureOf(clip), before));

		// A source whose only audible frame is subnormal (1e-39): 1/peak is
		// about 1e39, past float max, so the scale would be infinite.
		const auto count = before.frames.size();
		std::vector<SampleFrame> quiet(count, SampleFrame(0.0f, 0.0f));
		quiet.front() = SampleFrame(1.0e-39f, 0.0f);
		setSource(clip, std::move(quiet), before.rate);
		const Capture quietBefore = captureOf(clip);
		QVERIFY(quietBefore.frames.size() == count);
		const ControlResult overflow = run(QStringLiteral("sample.normalize"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("target_db"), 0.0}});
		QCOMPARE(overflow.ok, false);
		QCOMPARE(overflow.errorKind, ControlErrorKind::Refused);
		QVERIFY(overflow.errorMessage.contains(QStringLiteral("finite")));
		QVERIFY(sameFrames(captureOf(clip), quietBefore));

		// An infinite source sample: the peak is inf and the scale 0, and inf * 0 is NaN.
		std::vector<SampleFrame> infinite(count, SampleFrame(0.0f, 0.0f));
		infinite.front() = SampleFrame(std::numeric_limits<float>::infinity(), 0.0f);
		setSource(clip, std::move(infinite), before.rate);
		const Capture infiniteBefore = captureOf(clip);
		const ControlResult infiniteSource = run(QStringLiteral("sample.normalize"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("target_db"), 0.0}});
		QCOMPARE(infiniteSource.ok, false);
		QCOMPARE(infiniteSource.errorKind, ControlErrorKind::Refused);
		QVERIFY(infiniteSource.errorMessage.contains(QStringLiteral("finite")));
		QVERIFY(sameFrames(captureOf(clip), infiniteBefore));

		// The non-baking sibling declares the same -60..24 dB range, so it refuses NaN too.
		const ControlResult nanClipGain = run(QStringLiteral("clip.set_gain"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("gain_db"), nan}});
		QCOMPARE(nanClipGain.ok, false);
		QCOMPARE(nanClipGain.errorKind, ControlErrorKind::InvalidArgs);

		// The selected-range gain op declares the same range, so it refuses NaN too,
		// and the source frames it would have written are left as they were.
		const Capture beforeRange = captureOf(clip);
		const ControlResult nanRangeGain = run(QStringLiteral("sample.edit_range"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("from"), 0},
				{QStringLiteral("to"), static_cast<int>(count)},
				{QStringLiteral("op"), QStringLiteral("gain")}, {QStringLiteral("gain_db"), nan}});
		QCOMPARE(nanRangeGain.ok, false);
		QCOMPARE(nanRangeGain.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(sameFrames(captureOf(clip), beforeRange));

		// No refusal pushed an undo step: this undo removes the GENERATE.
		REV_UNDO_OR_FAIL();
		control::ClipRef ref;
		ControlResult error;
		QVERIFY(!control::resolveClip(clip, &ref, &error));
	}
};

QTEST_GUILESS_MAIN(ControlSampleBakeGuardTest)
#include "ControlSampleBakeGuardTest.moc"
