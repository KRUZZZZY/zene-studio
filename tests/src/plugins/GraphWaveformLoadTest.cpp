/*
 * GraphWaveformLoadTest.cpp - Q050-486: every plug-in that saves a drawn waveform loads it safely
 *
 * Watsyn, MultitapEcho, WaveShaper, FreeBoy, Vibed, BitInvader, Xpressive and DynamicsProcessor
 * store their graphs as base64 float arrays. Before Q050-486 most of them decoded into a raw
 * buffer and called a graph setter that took no source length: a truncated or empty attribute
 * was read past its end, Watsyn and MultitapEcho leaked every decoded buffer but the last, and
 * NaN/infinity went straight into the wavetable.
 *
 * Each plug-in is loaded through the plug-in factory from the build's own modules and driven
 * only through its saved XML (saveState/restoreState), so the code under test is the shipping
 * loader. For every graph attribute: a valid waveform round-trips bit for bit; then each damaged
 * payload (empty, absent, not base64, truncated, one value, excessive, huge, misaligned, NaN,
 * +/-infinity) must leave the graph as it was (DynamicsProcessor: its default curve, as before)
 * and report one diagnostic through the song's load errors. Built with AddressSanitizer on Linux,
 * so an out-of-range read aborts the run; repeated loads are checked by LeakSanitizer.
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include <QDirIterator>
#include <QDomDocument>
#include <QtTest>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectControls.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "Song.h"
#include "Track.h"

#if defined(__SANITIZE_ADDRESS__)
#define GRAPH_TEST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GRAPH_TEST_ASAN 1
#endif
#endif

#ifdef GRAPH_TEST_ASAN
#include <sanitizer/lsan_interface.h>
// Leaks are checked where the test asks (repeatedLoadsLeaveNothingBehind), not over the whole
// process: the engine's own teardown is not what this test measures.
// use_sigaltstack=0: a library the engine loads replaces a thread's alternate signal stack with
// one of its own, which ASan then fails to unmap at thread exit ("unable to unmmap", measured).
extern "C" __attribute__((visibility("default"))) const char* __asan_default_options() { return "detect_leaks=1:leak_check_at_exit=0:use_sigaltstack=0"; }
#endif

using namespace lmms;

namespace
{

//! One plug-in under test: how to make it, which attributes hold its graphs, their value range.
struct Subject
{
	const char* plugin;
	const char* owner;            //!< the name its diagnostics start with
	bool effect;
	QStringList attributes;
	float low;
	float high;
	bool integral;                //!< FreeBoy's wave pattern holds whole steps 0..15
	bool refusalRestoresDefault;  //!< DynamicsProcessor resets to its default curve (unchanged)
};

const std::vector<Subject>& subjects()
{
	static const std::vector<Subject> all{
		{"watsyn", "Watsyn", false, {"a1_wave", "a2_wave", "b1_wave", "b2_wave"}, -1.0f, 1.0f, false, false},
		{"freeboy", "FreeBoy", false, {"sampleShape"}, 0.0f, 15.0f, true, false},
		{"vibedstrings", "Vibed", false, {"graph0", "graph4", "graph8"}, -1.0f, 1.0f, false, false},
		{"xpressive", "Xpressive", false, {"W1sample", "W2sample", "W3sample"}, -1.0f, 1.0f, false, false},
		{"bitinvader", "BitInvader", false, {"sampleShape"}, -1.0f, 1.0f, false, false},
		{"multitapecho", "Multitap Echo", true, {"ampsteps", "lpsteps"}, -60.0f, 0.0f, false, false},
		{"waveshaper", "Wave Shaper", true, {"waveShape"}, 0.0f, 1.0f, false, false},
		{"dynamicsprocessor", "Dynamics Processor", true, {"waveShape"}, 0.0f, 1.0f, false, true},
	};
	return all;
}

const Subject& subject(const char* plugin)
{
	for (const auto& candidate : subjects())
	{
		if (qstrcmp(candidate.plugin, plugin) == 0) { return candidate; }
	}
	qFatal("unknown subject %s", plugin);
	return subjects().front();
}

//! A loaded instrument or effect, saved and restored through its own XML entry points.
class Device
{
public:
	explicit Device(const Subject& subject) : m_subject(subject)
	{
		if (subject.effect)
		{
			m_chain = &Engine::mixer()->mixerChannel(0)->m_fxChain;
			m_effect = Effect::instantiate(QString::fromLatin1(subject.plugin), m_chain, nullptr);
		}
		else
		{
			m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
			if (m_track != nullptr) { m_instrument = m_track->loadInstrument(QString::fromLatin1(subject.plugin)); }
		}
	}

	~Device()
	{
		delete m_effect;
		delete m_track;
	}

	Device(const Device&) = delete;
	Device& operator=(const Device&) = delete;

	bool loaded() const
	{
		return m_subject.effect ? (m_effect != nullptr && m_effect->isOkay()) : m_instrument != nullptr;
	}

	QDomElement save(QDomDocument& doc) const
	{
		QDomElement parent = doc.createElement(QStringLiteral("parent"));
		doc.appendChild(parent);
		if (m_subject.effect) { return m_effect->controls()->saveState(doc, parent); }
		return m_instrument->saveState(doc, parent);
	}

	void restore(const QDomElement& element)
	{
		if (m_subject.effect) { m_effect->controls()->restoreState(element); }
		else { m_instrument->restoreState(element); }
	}

private:
	const Subject& m_subject;
	EffectChain* m_chain = nullptr;
	Effect* m_effect = nullptr;
	InstrumentTrack* m_track = nullptr;
	Instrument* m_instrument = nullptr;
};

QString encode(const std::vector<float>& values)
{
	const QByteArray bytes{reinterpret_cast<const char*>(values.data()),
		static_cast<int>(values.size() * sizeof(float))};
	return QString::fromLatin1(bytes.toBase64());
}

std::vector<float> decodeSaved(const QString& encoded)
{
	const QByteArray bytes = QByteArray::fromBase64(encoded.toLatin1());
	std::vector<float> values(static_cast<std::size_t>(bytes.size()) / sizeof(float));
	std::memcpy(values.data(), bytes.constData(), values.size() * sizeof(float));
	return values;
}

//! A distinctive in-range waveform of `count` values; `seed` makes two of them differ everywhere.
std::vector<float> pattern(const Subject& subject, int count, int seed)
{
	std::vector<float> values(static_cast<std::size_t>(count));
	for (int i = 0; i < count; ++i)
	{
		const float position = (static_cast<float>((i * 7 + seed * 3) % count) + 0.5f) / static_cast<float>(count);
		float value = subject.low + (subject.high - subject.low) * position;
		if (subject.integral) { value = std::round(value); }
		values[static_cast<std::size_t>(i)] = value;
	}
	if (subject.integral && seed != 0)
	{
		for (auto& value : values) { value = subject.high - value; }  // still whole steps, mirrored
	}
	return values;
}

//! Every attribute any of these plug-ins saves a graph in (the subjects above test a sample of Vibed's nine).
const QStringList& allGraphAttributes()
{
	static const QStringList all{"a1_wave", "a2_wave", "b1_wave", "b2_wave", "sampleShape", "W1sample", "W2sample",
		"W3sample", "ampsteps", "lpsteps", "waveShape", "graph0", "graph1", "graph2", "graph3", "graph4", "graph5",
		"graph6", "graph7", "graph8"};
	return all;
}

enum class Damage { Empty, Absent, Invalid, Truncated, OneValue, Excessive, Huge, Misaligned, NaN, PosInf, NegInf };

//! The damaged attribute value, or a null QString for "remove the attribute".
QString damaged(Damage damage, const Subject& subject, int count)
{
	const std::vector<float> other = pattern(subject, count, 1);
	switch (damage)
	{
	case Damage::Empty: return QStringLiteral("");
	case Damage::Absent: return {};
	case Damage::Invalid: return QStringLiteral("this is not base64 ***");
	case Damage::Truncated: return encode(std::vector<float>(other.begin(), other.end() - 1));
	case Damage::OneValue: return encode(std::vector<float>(other.begin(), other.begin() + 1));
	case Damage::Excessive: { auto more = other; more.push_back(other.front()); return encode(more); }
	case Damage::Huge: return encode(std::vector<float>(static_cast<std::size_t>(count) * 512, other.front()));
	case Damage::Misaligned:
	{
		QByteArray bytes{reinterpret_cast<const char*>(other.data()), static_cast<int>(other.size() * sizeof(float))};
		bytes.chop(2);
		return QString::fromLatin1(bytes.toBase64());
	}
	case Damage::NaN: { auto bad = other; bad[1] = std::nanf(""); return encode(bad); }
	case Damage::PosInf: { auto bad = other; bad[0] = INFINITY; return encode(bad); }
	case Damage::NegInf: { auto bad = other; bad.back() = -INFINITY; return encode(bad); }
	}
	return {};
}

bool reportsNothing(Damage damage) { return damage == Damage::Empty || damage == Damage::Absent; }

void addDamageRows()
{
	QTest::addColumn<int>("damage");
	QTest::newRow("empty") << int(Damage::Empty);
	QTest::newRow("absent") << int(Damage::Absent);
	QTest::newRow("invalid") << int(Damage::Invalid);
	QTest::newRow("truncated") << int(Damage::Truncated);
	QTest::newRow("one value") << int(Damage::OneValue);
	QTest::newRow("excessive") << int(Damage::Excessive);
	QTest::newRow("huge") << int(Damage::Huge);
	QTest::newRow("misaligned") << int(Damage::Misaligned);
	QTest::newRow("NaN") << int(Damage::NaN);
	QTest::newRow("+infinity") << int(Damage::PosInf);
	QTest::newRow("-infinity") << int(Damage::NegInf);
}

//! The song's load-error messages naming `owner` and `attribute`.
QStringList diagnosticsFor(const QString& owner, const QString& attribute)
{
	QStringList found;
	for (const QString& message : Engine::getSong()->errors().keys())
	{
		if (message.startsWith(owner + QLatin1Char(':')) && message.contains(QLatin1Char('"') + attribute + QLatin1Char('"')))
		{
			found.append(message);
		}
	}
	return found;
}

/*! The whole contract for one plug-in: a valid waveform in every graph attribute round-trips
 *  exactly; then the damaged payload in each attribute in turn leaves that graph as it was (or
 *  default), leaves the other graphs untouched, and reports itself unless it is empty/absent. */
void checkPlugin(const char* plugin, Damage damage)
{
	const Subject& who = subject(plugin);
	Device device(who);
	if (!device.loaded()) { QSKIP(qPrintable(QStringLiteral("the %1 module is not built here").arg(plugin))); }

	QDomDocument defaultsDoc;
	const QDomElement defaults = device.save(defaultsDoc);
	QDomDocument validDoc;
	QDomElement valid = device.save(validDoc);
	std::map<QString, std::vector<float>> written;
	for (const QString& attribute : who.attributes)
	{
		const int count = static_cast<int>(decodeSaved(defaults.attribute(attribute)).size());
		QVERIFY2(count >= 2, qPrintable(QStringLiteral("%1 saved no %2 graph").arg(plugin, attribute)));
		written[attribute] = pattern(who, count, 0);
		valid.setAttribute(attribute, encode(written[attribute]));
	}
	device.restore(valid);
	{
		QDomDocument doc;
		const QDomElement saved = device.save(doc);
		for (const QString& attribute : who.attributes)
		{
			QVERIFY2(decodeSaved(saved.attribute(attribute)) == written[attribute],
				qPrintable(QStringLiteral("%1: a valid %2 did not round-trip exactly").arg(plugin, attribute)));
		}
	}

	for (const QString& attribute : who.attributes)
	{
		Engine::getSong()->clearErrors();
		device.restore(valid);
		QDomDocument damagedDoc;
		QDomElement broken = valid.cloneNode(true).toElement();
		const QString payload = damaged(damage, who, static_cast<int>(written[attribute].size()));
		if (payload.isNull()) { broken.removeAttribute(attribute); }
		else { broken.setAttribute(attribute, payload); }
		device.restore(broken);

		QDomDocument doc;
		const QDomElement saved = device.save(doc);
		const std::vector<float> expected = who.refusalRestoresDefault
			? decodeSaved(defaults.attribute(attribute)) : written[attribute];
		QVERIFY2(decodeSaved(saved.attribute(attribute)) == expected,
			qPrintable(QStringLiteral("%1: a damaged %2 changed the graph instead of keeping it").arg(plugin, attribute)));
		for (const QString& other : who.attributes)
		{
			if (other == attribute) { continue; }
			QVERIFY2(decodeSaved(saved.attribute(other)) == written[other],
				qPrintable(QStringLiteral("%1: a damaged %2 changed %3").arg(plugin, attribute, other)));
		}
		const QStringList reported = diagnosticsFor(QString::fromLatin1(who.owner), attribute);
		if (reportsNothing(damage))
		{
			QVERIFY2(reported.isEmpty(), qPrintable(reported.join(QLatin1Char('\n'))));
		}
		else
		{
			QVERIFY2(reported.size() == 1, qPrintable(QStringLiteral("%1: expected one diagnostic for %2, got [%3]")
				.arg(plugin, attribute, Engine::getSong()->errors().keys().join(QStringLiteral(" | ")))));
		}
	}
	Engine::getSong()->clearErrors();
}

} // namespace

class GraphWaveformLoadTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		{
#ifdef GRAPH_TEST_ASAN
			__lsan::ScopedDisabler engineIsNotUnderTest;
#endif
			Engine::init(true);
			Engine::audioEngine()->audioDev()->stopProcessing();
		}
	}

	void cleanupTestCase()
	{
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	void watsynWaves_data() { addDamageRows(); }
	void watsynWaves() { QFETCH(int, damage); checkPlugin("watsyn", Damage(damage)); }
	void multitapEchoSteps_data() { addDamageRows(); }
	void multitapEchoSteps() { QFETCH(int, damage); checkPlugin("multitapecho", Damage(damage)); }
	void waveShaperShape_data() { addDamageRows(); }
	void waveShaperShape() { QFETCH(int, damage); checkPlugin("waveshaper", Damage(damage)); }
	void freeBoyShape_data() { addDamageRows(); }
	void freeBoyShape() { QFETCH(int, damage); checkPlugin("freeboy", Damage(damage)); }
	void vibedStrings_data() { addDamageRows(); }
	void vibedStrings() { QFETCH(int, damage); checkPlugin("vibedstrings", Damage(damage)); }
	void xpressiveWaves_data() { addDamageRows(); }
	void xpressiveWaves() { QFETCH(int, damage); checkPlugin("xpressive", Damage(damage)); }
	void bitInvaderShape_data() { addDamageRows(); }
	void bitInvaderShape() { QFETCH(int, damage); checkPlugin("bitinvader", Damage(damage)); }
	void dynamicsProcessorShape_data() { addDamageRows(); }
	void dynamicsProcessorShape() { QFETCH(int, damage); checkPlugin("dynamicsprocessor", Damage(damage)); }

	//! BitInvader's older writers saved `sampleLength` values (the bundled presets: 8 to 128);
	//! such a file loads exactly as before - those values, then zeros - and any other count is refused.
	void bitInvaderOlderWriterLengths()
	{
		const Subject& who = subject("bitinvader");
		for (int length : {8, 30, 56, 128})
		{
			// A fresh instrument per file, like opening a preset: its full 200-value table is cleared.
			Device device(who);
			if (!device.loaded()) { QSKIP("the bitinvader module is not built here"); }
			QDomDocument doc;
			QDomElement element = device.save(doc);
			const std::vector<float> values = pattern(who, length, 0);
			element.setAttribute(QStringLiteral("sampleLength"), length);
			element.setAttribute(QStringLiteral("sampleShape"), encode(values));
			Engine::getSong()->clearErrors();
			device.restore(element);
			QDomDocument savedDoc;
			const std::vector<float> saved = decodeSaved(device.save(savedDoc).attribute(QStringLiteral("sampleShape")));
			QCOMPARE(saved.size(), std::size_t{200});
			QVERIFY(std::equal(values.begin(), values.end(), saved.begin()));
			QVERIFY(std::all_of(saved.begin() + length, saved.end(), [](float v) { return v == 0.0f; }));
			QVERIFY(Engine::getSong()->errors().isEmpty());

			// A count that is neither sampleLength nor the full table keeps the table and is reported.
			QDomElement wrong = element.cloneNode(true).toElement();
			wrong.setAttribute(QStringLiteral("sampleShape"), encode(pattern(who, length + 3, 1)));
			device.restore(wrong);
			QDomDocument againDoc;
			QVERIFY(decodeSaved(device.save(againDoc).attribute(QStringLiteral("sampleShape"))) == saved);
			QCOMPARE(diagnosticsFor(QStringLiteral("BitInvader"), QStringLiteral("sampleShape")).size(), 1);
		}
		Engine::getSong()->clearErrors();
	}

	//! Every graph the bundled presets and demo projects carry for these plug-ins loads with no
	//! diagnostic and saves back with identical float values (BitInvader: its saved prefix).
	void bundledPresetsRoundTripExactly()
	{
		const QDir data{QDir{QStringLiteral(QT_TESTCASE_SOURCEDIR)}.filePath(QStringLiteral("../data"))};
		QVERIFY2(data.exists(), "the bundled data directory was not found");
		const std::map<QString, const char*> tags{{"watsyn", "watsyn"}, {"bitinvader", "bitinvader"},
			{"vibedstrings", "vibedstrings"}, {"xpressive", "xpressive"}, {"freeboy", "freeboy"},
			{"waveshapercontrols", "waveshaper"}, {"dynamicsprocessor_controls", "dynamicsprocessor"},
			{"multitapechocontrols", "multitapecho"}};
		int graphs = 0;
		QDirIterator files{data.path(), {"*.xpf", "*.mmp", "*.mmpz"}, QDir::Files, QDirIterator::Subdirectories};
		while (files.hasNext())
		{
			const QString path = files.next();
			QFile file{path};
			QVERIFY(file.open(QIODevice::ReadOnly));
			QByteArray bytes = file.readAll();
			if (path.endsWith(QStringLiteral(".mmpz"))) { bytes = qUncompress(bytes); }
			QDomDocument doc;
			if (!doc.setContent(bytes)) { continue; }
			for (const auto& [tag, plugin] : tags)
			{
				const QDomNodeList found = doc.elementsByTagName(tag);
				for (int i = 0; i < found.size(); ++i)
				{
					const QDomElement element = found.at(i).toElement();
					const Subject& who = subject(plugin);
					Device device(who);
					if (!device.loaded()) { QSKIP(qPrintable(QStringLiteral("the %1 module is not built here").arg(plugin))); }
					Engine::getSong()->clearErrors();
					device.restore(element);
					QVERIFY2(Engine::getSong()->errors().isEmpty(), qPrintable(path + QStringLiteral(": ")
						+ Engine::getSong()->errors().keys().join(QStringLiteral(" | "))));
					QDomDocument savedDoc;
					const QDomElement saved = device.save(savedDoc);
					const QDomNamedNodeMap attributes = element.attributes();
					for (int a = 0; a < attributes.size(); ++a)
					{
						const QString name = attributes.item(a).nodeName();
						if (!allGraphAttributes().contains(name)) { continue; }
						const std::vector<float> original = decodeSaved(element.attribute(name));
						const std::vector<float> resaved = decodeSaved(saved.attribute(name));
						QVERIFY2(resaved.size() >= original.size() && std::equal(original.begin(), original.end(), resaved.begin()),
							qPrintable(QStringLiteral("%1: %2 %3 did not round-trip exactly").arg(path, tag, name)));
						++graphs;
					}
				}
			}
		}
		qInfo("%d bundled graphs round-tripped exactly", graphs);
		QVERIFY2(graphs >= 100, "fewer bundled graphs were found than the corpus holds");
	}

	//! Loading over and over - valid and damaged payloads alike - leaves no decoded buffer behind.
	void repeatedLoadsLeaveNothingBehind()
	{
#ifndef GRAPH_TEST_ASAN
		QSKIP("the leak check needs LeakSanitizer (this test is built with it on Linux)");
#else
		for (const Subject& who : subjects())
		{
			std::unique_ptr<Device> device;
			QDomDocument doc;
			QDomElement valid;
			QDomElement truncated;
			{
				__lsan::ScopedDisabler setupIsNotUnderTest;
				device = std::make_unique<Device>(who);
				if (!device->loaded()) { continue; }
				valid = device->save(doc);
				truncated = valid.cloneNode(true).toElement();
				for (const QString& attribute : who.attributes)
				{
					const int count = static_cast<int>(decodeSaved(valid.attribute(attribute)).size());
					truncated.setAttribute(attribute, damaged(Damage::Truncated, who, count));
				}
			}
			// Valid files first: the pre-Q050-486 Watsyn and MultitapEcho leaked on every one of these.
			for (int i = 0; i < 50; ++i) { device->restore(valid); }
			QVERIFY2(__lsan_do_recoverable_leak_check() == 0,
				qPrintable(QStringLiteral("loading a valid %1 leaked memory (LeakSanitizer report above)").arg(who.plugin)));
			for (int i = 0; i < 50; ++i)
			{
				device->restore(truncated);
				device->restore(valid);
			}
			Engine::getSong()->clearErrors();
			QVERIFY2(__lsan_do_recoverable_leak_check() == 0,
				qPrintable(QStringLiteral("loading a damaged %1 leaked memory (LeakSanitizer report above)").arg(who.plugin)));
		}
#endif
	}
};

QTEST_GUILESS_MAIN(GraphWaveformLoadTest)

#include "GraphWaveformLoadTest.moc"
