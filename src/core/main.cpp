/*
 * main.cpp - just main.cpp which is starting up app...
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2012-2013 Paul Giblock    <p/at/pgiblock.net>
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

#include "lmmsconfig.h"
#include "lmmsversion.h"
#include "versioninfo.h"

#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QTimer>
#include <QTranslator>
#include <QApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QTextStream>
#include <QUrl>

#ifdef LMMS_BUILD_WIN32
#include <windows.h>
#endif

#ifdef LMMS_HAVE_PROCESS_H
#include <process.h>
#endif

#ifdef LMMS_HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef LMMS_HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include <csignal>  // To register the signal handler

#include "MainApplication.h"
#include "CrashReporter.h"
#include "ConfigManager.h"
#include "DataFile.h"
#include "NotePlayHandle.h"
#include "embed.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "Hardware.h"
#include "ImportFilter.h"
#include "MainWindow.h"
#include "MixHelpers.h"
#include "OutputSettings.h"
#include "ProjectRecovery.h"
#include "ProjectRenderer.h"
#include "MasteringJob.h"
#include "RenderManager.h"
#include "Song.h"
#include "ScriptConsole.h"
#include "ScriptEngine.h"

#ifdef LMMS_DEBUG_FPE
#include <fenv.h> // For feenableexcept
#include <execinfo.h> // For backtrace and backtrace_symbols_fd
#include <unistd.h> // For STDERR_FILENO
#endif


#ifdef LMMS_DEBUG_FPE
void sigfpeHandler(int signum)
{

	// Get a back trace
	void *array[10];
	size_t size;

	// get void*'s for all entries on the stack
	size = backtrace(array, 10);

	backtrace_symbols_fd(array, size, STDERR_FILENO);

	// cleanup and close up stuff here
	// terminate program

	exit(signum);
}
#endif

static inline QString baseName( const QString & file )
{
	return QFileInfo( file ).absolutePath() + "/" +
			QFileInfo( file ).completeBaseName();
}


#ifdef LMMS_BUILD_WIN32
// Workaround for old MinGW
#ifdef __MINGW32__
extern "C" _CRTIMP errno_t __cdecl freopen_s(FILE** _File,
	const char *_Filename, const char *_Mode, FILE *_Stream);
#endif

// For qInstallMessageHandler
void consoleMessageHandler(QtMsgType type,
	const QMessageLogContext &context, const QString &msg)
{
    QByteArray localMsg = msg.toLocal8Bit();
    fprintf(stderr, "%s\n", localMsg.constData());
}
#endif // LMMS_BUILD_WIN32


inline void loadTranslation( const QString & tname,
	const QString & dir = lmms::ConfigManager::inst()->localeDir() )
{
	auto t = new QTranslator(QCoreApplication::instance());
	QString name = tname + ".qm";

	if (t->load(name, dir))
	{
		QCoreApplication::instance()->installTranslator(t);
	}
}




void printVersion( char *executableName )
{
	printf("Zene Studio %s\n(%s %s, Qt %s, %s)\n\n"
		"Build options:\n%s\n\n"
		"Copyright (c) %s\n\n"
		"This program is free software; you can redistribute it and/or\n"
		"modify it under the terms of the GNU General Public\n"
		"License as published by the Free Software Foundation; either\n"
		"version 2 of the License, or (at your option) any later version.\n\n"
		"Try \"%s --help\" for more information.\n\n", LMMS_VERSION,
		LMMS_BUILDCONF_PLATFORM, LMMS_BUILDCONF_MACHINE, QT_VERSION_STR, LMMS_BUILDCONF_COMPILER_VERSION, LMMS_BUILD_OPTIONS,
		LMMS_PROJECT_COPYRIGHT, executableName);
}




void printHelp()
{
	printf( "Zene Studio %s\n"
		"Copyright (c) %s\n\n"
		"Usage: zene [global options...] [<action> [action parameters...]]\n\n"
		"Actions:\n"
		"  <no action> [options...] [<project>]  Start Zene Studio in normal GUI mode\n"
		"  dump <in>                             Dump XML of compressed file <in>\n"
		"  compress <in>                         Compress file <in>\n"
		"  render <project> [options...]         Render given project file\n"
		"  rendertracks <project> [options...]   Render each track to a different file\n"
		"  exportstems <project> [options...]    Export each track as its own stem file\n"
		"  master <project> [options...]         Render the mix once, then write a\n"
		"                                        set of mastered candidates\n"
		"  upgrade <in> [out]                    Upgrade file <in> and save as <out>\n"
		"                                        Standard out is used if no output file\n"
		"                                        is specified\n"
		"  makebundle <in> [out]                 Make a project bundle from the project\n"
		"                                        file <in> saving the resulting bundle\n"
		"                                        as <out>\n"
		"\nGlobal options:\n"
		"      --allowroot                Bypass root user startup check (use with\n"
		"          caution).\n"
		"  -c, --config <configfile>      Get the configuration from <configfile>\n"
		"  -h, --help                     Show this usage information and exit.\n"
		"  -v, --version                  Show version information and exit.\n"
		"\nOptions if no action is given:\n"
		"      --geometry <geometry>      Specify the size and position of\n"
		"          the main window\n"
		"          geometry is <xsizexysize+xoffset+yoffsety>.\n"
		"      --import <in> [-e]         Import MIDI or Hydrogen file <in>.\n"
		"          If -e is specified zene exits after importing the file.\n"
		"\nOptions for \"render\", \"rendertracks\" and \"exportstems\":\n"
		"  -a, --float                    Use 32bit float bit depth\n"
		"  -b, --bitrate <bitrate>        Specify output bitrate in KBit/s\n"
		"          Default: 160.\n"
		"  -f, --format <format>         Specify format of render-output where\n"
		"          Format is either 'wav', 'flac', 'ogg' or 'mp3'.\n"
		"  -l, --loop                     Render as a loop\n"
		"      --loudness-report          Also measure the render and write an\n"
		"          EBU R128 loudness report beside it\n"
		"          (<output>.loudness.txt); measure-only, the audio is\n"
		"          byte-identical to a render without this option\n"
		"  -m, --mode                     Stereo mode used for MP3 export\n"
		"          Possible values: s, j, m\n"
		"            s: Stereo\n"
		"            j: Joint Stereo\n"
		"            m: Mono\n"
		"          Default: j\n"
		"  -o, --output <path>            Render into <path>\n"
		"          For \"render\", provide a file path\n"
		"          For \"rendertracks\", provide a directory path\n"
		"          For \"exportstems\", provide a directory path (required)\n"
		"          If not specified, render will overwrite the input file\n"
		"          For \"rendertracks\", this might be required\n"
		"      --tail-bars <bars>         Bars rendered past the project end so an\n"
		"          effect tail is not truncated. Default: 1 (the whole-project\n"
		"          render's convention). \"exportstems\" only.\n"
		"  -p, --profile <out>            Dump profiling information to file <out>\n"
		"      --run-script <file>        Run the Lua script <file> headless and exit\n"
		"          Prints the script's LuaLog output to stdout\n"
		"  -s, --samplerate <samplerate>  Specify output samplerate in Hz\n"
		"          Range: 44100 (default) to 192000\n"
		"          Possible values: 1, 2, 4, 8\n"
		"          Default: 2\n"
		"\nOptions for \"master\":\n"
		"  -o, --output <dir>             Directory the candidates are written into\n"
		"          Required: a candidate set is several files, never one\n"
		"  -f, --format <format>          Format of the candidates; wave-1 mastering\n"
		"          writes wav only\n"
		"  -s, --samplerate <samplerate>  Specify output samplerate in Hz\n"
		"  -a, --float                    Use 32bit float bit depth\n"
		"  -b, --bitrate <bitrate>        Accepted and ignored by \"master\" (wav)\n\n",
		LMMS_VERSION, LMMS_PROJECT_COPYRIGHT );
}




void fileCheck( QString &file )
{
	QFileInfo fileToCheck( file );

	if( !fileToCheck.size() )
	{
		printf( "The file %s does not have any content.\n",
				file.toUtf8().constData() );
		exit( EXIT_FAILURE );
	}
	else if( fileToCheck.isDir() )
	{
		printf( "%s is a directory.\n",
				file.toUtf8().constData() );
		exit( EXIT_FAILURE );
	}
}

int usageError(const QString& message)
{
	qCritical().noquote() << QString("\n%1.\n\nTry \"%2 --help\" for more information.\n\n")
				   .arg( message ).arg( qApp->arguments()[0] );
	return EXIT_FAILURE;
}

int noInputFileError()
{
	return usageError( "No input file specified" );
}


// Print one line per candidate: the three BS.1770-4 readings the task asks for,
// the verdict against that candidate's named target, and the file it was written
// to. Nothing here ranks the candidates or picks one - that is not measured.
void printMasteringReport( const lmms::MasteringJob& job )
{
	const auto& source = job.sourceMetrics();
	printf( "\nAuto-mastering: %d candidates from %d project render\n",
		static_cast<int>( job.reports().size() ), job.renderCount() );
	printf( "one render: %s\n", job.sourceRenderFile().toUtf8().constData() );
	printf( "%-26s %-10s %8s %8s %8s  %s\n",
		"candidate", "target", "LUFS-I", "ST-max", "dBTP", "verdict" );
	printf( "%-26s %-10s %8.2f %8.2f %8.2f  %s\n", "source (no mastering)", "-",
		source.integratedLufs, source.shortTermMaxLufs, source.truePeakDbtp, "-" );

	for( const auto& report : job.reports() )
	{
		QStringList issues;
		if( !report.lufsPass )
		{
			issues << QStringLiteral( "loudness %1 off target" )
				.arg( report.lufsResidual, 0, 'f', 2 );
		}
		if( !report.truePeakPass )
		{
			issues << QStringLiteral( "over ceiling" );
		}
		if( report.shortTermWarn )
		{
			issues << QStringLiteral( "short-term flag" );
		}
		const QString verdict = issues.isEmpty() ? QStringLiteral( "pass" )
			: QStringLiteral( "warn: " ) + issues.join( QStringLiteral( ", " ) );
		printf( "%-26s %-10s %8.2f %8.2f %8.2f  %s\n",
			report.name.toUtf8().constData(), report.targetName.toUtf8().constData(),
			report.metrics.integratedLufs, report.metrics.shortTermMaxLufs,
			report.metrics.truePeakDbtp, verdict.toUtf8().constData() );
		printf( "%-26s %s\n", "", report.outputFile.toUtf8().constData() );
	}
	printf( "\nNo candidate is preferred: the readings above are the measurements, "
		"the choice is the user's.\n" );
}


int main( int argc, char * * argv )
{
	using namespace lmms;

	bool coreOnly = false;
	bool fullscreen = true;
	bool exitAfterImport = false;
	bool allowRoot = false;
	bool renderLoop = false;
	bool renderTracks = false;
	bool renderStems = false;
	int stemTailBars = 1;
	bool mastering = false;
	// One flag for "the user gave -o": the stem export's directory check and the
	// mastering job's candidate-directory check read the same fact. The two lanes
	// named it differently (outputSpecified / outputGiven); merge train 3C keeps
	// this single name so there is one place that sets it.
	bool outputGiven = false;
	// Carries the exit code of whichever headless action ran - the Lua script or
	// the mastering job. The process returns it in place of the event loop's.
	int headlessExitCode = EXIT_SUCCESS;
	QString fileToLoad, fileToImport, renderOut, profilerOutputFile, configFile,
			scriptFile;

	// first of two command-line parsing stages
	for (int i = 1; i < argc; ++i)
	{
		QString arg = argv[i];

		if (arg == "--help" || arg == "-h")
		{
			printHelp();
			return EXIT_SUCCESS;
		}
		else if (arg == "--version" || arg == "-v")
		{
			printVersion(argv[0]);
			return EXIT_SUCCESS;
		}
		else if (arg == "render" || arg == "--render" || arg == "-r" )
		{
			coreOnly = true;
		}
		else if (arg == "rendertracks" || arg == "--rendertracks")
		{
			coreOnly = true;
			renderTracks = true;
		}
		else if (arg == "exportstems" || arg == "--exportstems")
		{
			coreOnly = true;
			renderStems = true;
		}
		else if (arg == "master" || arg == "--master")
		{
			// Offline auto-mastering: the mix is rendered once and every
			// candidate branches off that render (task #610, wave 1).
			coreOnly = true;
			mastering = true;
		}
		else if (arg == "--run-script")
		{
			// Scripts always run headless; the script's LuaLog output goes to
			// stdout and the process exit code reports success.
			coreOnly = true;
		}
		else if (arg == "--allowroot")
		{
			allowRoot = true;
		}
		else if (arg == "--geometry" || arg == "-geometry")
		{
			if (arg == "--geometry") { argv[i]++; } // Delete the first "-" so Qt recognize the option
			// option -geometry is filtered by Qt later,
			// so we need to check its presence now to
			// determine, if the application should run in
			// fullscreen mode (default, no -geometry given).
			fullscreen = false;
		}
	}

#ifdef LMMS_DEBUG_FPE
	// Enable exceptions for certain floating point results
	// FE_UNDERFLOW is disabled for the time being
	feenableexcept( FE_INVALID   |
			FE_DIVBYZERO |
			FE_OVERFLOW  /*|
			FE_UNDERFLOW*/);

	// Install the trap handler
	// register signal SIGFPE and signal handler
	signal(SIGFPE, sigfpeHandler);
#endif
	signal(SIGINT, gui::GuiApplication::sigintHandler);

#ifdef LMMS_BUILD_WIN32
	// Don't touch redirected streams here
	// GetStdHandle should be called before AttachConsole
	HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
	HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
	HANDLE hStdErr = GetStdHandle(STD_ERROR_HANDLE);
	FILE *fIn, *fOut, *fErr;
	// Enable console output if available
	if (AttachConsole(ATTACH_PARENT_PROCESS))
	{
		if (!hStdIn)
		{
			freopen_s(&fIn, "CONIN$", "r", stdin);
		}
		if (!hStdOut)
		{
			freopen_s(&fOut, "CONOUT$", "w", stdout);
		}
		if (!hStdErr)
		{
			freopen_s(&fErr, "CONOUT$", "w", stderr);
		}
	}
	// Make Qt's debug message handlers work
	qInstallMessageHandler(consoleMessageHandler);
#endif

#if defined(LMMS_HAVE_SYS_PRCTL_H) && defined(PR_SET_CHILD_SUBREAPER)
	// Set the "child subreaper" attribute so that plugin child processes remain as lmms'
	// children even when some wrapper process exits, as it may happen with wine
	if (prctl(PR_SET_CHILD_SUBREAPER, 1))
	{
		perror("prctl(PR_SET_CHILD_SUBREAPER)");
	}
#endif

	// initialize memory managers
	NotePlayHandleManager::init();

	// initialize RNG
	srand( getpid() + time( 0 ) );

	disableDenormals();

#if !defined(LMMS_BUILD_WIN32) && !defined(LMMS_BUILD_HAIKU)
	if ( ( getuid() == 0 || geteuid() == 0 ) && !allowRoot )
	{
		printf( "Zene Studio cannot be run as root.\nUse \"--allowroot\" to override.\n\n" );
		return EXIT_FAILURE;
	}
#endif

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
	// High-DPI scaling is always enabled in Qt >= 6.0
	QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

	QCoreApplication * app = coreOnly ?
			new QCoreApplication( argc, argv ) :
					new gui::MainApplication(argc, argv);

	OutputSettings os(44100, 160, OutputSettings::BitDepth::Depth16Bit, OutputSettings::StereoMode::JointStereo);
	ProjectRenderer::ExportFileFormat eff = ProjectRenderer::ExportFileFormat::Wave;

	// second of two command-line parsing stages
	for( int i = 1; i < argc; ++i )
	{
		QString arg = argv[i];

		if (arg == "upgrade" || arg == "--upgrade" || arg  == "-u")
		{
			++i;

			if( i == argc )
			{
				return noInputFileError();
			}


			DataFile dataFile( QString::fromLocal8Bit( argv[i] ) );

			if( argc > i+1 ) // output file specified
			{
				dataFile.writeFile( QString::fromLocal8Bit( argv[i+1] ) );
			}
			else // no output file specified; use stdout
			{
				QTextStream ts( stdout );
				dataFile.write( ts );
				fflush( stdout );
			}

			return EXIT_SUCCESS;
		}
		else if (arg == "makebundle")
		{
			++i;

			if (i == argc)
			{
				return noInputFileError();
			}

			DataFile dataFile(QString::fromLocal8Bit(argv[i]));

			if (argc > i+1) // Project bundle file name given
			{
				printf("Making bundle\n");
				dataFile.writeFile(QString::fromLocal8Bit(argv[i+1]), true);
				return EXIT_SUCCESS;
			}
			else
			{
				return usageError("No project bundle name given");
			}
		}
		else if( arg == "--allowroot" )
		{
			// Ignore, processed earlier
#ifdef LMMS_BUILD_WIN32
			if( allowRoot )
			{
				printf( "\nOption \"--allowroot\" will be ignored on this platform.\n\n" );
			}
#endif

		}
		else if( arg == "dump" || arg == "--dump" || arg  == "-d" )
		{
			++i;

			if( i == argc )
			{
				return noInputFileError();
			}


			QFile f( QString::fromLocal8Bit( argv[i] ) );
			f.open( QIODevice::ReadOnly );
			QString d = qUncompress( f.readAll() );
			printf( "%s\n", d.toUtf8().constData() );

			return EXIT_SUCCESS;
		}
		else if( arg == "compress" || arg == "--compress" )
		{
			++i;

			if( i == argc )
			{
				return noInputFileError();
			}

			QFile f( QString::fromLocal8Bit( argv[i] ) );
			f.open( QIODevice::ReadOnly );
			QByteArray d = qCompress( f.readAll() ) ;
			fwrite( d.constData(), sizeof(char), d.size(), stdout );

			return EXIT_SUCCESS;
		}
		else if( arg == "render" || arg == "--render" || arg == "-r" ||
			arg == "rendertracks" || arg == "--rendertracks" ||
			arg == "exportstems" || arg == "--exportstems" ||
			arg == "master" || arg == "--master" )
		{
			++i;

			if( i == argc )
			{
				return noInputFileError();
			}


			fileToLoad = QString::fromLocal8Bit( argv[i] );
			renderOut = fileToLoad;
		}
		else if( arg == "--loop" || arg == "-l" )
		{
			renderLoop = true;
		}
		else if( arg == "--output" || arg == "-o" )
		{
			++i;

			if( i == argc )
			{
				return usageError( "No output file specified" );
			}

			outputGiven = true;
			renderOut = QString::fromLocal8Bit( argv[i] );
		}
		else if( arg == "--tail-bars" )
		{
			++i;

			if( i == argc )
			{
				return usageError( "No tail length specified" );
			}

			bool ok = false;
			const int bars = QString( argv[i] ).toInt( &ok );
			if( !ok || bars < 0 )
			{
				return usageError( QString( "Invalid tail length %1" ).arg( argv[i] ) );
			}
			stemTailBars = bars;
		}
		else if( arg == "--format" || arg == "-f" )
		{
			++i;

			if( i == argc )
			{
				return usageError( "No output format specified" );
			}


			const QString ext = QString( argv[i] );

			if( ext == "wav" )
			{
				eff = ProjectRenderer::ExportFileFormat::Wave;
			}
#ifdef LMMS_HAVE_OGGVORBIS
			else if( ext == "ogg" )
			{
				eff = ProjectRenderer::ExportFileFormat::Ogg;
			}
#endif
#ifdef LMMS_HAVE_MP3LAME
			else if( ext == "mp3" )
			{
				eff = ProjectRenderer::ExportFileFormat::MP3;
			}
#endif
			else if (ext == "flac")
			{
				eff = ProjectRenderer::ExportFileFormat::Flac;
			}
			else
			{
				return usageError( QString( "Invalid output format %1" ).arg( argv[i] ) );
			}
		}
		else if( arg == "--samplerate" || arg == "-s" )
		{
			++i;

			if( i == argc )
			{
				return usageError( "No samplerate specified" );
			}


			sample_rate_t sr = QString( argv[i] ).toUInt();
			if( sr >= 44100 && sr <= 192000 )
			{
				os.setSampleRate(sr);
			}
			else
			{
				return usageError( QString( "Invalid samplerate %1" ).arg( argv[i] ) );
			}
		}
		else if( arg == "--bitrate" || arg == "-b" )
		{
			++i;

			if( i == argc )
			{
				return usageError( "No bitrate specified" );
			}


			int br = QString( argv[i] ).toUInt();

			if( br >= 64 && br <= 384 )
			{
				os.setBitrate(br);
			}
			else
			{
				return usageError( QString( "Invalid bitrate %1" ).arg( argv[i] ) );
			}
		}
		else if( arg == "--mode" || arg == "-m" )
		{
			++i;

			if( i == argc )
			{
				return usageError( "No stereo mode specified" );
			}

			QString const mode( argv[i] );

			if( mode == "s" )
			{
				os.setStereoMode(OutputSettings::StereoMode::Stereo);
			}
			else if( mode == "j" )
			{
				os.setStereoMode(OutputSettings::StereoMode::JointStereo);
			}
			else if( mode == "m" )
			{
				os.setStereoMode(OutputSettings::StereoMode::Mono);
			}
			else
			{
				return usageError( QString( "Invalid stereo mode %1" ).arg( argv[i] ) );
			}
		}
		else if( arg =="--float" || arg == "-a" )
		{
			os.setBitDepth(OutputSettings::BitDepth::Depth32Bit);
		}
		else if( arg == "--loudness-report" )
		{
			// Ask the render for an EBU R128 loudness report: the measured
			// values are printed to stdout and written beside the render as
			// <output>.loudness.txt. Measure-only; the audio is unchanged.
			os.setLoudnessReport(true);
		}
		else if( arg == "--import" )
		{
			++i;

			if( i == argc )
			{
				return usageError( "No file specified for importing" );
			}

			fileToImport = QString::fromLocal8Bit( argv[i] );

			// exit after import? (only for debugging)
			if( QString( argv[i + 1] ) == "-e" )
			{
				exitAfterImport = true;
				++i;
			}
		}
		else if( arg == "--profile" || arg == "-p" )
		{
			++i;

			if( i == argc )
			{
				return usageError( "No profile specified" );
			}


			profilerOutputFile = QString::fromLocal8Bit( argv[i] );
		}
		else if( arg == "--config" || arg == "-c" )
		{
			++i;

			if( i == argc )
			{
				return usageError( "No configuration file specified" );
			}

			configFile = QString::fromLocal8Bit( argv[i] );
		}
		else if( arg == "--run-script" )
		{
			++i;

			if( i == argc )
			{
				return usageError( "No Lua script specified" );
			}

			scriptFile = QString::fromLocal8Bit( argv[i] );
		}
		else
		{
			if( argv[i][0] == '-' )
			{
				return usageError( QString( "Invalid option %1" ).arg( argv[i] ) );
			}
			fileToLoad = QString::fromLocal8Bit( argv[i] );
		}
	}

	// Test file argument before continuing
	if( !fileToLoad.isEmpty() )
	{
		fileCheck( fileToLoad );
	}
	else if( !fileToImport.isEmpty() )
	{
		fileCheck( fileToImport );
	}

	// A candidate set is several files; scattering them beside the project by
	// default is never what the user meant, so "master" requires -o.
	if( mastering && !outputGiven )
	{
		return usageError( "No output directory specified for master (use -o)" );
	}

	ConfigManager::inst()->loadConfigFile(configFile);

	// Install the local crash reporter as soon as the user's working directory
	// is known.  It writes ONE bounded text file and dies exactly as the
	// process would have without it; the next launch offers the file.  There is
	// no upload, no network and no telemetry anywhere in this path (see
	// include/CrashReporter.h and docs/CRASH-REPORTER.md).
	crashreporter::install( ConfigManager::inst()->workingDir().toStdString() );
	crashreporter::beginSession();
	if( coreOnly && crashreporter::hasPendingReport() )
	{
		// A headless run (render / --run-script) cannot show a dialog; offer
		// the report on stderr instead, and treat that as the one offer so
		// every later render does not repeat it.  The file itself stays put so
		// it can still be attached by hand.
		fprintf( stderr, "A crash report from an earlier session is pending: %s\n",
			crashreporter::pendingReportPath().c_str() );
		crashreporter::acknowledgePendingReport();
	}

	// set language
	QString pos = ConfigManager::inst()->value( "app", "language" );
	if( pos.isEmpty() )
	{
		pos = QLocale::system().name().left( 2 );
	}

	// load actual translation for LMMS
	loadTranslation( pos );

	// load translation for Qt-widgets/-dialogs
#ifdef QT_TRANSLATIONS_DIR
	// load from the original path first
	loadTranslation(QString("qt_") + pos, QT_TRANSLATIONS_DIR);
#endif
	// override it with bundled/custom one, if exists
	loadTranslation(QString("qt_") + pos, ConfigManager::inst()->localeDir());

#if _POSIX_C_SOURCE >= 1 || _XOPEN_SOURCE || _POSIX_SOURCE
	struct sigaction sa;
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = SA_SIGINFO;
	if ( sigemptyset( &sa.sa_mask ) )
	{
		fprintf( stderr, "Signal initialization failed.\n" );
	}
	if ( sigaction( SIGPIPE, &sa, nullptr ) )
	{
		fprintf( stderr, "Signal initialization failed.\n" );
	}
#endif

	bool destroyEngine = false;

	// if we have an output file for rendering, just render the song
	// without starting the GUI
	if( !renderOut.isEmpty() )
	{
		Engine::init( true );
		destroyEngine = true;

		printf( "Loading project...\n" );
		Engine::getSong()->loadProject( fileToLoad );
		crashreporter::setProjectPath(
			Engine::getSong()->projectFileName().toStdString() );
		if( Engine::getSong()->isEmpty() )
		{
			printf("The project %s is empty, aborting!\n", fileToLoad.toUtf8().constData() );
			crashreporter::endSession();
			exit( EXIT_FAILURE );
		}
		printf( "Done\n" );

		Engine::getSong()->setExportLoop( renderLoop );

		if( mastering )
		{
			// Offline: ONE project render, then every candidate branches off it.
			// The job runs on this thread; the render runs on ProjectRenderer's.
			// Nothing here is reachable from the audio callback.
			MasteringJob job( os, eff, renderOut, MasteringJob::defaultCandidates() );
			QString error;
			if( job.run( &error ) )
			{
				printMasteringReport( job );
			}
			else
			{
				fprintf( stderr, "master: %s\n", error.toUtf8().constData() );
				headlessExitCode = EXIT_FAILURE;
			}
			QTimer::singleShot( 0, qApp, &QCoreApplication::quit );
		}
		else
		{
			// when rendering multiple tracks, renderOut is a directory
			// otherwise, it is a file, so we need to append the file extension
			if ( !renderTracks && !renderStems )
			{
				renderOut = baseName( renderOut ) +
					ProjectRenderer::getFileExtensionFromFormat(eff);
			}

			if ( renderStems )
			{
				// A stem export writes many files, so the destination must be a
				// directory the user asked for -- never a guess next to the project.
				if ( !outputGiven )
				{
					return usageError( "exportstems needs an output directory (-o <dir>)" );
				}
				if ( !QDir().mkpath( renderOut ) )
				{
					return usageError( QString( "Could not create output directory %1" ).arg( renderOut ) );
				}
			}

			// create renderer
			auto r = new RenderManager(os, eff, renderOut);
			QCoreApplication::instance()->connect( r,
					SIGNAL(finished()), SLOT(quit()));

			// timer for progress-updates
			auto t = new QTimer(r);
			r->connect( t, SIGNAL(timeout()),
					SLOT(updateConsoleProgress()));
			t->start( 200 );

			if( profilerOutputFile.isEmpty() == false )
			{
				Engine::audioEngine()->profiler().setOutputFile( profilerOutputFile );
			}

			// start now!
			if ( renderStems )
			{
				StemExportOptions stemOptions;
				stemOptions.tailBars = stemTailBars;
				r->exportStems( stemOptions );
			}
			else if ( renderTracks )
			{
				r->renderTracks();
			}
			else
			{
				r->renderProject();
			}
		}
	}
	else if( !scriptFile.isEmpty() )
	{
		Engine::init( true );
		destroyEngine = true;

		if( !fileToLoad.isEmpty() )
		{
			Engine::getSong()->loadProject( fileToLoad );
			crashreporter::setProjectPath(
				Engine::getSong()->projectFileName().toStdString() );
		}

		QString error;
		// This action's contract is the script's LuaLog output on stdout (see
		// printHelp). The engine's console streams the same lines onto Qt's
		// message log, so leaving it on would emit every line twice.
		ScriptConsole::setEnabled( false );
		const auto result = ScriptEngine::instance()->runFile( scriptFile, &error );
		ScriptEngine::instance()->processCommands();

		for( const QString& line : ScriptEngine::instance()->takeLogMessages() )
		{
			printf( "%s\n", line.toUtf8().constData() );
		}
		if( result != ScriptEngine::RunResult::Ok )
		{
			fprintf( stderr, "lua: %s\n", error.toUtf8().constData() );
			headlessExitCode = EXIT_FAILURE;
		}

		QTimer::singleShot( 0, qApp, &QCoreApplication::quit );
	}
	else // otherwise, start the GUI
	{
		using namespace lmms::gui;

		new GuiApplication();

		// GuiApplication may have just created the working directory after the
		// first-run prompt, so give the crash reporter another chance to attach
		// to it; if the directory already existed this is a re-point, and if it
		// still does not exist the reporter stays off for this session.
		crashreporter::install( ConfigManager::inst()->workingDir().toStdString() );
		crashreporter::beginSession();

		// Offer a crash report written by a previous session, if one is
		// pending.  This is the whole reporting UX: the user is given the file
		// path and can attach it to a bug report by hand.  Nothing is sent
		// anywhere, and the offer is made once (acknowledging keeps the file).
		if( crashreporter::hasPendingReport() )
		{
			const QString reportPath = QString::fromStdString(
				crashreporter::pendingReportPath() );
			QMessageBox crashBox;
			crashBox.setWindowTitle( MainWindow::tr( "Crash report" ) );
			crashBox.setIcon( QMessageBox::Warning );
			crashBox.setWindowIcon( embed::getIconPixmap( "icon_small" ) );
			crashBox.setTextFormat( Qt::PlainText );
			crashBox.setText( MainWindow::tr( "The previous session ended "
				"unexpectedly and Zene Studio saved a crash report." ) );
			crashBox.setInformativeText( MainWindow::tr( "Zene Studio never "
				"sends anything on its own. To report this crash, attach the "
				"file below to your bug report.\n\n%1" ).arg( reportPath ) );
			auto keepReport = crashBox.addButton(
				MainWindow::tr( "Keep report" ), QMessageBox::AcceptRole );
			auto openFolder = crashBox.addButton(
				MainWindow::tr( "Open folder" ), QMessageBox::ActionRole );
			auto discardReport = crashBox.addButton(
				MainWindow::tr( "Discard" ), QMessageBox::DestructiveRole );
			crashBox.setDefaultButton( keepReport );
			crashBox.exec();
			if( crashBox.clickedButton() == discardReport )
			{
				crashreporter::discardPendingReport();
			}
			else
			{
				if( crashBox.clickedButton() == openFolder )
				{
					QDesktopServices::openUrl( QUrl::fromLocalFile(
						QFileInfo( reportPath ).absolutePath() ) );
				}
				crashreporter::acknowledgePendingReport();
			}
		}

		// re-intialize RNG - shared libraries might have srand() or
		// srandom() calls in their init procedure
		srand( getpid() + time( 0 ) );

		// recover a file?
		QString const recoveryFile = ConfigManager::inst()->recoveryFile();

		// A recovery file is offered only when it is THIS session's. Upstream
		// offers any recover.mmp, so a stale file left by an unrelated project is
		// offered forever (and suppresses "open last project" until discarded),
		// and an explicit project named on the command line is hijacked by a
		// recovery of something else. decideRecovery() is the whole rule, as a
		// pure function, unit-tested headless in tests/src/core/ProjectRecoveryTest.cpp.
		const ProjectRecovery::RecoveryInfo recovery =
				ProjectRecovery::readRecoveryInfo( recoveryFile );
		const ProjectRecovery::RecoveryDecision recoveryDecision =
				ProjectRecovery::decideRecovery( recovery, fileToLoad );
		const bool recoveryFilePresent = recoveryDecision.offered();
		const bool autoSaveEnabled =
			ConfigManager::inst()->value( "ui", "enableautosave" ).toInt();

		// Say why a recovery file that IS on disk was not offered, so a report can
		// show it instead of describing a dialog that never appeared.
		if( recovery.fileExists && !recoveryFilePresent )
		{
			fprintf( stderr, "zene: not offering recovery file %s: %s\n",
					recoveryFile.toUtf8().constData(),
					recoveryDecision.detail.toUtf8().constData() );
		}

		if( recoveryFilePresent )
		{
			QMessageBox mb;
			mb.setWindowTitle( MainWindow::tr( "Project recovery" ) );
			// Name the project the recovery actually belongs to. Upstream's dialog
			// says "the project of this session", which a user cannot check.
			QString const recoveredProject =
				recoveryDecision.projectLabel.isEmpty()
					? MainWindow::tr( "It is a project that has not been saved yet." )
					: MainWindow::tr( "Project it belongs to: %1" )
						.arg( recoveryDecision.projectLabel );
			mb.setText( QString(
				"<html>"
				"<p style=\"margin-left:6\">%1</p>"
				"<table cellpadding=\"3\">"
				"  <tr>"
				"    <td><b>%2</b></td>"
				"    <td>%3</td>"
				"  </tr>"
				"  <tr>"
				"    <td><b>%4</b></td>"
				"    <td>%5</td>"
				"  </tr>"
				"</table>"
				"</html>" ).arg(
				MainWindow::tr( "There is a recovery file present. "
					"It looks like the last session did not end "
					"properly or another instance of Zene Studio is "
					"already running. Do you want to recover the "
					"project of this session?" ),
				MainWindow::tr( "Recover" ),
				MainWindow::tr( "Recover the file. Please don't run "
					"multiple instances of Zene Studio when you do this." ),
				MainWindow::tr( "Discard" ),
				MainWindow::tr( "Launch a default session and delete "
					"the restored files. This is not reversible." )
							) + QStringLiteral( "<p style=\"margin-left:6\">%1</p>" )
							.arg( recoveredProject ) );

			mb.setIcon( QMessageBox::Warning );
			mb.setWindowIcon( embed::getIconPixmap( "icon_small" ) );
			mb.setWindowFlags( Qt::WindowCloseButtonHint );

			// setting all buttons to the same roles allows us
			// to have a custom layout
			auto discard = mb.addButton(MainWindow::tr("Discard"), QMessageBox::AcceptRole);
			auto recover = mb.addButton(MainWindow::tr("Recover"), QMessageBox::AcceptRole);

			// have a hidden exit button
			auto exit = mb.addButton("", QMessageBox::RejectRole);
			exit->setVisible(false);

			// set icons
			recover->setIcon( embed::getIconPixmap( "recover" ) );
			discard->setIcon( embed::getIconPixmap( "discard" ) );

			mb.setDefaultButton( recover );
			mb.setEscapeButton( exit );

			mb.exec();
			if( mb.clickedButton() == discard )
			{
				getGUI()->mainWindow()->sessionCleanup();
			}
			else if( mb.clickedButton() == recover ) // Recover
			{
				fileToLoad = recoveryFile;
				getGUI()->mainWindow()->setSession( MainWindow::SessionState::Recover );
			}
			else // Exit
			{
				crashreporter::endSession();
				return EXIT_SUCCESS;
			}
		}

		// first show the Main Window and then try to load given file

		// [Settel] workaround: showMaximized() doesn't work with
		// FVWM2 unless the window is already visible -> show() first
		getGUI()->mainWindow()->show();
		if( fullscreen )
		{
			getGUI()->mainWindow()->showMaximized();
		}

		// Handle macOS-style FileOpen QEvents
		QString queuedFile = static_cast<MainApplication *>( app )->queuedFile();
		if ( !queuedFile.isEmpty() ) {
			fileToLoad = queuedFile;
		}

		if( !fileToLoad.isEmpty() )
		{
			if( fileToLoad == recoveryFile )
			{
				Engine::getSong()->createNewProjectFromTemplate( fileToLoad );
			}
			else
			{
				Engine::getSong()->loadProject( fileToLoad );
				// Set the hint here as well as at the end of the block: a
				// modal dialog raised during the load (missing plugins, for
				// instance) can leave the process sitting before the later
				// call, and a crash at that moment should still name the file.
				crashreporter::setProjectPath(
					Engine::getSong()->projectFileName().toStdString() );
			}
		}
		else if( !fileToImport.isEmpty() )
		{
			ImportFilter::import( fileToImport, Engine::getSong() );
			if( exitAfterImport )
			{
				crashreporter::endSession();
				return EXIT_SUCCESS;
			}
		}
		// If enabled, open last project if there is one. Else, create
		// a new one.
		else if( ConfigManager::inst()->
				value( "app", "openlastproject" ).toInt() &&
			!ConfigManager::inst()->
				recentlyOpenedProjects().isEmpty() &&
				!recoveryFilePresent )
		{
			QString f = ConfigManager::inst()->
					recentlyOpenedProjects().first();
			QFileInfo recentFile( f );

			if ( recentFile.exists() &&
				recentFile.suffix().toLower() != "mpt" )
			{
				Engine::getSong()->loadProject( f );
			}
			else
			{
				Engine::getSong()->createNewProject();
			}
		}
		else
		{
			Engine::getSong()->createNewProject();
		}

		// Keep the crash reporter's "project that was open" hint current: set
		// it for whatever was just loaded, then follow later changes (Open,
		// Save As, New) so a crash names the file the user was working on.
		crashreporter::setProjectPath(
			Engine::getSong()->projectFileName().toStdString() );
		QObject::connect( Engine::getSong(), &Song::projectFileNameChanged,
			Engine::getSong(),
			[](){ crashreporter::setProjectPath(
				Engine::getSong()->projectFileName().toStdString() ); } );

		// Finally we start the auto save timer and also trigger the
		// autosave one time as recover.mmp is a signal to possible other
		// instances of LMMS.
		if( autoSaveEnabled )
		{
			gui::getGUI()->mainWindow()->autoSaveTimerReset();
		}
	}

	const int ret = app->exec();
	// A clean exit: clear the "session was open" marker so the next launch
	// knows the difference between a crash and a deliberate quit.
	crashreporter::endSession();
	delete app;

	if( destroyEngine )
	{
		Engine::destroy();
	}

	// ProjectRenderer::updateConsoleProgress() doesn't return line after render
	if( coreOnly )
	{
		printf( "\n" );
	}

#ifdef LMMS_BUILD_WIN32
	// Cleanup console
	HWND hConsole = GetConsoleWindow();
	if (hConsole)
	{
		SendMessage(hConsole, WM_CHAR, (WPARAM)VK_RETURN, (LPARAM)0);
		FreeConsole();
	}
#endif


	NotePlayHandleManager::free();

	return headlessExitCode != EXIT_SUCCESS ? headlessExitCode : ret;
}
