/* AudioEngineProfilerTest - the profiling write path must stay off the audio
 * thread, and the file object must belong to the thread that uses it.
 *
 * Two claims, checked against the source text of the profiler (the same shape
 * WriteRefusalGateTest and RtSafetySweep use - a text gate needs no binary
 * state) plus one behavioural claim:
 *
 *  1. finishPeriod() (the render path) must not format a QString and must not
 *     touch a file: it hands the measurement to the bounded queue instead.
 *  2. The output file is a QFile, and QFile is a QObject: it may only be
 *     created, used and destroyed on ONE thread. The writer thread owns the
 *     whole lifecycle, so the object has to be created INSIDE writerLoop() - a
 *     member would be constructed on the profiler's creating thread and
 *     destroyed with the profiler while the writer thread used it, which is the
 *     cross-thread QObject use Qt does not support.
 *  3. Behaviourally: a measurement handed to the profiler reaches the output
 *     file, and the file is complete and closed when the profiler goes out of
 *     scope (the destructor joins the writer thread, which drains and closes).
 */

#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QString>

#include <fstream>
#include <sstream>
#include <string>

#include "AudioEngineProfiler.h"

namespace {

constexpr const char* PROFILER_SOURCE = "../../src/core/AudioEngineProfiler.cpp";
constexpr const char* PROFILER_HEADER = "../../include/AudioEngineProfiler.h";

std::string readSource( const std::string& path )
{
	std::ifstream in( path );
	if ( !in.is_open() )
	{
		return {};
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

//! The text range of one function definition: from its signature to the closing
//! brace at column 0.
struct FunctionRange
{
	std::size_t begin = 0;
	std::size_t end = 0;
	bool valid = false;
};

FunctionRange findFunction( const std::string& source, const std::string& signature )
{
	FunctionRange range;
	const std::size_t start = source.find( signature );
	if ( start == std::string::npos )
	{
		return range;
	}
	const std::size_t end = source.find( "\n}\n", start );
	if ( end == std::string::npos )
	{
		return range;
	}
	range.begin = start;
	range.end = end;
	range.valid = true;
	return range;
}

std::string functionBody( const std::string& source, const std::string& signature )
{
	const FunctionRange range = findFunction( source, signature );
	if ( !range.valid )
	{
		return {};
	}
	return source.substr( range.begin, range.end - range.begin );
}

//! The line the token at `pos` sits on, without its line break.
std::string lineAt( const std::string& source, std::size_t pos )
{
	const std::size_t lineStart = source.rfind( '\n', pos ) + 1;
	const std::size_t lineEnd = source.find( '\n', pos );
	if ( lineEnd == std::string::npos )
	{
		return source.substr( lineStart );
	}
	return source.substr( lineStart, lineEnd - lineStart );
}

//! True for a line that only carries prose (a comment line), so a text gate
//! does not trip over a sentence that mentions the type.
bool isCommentLine( const std::string& line )
{
	const std::size_t first = line.find_first_not_of( " \t" );
	if ( first == std::string::npos )
	{
		return false;
	}
	return line[first] == '/' || line[first] == '*';
}

} // namespace

class AudioEngineProfilerTest : public QObject
{
	Q_OBJECT

private slots:
	void finishPeriodDoesNotTouchOutputSynchronously()
	{
		const std::string source = readSource( PROFILER_SOURCE );
		QVERIFY2( !source.empty(), "AudioEngineProfiler.cpp not found" );

		const std::string body = functionBody( source, "void AudioEngineProfiler::finishPeriod" );
		QVERIFY2( !body.empty(), "finishPeriod not found" );

		QVERIFY2( body.find( "QFile" ) == std::string::npos,
			"finishPeriod still refers to the output file on the audio path" );
		QVERIFY2( body.find( "QString" ) == std::string::npos,
			"finishPeriod still formats a QString on the audio path" );
		QVERIFY2( body.find( "write(" ) == std::string::npos,
			"finishPeriod still writes to a file on the audio path" );
	}

	void writerThreadOwnsFileLifecycle()
	{
		const std::string source = readSource( PROFILER_SOURCE );
		QVERIFY2( !source.empty(), "AudioEngineProfiler.cpp not found" );

		const std::string setOutputFile = functionBody( source, "void AudioEngineProfiler::setOutputFile" );
		QVERIFY2( !setOutputFile.empty(), "setOutputFile not found" );
		QVERIFY2( setOutputFile.find( "QFile" ) == std::string::npos,
			"setOutputFile touches a file object on the caller thread" );

		const std::string stopWriter = functionBody( source, "void AudioEngineProfiler::stopWriter" );
		QVERIFY2( !stopWriter.empty(), "stopWriter not found" );
		QVERIFY2( stopWriter.find( "QFile" ) == std::string::npos,
			"stopWriter touches a file object on the caller thread" );

		const std::string destructor = functionBody( source, "AudioEngineProfiler::~AudioEngineProfiler" );
		QVERIFY2( !destructor.empty(), "~AudioEngineProfiler not found" );
		QVERIFY2( destructor.find( "QFile" ) == std::string::npos,
			"~AudioEngineProfiler touches a file object on the caller thread" );

		const std::string writerLoop = functionBody( source, "void AudioEngineProfiler::writerLoop" );
		QVERIFY2( !writerLoop.empty(), "writerLoop not found" );
		QVERIFY2( writerLoop.find( "open(" ) != std::string::npos,
			"writerLoop does not open the output file" );
		QVERIFY2( writerLoop.find( "write(" ) != std::string::npos,
			"writerLoop does not write to the output file" );
		QVERIFY2( writerLoop.find( "close(" ) != std::string::npos,
			"writerLoop does not close the output file" );
	}

	//! The file object must be CREATED on the writer thread, not on the thread
	//! that creates the profiler. A QFile member would be constructed and
	//! destroyed with the profiler on its creating thread while the writer
	//! thread called setFileName()/open()/write()/close() on it - Qt does not
	//! support using a QObject from a thread other than the one it lives in.
	void outputFileObjectIsCreatedOnTheWriterThread()
	{
		const std::string header = readSource( PROFILER_HEADER );
		QVERIFY2( !header.empty(), "AudioEngineProfiler.h not found" );
		QVERIFY2( header.find( "QFile" ) == std::string::npos,
			"the profiler still declares a QFile: a member is constructed on the "
			"profiler's creating thread and destroyed with it, while the writer "
			"thread uses it - the file object must be a local of writerLoop()" );

		const std::string source = readSource( PROFILER_SOURCE );
		QVERIFY2( !source.empty(), "AudioEngineProfiler.cpp not found" );

		const FunctionRange writerLoop = findFunction( source, "void AudioEngineProfiler::writerLoop" );
		QVERIFY2( writerLoop.valid, "writerLoop not found" );

		// Every use of the file type has to sit inside writerLoop(): that is the
		// only place where the object may be created.
		bool sawInclude = false;
		bool sawObject = false;
		std::size_t pos = 0;
		while ( ( pos = source.find( "QFile", pos ) ) != std::string::npos )
		{
			const std::string line = lineAt( source, pos );
			if ( !isCommentLine( line ) )
			{
				if ( line.find( "#include" ) != std::string::npos )
				{
					sawInclude = true;
				}
				else
				{
					QVERIFY2( pos >= writerLoop.begin && pos < writerLoop.end,
						"the output file object is created outside writerLoop(): it "
						"would not belong to the writer thread" );
					sawObject = true;
				}
			}
			pos += 5;
		}
		QVERIFY2( sawInclude, "AudioEngineProfiler.cpp does not include the file type itself" );
		QVERIFY2( sawObject, "writerLoop does not create the output file object" );
	}

	void writesMeasurementToOutputFile()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		const QString path = dir.path() + "/profile.txt";

		{
			lmms::AudioEngineProfiler profiler;
			profiler.setOutputFile( path );
			profiler.finishPeriod( 48000, 256 );
			// The destructor joins the writer thread, which drains the queue and
			// closes the file - so the file is complete when the scope ends.
			// No sleep, no polling: the join is the synchronisation point.
		}

		QFile file( path );
		QVERIFY( file.open( QIODevice::ReadOnly ) );
		const QByteArray data = file.readAll();
		QVERIFY( !data.isEmpty() );
	}
};

QTEST_GUILESS_MAIN( AudioEngineProfilerTest )

#include "AudioEngineProfilerTest.moc"
