#include <atomic>
#include <type_traits>
#include <utility>
#include <vector>

#include <fftw3.h>
#include <QImage>
#include <QMutex>
#include <QRgb>
#include <QtTest>

// The analyzer's control flags are private state; open the access specifiers for
// this translation unit only. That affects neither the layout nor the name
// mangling, so the plugin's own translation units and the link are unaffected
// (the CompressorVisualizerTest and SlicerTZoomTest precedent).
#define private public
#include SA_PROCESSOR_HEADER
#undef private

namespace
{

//! Whether a data member is published as an atomic flag.
template <typename Member>
constexpr bool isAtomicBool = std::is_same_v<std::remove_cvref_t<Member>, std::atomic<bool>>;

} // namespace

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

	void spectrumReadsUseDataAccessLock()
	{
		QFile header(QString::fromUtf8(SA_SPECTRUM_VIEW_HEADER));
		QVERIFY2(header.open(QIODevice::ReadOnly | QIODevice::Text),
			qPrintable(header.errorString()));

		const auto sourcePath = QFileInfo(header.fileName()).absolutePath() + "/SaSpectrumView.cpp";
		QFile source(sourcePath);
		QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text),
			qPrintable(source.errorString()));

		const QString sourceText = QString::fromUtf8(source.readAll());
		const auto refreshStart = sourceText.indexOf("void SaSpectrumView::refreshPaths()");
		const auto updateStart = sourceText.indexOf("void SaSpectrumView::updateBuffers(", refreshStart);
		QVERIFY(refreshStart >= 0);
		QVERIFY(updateStart > refreshStart);

		const auto refresh = sourceText.mid(refreshStart, updateStart - refreshStart);
		const QRegularExpression lockedReads(
			R"(\{\s*QMutexLocker\s+\w+\(&m_processor->m_dataAccess\);\s*updateBuffers\(m_processor->getSpectrumL\(\),[^;]*;\s*updateBuffers\(m_processor->getSpectrumR\(\),[^;]*;\s*\})",
			QRegularExpression::DotMatchesEverythingOption);
		QVERIFY2(lockedReads.match(refresh).hasMatch(),
			"both spectrum channels must be read while m_dataAccess is held");
	}

	void spectrumNotEmptyUsesDataAccessLock()
	{
		QFile header(QString::fromUtf8(SA_SPECTRUM_VIEW_HEADER));
		QVERIFY2(header.open(QIODevice::ReadOnly | QIODevice::Text),
			qPrintable(header.errorString()));

		const auto sourcePath = QFileInfo(header.fileName()).absolutePath() + "/SaProcessor.cpp";
		QFile source(sourcePath);
		QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text),
			qPrintable(source.errorString()));

		const QString sourceText = QString::fromUtf8(source.readAll());
		const QRegularExpression synchronizedScan(
			R"(bool\s+SaProcessor::spectrumNotEmpty\(\)\s*\{\s*QMutexLocker\s+\w+\(&m_reallocationAccess\);\s*QMutexLocker\s+\w+\(&m_dataAccess\);\s*return\s+notEmpty\(m_normSpectrumL\)\s*\|\|\s*notEmpty\(m_normSpectrumR\);\s*\})",
			QRegularExpression::DotMatchesEverythingOption);
		QVERIFY2(synchronizedScan.match(sourceText).hasMatch(),
			"spectrumNotEmpty must scan both channels under reallocationAccess then m_dataAccess");
	}

	//! Cluster new-p9-411. The analyzer's control flags have two owners: the GUI
	//! thread sets them (the spectrum and waterfall view timers call
	//! setSpectrumActive()/setWaterfallActive() and the waterfall view calls
	//! flipRequest(), the controls dialog's block-size combo calls
	//! reallocateBuffers(), and ~Analyzer calls terminate()), while
	//! SaProcessor::analyze() reads them on its own worker thread. Plain bool
	//! members make that a data race, which is undefined behaviour. The
	//! assertion is on the declared types, not on timing: no thread, no clock
	//! and no sleep, so the result is the same on every run.
	void controlFlagsAreAtomic()
	{
		using Processor = lmms::SaProcessor;

		QVERIFY2(isAtomicBool<decltype(std::declval<Processor&>().m_terminate)>,
			"m_terminate is stored by ~Analyzer on the GUI thread and read by analyze() on the analyzer thread");
		QVERIFY2(isAtomicBool<decltype(std::declval<Processor&>().m_flipRequest)>,
			"m_flipRequest is stored by the waterfall view on the GUI thread and cleared by analyze()");
		QVERIFY2(isAtomicBool<decltype(std::declval<Processor&>().m_spectrumActive)>,
			"m_spectrumActive is stored by the spectrum view timer and read by analyze()");
		QVERIFY2(isAtomicBool<decltype(std::declval<Processor&>().m_waterfallActive)>,
			"m_waterfallActive is stored by the waterfall view timer and read by analyze()");
		QVERIFY2(isAtomicBool<decltype(std::declval<Processor&>().m_reallocating)>,
			"m_reallocating is stored by reallocateBuffers() on the GUI thread and read by analyze()");
	}
};

QTEST_APPLESS_MAIN(SaSpectrumViewTest)
#include "SaSpectrumViewTest.moc"
