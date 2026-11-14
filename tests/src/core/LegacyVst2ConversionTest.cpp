#include <QtTest>
#include <QDomDocument>

#include "LegacyVst2Conversion.h"
#include "PathUtil.h"

using namespace lmms;
using namespace lmms::legacyvst2;

namespace
{
QDomDocument xml(const char* source)
{
	QDomDocument doc;
	doc.setContent(QString::fromUtf8(source));
	return doc;
}
const Resolver resolver = [](const QString&, Plugin::Type) -> std::optional<Attributes> {
	return Attributes{{"file", "/resolved.so"}, {"name", "Resolved"}};
};
using Key = Plugin::Descriptor::SubPluginFeatures::Key;
}

class LegacyVst2ConversionTest : public QObject
{
	Q_OBJECT
private slots:
	void instrument()
	{
		auto doc = xml("<instrument name='vestige'><vestige plugin='/old.so' chunk='abc' program='3' guivisible='1'><param1 id='4242' value='0.3' scale_type='linear'/><connection><param0><controllerconnection nodename='param0'/></param0></connection><pins inputs='2'/></vestige></instrument>");
		auto result = convertInstrument(doc.documentElement(), resolver);
		QCOMPARE(result.attribute("name"), QString("carlavst2"));
		auto state = result.firstChildElement();
		QCOMPARE(state.tagName(), QString("carlavst2"));
		auto marker = state.firstChildElement();
		QCOMPARE(marker.tagName(), legacyStateTagName());
		QCOMPARE(marker.attribute("host"), QString("vestige"));
		QCOMPARE(marker.attribute("chunk"), QString("abc"));
		QCOMPARE(marker.attribute("program"), QString("3"));
		QVERIFY(!marker.hasAttribute("plugin"));
		QVERIFY(!marker.hasAttribute("guivisible"));
		QVERIFY(state.firstChildElement("pins").isNull());
		QCOMPARE(state.firstChildElement("PARAM_KNOB_1").attribute("id"), QString("4242"));
		auto connection = state.firstChildElement("connection").firstChildElement("PARAM_KNOB_0");
		QCOMPARE(connection.firstChildElement().attribute("nodename"), QString("PARAM_KNOB_0"));
		QCOMPARE(Key(state.firstChildElement("key")).attributes.value("file"), QString("/resolved.so"));
		QCOMPARE(Key(state.firstChildElement("key")).attributes.value("name"), QString("Resolved"));
	}
	void dump()
	{
		auto doc = xml("<instrument name='vestige'><vestige plugin='/old.so' numparams='2' param0='0:Level:0.7' param1='1:Tone:0.25'/></instrument>");
		auto marker = convertInstrument(doc.documentElement(), resolver).firstChildElement().firstChildElement();
		QCOMPARE(marker.attribute("numparams"), QString("2"));
		QCOMPARE(marker.attribute("param0"), QString("0:Level:0.7"));
		QCOMPARE(marker.attribute("param1"), QString("1:Tone:0.25"));
	}
	void boundaries()
	{
		auto doc = xml("<instrument name='vestige'><vestige plugin='/old.so'><param10/><param1/><paramX/><automationpattern><param10/></automationpattern></vestige></instrument>");
		auto state = convertInstrument(doc.documentElement(), resolver).firstChildElement();
		QVERIFY(!state.firstChildElement("PARAM_KNOB_10").isNull());
		QVERIFY(!state.firstChildElement("PARAM_KNOB_1").isNull());
		QVERIFY(!state.firstChildElement("paramX").isNull());
		QVERIFY(!state.firstChildElement("automationpattern").firstChildElement("PARAM_KNOB_10").isNull());
	}
	void effect()
	{
		auto doc = xml("<effect name='vsteffect' on='1' wet='0.5' id='9'><before/><key><attribute name='file' value='/old.so'/></key><vsteffectcontrols plugin='/other.so' chunk='xyz'><param0 id='7'/></vsteffectcontrols><after/></effect>");
		QString received;
		Resolver capture = [&received](const QString& file, Plugin::Type type) -> std::optional<Attributes> { received = file; return type == Plugin::Type::Effect ? resolver(file, type) : std::nullopt; };
		auto result = convertEffect(doc.documentElement(), capture);
		QCOMPARE(received, PathUtil::toAbsolute("/old.so"));
		QCOMPARE(result.attribute("name"), QString("carlavst2effect"));
		QCOMPARE(result.attribute("wet"), QString("0.5"));
		QCOMPARE(result.attribute("on"), QString("1"));
		QCOMPARE(result.attribute("id"), QString("9"));
		auto child = result.firstChildElement();
		QCOMPARE(child.tagName(), QString("before"));
		child = child.nextSiblingElement();
		QCOMPARE(Key(child).attributes.value("file"), QString("/resolved.so"));
		child = child.nextSiblingElement();
		QCOMPARE(child.tagName(), QString("carlavst2effectcontrols"));
		QCOMPARE(child.firstChildElement().attribute("host"), QString("vsteffect"));
		QCOMPARE(child.firstChildElement().attribute("chunk"), QString("xyz"));
		QCOMPARE(child.firstChildElement("PARAM_KNOB_0").attribute("id"), QString("7"));
		QCOMPARE(child.nextSiblingElement().tagName(), QString("after"));
	}
	void invalid()
	{
		auto doc = xml("<instrument name='vestige'><vestige plugin='/old.so'/></instrument>");
		const auto before = doc.toString();
		Resolver missing = [](const QString&, Plugin::Type) -> std::optional<Attributes> { return std::nullopt; };
		QVERIFY(convertInstrument(doc.documentElement(), missing).isNull());
		setConversionEnabled(false);
		const bool disabled = convertInstrument(doc.documentElement(), resolver).isNull();
		setConversionEnabled(true);
		QVERIFY(disabled);
		QCOMPARE(doc.toString(), before);
		auto wrong = xml("<instrument name='other'><vestige plugin='/old.so'/></instrument>");
		auto empty = xml("<instrument name='vestige'><vestige/></instrument>");
		const auto wrongBefore = wrong.toString(), emptyBefore = empty.toString();
		QVERIFY(convertInstrument(wrong.documentElement(), resolver).isNull());
		QVERIFY(convertInstrument(empty.documentElement(), resolver).isNull());
		QCOMPARE(wrong.toString(), wrongBefore);
		QCOMPARE(empty.toString(), emptyBefore);
	}
	void absolutePath()
	{
		auto doc = xml("<instrument name='vestige'><vestige plugin='relative.so'/></instrument>");
		QString received;
		Resolver capture = [&received](const QString& path, Plugin::Type type) -> std::optional<Attributes> { received = path; return resolver(path, type); };
		QVERIFY(!convertInstrument(doc.documentElement(), capture).isNull());
		QCOMPARE(received, PathUtil::toAbsolute("relative.so"));
	}
};
QTEST_GUILESS_MAIN(LegacyVst2ConversionTest)
#include "LegacyVst2ConversionTest.moc"
