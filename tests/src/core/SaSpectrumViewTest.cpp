#include <QFile>
#include <QtTest>

class SaSpectrumViewTest : public QObject
{
	Q_OBJECT

private slots:
	void decayAccumulatorStartsAtZero()
	{
		QFile header(QString::fromUtf8(SA_SPECTRUM_VIEW_HEADER));
		QVERIFY2(header.open(QIODevice::ReadOnly | QIODevice::Text),
			qPrintable(header.errorString()));

		const QByteArray source = header.readAll();
		QVERIFY2(source.contains("float m_decaySum = 0.0f;"),
			"SaSpectrumView must initialize m_decaySum before its first paint");
	}
};

QTEST_APPLESS_MAIN(SaSpectrumViewTest)
#include "SaSpectrumViewTest.moc"
