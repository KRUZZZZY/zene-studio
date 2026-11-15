/*
 * CarlaVst2LegacyStateTest.cpp - legacy VST2 state restoration
 *
 * This file is part of Zene Studio.
 */

#include <array>
#include <utility>
#include <vector>

#include <QDomDocument>
#include <QtTest>

#include "CarlaVst2Legacy.h"

using namespace lmms::carlavst2;

namespace
{
struct FakePlugin
{
	std::array<float, 2> values{};
	std::vector<std::pair<uint32_t, float>> parameters;
	int chunks = 0;
	int programs = 0;

	LegacyCalls calls()
	{
		return {[] { return 1u; }, [this](uint32_t) { ++programs; },
			[this](const char*) { ++chunks; }, [] { return 2u; },
			[this](uint32_t index, float value) {
				parameters.emplace_back(index, value);
				values.at(index) = value;
			}};
	}
};

QDomElement dump(QDomDocument& document, const QString& count)
{
	auto element = document.createElement("legacy");
	element.setAttribute("numparams", count);
	element.setAttribute("param0", "0:A:0.25");
	element.setAttribute("param1", "1:B:0.5");
	return element;
}
}

class CarlaVst2LegacyStateTest final : public QObject
{
	Q_OBJECT

private slots:
	void anOversizedDumpCannotOverwriteAParameter()
	{
		QDomDocument document;
		auto element = dump(document, "3");
		element.setAttribute("param2", "0:A:0.9");
		FakePlugin plugin;
		applyLegacyState(element, plugin.calls());
		QCOMPARE(plugin.parameters.size(), std::size_t{2});
		QCOMPARE(plugin.values[0], 0.25f);
	}

	void aHugeCountStopsAtThePluginsParameters()
	{
		QDomDocument document;
		FakePlugin plugin;
		applyLegacyState(dump(document, "2147483647"), plugin.calls());
		QCOMPARE(plugin.parameters.size(), std::size_t{2});
	}

	void aDumpWithinTheCountAppliesEveryEntry()
	{
		QDomDocument document;
		FakePlugin plugin;
		applyLegacyState(dump(document, "2"), plugin.calls());
		QCOMPARE(plugin.parameters.size(), std::size_t{2});
		QCOMPARE(plugin.values[0], 0.25f);
		QCOMPARE(plugin.values[1], 0.5f);
	}

	void aNegativeOrGarbageCountAppliesNothing()
	{
		QDomDocument document;
		FakePlugin plugin;
		applyLegacyState(dump(document, "-5"), plugin.calls());
		QCOMPARE(plugin.parameters.size(), std::size_t{0});
		applyLegacyState(dump(document, "abc"), plugin.calls());
		QCOMPARE(plugin.parameters.size(), std::size_t{0});
	}

	void aChunkWinsOverTheDump()
	{
		QDomDocument document;
		auto element = dump(document, "2");
		element.setAttribute("chunk", "dGVzdA==");
		FakePlugin plugin;
		applyLegacyState(element, plugin.calls());
		QCOMPARE(plugin.chunks, 1);
		QCOMPARE(plugin.parameters.size(), std::size_t{0});
	}

	void aMalformedEntryIsSkipped()
	{
		QDomDocument document;
		auto element = dump(document, "2");
		element.setAttribute("param0", "9:Bogus:x");
		FakePlugin plugin;
		applyLegacyState(element, plugin.calls());
		QCOMPARE(plugin.parameters.size(), std::size_t{1});
		QCOMPARE(plugin.parameters[0].first, uint32_t{1});
		QCOMPARE(plugin.parameters[0].second, 0.5f);
	}
};

QTEST_GUILESS_MAIN(CarlaVst2LegacyStateTest)

#include "CarlaVst2LegacyStateTest.moc"
