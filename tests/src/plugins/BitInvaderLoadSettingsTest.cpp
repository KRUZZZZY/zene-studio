/*
 * BitInvaderLoadSettingsTest.cpp - BitInvader's loadSettings contract for the saved wavetable
 *
 * A saved wavetable of a legal length is loaded. Values outside [-1, 1] are clamped to that
 * range. A payload that is not a legal wavetable is refused, and a refused load leaves the
 * installed wavetable as it was.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <vector>

#include <QByteArray>
#include <QDomDocument>
#include <QDomElement>
#include <QList>
#include <QString>
#include <QtTest>

#include "AudioEngine.h"
#include "BitInvader.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "Song.h"

namespace lmms
{

namespace
{

//! Periods a held note is rendered for.
constexpr int HeldPeriods = 3;
//! How far a rendered peak may exceed 1.0.
constexpr float RenderEpsilon = 1e-4f;
//! Bytes in one stored wavetable value.
constexpr qsizetype FloatBytes = static_cast<qsizetype>(sizeof(float));

//! A non-constant wavetable of `count` values in [-0.75, 0.75], all inside the legal range.
QList<float> pattern(qsizetype count)
{
	QList<float> values;
	for (qsizetype i = 0; i < count; ++i)
	{
		values.append(static_cast<float>(((i * 7) % 13) - 6) / 8.0f);
	}
	return values;
}

//! The table a refused load must leave in place. No row's payload carries it.
QList<float> preloadTable()
{
	QList<float> values;
	for (qsizetype i = 0; i < wavetableSize; ++i)
	{
		values.append(i % 2 == 0 ? 0.5f : -0.5f);
	}
	return values;
}

//! The sampleShape form: each value's four float bytes, base64-encoded.
QString encodeFloats(const QList<float>& values)
{
	QByteArray raw;
	for (const float value : values)
	{
		raw.append(reinterpret_cast<const char*>(&value), FloatBytes);
	}
	return QString::fromLatin1(raw.toBase64());
}

//! The index of the first entry of `want` that `got` does not hold exactly, or -1 when all match.
qsizetype firstMismatch(const QList<float>& got, const QList<float>& want)
{
	for (qsizetype i = 0; i < want.size(); ++i)
	{
		if (i >= got.size() || got[i] != want[i]) { return i; }
	}
	return -1;
}

//! The wavetable as the instrument saves it, read back from sampleShape.
QList<float> installedTable(BitInvader& instrument)
{
	QDomDocument doc;
	QDomElement settings = doc.createElement("bitinvader");
	instrument.saveSettings(doc, settings);

	const QByteArray raw = QByteArray::fromBase64(settings.attribute("sampleShape").toLatin1());
	QList<float> table;
	for (qsizetype offset = 0; offset + FloatBytes <= raw.size(); offset += FloatBytes)
	{
		float value = 0.0f;
		std::memcpy(&value, raw.constData() + offset, sizeof(value));
		table.append(value);
	}
	return table;
}

//! Loads a candidate sampleShape with normalize on, through the settings round trip a project
//! load uses.
void loadCandidate(BitInvader& instrument, const QString& payload, bool omitShape, int sampleLength)
{
	QDomDocument doc;
	QDomElement settings = doc.createElement("bitinvader");
	instrument.saveSettings(doc, settings);
	settings.setAttribute("sampleLength", sampleLength);
	settings.setAttribute("normalize", 1);
	if (omitShape)
	{
		settings.removeAttribute("sampleShape");
	}
	else
	{
		settings.setAttribute("sampleShape", payload);
	}
	instrument.loadSettings(settings);
}

//! One BitInvader on its own track, with one note held across periods.
class HeldNote
{
public:
	explicit HeldNote(f_cnt_t frames)
		: m_track{std::make_unique<InstrumentTrack>(Engine::getSong())}
		, m_instrument{std::make_unique<BitInvader>(m_track.get())}
		, m_handle{m_track.get(), 0, frames * HeldPeriods,
			Note{TimePos{static_cast<tick_t>(frames * HeldPeriods)}, TimePos{0}, DefaultKey},
			nullptr, -1, NotePlayHandle::Origin::MidiClip}
		, m_buffer(static_cast<std::size_t>(frames))
	{
	}

	~HeldNote()
	{
		// Hand the plugin's note state back while the instrument is alive. The handle's
		// own destructor would otherwise reach the instrument through the track.
		if (m_handle.m_pluginData != nullptr)
		{
			m_instrument->deleteNotePluginData(&m_handle);
			m_handle.m_pluginData = nullptr;
		}
	}

	HeldNote(const HeldNote&) = delete;
	HeldNote& operator=(const HeldNote&) = delete;

	//! Renders the next period; the frames are then in buffer().
	void playPeriod()
	{
		std::fill(m_buffer.begin(), m_buffer.end(), SampleFrame{0.0f, 0.0f});
		m_instrument->playNote(&m_handle, std::span<SampleFrame>{m_buffer});
	}

	const std::vector<SampleFrame>& buffer() const { return m_buffer; }
	BitInvader& instrument() { return *m_instrument; }

private:
	std::unique_ptr<InstrumentTrack> m_track;
	std::unique_ptr<BitInvader> m_instrument;
	NotePlayHandle m_handle;
	std::vector<SampleFrame> m_buffer;
};

} // namespace

class BitInvaderLoadSettingsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
	}

	void cleanupTestCase()
	{
		Engine::getSong()->clearProject();
		Engine::destroy();
		NotePlayHandleManager::free();
	}

	void loadSettingsShape_data()
	{
		QTest::addColumn<QString>("payload");
		QTest::addColumn<bool>("omitShape");
		QTest::addColumn<int>("sampleLength");
		QTest::addColumn<bool>("accepted");
		QTest::addColumn<QList<float>>("expected");

		const QList<float> full = pattern(wavetableSize);
		const QList<float> none;

		QList<float> posIn = full;
		posIn[37] = 1e30f;
		QList<float> posOut = full;
		posOut[37] = 1.0f;

		QList<float> negIn = full;
		negIn[37] = -1e30f;
		QList<float> negOut = full;
		negOut[37] = -1.0f;

		QList<float> mixedIn = full;
		mixedIn[0] = 0.5f;
		mixedIn[1] = 1e30f;
		mixedIn[2] = -0.25f;
		QList<float> mixedOut = full;
		mixedOut[0] = 0.5f;
		mixedOut[1] = 1.0f;
		mixedOut[2] = -0.25f;

		QList<float> nanIn = full;
		nanIn[37] = std::numeric_limits<float>::quiet_NaN();
		QList<float> posInfIn = full;
		posInfIn[37] = std::numeric_limits<float>::infinity();
		QList<float> negInfIn = full;
		negInfIn[37] = -std::numeric_limits<float>::infinity();

		QTest::newRow("valid 200 floats load unchanged")
			<< encodeFloats(full) << false << 200 << true << full;
		QTest::newRow("legacy 199 floats with sampleLength 199 load")
			<< encodeFloats(pattern(199)) << false << 199 << true << pattern(199);
		QTest::newRow("+1e30 clamps to +1")
			<< encodeFloats(posIn) << false << 200 << true << posOut;
		QTest::newRow("-1e30 clamps to -1")
			<< encodeFloats(negIn) << false << 200 << true << negOut;
		QTest::newRow("mixed row clamps only the out-of-range value")
			<< encodeFloats(mixedIn) << false << 200 << true << mixedOut;

		QTest::newRow("NaN rejects")
			<< encodeFloats(nanIn) << false << 200 << false << none;
		QTest::newRow("+Inf rejects")
			<< encodeFloats(posInfIn) << false << 200 << false << none;
		QTest::newRow("-Inf rejects")
			<< encodeFloats(negInfIn) << false << 200 << false << none;
		QTest::newRow("199 floats with sampleLength 200 reject")
			<< encodeFloats(pattern(199)) << false << 200 << false << none;
		QTest::newRow("201 floats reject")
			<< encodeFloats(pattern(201)) << false << 200 << false << none;
		QTest::newRow("0 bytes reject")
			<< encodeFloats(none) << false << 200 << false << none;
		QTest::newRow("3 bytes reject")
			<< QString::fromLatin1(QByteArray("\x01\x02\x03", 3).toBase64()) << false << 200 << false << none;
		QTest::newRow("invalid base64 rejects")
			<< QStringLiteral("!!not base64!!") << false << 200 << false << none;
		QTest::newRow("missing sampleShape attribute rejects")
			<< QString() << true << 200 << false << none;
	}

	//! A settings load keeps a legal wavetable, clamps values outside [-1, 1], and refuses any
	//! other payload, leaving the installed wavetable as it was. The render with normalize on
	//! stays finite and within [-1, 1] plus a small epsilon.
	void loadSettingsShape()
	{
		QFETCH(QString, payload);
		QFETCH(bool, omitShape);
		QFETCH(int, sampleLength);
		QFETCH(bool, accepted);
		QFETCH(QList<float>, expected);

		const QList<float> preload = preloadTable();
		HeldNote note{Engine::audioEngine()->framesPerPeriod()};

		// Start from a table that no row carries, so a refused load shows as an unchanged table.
		loadCandidate(note.instrument(), encodeFloats(preload), false, wavetableSize);
		const qsizetype preloadBad = firstMismatch(installedTable(note.instrument()), preload);
		QVERIFY2(preloadBad < 0, "the pre-load table did not install");

		loadCandidate(note.instrument(), payload, omitShape, sampleLength);

		const QList<float> table = installedTable(note.instrument());
		QCOMPARE(table.size(), static_cast<qsizetype>(wavetableSize));
		const QList<float>& want = accepted ? expected : preload;
		const qsizetype bad = firstMismatch(table, want);
		QVERIFY2(bad < 0, qPrintable(QStringLiteral("%1: table[%2] is %3, expected %4")
			.arg(accepted ? QStringLiteral("accepted load") : QStringLiteral("refused load"))
			.arg(bad)
			.arg(table.value(bad))
			.arg(want.value(bad))));

		note.playPeriod();
		bool finite = true;
		float peak = 0.0f;
		for (const SampleFrame& frame : note.buffer())
		{
			const float channels[2] = {frame.left(), frame.right()};
			for (const float value : channels)
			{
				if (std::isfinite(value)) { peak = std::max(peak, std::abs(value)); }
				else { finite = false; }
			}
		}
		QVERIFY2(finite, "rendered output with normalize on is not finite");
		QVERIFY2(peak > 0.0f, "rendered output with normalize on is silent: the normalization factor is not positive");
		QVERIFY2(peak <= 1.0f + RenderEpsilon,
			qPrintable(QStringLiteral("rendered peak %1 exceeds 1 with normalize on").arg(peak)));
	}
};

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::BitInvaderLoadSettingsTest)

#include "BitInvaderLoadSettingsTest.moc"
